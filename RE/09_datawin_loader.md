# The `data.win` loader

Track A pass **P6**. Traces the full path from process startup to the raw bytes of
`data.win` landing in memory and being dispatched chunk-by-chunk, and identifies the
runner's universal file-read primitive. All names below are anchored to literal trace
strings the functions themselves print, or to unambiguous call-site evidence — see the
methodology in [01_methodology.md](01_methodology.md).

## Call chain

```
sub_1401FF5E0 (WinMain-era bootstrap, unnamed — prints the
               "YoYo Games Runner v2022.0(3)[r115]" banner, "VM Init", "Do The Work", ...)
  └─ RunnerLoadGame        (0x1400B9230)  — resolve path, read file, sanity-check
       └─ LoadGameData     (0x1400B9830)  — thin wrapper
            └─ LoadGameData_ProcessChunks (0x1400B7E50) — walk FORM chunks, dispatch
```

### `RunnerLoadGame` (`0x1400B9230`)

Evidence: prints `"RunnerLoadGame: %s\n"` on entry and `"RunnerLoadGame() - %s\n"` once
the path is resolved.

1. **Resolve the game file path**, in order:
   - Embedded/bundled data: if `qword_1406A9DD8` (a struct with the embedded blob at
     `+0`) is set and non-null, use that directly — the exe-embedded `data.win` case.
   - Command-line: if `qword_1408BA7A0` (the runner's own copy of a `-game <path>`
     argument) is set, use it. **This is the same argument our own dynamic testing
     observed**: the relaunched chapter process runs as
     `DELTARUNE.exe -game data.win launcher switch_-1 returning_0` (see
     [`TODO.md`](TODO.md) Track B, 2026-09-16/17) — confirms `-game` is how the runner
     is told which data file to open, and that the launcher relaunches itself with an
     explicit path rather than only relying on a default alongside the exe.
   - Otherwise: `sub_1400BA000()` — a default-search fallback (not yet decompiled).
2. **Load sibling files** next to the resolved path: replaces the filename with
   `/options.ini` (prints `"Checking if INIFile exists at %s\n"`) and separately with
   `.yydebug` — the latter is parsed with its own tiny inline FORM-style chunk walker,
   tags `DBGI`/`SCPT`/`INST` (debug-info sidecar, unrelated to the main asset chunks
   below; not pursued further here).
3. **Read the main file**: calls `ReadEntireFile_Bundle` or `ReadEntireFile_Plain`
   (chosen by a `v0` flag set earlier in the same function, itself driven by whether
   the game path came from the bundle/embedded case) with the resolved path, getting
   back a heap buffer (`qword_1406A9D78`) and its size (`dword_1406A9D3C`).
4. **Validate**: checks the buffer's first 4 bytes equal `FORM` (`0x4D524F46`) or the
   byte-swapped `MROF` (`0x464F524D`) — prints `"IFF wad found\n"` if so. No explicit
   handling was observed for the negative case beyond falling through (a missing/
   corrupt file is caught earlier by a null-buffer check that calls `exit(1)` with
   `"Unable to find game!!: %s"`).

### `LoadGameData` (`0x1400B9830`)

Evidence: prints the literal `"LoadGameData()\n"`. A one-line wrapper:
`LoadGameData_ProcessChunks(qword_1406A9D78, dword_1406A9D3C)` — passes through exactly
the buffer/size `RunnerLoadGame` populated.

### `LoadGameData_ProcessChunks` (`0x1400B7E50`)

Name is evidence-anchored to its role and call site, not a literal string match (its
own entry trace is the more generic `"initialise everything! %p, %u\n"`).

Walks the buffer starting at offset 8 (the initial 8 bytes are the outer `FORM` header:
4-byte magic + 4-byte total length, already validated by the caller). Each chunk is
`{ char tag[4]; uint32 length; }` followed by `length` bytes of payload, repeated until
the whole buffer is consumed. The tag is read as a raw little-endian `uint32` — i.e. the
4 ASCII bytes in file order become that integer's bytes from LSB to MSB — and dispatched
through a large if/switch chain. Every constant below was decoded with a script (not by
hand), per this project's no-manual-base-math rule.

