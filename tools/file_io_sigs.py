#!/usr/bin/env python3
"""Locate the GameMaker runner's file-read path in a Windows x64 runner exe, without fixed RVAs.

Works on both the VM runner (DELTARUNE) and the YYC runner (Dwarf Eats Mountain, GM 2024.14).
Stdlib only, so it mirrors what a C++ resolver can do at run time (string scan, RIP-relative
LEA scan, E8 call scan, .pdata function bounds). Evidence and disassembly: RE/11_file_io.md.

    python3 tools/file_io_sigs.py <runner.exe> [--json]

Resolves:
  ReadEntireFile(path_utf8, u32* out_size) -> malloc'd, NUL-terminated buffer (or NULL)
      anchor A (string): the only function that takes the address of a .rdata string ending in
          "\\Files\\IO\\Win32\\LoadSave.cpp" AND contains `mov ecx, 0FDE9h` (CP_UTF8) and
          `mov r9d, 8000h` (setvbuf size)
      anchor B (bytes):  SIG_READ_ENTIRE_FILE below (unique in .text of both runners)
      The two must agree.
  GM allocator: target of the E8 call right after ReadEntireFile's `lea rdx, <LoadSave.cpp>`.
  ReadEntireFile wrappers (path converter + ReadEntireFile, 0x800-byte stack path):
      "sandbox" = converter references "Error! not allowing file operation ..." (save-area mapper)
      "bundle"  = the other one (program/bundle directory)
  RunnerLoadGame: only function referencing "not in bundle" (loads data.win via the wrappers).
  FOpen: function referencing "\\Files\\IO\\LoadSaveFOpen.cpp" that calls both wrappers
      (backs file_text_open_read / file_bin_open / ini_open / json/csv loads).
  Built-ins: the registration pairs `lea rdx, fn ; ... ; lea rcx, "name"`.
"""
import json
import re
import struct
import sys

SIG_READ_ENTIRE_FILE = (
    "48 8B F2 C7 44 24 28 00 00 00 00 48 8B F9 48 C7 44 24 20 00 00 00 00 "
    "4C 8B C1 33 D2 B9 E9 FD 00 00 41 B9 FF FF FF FF FF 15"
)
SIG_READ_ENTIRE_FILE_START_DELTA = -0x25   # signature starts at function + 0x25

# push rbx; sub rsp,830h; mov rax,[__security_cookie]; xor rax,rsp; mov [rsp+820h],rax;
# mov rbx,rdx; mov r8,rcx; mov edx,800h; lea rcx,[rsp+20h]; call <converter>;
# mov rdx,rbx; lea rcx,[rsp+20h]; call ReadEntireFile
SIG_WRAPPER = (
    "40 53 48 81 EC 30 08 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 20 08 00 00 "
    "48 8B DA 4C 8B C1 BA 00 08 00 00 48 8D 4C 24 20 E8 ?? ?? ?? ?? "
    "48 8B D3 48 8D 4C 24 20 E8 ?? ?? ?? ??"
)

BUILTINS = ["file_text_open_read", "file_bin_open", "buffer_load", "buffer_load_async",
            "sprite_add", "sprite_replace", "audio_create_stream", "font_add", "ini_open",
            "file_exists", "file_copy", "json_parse", "load_csv"]


def pattern(sig):
    return re.compile(b"".join(b"." if t == "??" else re.escape(bytes([int(t, 16)]))
                               for t in sig.split()), re.S)


