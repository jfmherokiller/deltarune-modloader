# An IL2CPP-style toolchain for YYC GameMaker games

Status: working toolchain, verified on one game. Every claim below is labelled
**verified** (observed here), **existing tool**, or **inferred**/**untested**.

Test target: Dwarf Eats Mountain 1.1.0, x64, runner `2024.14.4.268`.
`Dwarf Eats Mountain.exe` SHA-256 `9a2370be7bd9196722a44c69df064ad5cde72258d8771668c3d17da891de37dd`.
Runtime tests ran under Wine 11, using the repo's `version.dll` loader, YYToolkit built
from the submodule, and the plugins below.

| Piece | IL2CPP equivalent | Where |
|---|---|---|
| static symbol dump + IDA script | Il2CppDumper / Cpp2IL | `tools/yyc_dump.py` |
| runtime name dump | (il2cpp API names) | `modloader/yyc_runtime_dump/` |
| call/hook by name | Il2CppInterop + Harmony | `modloader/yyc_interop/` (header + example mod) |
| loader | BepInEx / MelonLoader | `version.dll` modloader + Aurie + YYToolkit |
| GML bytecode on YYC | (none: IL2CPP has no interpreter) | `modloader/yyc_vm_probe/` (experiment) |

## Why the comparison holds

Unity IL2CPP and GameMaker YYC both translate script code to C++ and compile it into
the executable. In both, the binary keeps metadata, because the runtime still has to find
code by name:

| IL2CPP | YYC |
|---|---|
| `global-metadata.dat` | `data.win` without the `CODE`/`VARI`/`FUNC` chunks. `SCPT`, `OBJT`, `ROOM`, `GLOB` and `STRG` remain. |
| method pointer tables | a static `YYGMLFuncs[]` array in `.data`: `{name, native function, YYVAR*}` |
| metadata tokens | `YYVAR {const char* name; int32 id}` records; the ids are assigned at startup |

GML is dynamically typed, so every compiled script has the same native ABI. The interop
layer therefore needs no per-type code generation:

```c
RValue& gml_Script_x(CInstance* self, CInstance* other, RValue& result, int argc, RValue** argv);
void    gml_Object_<obj>_<Event>_<n>(CInstance* self, CInstance* other);
```

## 1. Static dump: `tools/yyc_dump.py` (verified)

`python3 tools/yyc_dump.py "<game>/<Game>.exe" --out <dir>` needs only the standard
library and takes about 3 s:

- **Function table.** 5917 entries at RVA `0x1b09940`: 5499 `gml_Script_*`, 190
  `gml_GlobalScript_*`, 227 `gml_Object_*` events and 1 `gml_RoomCC_*`.
  - Each of the 5851 entries with a `.pdata` entry loads its own name string for its
    stack-trace frame, and all 5851 match the table's name.
  - The 66 without `.pdata` are leaf stubs: 26 return `undefined`, 37 return `true`, and
    3 are empty events.
- **YYVAR records.** 8339 records referenced from compiled code. They name instance and
  global variables and late-bound built-ins.
- **Cross-references.** Direct callees: 488 scripts receive 6925 direct `call rel32`.
  The dump also records the variables each function touches; `objects.json` groups them
  per object.
- **IDA.** The dump writes `ida_names.py`, which does the following:
  - names every script and event and comments it with its GML name;
  - names each YYVAR record `yyvar_<name>` and its id field `v_<name>` (compiled code reads
    the field directly);
  - declares a minimal `RValue` and applies script, event and built-in prototypes.

  Verified with IDA 9.1 headless on a copy of an analyzed database:
  - before the script, 0 functions were named `gml_*`; afterwards 5917 were, and 8781
    functions were typed;
  - Hex-Rays then shows `RValue* gml_Script_getPriestCount(CInstance* self, CInstance* other, ...)`,
    direct script calls by name, and variable accesses as `v_buildings`, `v_priestArray`.

The dumper refuses VM builds. DELTARUNE, a VM build, is correctly rejected.

Recorded mistake: the `YYVAR` id stored with each function (`func_id`) is not the
runner's script index after the first nested script. Object events take slots in the same
numbering. The runtime merge supplies the real `script_index`; it matched for only
4742 of 5690 scripts.

## 2. Runtime dump: `YYCRuntimeDump` (verified)

The exe has no symbols for runtime helpers and built-ins. The plugin dumps what the
running runner knows:

- **Scripts.** 5690 from `GetScriptData`. All 5690 native addresses equal the static
  table's, and 5690 + 227 events = 5917.
- **Built-in functions: 2864.** There is no index API, so every identifier-like
  `.rdata` string is passed to `GetNamedRoutineIndex`. The scan takes 41 ms. One index
  (2428) has no name string.
- **Built-in variables: 210.** Each has a getter and a setter.
- **`YYRunnerInterface`: 100 slots.** Names come from the YYTK header, and the merge
  refuses to run if the field count does not match.
- **`.data` snapshot.** Runtime ids for all 8339 YYVAR records. For 356 of the 359
  records named after a built-in, the id equals that built-in's index, which confirms they
  are late-bound built-in references.

`yyc_dump.py --runtime <game>/yyc_runtime` merges the runtime dump with the static one, and
`ida_names.py` then also names:
- `F_<builtin>`: 2214 distinct addresses, because some built-ins share an implementation;
- `GV_`/`SV_<variable>` for the built-in variable getters and setters;
- the runner-interface functions.

The RValue helpers that compiled code calls are a separate, YYC-internal set. For example,
the exported `FREE_RValue` (`0x14103C850`) checks the kind mask and then calls
`0x140078AE0`, which is the internal free that compiled code calls directly.

The merge finds such cases automatically: any exported function of at most 96 bytes that
makes exactly one direct call names its callee `<export>_impl`. There are 14 on this exe,
including `FREE_RValue_impl` = `0x140078AE0`. Helpers without an exported wrapper (copy,
variable get/set, legacy call) are still unnamed.

## 3. Interop and hooks: `yyc_interop.hpp` (verified)

The header is header-only. `yyc::Functions().Init()` finds the static `YYGMLFuncs` table
at run time: every `CScript::m_Functions` points into it (all 5690 do, at 24-byte stride),
so it walks outwards from one. It then resolves scripts and events by name, so no address
is hard-coded. It provides:
- `yyc::CallScript(name, ...)` to call a script directly;
- `yyc::Hook(module, name, detour, &original)` to install an Aurie inline detour.

Why inline detours: scripts call each other with direct `call`s, so patching the function
pointer in the table catches only dynamic calls.

The example mod `YYCHookDemo` showed the following under Wine:
- **Script hook.** The `loc` detour sees about 7 calls per frame from 7 call sites in
  compiled GML: `gml_Object_oGUI_Step_0`, `gml_Object_oGUI_Draw_64` and closures inside
  them. The direct calls are intercepted.
- **Event hook.** The `gml_Object_oGUI_Step_0` detour fires once per frame.
- **Calling a script.** `yyc::CallScript("getPriestCount")` returns the same value as YYTK's
  `CallGameScript`.
- **Changing output.** Rewriting `loc`'s result changes the text the game draws: the HUD,
  the tutorial header and the tutorial body get a `MOD ` prefix. This was confirmed with the
  engine's `screen_save`.

  Pitfall: this game renders text with Scribble, which strips `[...]` as formatting tags,
  so a `[mod]` prefix is invisible.

## 4. GML bytecode on YYC: `YYCVmProbe` (verified)

The YYC runner still contains the VM.
- **Static evidence.** `Code_Execute` (`0x140ECE8C0`) calls the compiled function at
  `CCode+0x90` when it is set. Otherwise, if `CCode+0x68` holds a non-empty `VMBuffer`,
  it calls `VM::Exec` (`0x140EF1C30`).
- **Lazy conversion.** `VMBuffer::Prepare` (`0x140F75760`, which references `VMBuffer.cpp`)
  builds the threaded code at `+0x20`/`+0x28` on first execution.

The probe hand-builds a `CCode` with no compiled function and runs bytecode-17 programs
through the original `Code_Execute`, taken from YYTK's hook trampoline:

| Program | Result |
|---|---|
| `push.d 42; conv.d.v; ret.v` | `42` |
| `push.d 7; conv.d.v; call.i sqr; ret.v` (operand = runtime built-in index) | `49` |
| `push.v global.<slot>; ret.v` (operand = `0xA0<<24 \| slot`) | `60`, the same value YYTK reads |
| `push.d 123; conv.d.v; pop.v.v global.<slot>; exit.i` | YYTK then reads `123` (the original value was restored afterwards) |
| `call.i script_execute(<script id>)` | runs the compiled script |
| raw `call.i <script id>` | access violation: the VM expects bytecode in the script's `CCode` |

So loose GML can run on YYC games through the game's own interpreter. Operands just need to
carry runtime ids:
- built-in indices: `GetNamedRoutineIndex`;
- variable slots: `GetVariableSlot` or the YYVAR runtime ids;
- calls to compiled scripts: route through `script_execute` or a native thunk.

That gives one GML mod format for VM and YYC games. The missing piece is the shared
"link at runtime" step in `compiler/RUNTIME_COMPILER.md`: rewriting operands from
data.win references to runtime ids, including the runtime string table.

## Existing tools

- **YYToolkit** (existing tool). It detects YYC (`m_IsYYCRunner => true`), and its read API
  passes `yytk_probe` 19/19 on this game under Wine.
  - Fix made for this: `yytk_probe` kept a `static RValue`, whose constructor calls YYTK
    during CRT init. Under Wine there is no `ntdll!LdrpCallInitRoutine`, so Aurie refused to
    map the DLL (`GLE 0x3e6`). The probe now keeps it in a `std::optional`.
- **ReturnOfModding** (MIT, existing tool). A Lua loader with JIT detours for Risk of Rain
  Returns (YYC 2024.13). It uses the same hooking model as `yyc_interop`, but for one game.
- **ida_gamemaker** (return-of-modding). Manual IDA renaming. `yyc_dump.py` automates it
  and adds the runtime names.

## Running the test game under Wine

- Use a separate prefix with `WINEDLLOVERRIDES="version=n,b"`, launch the exe, wait for the
  plugin's log marker, then run `wineserver -k` for that prefix.
- The loader must be the current build. The installed copy from before the Wine fixes
  crashed: Aurie mapped YYToolkit late, giving a null page fault.
- ImageMagick `import` cannot capture the window on this desktop. Use GML `screen_save`
  from a plugin instead; the file lands in the prefix's `AppData/Local/<game>/`.

## Next steps

1. ~~Link compiled GML at run time~~ (done 2026-11-14): `modloader/include/gmlo_link.hpp`
   links `compiler/GmlObjectCompiler` output against the live runner and `YYCVmProbe` runs it;
   see `compiler/RUNTIME_COMPILER.md`, "YYC: running linked bytecode".
2. Generic `Code_Execute` lookup for mods: YYTK should expose its trampoline or a public
   "execute CCode" call, instead of plugins looking up YYTK's hook by name.
3. Name the remaining YYC-internal helpers (copy, variable get/set, legacy call) by call
   shape or from a runner whose symbols are known, and give them prototypes.
4. Check `yyc_interop` on a second YYC game and another runner version.