#### Chunk tag → handler table (confirmed 2026-09-17, re-verified against raw disassembly)

The full table below was cross-checked against the actual `cmp`/`je`/`ja` instruction
stream of the dispatch (via `capstone`, not just Hex-Rays' if-chain reconstruction —
see the `STRG`/`TPAG` corrections below for why that distinction mattered).

| Tag | Handler | Notes |
|---|---|---|
| `GEN8` | `LoadGameData_ProcessChunk_GEN8` (`0x1400B7930`) | Bytecode version, default room, 16-byte game GUID, window w/h/color-depth settings, anti-tamper checksum check (Steam/protected builds) |
| `OPTN` | `LoadGameData_ProcessChunk_OPTN` (`0x140099F50`) | **Decompiled 2026-09-17.** Two on-disk formats (old flag-array vs. newer bitfield, distinguished by a version sentinel in the header) populate ~25 global option flags (fullscreen/vsync/scale-mode/etc.); trailing key-value pairs handle `@@SleepMargin`/`@@DrawColour`/`VersionMajor`/`VersionMinor` as special-cased string keys, everything else goes into a generic name→value array |
| `EXTN` | *(no-op)* | Recognized, falls through without a call in this build |
| `SOND` | `LoadGameData_ProcessChunk_SOND` (`0x1400C0A40`) | **Decompiled 2026-09-17.** Prints `"Audio_Load()\n"`. Allocates one 0x98-byte sound record per entry, resolves name + type/kind fields |
| `AGRP` | `sub_1400BDD60` | Audio groups — **conditional**: only called if `byte_1406AAE88` is set |
| `SPRT` | `LoadGameData_ProcessChunk_SPRT` (`0x1400E4A30`) | Per entry: allocate a 0xD8-byte sprite record, populate via `sub_1400DDB50` (frame/TPAG references, not yet decompiled — next target for per-sprite override), index by name |
| `BGND` | `LoadGameData_ProcessChunk_BGND` (`0x140046750`) | **Decompiled 2026-09-17.** Same allocate-record/resolve-name/name-dup pattern as `FONT`/`PATH`; 0x50-byte records. Storage lives in `g_ChunkResourceGlobals` (offset ~4862) — see note below on that global |
| `PATH` | `LoadGameData_ProcessChunk_PATH` (`0x1400A47D0`) | **Decompiled 2026-09-17.** 0x38-byte path records, own dedicated globals (`unk_1406A7D48`/`_50`/`_58`/`_60`), *not* part of `g_ChunkResourceGlobals` |
| `SCPT` | `LoadGameData_ProcessChunk_SCPT` (`0x1400BADA0`) | Per entry: resolve name, allocate a 0x38-byte script record |
| `FONT` | `LoadGameData_ProcessChunk_FONT` (`0x14005C9E0`) | **Decompiled 2026-09-17.** 0xD0-byte font records via `sub_14005B960`; storage in `g_ChunkResourceGlobals` (offset ~9696) |
| `SHDR` | `LoadGameData_ProcessChunk_SHDR` (`0x1401F54E0`) | **Decompiled 2026-09-17.** 0xD8-byte shader records (own globals, `dword_1408D66AC`); each carries up to 5 named GLSL/HLSL-ES variants (vertex/pixel source + attribute/uniform name arrays per variant, gated by a per-entry variant count ≥1/≥2 check) and validates via `sub_1402F96D0`, logging `"Invalid shader (is it marked as incompatible type for this target?) \"%s\":\n"` and tagging the record `"Invalid shader"` on failure |
| `TMLN` | `LoadGameData_ProcessChunk_TMLN` (`0x140126380`) | **Decompiled 2026-09-17.** Allocates real C++ `CTimeLine` objects (RTTI vtable `??_7CTimeLine@@6B@` set explicitly), each owning a `cOwningArrayDelete<CEvent*>` array — ties into the same RTTI class-hierarchy work as P2 |
| `OBJT` | `LoadGameData_ProcessChunk_OBJT` (`0x140099880`) | Per entry: allocate a 0x98-byte object-type record, insert into a hashmap by object index; appends one synthetic `__YYInternalObject__` sentinel entry after all real objects |
| `ROOM` | `LoadGameData_ProcessChunk_ROOM` (`0x1400B6910`) | Per entry: resolve name, allocate a 0x218-byte room record |
| `TPAG` | *inline* — **corrected 2026-09-17** | Disasm-confirmed (`0x1400B830F`–`0x1400B8339`): stores the raw chunk-payload pointer into `g_TPAGChunkPtr` (renamed from `unk_1406A9D20`), **no sub-function call**. Previously mis-documented as calling `sub_140072560` — that call is `EMBI`'s handler (see below), a different case entirely; per-`TPAG`-entry parsing happens lazily elsewhere via `g_TPAGChunkPtr`, not decompiled |
| `CODE` | *inline* | GML bytecode fixup — see below, not a sub-function call |
| `VARI` | *(no-op — `break`)* | Deliberately ignored here; the real variable-name table comes from `qword_1406A9DD8` and is processed separately, after the chunk loop (see below) |
| `FUNC` | `LoadGameData_ProcessChunk_FUNC` (`0x1400B72E0`) | Per entry: resolve the function by name (fatal error if not found), then walks a linked relocation chain patching every bytecode call-site with the resolved runtime function id — the symbolic-name→runtime-id fixup companion to the inline `VARI` fixup |
| `STRG` | *(no-op)* — **corrected 2026-09-17** | Disasm-confirmed (`0x1400B823E`–`0x1400B824A`, `cmp`/`je` directly to the shared no-call continuation label): **STRG has no handler at all in this dispatch — a pure no-op**, exactly like `VARI`/`RASP`/`EXTN`/`HELP`/`STAT`/`PSPS`. The earlier "STRG = `sub_140123AA0`" mapping was wrong (that address is `SEQN`, already corrected once); this closes the open item for real — there is no separate string-table chunk handler to find, the string data must be consumed some other way (likely lazily, via the same `unk_1406A9DE8`-relative offset resolution every other chunk uses to turn string *references* into pointers) |
| `TXTR` | `LoadGameData_ProcessChunk_TXTR` (`0x1400B9860`) | Per entry, calls `Texture_DecodePNGAndUpload` (`0x1400728B0` — decodes the embedded PNG, this binary statically links libpng, and uploads a GPU texture) and wires up the resulting handle; a second pass patches texture-page-item→TXTR-index references. **This is the key function for image/texture override** — see below |
| `AUDO` | `LoadGameData_ProcessChunk_AUDO` (`0x1400C47A0`) | Prints `"Audio_WAVs()\n"`. Per live sound object matching the loading audio-group id, computes a pointer into this chunk's embedded WAV blob and stores it as the sound's raw-data pointer — this is what "Audio group N -> Loading..." (seen in the dynamic session) corresponds to |
| `ACRV` | `LoadGameData_ProcessChunk_ACRV` (`0x140104B10`) | **Decompiled 2026-09-17.** A 3-level hierarchy (curve → channel → point; 0xC0-byte channel records, 0xA8-byte point records) allocated per entry; calls `Instance_UpdateObjectTypeLinks` (the P3 instance-manager function, `08_instances.md`) after each curve and each channel — confirms that function is a generic per-resource-type registration hook, not specific to live game-object instances |
| `SEQN` | `LoadGameData_ProcessChunk_SEQN` (`0x140123AA0`) | Allocates 0x108-byte `CSequenceInstance`-shaped records; in debug builds (`byte_1405FCF10`) registers each with the instance manager |
| `TAGS` | `LoadGameData_ProcessChunk_TAGS` (`0x1400368E0`) | **Decompiled 2026-09-17.** A one-line wrapper: `sub_140036260(&g_ChunkResourceGlobals[324], a1, a2, a3)` — delegates to a shared helper, storage at offset ~324 of the same shared globals block `FONT`/`BGND`/`LANG` use |
| `FEAT` | *inline* | Sets `dword_1406A9DB0`(count)/`qword_1406A9DB8`(array) — feature flags, not yet fully traced |
| `STAT` | *(no-op)* | Recognized, falls through |
| `TGIN` | `LoadGameData_ProcessChunk_TGIN` (`0x140078250`) | **Decompiled 2026-09-17.** 0x60-byte-per-entry records: name + nested arrays of texture-page/sprite/font/tileset-style indices, with a pass that expands per-source-texture-page groups into per-frame index lists (`sub_1401FC320`/`sub_1401FBC20`) — "texture group info" reading confirmed plausible, exact consumer not traced further |
| `EMBI` | `sub_140072560` + `sub_14009F270` | **Disambiguated 2026-09-17** (see `TPAG` above — that entry was the actual mixup, this one was already correct): `sub_140072560` resizes/copies a 16-byte-per-entry array resolving two offset fields per entry; the unconditional trailing `sub_14009F270()` call is unrelated to `EMBI`'s own data — it resolves 14 hardcoded `pt_shape_*` particle-shape names into sprite indices (a one-time particle-system bootstrap that happens to run right after `EMBI`, since sprites are loaded by this point in the FORM order) |
| `DAFL` / `VARI` (dup case) | *(no-op — `break`)* | `DAFL` shares a no-op case with `VARI` above |
| `HELP` | *(no-op)* | Recognized, falls through |
| `RASP` | *(no-op)* | Recognized, falls through — silently skips the whole `AGRP`/`SHDR`/`TXTR`/`FEDS` sub-switch when seen |
| `FEDS` | `LoadGameData_ProcessChunk_FEDS` (`0x1401F05D0`) | **Decompiled 2026-09-17.** Small 0x28-byte records; per entry, resolves a name string and calls a name→handle registration pair (`sub_1401F4590`/`sub_1401F2B20` against `dword_1405FCFA0`) — purpose still unconfirmed beyond "another named-registration table", not guessed at further |
| `GMEN` | *inline* | Sets `dword_1406A9D9C`(count)/`qword_1406A9DA8`(pointer) — undocumented in public GameMaker-format references; not yet traced further |
| `GLOB` | *inline* | Sets `dword_1406A9D98`(count)/`qword_1406A9DA0`(pointer) — likewise undocumented publicly |
| `PSPS` | *(no-op)* | **New, previously undocumented 2026-09-17.** Disasm-confirmed at `0x1400B8772` — recognized and silently skipped (`je` straight to the no-call continuation), like `GMEN`/`GLOB` not in UndertaleModTool's public chunk list. Purpose unconfirmed |
| `LANG` | `LoadGameData_ProcessChunk_LANG` (`0x1400109C0`) | **New, previously undocumented tag, fully decompiled 2026-09-17.** See dedicated section below — a complete localization table embedded directly in `data.win`, separate from the loose `lang_en.json`/`lang_ja.json` files |
| `NINE` | *(no-op, warns)* | Prints `"Nine-slice resource type not handled yet\n"` |
| *anything else* | — | Falls to `"unknown Chunk %s:%d\n"`, does **not** abort loading |

