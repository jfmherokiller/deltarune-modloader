# 11 — Runner file I/O across runners (DELTARUNE VM + Dwarf Eats Mountain YYC)

How the GameMaker Windows runner reads files, and how to find the hook points
without fixed RVAs. Two runners were compared:

| | DELTARUNE | Dwarf Eats Mountain v1.1.0 (DEM) |
|---|---|---|
| Runner | VM (bytecode in data.win), older `...\Runner\VC_Runner\...` build | YYC, GM 2024.14.4.268, `...\Runner\GMS2-Runner-Main\VC_Runner\...` |
| exe SHA-256 | `b882cc18…04e7ed` | `9a2370be…de37dd` (`Dwarf Eats Mountain.exe`) |
| Image base | 0x140000000 | 0x140000000 |

All addresses below are **RVAs** (add 0x140000000 for IDA). Resolver:
`tools/file_io_sigs.py <exe> [--json]` (stdlib only), which finds everything below on both
exes with no hardcoded addresses.

Method: IDA 9.1 headless (`idat -A -S<script>`) on scratch copies of both `.i64`
databases, Hex-Rays dumps of each function, plus a pefile/capstone sweep of `.pdata`,
E8 calls and RIP-relative LEAs to confirm callers and signature uniqueness.

## Call graph (same shape in both runners)

```
ReadEntireFile(path_utf8, u32* out_size) -> GmAlloc'd, NUL-terminated buffer | NULL
  ├─ wrapper "bundle"  (converter: GetFullPathNameW + program dir)
  ├─ wrapper "sandbox" (converter: PathCchCombine with save dir; "Error! not allowing file operation…")
  ├─ async-request thunk (path, callback)      — 0x100a920 / 0x91b80
  └─ font loader with "C:\Windows\Fonts\" prefix (DEM 0x1214c80; DR has 4 more direct callers)
```

| Function | DEM | DELTARUNE (IDB name) |
|---|---|---|
| ReadEntireFile | 0x100b8b0 | 0x92c40 `ReadEntireFile` |
| GmAlloc (allocator RER uses) | 0xefc530 (line arg 330) | 0x38f430 (line arg 339) |
| wrapper, bundle | 0x100a7c0 → conv 0x100ae90 | 0x91a40 `ReadEntireFile_Bundle` → conv 0x921b0 |
| wrapper, sandbox | 0x100a970 → conv 0x100b260 | 0x91bd0 `ReadEntireFile_Plain` → conv 0x92580 |
| async load thunk | 0x100a920 | 0x91b80 |
| FOpen (`LoadSaveFOpen.cpp`) | 0x1008bf0 | 0x8e150 |
| RunnerLoadGame (data.win) | 0xf1eff0 | 0xb9230 |
| exists-in-sandbox / exists-in-bundle | 0x100ab10 / 0x1009570 | 0x91e20 / 0x90270 |

### Per-feature paths

