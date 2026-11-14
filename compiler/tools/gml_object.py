#!/usr/bin/env python3
"""gml_object.py - linkable GML objects (.gmlo) from compiler/GmlObjectCompiler, offline side.

A .gmlo is one code entry compiled by Underanalyzer with every reference operand left unresolved,
plus a relocation table. The runtime linker (modloader/include/gmlo_link.hpp) resolves
it against the running game; this module reads the same format and links it offline, so the two
can be compared (compiler/tests/test_gml_object.py). Format, one record per line:

    GMLO 1
    name gml_GlobalScript_foo
    locals 2
    args 0
    local <name>                         (informational)
    code <hex word> <hex word> ...       (little-endian u32 words)
    reloc <offset> var <inst type> <name>
    reloc <offset> func <name>
    reloc <offset> string <text>         (\\n \\r \\t \\\\ escaped)
    end

<offset> is the instruction's byte offset; its operand word (offset + 4) is the one linked:
    var     (word & 0xF8000000) | id     id: built-in index or 100000 + variable slot
    func    id                           built-in routine index or 100000 + script index
    string  index in the runner's string table (STRG; new strings are appended)

CLI:
    gml_object.py dump <file.gmlo>                  disassembly-free summary + relocations
    gml_object.py ids <file.gmlo> <linked.bin>      ids the runtime linker wrote, per name
"""
import os, struct, sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bytecode_fixup import ID_MASK, REF_TYPE_MASK, Reloc, apply_fixup  # noqa: E402


@dataclass
class GmlObject:
    name: str = ""
    locals: int = 0
    args: int = 0
    local_names: list = field(default_factory=list)
    code: bytes = b""
    relocs: list = field(default_factory=list)      # bytecode_fixup.Reloc, kind "var" | "func" | "string"


def _unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            i += 1
            c = {"n": "\n", "r": "\r", "t": "\t"}.get(s[i], s[i])
        out.append(c)
        i += 1
    return "".join(out)


def parse(text):
    obj, lines = GmlObject(), text.split("\n")
    if not lines or lines[0].rstrip("\r") != "GMLO 1":
        raise ValueError("not a GMLO 1 object")
    ended = False
    for line in lines[1:]:
        line = line[:-1] if line.endswith("\r") else line
        if not line:
            continue
        key, _, rest = line.partition(" ")
        if key == "name":
            obj.name = rest
        elif key in ("locals", "args"):
            setattr(obj, key, int(rest))
        elif key == "local":
            obj.local_names.append(rest)
        elif key == "code":
            obj.code = b"".join(struct.pack("<I", int(w, 16)) for w in rest.split())
        elif key == "reloc":
            offset, kind, tail = rest.split(" ", 2)
            if kind == "var":
                inst, name = tail.split(" ", 1)
                obj.relocs.append(Reloc(int(offset), "var", name, int(inst)))
            elif kind in ("func", "string"):
                obj.relocs.append(Reloc(int(offset), kind, _unescape(tail)))
            else:
                raise ValueError(f"unknown relocation kind {kind!r}")
        elif key == "end":
            ended = True
            break
        else:
            raise ValueError(f"unknown record {key!r}")
    if not ended or not obj.name or not obj.code:
        raise ValueError("truncated object")
    for r in obj.relocs:
        if r.offset % 4 or r.offset + 8 > len(obj.code):
            raise ValueError(f"relocation outside the code: {r.offset}")
    return obj


def load(path):
    with open(path, encoding="utf-8", newline="") as f:
        return parse(f.read())


def link(obj, resolver, string_index):
    """Linked bytecode. resolver: bytecode_fixup.Resolver (variables, functions);
    string_index(text) -> runtime string index."""
    code = apply_fixup(obj.code, [r for r in obj.relocs if r.kind != "string"], resolver)
    out = bytearray(code)
    for r in obj.relocs:
        if r.kind == "string":
            struct.pack_into("<I", out, r.offset + 4, string_index(r.name))
    return bytes(out)


def runtime_ids(obj, linked):
    """{(kind, name): id} read back from bytes the runtime linker produced; raises if one name
    got two ids or a reference-type bit changed."""
    ids = {}
    for r in obj.relocs:
        before = struct.unpack_from("<I", obj.code, r.offset + 4)[0]
        after = struct.unpack_from("<I", linked, r.offset + 4)[0]
        if r.kind == "var":
            if (after & REF_TYPE_MASK) != (before & REF_TYPE_MASK):
                raise ValueError(f"{obj.name}@{r.offset}: reference type changed")
            value = after & ID_MASK
        else:
            value = after
        key = (r.kind, r.name)
        if ids.setdefault(key, value) != value:
            raise ValueError(f"{obj.name}: {key} linked to {ids[key]} and {value}")
    return ids


def main(argv):
    if len(argv) >= 3 and argv[1] == "dump":
        obj = load(argv[2])
        print(f"{obj.name}: {len(obj.code)} bytes, locals {obj.locals}, args {obj.args}")
        for r in obj.relocs:
            print(f"  {r.offset:5d} {r.kind:6s} {r.name!r}" + (f" (inst {r.inst_type})" if r.kind == "var" else ""))
        return 0
    if len(argv) >= 4 and argv[1] == "ids":
        obj = load(argv[2])
        with open(argv[3], "rb") as f:
            for (kind, name), value in sorted(runtime_ids(obj, f.read()).items()):
                print(f"{kind:6s} {value:8d}  {name!r}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