`GMEN`/`GLOB`/`PSPS` are **not** in UndertaleModTool's public documented chunk list —
flagged, needs further analysis rather than guessed at. `LANG` is also undocumented
publicly but *is* fully understood here (see below). Only `sub_1400DDB50` (`SPRT`'s
per-entry populate function) and the `EMBI`/`GEN8`-adjacent sub-helpers remain as
loose ends from this chunk-table pass — every tag now has a confirmed disposition.

##### A shared globals block, not a "table": `g_ChunkResourceGlobals`

`FONT`, `BGND`, `TAGS` (via its `sub_140036260` worker), and `LANG` all read/write
through the *same* symbol, `g_ChunkResourceGlobals` (`0x14067E430`), but at very
different, non-overlapping offsets (`LANG` at +0, `TAGS` at +324, `BGND` at ~+4862,
`FONT` at ~+9696). This is almost certainly not one conceptual "table" — it's the
IDA/Hex-Rays decompiler merging several adjacent-but-unrelated linker-placed globals
(one small state block per resource-manager subsystem) into a single array symbol
purely because they're contiguous in `.bss`/`.data`. Treat each subsystem's slice as
its own thing; don't assume fields at one offset relate to fields at another. (An
earlier pass in this same session briefly renamed this symbol to `g_LangTable` before
this was understood — corrected before saving.)