| GML / engine path | DEM | DELTARUNE | Reaches ReadEntireFile? |
|---|---|---|---|
| data.win, `.yydebug` | RunnerLoadGame 0xf1eff0 → bundle wrapper (or sandbox if "not in bundle") | 0xb9230 → same | **yes** (absolute path) |
| options.ini | RunnerLoadGame → ini ctor 0x109cea0 → wrappers | 0xb9230 → 0x47300 → wrappers | yes |
| `file_text_open_read` | builtin 0x108e1a0 → FOpen 0x1008bf0 → bundle, then sandbox wrapper | 0x15eb60 → 0x8e150 → same | yes |
| `file_bin_open` | 0x108d0c0 → converts path itself → FOpen(abs, "rb") | 0x15d9a0 → same | yes (already absolute) |
| `ini_open` | 0x108fab0 → ini ctor 0x109cea0 → wrappers | 0x160370 → 0x47300 → wrappers | yes |
| `load_csv` | 0x1090410 → 0x100d5a0 → wrappers | 0x160bd0 → 0xf77a0 → wrappers | yes |
| `buffer_load` | 0xee26c0 → `Buffer_Standard` vtable slot 8 (0xf76fa0): sandbox wrapper if it exists there, else bundle | 0x1dcdc0 → slot 8 (0x1e37e0) | yes |
| `buffer_load_async` | 0xee27d0 → 0xee3f20 → worker 0xee4110 → wrappers | 0x1dcec0 → 0x1de2c0 → worker 0x1de470 | yes |
| `sprite_add` / `sprite_replace` | 0xf84db0 → 0xfcc0e0 → 0xfc45b0 / 0xfc49f0 → wrappers (not for `data:` URIs) | 0x19f310 → 0xe3740 → 0xde460 / 0xde9c0 | yes |
| `font_add` | 0xf7d420 → 0x1060e60 → 0x10c3d60 → 0x10c47a0 → wrappers (or the `C:\Windows\Fonts` caller) | 0x199160 → 0x5c1d0 → 0x57920 → 0x58310 → wrappers | yes |
| audio groups (`audiogroupN.dat`) | 0x1065b80 → bundle wrapper | `audio_group_load` → 0xbc170 → bundle wrapper | yes |
| `audio_create_stream` | 0x1104ff0 → 0x1067eb0: converts the path, hands it to yyal stream open (0x117fac0) | 0x1a4840 → 0xbea60: converts the path, stores it in a stream slot; ogg opened later | **no** — needs a path-swap hook on the builtin (LooseMods already does this for DR) |

Caveats that affect hooks:

- **Existence checks bypass ReadEntireFile.** `buffer_load`, `sprite_add`,
  `audio_create_stream` and `file_bin_open` first call exists-in-sandbox / exists-in-bundle
  and return early on a miss. An override can therefore only replace a file that already
  exists in the game, unless those two checks are hooked too.
- `file_text_open_read` / `file_bin_open` read the whole file through ReadEntireFile into a
  `Buffer_Standard`; writes don't go through it.
- `.pdata` splits large functions into chained chunks (e.g. `file_text_open_read` DEM
  0x108e1a0 + 0x108e228), so "callers by E8 site" must merge chunks.
- The runner frees the RER buffer with its own free (`0xefc9d0` / `0x38fa00`); a hook
  that returns a substitute buffer must allocate it with GmAlloc.

## Disassembly

### ReadEntireFile — identical body in both runners

```
; DEM 0x100b8b0                                  ; DR 0x92c40 — same bytes except rel32s
push   rbp
sub    rsp, 40h
lea    rbp, [rsp+30h]
mov    [rbp+20h], rbx
mov    [rbp+28h], rsi
mov    [rbp+30h], rdi
mov    rax, cs:__security_cookie
xor    rax, rbp
mov    [rbp+8], rax
mov    rsi, rdx                    ; <- SIG_READ_ENTIRE_FILE starts here (+0x25)
mov    dword ptr [rsp+28h], 0
mov    rdi, rcx
mov    qword ptr [rsp+20h], 0
mov    r8, rcx
xor    edx, edx
mov    ecx, 0FDE9h                 ; CP_UTF8
mov    r9d, 0FFFFFFFFh
call   cs:MultiByteToWideChar
...                                ; alloca, second MultiByteToWideChar
lea    rdx, aRb                    ; L"rb"
call   _wfopen
test   rax, rax / jz fail
mov    r9d, 8000h                  ; setvbuf(f, 0, 0, 0x8000)
call   setvbuf
... fseek(SEEK_END) / fgetpos / fseek(SEEK_SET)
mov    rcx, [rbp+0]
lea    rdx, aLoadSaveCpp           ; "...\Files\IO\Win32\LoadSave.cpp"
inc    rcx
xor    r9d, r9d
mov    r8d, 14Ah                   ; DR: 153h (source line)
call   GmAlloc                     ; DEM 0xefc530, DR 0x38f430
```

