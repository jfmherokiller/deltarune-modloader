# Methodology

## Tooling

- **IDA Pro** with Hex-Rays, driven headless through idalib/IDAPython (decompile,
  disasm, rename, set_type, set_comments, xrefs, search, and an embedded IDAPython
  `py_eval` / `py_exec_file`).
- All number-base conversions were delegated to tooling; no manual base math was done
  (per project constraint). Hex values quoted here are copied verbatim from IDA output.

## Process

### 1. Triage
`survey_binary` gave the segment map, import categories, and the top functions ranked
by xref count. Two signals immediately identified the binary as the GameMaker VC_Runner:
- Compiler artifact paths embedded in assert strings:
  `D:\a\GameMaker\GameMaker\GameMaker\Runner\VC_Runner\...`
- GameMaker-specific error strings: *"Data structure with index does not exist."*,
  *"trying to index a property which is not an array"*, `DsMutex`, etc.

### 2. Core value model
Decompiling the highest-xref functions (`sub_1401BAA10`, `sub_1401BEF50`,
`sub_1401BEDE0`, …) revealed a recurring 16-byte stride (`a1 + 16*index`) and a type
switch on `*(int*)(p+12) & 0xFFFFFF`. The type-name function (`RValue_GetKindName`,
ex-`sub_1401BCBE0`) enumerates all 16 kind names verbatim, fixing the `RValueKind`
enum from evidence rather than assumption. The `RValue` struct and `RValueKind` enum
were declared in the IDB's local type library via `declare_type`.

### 3. Mass builtin recovery (the high-leverage step)
`sub_1401BAA10` was identified as `Function_Add(name, func, argc)` — it appends 24-byte
records to a global table — and its first caller `sub_140003640` registers
`time_source_*` / `call_later` builtins with their **real GML names as string literals**.

An IDAPython pass (`name_builtins.py`) then:
1. Collected all 2,514 `call` xrefs to `Function_Add`.
2. For each call, walked backwards through the basic block tracking the last loads of
   `rcx` (name string, via `lea`), `rdx` (function pointer, via `lea`), and `r8d`
   (argc — encoded as `lea r8d,[r9±N]` after `xor r9d,r9d`, *not* `mov`).
3. Read the name string, grouped by target function (handling aliases), and renamed
   each target to `gml_<name>`, attaching a repeatable comment with argc + aliases.

Result: **2,514/2,514 sites resolved, 0 failures**, 1,877 unique target functions,
1,876 renamed (one left as a CRT-mislabeled function). A `gml_` prefix was chosen to
avoid collisions with C-runtime symbols (`abs`, `sin`, `cos`, `round`, …) that share
names with GML builtins.

> Why this is reliable: the names are not inferred from behavior — they are the exact
> string literals the runtime itself passes to its own registration function. The only
> judgement call is alias selection (first-registered name is treated as canonical).

### 4. Primitive deep-dive
The `YYGet*` accessor family was completed by xref'ing the unique error-format strings
each one emits (e.g. *"expecting a Number (YYGU32)"*), guaranteeing correct
identification. Each was renamed, given a correct signature (`RValue*` args), and
commented.

### 5. Subsystems
Per-subsystem deep dives (e.g. `ds_*`) were done as focused passes that read
the decompilation, renamed internal helpers, and wrote a subsystem document.

## Accuracy principles applied

### Chapter GML extraction (2026-09-06)

Executed UndertaleModTool CLI 0.9.2.0 on `<user>@<linux-host>`, where the game archives
and NTFS volume are local. The SSHFS view on the working machine is `/tmp/mount`.
The original flat `code/` dump was preserved. New dumps are flat within
`code/chapterN/`; each has `extraction.json` recording source path, byte size,
SHA-256, tool version, extraction scope, and GML count.

| Directory | GML files | Scope |
|---|---:|---|
| `code/chapter1/` | 1,293 | Full CLI code dump; exit 0 |
| `code/chapter2/` | 5,840 | Full CLI code dump; exit 0 |
| `code/chapter3/` | 1,188 | Interrupted bulk dump plus four targeted stat scripts |
| `code/chapter4/` | 4 | Targeted stat scripts only |
| `code/chapter5/` | 4 | Targeted stat scripts only |

Full dumps of chapters 3-5 remain unfinished. Bulk output to the NTFS drive was
slow; targeted extraction used a temporary directory on the host and copied only
the requested GML into the repo. Every targeted extraction produced all four
requested files. Partial bulk files outside that set are reference material requiring
verification before use. No game archive or IDB was modified.

Commands, with paths adjusted to the desired chapter and staging directory:

```sh
utmtcli dump /path/to/chapterN_windows/data.win -o /tmp/chapterN-dump \
  -c UMT_DUMP_ALL < /dev/null

utmtcli dump /path/to/chapterN_windows/data.win -o /tmp/chapterN-stats \
  -c gml_GlobalScript_scr_gamestart \
  -c gml_GlobalScript_scr_initialize_charnames \
  -c gml_GlobalScript_scr_damage \
  -c gml_GlobalScript_scr_heal < /dev/null
```

The CLI emits GML under `CodeEntries/`; place those files directly in the chapter
directory. Repeat `-c` for multiple names: a quoted space-separated list is treated
as one name, and an absent entry can still yield exit 0. Verify actual output files
and inspect logs, not just process status. A full dump's exit 0 does not prove every
decompiled script is error-free.

### Evidence standards

- Names for builtins come from the binary's own string table, not guesses.
- Type identities (`YYGetUint32`, etc.) are anchored to unique error strings.
- Struct offsets quoted in the docs are taken from decompiled field accesses.
- Where a function's purpose was not fully determined, it was left `sub_*` rather than
  given a speculative name. A handful of high-xref functions (e.g. `sub_140082340`, an
  instance depth/grid manager) are flagged as "needs further analysis".


## Linux FakePDB headless verification (2026-09-06)

CONFIRMED: FakePDB is installed in Linux **user plugins**, with entry point
`~/.idapro/plugins/fakepdb.py`, package `fakepdb/`, and ELF x86-64 native
backends in `fakepdb/linux_amd64/` (`fakepdb_pdb`, `fakepdb_pe`, `fakepdb_coff`).
The local `native.py` maps `platform.machine() == "x86_64"` to `amd64`.

Tested in a separate idalib process using an idalib Python virtualenv.
Copied the reference IDB and EXE into `/tmp/deltarune-fakepdb-probe/` before opening.
`idapro.open_database(copy_path, run_auto_analysis=False)` returned zero;
`ida_diskio.get_user_idadir()` returned `~/.idapro`. FakePDB modules were
already in `sys.modules`, and `ida_kernwin.get_registered_actions()` included
`fakepdb_pdb_generation` and `fakepdb_pdb_generation_labels` without manual loading.

CONFIRMED: direct headless export succeeded with:

```python
from fakepdb.dumpinfo import DumpInfo
from fakepdb.native import Native

DumpInfo().dump_info("/tmp/deltarune-fakepdb-probe/export.json")
Native().pdb_generate(
    "/tmp/deltarune-fakepdb-probe/export.json",
    "/tmp/deltarune-fakepdb-probe/DELTARUNE.pdb",
    "/tmp/deltarune-fakepdb-probe/DELTARUNE.exe",
    False,
)
```

Closed the copied database with `idapro.close_database(False)`. The reference IDB,
EXE, JSON export, and PDB were not overwritten. This verifies automatic plugin loading
and native export headlessly on Linux. A process
running under another user or `IDAUSR` configuration would need its own check.
These generated symbols remain downstream analysis output, not shipped debug symbols.