##### New chunk: `LANG` — embedded localization table

On-disk layout (all offsets confirmed by decompiling `LoadGameData_ProcessChunk_LANG`):

```
int32 languageCount;
int32 entryCount;                          // number of localization keys
int32 keyNameOffsets[entryCount];          // -> key-name strings, e.g. "lb_credits"
struct {
    int32 langCodeOffset;                  // -> e.g. "en", "ja"
    int32 langNameOffset;                  // -> e.g. "English", "日本語"
    int32 valueOffsets[entryCount];        // -> localized string, same key order
} languages[languageCount];
```

All offsets resolve the same way every other chunk's string references do:
`unk_1406A9DE8 + offset` (zero offset = null). The runtime builds an in-memory table
at `g_ChunkResourceGlobals+0` (languageCount, entryCount, a resolved key-name pointer
array, and a resolved `languages[]` array each with its own resolved value-pointer
array) and sets `g_LangTableStatus` (renamed from `dword_1405F4130`) to `0` if the
chunk had usable data or `-1` if `languageCount`/`entryCount` was ≤0 (disabled).

**Open question worth flagging, not yet resolved**: this is a second, independent
localization mechanism from the loose `chapter1_windows/lang/lang_en.json` /
`lang_ja.json` files Track B's dynamic testing already found and successfully
overrode via the `ReadEntireFile` hook (see below and `TODO.md`). Whether the engine
actually *uses* this `LANG` chunk's data at runtime (vs. it being vestigial, or used
by a different game/build than DELTARUNE, or consulted only as a fallback) is not
established — no xrefs to `g_ChunkResourceGlobals+0`/`g_LangTableStatus` were traced
beyond the chunk loader itself in this pass. Worth checking before assuming this is a
second override surface worth targeting.