Decompiled (DEM):

```c
FILE *ReadEntireFile(LPCCH path, _DWORD *out_size) {
  n = MultiByteToWideChar(CP_UTF8, 0, path, -1, 0, 0);
  w = alloca(2*(n+1));  MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
  f = _wfopen(w, L"rb");
  if (f) {
    setvbuf(f, 0, 0, 0x8000); fseek(f, 0, SEEK_END); fgetpos(f, &sz);
    if (out_size) *out_size = sz;
    fseek(f, 0, SEEK_SET);
    buf = GmAlloc(sz + 1, "...\\Files\\IO\\Win32\\LoadSave.cpp", 330, 0);
    buf[sz] = 0; fread(buf, 1, sz, f); fclose(f);
    return buf;
  }
  return f;
}
```

### Wrappers — identical except the converter

```
; DEM 0x100a7c0 (bundle)            DR 0x91a40 `ReadEntireFile_Bundle`
40 53                   push rbx
48 81 EC 30 08 00 00    sub  rsp, 830h
48 8B 05 xx xx xx xx    mov  rax, __security_cookie
48 33 C4                xor  rax, rsp
48 89 84 24 20 08 00 00 mov  [rsp+820h], rax
48 8B DA                mov  rbx, rdx
4C 8B C1                mov  r8, rcx
BA 00 08 00 00          mov  edx, 800h
48 8D 4C 24 20          lea  rcx, [rsp+20h]
E8 xx xx xx xx          call converter            ; +0x2B  bundle: 0x100ae90 / 0x921b0
48 8B D3                mov  rdx, rbx                      ; sandbox: 0x100b260 / 0x92580
48 8D 4C 24 20          lea  rcx, [rsp+20h]
E8 xx xx xx xx          call ReadEntireFile       ; +0x38
```

### FOpen (`file_text_open_read` / `file_bin_open`)

```c
// DEM 0x1008bf0, DR 0x8e150
if (mode has 'r' or 'a') {
  data = Wrapper_Bundle(path, &size);
  if (!data) data = Wrapper_Sandbox(path, &size);
  if (data) { buf = new Buffer_Standard(size); memmove(buf->data, data, size);
              if ('t' && BOM) skip 3; GmFree(data); }
}
```

### RunnerLoadGame (data.win)

```c
// DEM 0xf1eff0, DR 0xb9230
if (Exists_Bundle(game_path)) Convert_Bundle(buf, len, game_path);
else { Convert_Sandbox(buf, len, game_path); in_bundle = 0; log("not in bundle"); }
...  // options.ini via the ini ctor, <game>.yydebug via Wrapper_Bundle
data = in_bundle ? Wrapper_Bundle(buf, &size) : Wrapper_Sandbox(buf, &size);
log("Loaded File %s(%d)\n", buf, size);
```

### audio_create_stream (not on the ReadEntireFile path)

```c
// DEM 0x1067eb0 (yyal)                         // DR 0xbea60
if (Exists_Sandbox(p)) Convert_Sandbox(tmp, 1024, p);   // same branches, 2048-byte buffer
else if (!Exists_Bundle(p)) { "audio_create_stream: could not find file"; return -1; }
else Convert_Bundle(tmp, 1024, p);
yyal_stream_open(tmp, 1, &id);                  // DR: slot->path = strdup(tmp); opened on play
```

## Signatures / anchors

Both of these resolve exactly one function in each exe, and both must agree
(`tools/file_io_sigs.py` checks this):

1. **Byte signature** (function + 0x25, unique in `.text` of both):

   ```
   48 8B F2 C7 44 24 28 00 00 00 00 48 8B F9 48 C7 44 24 20 00 00 00 00
   4C 8B C1 33 D2 B9 E9 FD 00 00 41 B9 FF FF FF FF FF 15
   ```

   `mov rsi,rdx; mov [rsp+28h],0; mov rdi,rcx; mov qword [rsp+20h],0; mov r8,rcx; xor edx,edx;
   mov ecx,CP_UTF8; mov r9d,-1; call [MultiByteToWideChar]`. It contains no rel32, so it
   survives relinking.