class Image:
    def __init__(self, path):
        raw = open(path, "rb").read()
        pe = struct.unpack_from("<I", raw, 0x3C)[0]
        nsec = struct.unpack_from("<H", raw, pe + 6)[0]
        optsz = struct.unpack_from("<H", raw, pe + 20)[0]
        opt = pe + 24
        size = struct.unpack_from("<I", raw, opt + 56)[0]
        self.img = bytearray(size)
        self.img[:0x1000] = raw[:0x1000]
        self.secs = {}
        for i in range(nsec):
            o = opt + optsz + 40 * i
            name = raw[o:o + 8].rstrip(b"\0").decode()
            vsz, va, rsz, rptr = struct.unpack_from("<IIII", raw, o + 8)
            n = min(vsz or rsz, rsz)
            self.img[va:va + n] = raw[rptr:rptr + n]
            self.secs[name] = (va, va + vsz)
        exc_rva, exc_sz = struct.unpack_from("<II", raw, opt + 112 + 3 * 8)
        self.funcs = sorted(struct.unpack_from("<II", self.img, exc_rva + 12 * i)
                            for i in range(exc_sz // 12))
        self.starts = [b for b, _ in self.funcs]
        self._lea = self._e8 = None

    def sec(self, name):
        return self.secs[name]

    def func_of(self, rva):
        import bisect
        k = bisect.bisect_right(self.starts, rva) - 1
        if k >= 0 and self.funcs[k][0] <= rva < self.funcs[k][1]:
            return self.funcs[k]
        return None

    def body(self, f):
        return bytes(self.img[f[0]:f[1]])

    def find(self, rx, sec=".text"):
        lo, hi = self.sec(sec)
        return [m.start() for m in rx.finditer(self.img, lo, hi)]  # pos/endpos keep absolute offsets

    def strings_ending(self, suffix, sec=".rdata"):
        """start RVAs of NUL-terminated strings in `sec` that end with `suffix`."""
        lo, hi = self.sec(sec)
        out, i = [], self.img.find(suffix + b"\0", lo, hi)
        while i != -1:
            out.append(self.img.rfind(b"\0", lo, i) + 1)
            i = self.img.find(suffix + b"\0", i + 1, hi)
        return out

    @property
    def lea(self):
        """target -> [site] for REX.W(R) 8D /r with RIP-relative ModRM."""
        if self._lea is None:
            lo, hi = self.sec(".text")
            self._lea = {}
            for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", self.img[lo:hi]):
                s = lo + m.start()
                self._lea.setdefault(s + 7 + struct.unpack_from("<i", self.img, s + 3)[0], []).append(s)
        return self._lea

    @property
    def e8(self):
        if self._e8 is None:
            lo, hi = self.sec(".text")
            self._e8 = {}
            for m in re.finditer(rb"\xe8", self.img[lo:hi - 5]):
                s = lo + m.start()
                self._e8.setdefault(s + 5 + struct.unpack_from("<i", self.img, s + 1)[0], []).append(s)
        return self._e8

    def funcs_referencing(self, str_rva):
        return sorted({self.func_of(s) for s in self.lea.get(str_rva, []) if self.func_of(s)})

    def callers(self, fn):
        return sorted({self.func_of(s) for s in self.e8.get(fn, []) if self.func_of(s)})

    def call_targets(self, f):
        b = self.body(f)
        return [f[0] + i + 5 + struct.unpack_from("<i", b, i + 1)[0]
                for i in range(len(b) - 4) if b[i] == 0xE8]


def resolve(path):
    im = Image(path)
    r = {"exe": path.replace("\\", "/").split("/")[-1]}

    # --- ReadEntireFile, anchor A (string + body markers)
    loadsave = [s for s in im.strings_ending(b"\\Files\\IO\\Win32\\LoadSave.cpp")]
    cand = []
    for s in loadsave:
        for f in im.funcs_referencing(s):
            b = im.body(f)
            if b"\xb9\xe9\xfd\x00\x00" in b and b"\x41\xb9\x00\x80\x00\x00" in b:
                cand.append(f)
    r["ReadEntireFile_by_string"] = [hex(f[0]) for f in cand]
    # --- anchor B (byte signature)
    hits = im.find(pattern(SIG_READ_ENTIRE_FILE))
    r["ReadEntireFile_by_sig"] = [hex(h + SIG_READ_ENTIRE_FILE_START_DELTA) for h in hits]
    rer = cand[0][0] if len(cand) == 1 else None
    r["ReadEntireFile"] = hex(rer) if rer and r["ReadEntireFile_by_sig"] == [hex(rer)] else None
    if not r["ReadEntireFile"]:
        return r
    f = im.func_of(rer)

    # --- allocator: E8 after `lea rdx, <LoadSave.cpp>` inside ReadEntireFile
    for s in loadsave:
        for site in im.lea.get(s, []):
            if f[0] <= site < f[1]:
                for k in range(site + 7, site + 40):
                    if im.img[k] == 0xE8:
                        r["GmAlloc"] = hex(k + 5 + struct.unpack_from("<i", im.img, k + 1)[0])
                        r["GmAlloc_line_arg"] = struct.unpack_from("<I", im.img, k - 4)[0]
                        break

    # --- wrappers
    sandbox = set()
    for s in im.strings_ending(b"Error! not allowing file operation with filename '%s'.\n"):
        sandbox |= {g[0] for g in im.funcs_referencing(s)}
    wrx = pattern(SIG_WRAPPER)
    wr = {}
    for c in im.callers(rer):
        m = wrx.match(bytes(im.img[c[0]:c[0] + 64]))
        if not m:
            continue
        e1 = c[0] + 0x2B   # first E8 (converter)
        conv = e1 + 5 + struct.unpack_from("<i", im.img, e1 + 1)[0]
        e2 = c[0] + 0x38   # second E8 (ReadEntireFile)
        if e2 + 5 + struct.unpack_from("<i", im.img, e2 + 1)[0] != rer:
            continue
        kind = "sandbox" if conv in sandbox else "bundle"
        wr.setdefault(kind, []).append({"wrapper": hex(c[0]), "converter": hex(conv)})
    r["wrappers"] = wr
    r["ReadEntireFile_callers"] = [hex(c[0]) for c in im.callers(rer)]
    wset = {int(w["wrapper"], 16) for v in wr.values() for w in v}

    # --- RunnerLoadGame (data.win) and FOpen
    for s in im.strings_ending(b"not in bundle"):
        if im.img[s:s + 14] == b"not in bundle\0":
            fs = im.funcs_referencing(s)
            r["RunnerLoadGame"] = [hex(g[0]) for g in fs]
            r["RunnerLoadGame_calls_wrappers"] = [bool(wset & set(im.call_targets(g))) for g in fs]
    fo = []
    for s in im.strings_ending(b"\\Files\\IO\\LoadSaveFOpen.cpp"):
        for g in im.funcs_referencing(s):
            if len(wset & set(im.call_targets(g))) >= 2:
                fo.append(hex(g[0]))
    r["FOpen"] = fo

    # --- built-in registration pairs
    bi = {}
    for n in BUILTINS:
        for s in im.strings_ending(n.encode()):
            if bytes(im.img[s:s + len(n) + 1]) != n.encode() + b"\0":
                continue
            for site in im.lea.get(s, []):
                if im.img[site:site + 3] != b"\x48\x8d\x0d":
                    continue
                for k in range(site - 1, site - 0x20, -1):
                    if im.img[k:k + 3] == b"\x48\x8d\x15":
                        t = k + 7 + struct.unpack_from("<i", im.img, k + 3)[0]
                        g = im.func_of(t)
                        if g and g[0] == t:
                            bi[n] = hex(t)
                        break
                if n in bi:
                    break
    r["builtins"] = bi
    return r


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    res = resolve(sys.argv[1])
    if "--json" in sys.argv:
        print(json.dumps(res, indent=1))
    else:
        for k, v in res.items():
            print(f"{k:32} {v}")