### Asset-override hook points (corrected/sharpened 2026-09-17)

For the original "load unpacked files" goal, decompiling the asset-bearing handlers
identified concrete, evidence-backed hook candidates beyond the whole-file
`ReadEntireFile` override already proven working (see below):

- **`Texture_DecodePNGAndUpload`** (`0x1400728B0`) — called once per `TXTR` entry with
  the embedded PNG bytes. Hooking here and substituting a different PNG's bytes (loaded
  from a loose file) overrides sprite/background/font pixel data without touching
  `data.win` at all — likely simpler than patching the `TXTR` chunk's on-disk bytes.
  This overrides an entire *texture page* (everything packed into it), not one sprite.
  **CONFIRMED live 2026-09-17** (signature: `__int64 __fastcall
  Texture_DecodePNGAndUpload(void* png_bytes, int size, __int64 a3, char a4)`) —
  extended the proven proxy-DLL + MinHook pipeline with a second hook alongside
  `ReadEntireFile`, always substituting a small solid-magenta 64×64 PNG for whatever
  bytes/size the runner passed in. Against the scratch rig's launcher `data.win`, the
  hook fired 3 times (once per `TXTR` entry, original sizes 1207/320817/307752 bytes),
  each successfully replaced with the 181-byte override — confirmed both via the hook
  log and visually: **every rendered sprite/button/cursor on the chapter-select screen
  turned solid magenta**, and the game kept running normally (no crash). Whole-texture-
  page override via this hook is proven, not just theorized. Build artifact kept at
  `mod_test/aurie_build/version_texdecode_test.dll` at the time, but this was later
  **removed entirely** (2026-09-17, same day) — the magenta override was unconditional
  and made the UI unreadable if the build was ever run again by accident, and the
  finding itself is already fully documented here. The rig's `dllmain.c` and live
  `version.dll` no longer contain any texture hook; re-add only if actually needed.