2. **String anchor**: the only function that takes the address of a `.rdata` string ending
   in `\Files\IO\Win32\LoadSave.cpp` *and* contains `B9 E9 FD 00 00` (`mov ecx, CP_UTF8`) and
   `41 B9 00 80 00 00` (`mov r9d, 8000h`). Match the suffix: the prefix differs between runners
   (`...\Runner\VC_Runner\` vs `...\Runner\GMS2-Runner-Main\VC_Runner\`).

Derived (no RVAs needed once RER is known):

- **GmAlloc** = target of the first `E8` after RER's `lea rdx, <LoadSave.cpp>`. Its prologue
  differs between runners (DEM `48 89 5C 24 10 57 48 81 EC 70 04…`, DR `40 53 56 57 48 81 EC
  70 04…`), so the current LooseMods `SIG_GM_ALLOC` check at the fixed RVA would fail on DEM.
- **Wrappers** = RER callers whose first 0x3D bytes match `SIG_WRAPPER` (in the tool) and whose
  `E8` at +0x38 targets RER. Classify by converter: the one referencing
  `"Error! not allowing file operation with filename '%s'.\n"` is the sandbox mapper.
- **RunnerLoadGame** = only function referencing the exact string `"not in bundle"`.
- **FOpen** = function referencing `\Files\IO\LoadSaveFOpen.cpp` that calls both wrappers.
- **Builtins** (`file_text_open_read`, `buffer_load`, `sprite_add`, `audio_create_stream`, …):
  from the registration pairs `lea rdx, fn … lea rcx, "name"`. At run time,
  YYTK `GetNamedRoutinePointer` is simpler and is what LooseMods already uses.

Rejected: the 16-byte prologue LooseMods checks at RVA 0x92C40
(`40 55 48 83 EC 40 48 8D 6C 24 30 48 89 5D 20 48`) is shared by 11 functions in DEM and 14 in
DELTARUNE. It works as a sanity check at a known address, not as a search signature.

## Results of `tools/file_io_sigs.py`

```
                         DEM                         DELTARUNE
ReadEntireFile_by_string 0x100b8b0                   0x92c40
ReadEntireFile_by_sig    0x100b8b0                   0x92c40
GmAlloc                  0xefc530 (line 330)         0x38f430 (line 339)
wrappers.bundle          0x100a7c0 / conv 0x100ae90  0x91a40 / conv 0x921b0
wrappers.sandbox         0x100a970 / conv 0x100b260  0x91bd0 / conv 0x92580
RunnerLoadGame           0xf1eff0 (calls wrappers)   0xb9230 (calls wrappers)
FOpen                    0x1008bf0                   0x8e150
file_text_open_read      0x108e1a0                   0x15eb60
buffer_load              0xee26c0                    0x1dcdc0
sprite_add               0xf84db0                    0x19f310
audio_create_stream      0x1104ff0                   0x1a4840
```

The DELTARUNE results match the existing IDB names and LooseMods' fixed RVAs
(`RVA_READ_ENTIRE_FILE` 0x92C40, `RVA_GM_ALLOC` 0x38F430).

## Next steps

- In LooseMods, replace `RVA_READ_ENTIRE_FILE` / `RVA_GM_ALLOC` with a runtime scan:
  `SIG_READ_ENTIRE_FILE` in `.text`, with the string anchor as a cross-check, then GmAlloc
  from the call site. Port `resolve()` to C++.
- If overrides must add files the game doesn't ship, also hook exists-in-bundle
  (DEM 0x1009570 / DR 0x90270). Its signature hasn't been derived yet.
- Not tested in-game on DEM; everything above is static evidence.