- **`SPRT_PopulateFrames`** (`0x1400DDB50`, renamed from `sub_1400DDB50`, called from
  `LoadGameData_ProcessChunk_SPRT`) — **decompiled 2026-09-17, and this changes the
  plan**: `TPAG` turned out to be a lazy pointer-stash (see above), so it is *not* where
  per-sprite frame wiring happens. `SPRT_PopulateFrames` is. For a normal (non-Spine,
  non-"layered") sprite — the common case — it builds a per-frame pointer array at
  `self+56`, where each entry is `unk_1406A9DE8 + <raw offset stored in the SPRT
  entry>`, resolved via the *same* universal file-relative-offset pattern every other
  chunk uses for string/array references — **not** via `g_TPAGChunkPtr` at all (that
  global is consumed only by `TXTR`'s own second pass, unrelated). **This is the real
  hook point for redirecting one named sprite's frame(s) to a different texture-page
  entry** — surgical, single-sprite override, as opposed to `Texture_DecodePNGAndUpload`
  which affects a whole shared texture page. Not yet exploited, just located.
  **Bonus finding**: this same function also handles two previously-undocumented sprite
  subtypes selected by a discriminator field in the `SPRT` entry — a "layered" composite
  sprite type (`sub_1400CE3A0`) and full **Spine 2D skeletal-animation sprite support**
  (`sub_1401F7B50`/`sub_1401F7D70` depending on sub-version, with `"Spine Error
  Detected: %s - %s\n"` validation) — i.e. this GameMaker runtime can load Spine-rigged
  sprites, gated further behind `unk_1408BA830` (the protected/Steam-build flag) for an
  additional skeleton+atlas tail. Not established whether DELTARUNE's own `data.win`
  files actually contain any Spine-typed sprites — worth a quick check
  (`data.win.offsetmap.txt` or a raw scan) before investing further here.

**`CODE` is handled inline, not via a sub-function call** — this is the GML bytecode
fixup pass: for each code entry, patches per-instruction operand bytes using a 32-entry
lookup table (`byte_1405FC160`) keyed by the top 5 bits of each instruction's high byte,
with a special case remapping opcode `21` and a `g_UseAltVarNameTable`-gated alternate
path for one of the two known engine variable-name-table layouts (see
[07_globals_and_variable_storage.md](07_globals_and_variable_storage.md)). This is the
step that converts the on-disk bytecode representation into whatever in-memory form the
VM actually executes — a strong candidate for the next drill-down if code-injection/
hot-reload of GML itself is ever pursued (a much harder target than a loose sprite or
sound, since it requires reproducing this fixup, not just substituting bytes).

After the chunk loop, the function also builds the runtime's global-variable-name
tables (`g_UserVarNameCount`/`g_UserVarNames`/`g_UserVarNamesAlt`) from
`qword_1406A9DD8`, and bootstraps `g_pGlobalObject`/`g_pInstanceManager` — this is the
same global-object graph documented in
[07_globals_and_variable_storage.md](07_globals_and_variable_storage.md).

## The universal file-read primitive

Distinct from the chunk-format parsing above, and much more broadly useful: the runner
has one low-level "read an entire file into memory" primitive used for essentially
*everything* it reads from disk, not just `data.win`.

```
ReadEntireFile_Bundle(path, &out_size)   [0x140091A40, 27 call sites]
ReadEntireFile_Plain (path, &out_size)   [0x140091BD0, 24 call sites]
    │  (path-encoding wrappers — Bundle via sub_1400921B0, Plain via sub_140092580;
    │   RunnerLoadGame picks Bundle for embedded/bundled data, Plain otherwise —
    │   evidence: the "not in bundle" trace string sits on the Plain-converter branch)
    ▼
ReadEntireFile(path_utf8, &out_size)     [0x140092C40]
    MultiByteToWideChar(CP_UTF8) → _wfopen(path, L"rb") → setvbuf → fseek(END) +
    fgetpos (size) → fseek(0) → heap-alloc size+1 (null-terminated) → fread → fclose
```

`ReadEntireFile`'s callers span `data.win`/`options.ini`/`.yydebug` loading
(`RunnerLoadGame`), `Register_Gamepad_Functions` (gamepad config), the `gml_file_copy`
builtin, and a long list of still-unnamed `sub_*` functions — i.e. this is a
general-purpose utility, not something written specifically for game-data loading.

**CONFIRMED 2026-09-17 — this is the working hook point for whole-file overrides.** A
detour on `ReadEntireFile`, checking for an override at a shadow path before falling
through to the real read, was built and verified live against the real, fully
unmodified game (proxy-DLL injection + vendored MinHook — the pipeline built and
verified the same session, see [`TODO.md`](TODO.md) Track B 2026-09-16/17, which also
captured ~2,500 live `Function_Add` calls as an earlier proof of the technique). The
hook logged every file the vanilla runner reads through this path
(`data.win`, `options.ini`, `chapter1_windows/lang/lang_en.json`,
`%LOCALAPPDATA%\DELTARUNE\hiscore.dat`/`dr.ini`, a per-chapter `true_config.ini` — the
last three previously undocumented), then live-substituted a completely different file
(the game's own `lang_ja.json`) in place of `lang_en.json` — confirmed by the resulting
buffer size matching the substitute (700,438 bytes) rather than the original (542,800)
— and the game accepted it and kept running with no crash. Whole-file override via this
hook point is **done and proven**, not just theorized.

**Confirmed limit**: this hook point does *not* cover everything. Loose `.ogg` audio
files (tried `AUDIO_INTRONOISE.ogg`) never triggered the hook despite the file existing
and being audibly part of the intro — audio almost certainly streams through a separate
reader (opens once, reads incrementally) rather than this whole-file-preload primitive.
Redirecting audio, and surgically replacing one sprite/script *inside* an otherwise
untouched `data.win` (rather than swapping the whole file), both need the individual
per-chunk handlers below decompiled first — bigger lift, not started.

**Frida note**: per a user suggestion, Frida (read-only observation against the
*unmodified* exe, cleaner than injecting anything) was tried first for this
verification and did not work in this environment — `ptrace_scope=1` blocks
attach-by-PID outright (fixable, `sudo sysctl kernel.yama.ptrace_scope=0`, done this
session), but even then native-realm attach crashes Frida's own injection bootstrapper
against this wine-hosted process, `--realm emulated` (the mode for exactly this
scenario) isn't supported by this pipx-installed Frida 17.17.0 build, and spawn-mode
(`frida -f wine -- DELTARUNE.exe`) loses its agent immediately after resuming. The
proxy-DLL + MinHook pipeline was used instead and is what actually produced the results
above. Don't re-attempt the same Frida approach without a different Frida
build/version; full detail in `TODO.md`.

## Open ends / needs further analysis

**Status 2026-09-17 (chunk-handler pass, continued): every chunk tag in the dispatch
now has a confirmed disposition** — decompiled handler, confirmed inline logic, or
confirmed no-op — including the two corrections (`STRG`, `TPAG`) and two new tags
(`LANG`, `PSPS`) this pass found. Remaining open items are all one level deeper:

- `sub_1400BA000` — default game-file-search fallback when no `-game` arg and no
  embedded data. Not decompiled.
- `sub_1400921B0` / `sub_140092580` — the bundle vs. plain path-resolution converters.
  Named by inference from call-site symmetry with `RunnerLoadGame`'s "in bundle" /
  "not in bundle" branches; not individually decompiled or renamed.
- `GMEN`, `GLOB`, `PSPS` chunk tags are not in UndertaleModTool's public chunk-format
  documentation (`LANG` isn't either, but is now fully understood — see above). Could
  be YoYo-internal/build-tooling chunks not part of the format modding tools target —
  worth cross-checking against `data.win.offsetmap.txt` (the launcher `data.win`'s own
  UndertaleModLib-generated offset map) for a tag list to compare against.
- `SPRT_PopulateFrames` (formerly `sub_1400DDB50`) is now decompiled and the real
  per-sprite-frame override hook point is located (see the asset-override section
  above) — not yet exploited (no hook actually installed/tested). Its "layered" and
  Spine-skeletal sprite sub-branches (`sub_1400CE3A0`, `sub_1401F7B50`/`sub_1401F7D70`)
  are still unexplored — low priority unless DELTARUNE is confirmed to actually use
  either subtype.
- Shared/generic workers referenced by several handlers but not themselves
  decompiled: `sub_140036260` (`TAGS`'s real worker), `sub_1401F2B20`/`sub_1401F4590`
  (`FEDS`'s name→handle registration, purpose still unconfirmed), `sub_1401FC320`/
  `sub_1401FBC20` (`TGIN`'s per-frame index expansion).
- Whether the `LANG` chunk's in-memory table is actually consulted anywhere at
  runtime (see the open question in its section above) — no consumer xrefs traced yet.
- `FEAT`'s inline handler (sets a count/array pair) is stored but not traced to any
  consumer either.
