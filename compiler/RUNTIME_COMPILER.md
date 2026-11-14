# Runtime compiler (YYToolkit), design and spike status

Goal: compile GML inside a running GameMaker VM game and swap the code in memory, without
rebuilding data.win or restarting. This should work for any VM-runner game, not only DELTARUNE,
and could later live in YYToolkit.

## Pieces

1. **Hosting the compiler in the process** (done, verified). `RunnerCompiler/native/ClrHost.cpp`
   loads the `hostfxr` that ships with the self-contained `mods/tools/compiler` build and calls
   `[UnmanagedCallersOnly]` methods. It needs no global .NET install, and the same files serve
   the CLI. Pitfalls:
   - The app dir must be an absolute path. A relative one fails with `0x80008083`, because
     hostpolicy is not found.
   - Trimming disables native hosting. `0x800080a7` is fixed by
     `_EnableConsumingManagedCodeFromNativeHosting=true` in the csproj.
   - Measured with `clrhost-test`, Linux native, Ch1: runtime up in 22 ms, first compile 303 ms
     (includes the data.win load), next compiles 6 ms.
   - Measured under Wine, Ch4: runtime up in 69 ms, first compile 1.2 s, next compiles 35 ms.
2. **Game context** (TODO). The spike compiles against a data.win loaded in-process with
   UndertaleModLib. The general version implements Underanalyzer's `IGameContext` from the live
   runner: asset names, built-in and script function tables, and the variable list. YYToolkit
   already exposes most of these lookups. With that, the managed side needs no data.win at all.
3. **Linking and injection** (fixup derived, injection TODO). The runner rewrites bytecode
   operands while it loads the CODE chunk: variable and function references become runtime ids.
   String indices are not rewritten. The rules are in "Load-time operand fixup" below, and
   `tools/bytecode_fixup.py` implements them. Injected bytecode has to get the same fixup. After that, `CCode->m_VmInstance`
   (VMBuffer: length @8, bytecode @24) is pointed at the new buffer between frames, on the game
   thread. The spike's dump experiment collects the data needed for this:
   ```
   python3 compiler/tools/compare_runtime_bytecode.py <served data.win> <game>/mods/aurie/runnercompiler
   ```
   It lists which instruction words differ between the file and memory, grouped by opcode.

## Spike plugin (`RunnerCompiler/`, `build.sh`)

The plugin is read-only toward the game. On load it:
- starts the CLR on a worker thread and compiles a test snippet, writing the result to
  `mods/aurie/runnercompiler/report.txt` and `aurie.log`;
- dumps the VMBuffer bytecode of the first 400 distinct scripts that run, writing `index.tsv`
  and `*.bin`;
- if `mods/aurie/runnercompiler/capture.txt` exists (one code-name prefix per line, `#` for
  comments), it walks the runner's global CCode list on the first `EVENT_OBJECT_CALL`. This
  covers every code entry, whether it has run or not, and the bytes are already fixed up. It
  copies each matching entry's VMBuffer to `runnercompiler/capture/<name>.bin`, with metadata in
  `capture.tsv`. The list head is a static RVA (`0x8C9EC8`, RE/10), so the walk only runs when the
  exe's TimeDateStamp is `0x685E7007`. Otherwise it logs "named capture skipped".

To disable it, rename `mods/aurie/RunnerCompiler.dll` to `RunnerCompiler.dll.off`.

The served data.win is the vanilla `-game` file only when no loose mods are active. When loose
mods are active, the game reads the patched data from the loader's pipe. Compare against
`DR_OUT_PIPE` output, or run the dump with loose mods disabled.

## Live run and fixtures (ROADMAP 41)

Result (2026-11-13, Proton): `compiler/fixtures/runtime_bytecode/` holds seven snippets with
paired pre-fixup and runtime bytecode. See its README for the snippet list and findings. The
game ran without crashing. To reproduce (Linux + Proton; on Windows run `DELTARUNE.exe` directly
and skip the Proton variables):

1. Build: `compiler/RunnerCompiler/build.sh` (plugin) and `compiler/build.sh linux-x64` (offline
   LooseModCompiler). The game needs the loader, YYToolkit and the win-x64 compiler in
   `mods/tools/compiler/` (see `compiler/README.md`).
2. Back up `mods/aurie/RunnerCompiler.dll`, `mods/aurie/runnercompiler/`, `aurie.log` and
   `mods/modloader*.log`.
3. Install:
   ```sh
   G=<game dir>
   cp compiler/RunnerCompiler/build/RunnerCompiler.dll $G/mods/aurie/
   cp -r compiler/fixtures/runtime_bytecode/mod/RuntimeBytecodeFixtures $G/mods/
   mkdir -p $G/mods/aurie/runnercompiler
   printf 'gml_GlobalScript_rcfx_\ngml_Script_rcfx_\n' > $G/mods/aurie/runnercompiler/capture.txt
   ```
   The fixture mod has only a root `code/` folder, so it patches only the launcher's data.win.
   Other installed mods do not touch the launcher.
4. Run the launcher (no `-game`, so no input is needed):
   ```sh
   cd $G && STEAM_COMPAT_DATA_PATH=<library>/steamapps/compatdata/1671210 \
     STEAM_COMPAT_CLIENT_INSTALL_PATH=~/.local/share/Steam SteamAppId=1671210 \
     WINEDLLOVERRIDES="version=n,b" <proton dir>/proton run ./DELTARUNE.exe
   ```
   Wait for `[RunnerCompiler] named capture: walked N of N CCode objects, 14 matched` in
   `aurie.log`, check that `mods/modloader.log` says `compiled OK ... bytes served`, then kill the
   `DELTARUNE.exe` process by PID.
5. Rebuild the served data.win offline. The output is deterministic and matches the served size:
   ```sh
   : > served.raw
   DR_MOD_DIRS=$PWD/compiler/fixtures/runtime_bytecode/mod/RuntimeBytecodeFixtures DR_OUT_PIPE=$PWD/served.raw \
       compiler/build/linux-x64/LooseModCompiler load $G/data.win
   ```
6. Pair and list the entries, then summarise:
   ```sh
   python3 compiler/tools/runtime_bytecode_fixtures.py served.raw $G/mods/aurie/runnercompiler/capture \
       compiler/fixtures/runtime_bytecode/launcher \
       --source compiler/fixtures/runtime_bytecode/mod/RuntimeBytecodeFixtures/code
   python3 compiler/tools/compare_runtime_bytecode.py served.raw $G/mods/aurie/runnercompiler/capture
   ```
7. Restore the backups and remove `mods/RuntimeBytecodeFixtures`.

To add a snippet, add `gml_GlobalScript_rcfx_<name>.gml` (one `function rcfx_<name>()`) to the
fixture mod and rerun steps 3–6. Child entries (`gml_Script_*`) get a VMBuffer for the whole
parent buffer, not parent + child offset. `compare_runtime_bytecode.py` now compares from the
buffer start for that reason.

## Load-time operand fixup (ROADMAP 4, analysis)

Derived from the 14 fixture pairs in `fixtures/runtime_bytecode/launcher` and implemented in
`tools/bytecode_fixup.py`. `tests/test_bytecode_fixup.py` relinks every offline dump and checks
it is byte-equal to the runtime dump. `compare_runtime_bytecode.py` classifies every operand word
by kind and exits 1 if the rules do not explain a word. Current result: 0 unexplained.

```sh
python3 -m unittest discover -s compiler/tests -v                     # committed fixtures only
DR_SERVED_WIN=served.raw python3 -m unittest discover -s compiler/tests -v   # also re-derive from data.win
python3 compiler/tools/compare_runtime_bytecode.py served.raw compiler/fixtures/runtime_bytecode/launcher/runtime
python3 compiler/tools/bytecode_fixup.py check served.raw compiler/fixtures/runtime_bytecode/launcher
```

### What changes and what does not

The runner rewrites exactly one word per reference instruction: the operand word that follows a
variable or function reference. It does this in place, in its loaded copy of data.win. Nothing
else changes. Counts across all pairs:

| Operand kind | Words | Changed | Rule |
|---|---|---|---|
| Variable reference (`push`/`pop`/`pushloc`/`pushglb`/`pushbltn`, every instance type) | 112 | all | V |
| Function reference, `call` (built-in 30, script 6) | 36 | all | F |
| Function reference, `push.i` of a function (method/`function` declarations) | 14 | all | F |
| Branch offset (`b`, `bt`, `bf`, `pushenv`, `popenv`) | 18 | none | kept |
| String index (`push.s`) | 12 | none | kept |
| Other immediates (`push.i/.l/.d/.v`, `pushi`) | 100 | none | kept |
| Opcode words (type, instance type, comparison, `dup`/`conv` bits) | all | none | kept |

String operands are **not** remapped. A `push.s` operand stays the STRG index, so injected code
only needs its strings to exist in the runner's string table at the same index (or appended).
Branch offsets are relative and stay as they are.

### Rule C: the file-side reference chain

In the file, the operand words of all references to one name form a linked list (UTMT writes
it on save, Underanalyzer output goes through it):

- VARI entry (20 bytes: name, instance type, var id, occurrences, first address) and FUNC
  entry (12 bytes: name, occurrences, first address) give the head. VARI's first address is the
  instruction. FUNC's (GMS 2.3+) is the operand word, so subtract 4.
- Variable operand word: bits 27..31 are the reference type (`0xA0` normal, `0x80` stack top,
  `0x00` array, `0x10`/`0x90` array push/pop-af, `0xE0` instance; only `0xA0`, `0x80` and `0x00`
  occur in the fixtures); bits 0..26 are the link.
  A function operand word is the link in full.
- Link at every occurrence but the last = byte distance from this instruction to the next
  occurrence. The last occurrence holds the name's STRG index instead. (Checked on all 930
  non-empty chains in the served data.win.)
- Chains run through the CODE blob in file order and cross entry boundaries, so a buffer's
  relocations are the chain addresses that fall inside `[start, start + length)`.

`relocations_from_data_win(d, start, length)` walks this. For injected code, the compiler
already knows each reference instruction and its name. Emit the relocation list `(offset, kind,
name)` from the instruction list, do not build chains.

### Rule V: variable references

```
mem = (file & 0xF8000000) | (var_id(name) & 0x07FFFFFF)
```

Reference-type bits are kept. The instance type is in the opcode word and is not touched.
`var_id`:

- **Built-in** (VARI var id or instance type `-6`, e.g. `x`, `y`, `image_alpha`,
  `room_speed`, `argument0`): the runner's built-in variable index. It is not in data.win.
  The fixtures read these off the runtime dump. Live injection must look them up by name in
  the runner's built-in variable table (which runner function to call is not pinned down yet).
- **User** (everything else, any instance type: self, other, global, local, stack-top, array):
  `100000 + slot`. While loading, the runner walks VARI in file order and finds-or-adds each
  non-built-in name in **one** table shared by self, global and local variables; the slot is the
  insertion index. So a local `i` and an instance `i` have the same id. The fixtures confirm it
  over all 463 slots: local `i` is slot 49, from game code early in VARI, and the fixture names
  are 444–462 at the end.
- A name the runner has never seen is assumed to get the next free slot (`Resolver.var_id`).
  This is not yet observed live. Check it when injection exists.

### Rule F: function references

```
mem = func_id(name)            # the whole word
```

- **Built-in** function (`abs`, `method`, `@@NewGMLArray@@`, ...): the runner's routine index
  (the `Code_Function_Find` index). It is not in data.win. Fixtures: `method` = 162, `abs` =
  200, `floor` = 202, `array_length` = 170, `@@NewGMLArray@@` = 2253.
- **Script** (`gml_Script_*`): `100000 + SCPT index`. SCPT is in file order, and
  `script_indices(d)` reads it.

### Worked examples

`gml_GlobalScript_rcfx_globals` (file offset `0x3b3e4`), source `global.rcfx_counter = 5;
global.rcfx_counter += 1; return global.rcfx_counter * 2;`:

```
 off  op        file      runtime
   8  pop.v.i   4525fffb  4525fffb    instance -5 (global), kept
  12            a0000008  a0018862    chain +8 -> 16;  0xA0 | 100000+450
  20  push      a0000010  a0018862    chain +16 -> 32
  36  pop       a0000008  a0018862    chain +8 -> 40
  44  pushglb   a000048e  a0018862    last link: STRG 1166 = "rcfx_counter"
  68  push.i    00000481  0001878f    function gml_Script_rcfx_globals: 100000 + SCPT 239
  88  call      000000ac  000000a2    built-in method = 162
 104  pop       8000048f  80018863    stack-top ref kept (0x80); rcfx_globals slot 451
```

- Array ref, `gml_GlobalScript_rcfx_arrays@76` local `arr`: `00000040` -> `0001885c`.
  Ref type `0x00` is kept, and slot 444 gives 100444 = `0x1885c`.
- Built-in, `rcfx_builtins@112` `pushbltn room_speed`: `a000037d` -> `a0000088` (index 136).
- Script call, `rcfx_scripts@20` `call gml_Script_strlen`: `00000027` -> `000186f1`
  (SCPT 81).

### Child entries

`gml_Script_rcfx_*` (child offset 4) gets a VMBuffer for the whole parent buffer, so its runtime
dump equals the parent's. Its relocations are the parent's: apply the fixup once per buffer.

### Using it

```python
from bytecode_fixup import Reloc, Resolver, apply_fixup
linked = apply_fixup(code, relocs, resolver)   # relocs: [Reloc(offset, "var"|"func", name, inst_type)]
```

For live injection, back `Resolver` with the runner's own tables: built-in variables and
routines by name, user variables via the runner's variable-name table, and scripts via SCPT or
the runner's script list. Do not use data.win.

## YYC: running linked bytecode (ROADMAP 4, YYC half)

Verified 2026-11-14 on Dwarf Eats Mountain 1.1.0 (YYC, runner 2024.14.4.268, Wine 11): four GML
snippets were compiled with Underanalyzer, linked against the live game and run on the YYC
runner's embedded VM, 3 rounds × 100 runs on separate frames. Every side-effect check passed in
every round, with no fault and +0 KB private bytes per 100 runs. Log:
`modloader/yyc_vm_probe/gmlo/dwarf_eats_mountain.log`.

| Snippet (`modloader/yyc_vm_probe/gmlo/`) | Covers | Verified through YYTK, outside the bytecode |
|---|---|---|
| `gmlo_globals` | global set, `+=`, read-back | result `12`, `global.gmlo_counter` = `6` |
| `gmlo_debug` | `show_debug_message` with a new string, a `for` loop, locals, `string()` | result `6`; the hooked built-in saw `"gmlo debug message 6"` once per run (300 of 300); `global.gmlo_last_message` matches |
| `gmlo_script` | calls to a compiled script, inside a loop | `global.gmlo_script_result` = `gml_Script___spelunker_minutes_to_seconds(2)` called natively (`120`); the loop total = the script at 6 (`360`) |
| `gmlo_builtin` | built-in variable read (`pushbltn room_speed`) | `global.gmlo_room_speed` = YYTK `GetBuiltin("room_speed")` (`60`) |

### Pipeline

1. **Compile** (offline): `compiler/GmlObjectCompiler <game data.win> <out> <snippet.gml>...`
   writes a `.gmlo`: Underanalyzer's bytecode with every reference operand left open, plus the
   relocations `(offset, var|func|string, name)`. A YYC data.win has no `CODE`/`VARI`/`FUNC`
   chunks; `PrepareContext` adds empty ones and declares every `gml_Script_X` in `SCPT` as the
   global function `X`, so calls compile to `call.i gml_Script_X`. The prepared function table
   must be passed to `CodeImportGroup` (`new GlobalDecompileContext(data, data.GlobalFunctions)`):
   a fresh context rebuilds it from `CODE`, drops the declarations, and script calls then compile
   as `callv` on an instance variable.
2. **Link** (in the game, on the game thread): `modloader/include/gmlo_link.hpp`, `gmlo::Linker`.
   Every id comes from the runner, none from data.win:

   | Operand | Resolved with | Dwarf values |
   |---|---|---|
   | built-in variable | YYTK `GetBuiltinVariableIndex` (not tried for locals, instance type -7) | `room_speed` = 139. DELTARUNE's VM fixtures have 136, so built-in indices change between runner versions |
   | any other variable (global, self, local) | YYTK `GetVariableSlot`, the runner's find-or-allocate on its one name table | existing names keep their id (`n` 107572); new names get the next free ids in link order (111131, 111132, ...). This settles the open item in Rule V for YYC |
   | function | YYTK `GetNamedRoutineIndex` | `string` 266, `show_debug_message` 1280, `script_execute` 804, `gml_Script___spelunker_minutes_to_seconds` 104448 (100000 + index) |
   | string | `gmlo::RunnerStrings::Intern` (below) | existing strings keep their data.win index; `"gmlo debug message "` was appended as 15096 |

3. **Run**: the linked words go into a hand-built `VMBuffer` + `CCode` and through YYTK
   `ExecuteCodeObject` (the runner's own `Code_Execute`, from the repo's YYToolkit fork).

### Layout: the same on both runners

`Code_Execute` (`0x140ECE8C0`), `VM::Exec` (`0x140EF1C30`) and `VMBuffer::Prepare` (`0x140F75760`)
in the Dwarf exe read the same `CCode` and `VMBuffer` fields that RE/10 documents for DELTARUNE's
VM runner:

| Field | Offset | Set by the linker |
|---|---|---|
| `CCode` size | `0xB8` | allocated zeroed |
| kind | `+0x10` | 1 |
| `VMBuffer*` | `+0x68` | the linked buffer |
| name | `+0x80` | the object's name |
| script index | `+0x88` | -1 |
| compiled function | `+0x90` | **null.** This is the only YYC-specific field. On YYC, `Code_Execute` calls it when it is set and falls through to `VM::Exec` when it is null. On VM, RE/10 has a metadata pointer here |
| local count | `+0xA0` | `.gmlo` `locals` (`VM::Exec` makes a locals object when > 0) |
| argument count | `+0xA4` | `.gmlo` `args` |
| `VMBuffer` size | `0x30` | allocated zeroed (no vtable needed: it is never destroyed) |
| bytecode length / pointer | `+0x08` / `+0x18` | the linked words |
| threaded code / jump table | `+0x20` / `+0x28` | filled by `VMBuffer::Prepare` on the first run |

**Never free or reuse a bytecode buffer.** `VMBuffer::Prepare` caches the threaded form in a hash
keyed by `(bytecode address + 1) & 0x7FFFFFFF`. A new buffer at a freed buffer's address would run
the old code. Link once, then run the same `CCode` as often as needed. The linker leaks the few
hundred bytes per linked object on purpose.

### Fixup differences on YYC

- **Script calls are lowered.** `VMBuffer::Prepare` selects the `call.i` handler by id: below
  100000 is the built-in handler; 100000–500000 is the script handler (except 499999). The
  script handler runs the script's `CCode` as bytecode. YYC scripts have none, so it faults (the
  probe's raw `call.i` gets `0xC0000005`). On YYC the linker therefore rewrites each `call.i` to
  a script:
  ```
  call.i gml_Script_X(argc=n)   ->   push.i <id of gml_Script_X>; conv.i.v; call.i script_execute(argc=n+1)
  ```
  The pushed id lands on top of the stack, so it becomes `script_execute`'s first argument.
  Inserting 3 words moves the code after the call. The linker fixes every branch (`b`/`bt`/`bf`/
  `pushenv`/`popenv`, a 23-bit signed offset in words; the `popenv` exit magic `0xF00000` is left
  alone) and every later relocation. A branch to the call lands on the inserted `push.i`. In
  `gmlo_script` the loop's `bf +17` became `bf +20` and its `b -20` became `b -23`.
- **Strings work as on VM.** YYC keeps `STRG`, and the VM's `push.s` handler (the `Push` handler
  `0x140EE45E0`, type 6) reads `chars = image + table[i] + 4`. `image` is the loaded data.win
  (the FORM header). `table` is a `uint32_t[]` of STRG entry offsets; on Dwarf it starts as
  STRG's own offset array inside the image. `RunnerStrings` finds both pointers by value: it
  scans the exe's writable data sections for a pointer to a FORM image whose `STRG` parses, and
  for a pointer to the first eight STRG offsets. The slot on Dwarf is `0x141B6F780` (RVA
  `0x1B6F780`). A string the game already has keeps its index. A new string is appended: its
  text goes into memory within 4 GB above the image (the table holds 32-bit offsets), and the
  table pointer is swapped to a grown copy. Each appended string is read back through the VM's
  own `push.s` (`ReadRunnerString`), and every one matched. Whether DELTARUNE's VM runner reads
  strings the same way is not checked yet; if it does, the same code covers new strings on VM.
- Variable and built-in-call operands are the same as on VM: `(ref bits) | id`, and the whole id.

### Reproduce (Linux, Wine)

```sh
modloader/yyc_vm_probe/build.sh                       # YYCVmProbe.dll
tools/build_yytoolkit.sh                              # fork YYToolkit.dll with ExecuteCodeObject (out/yytk)
modloader/build.sh                                    # current loader: older ones crash under Wine 11
modloader/yyc_vm_probe/gmlo/compile.sh "<game>/data.win" "<game>/gmlo"
# back up <game>/version.dll and <game>/mods, then install: build/version.dll as version.dll,
# out/yytk/YYToolkit.dll and YYCVmProbe.dll in mods/aurie (other plugins disabled)
cd "<game>" && WINEPREFIX=<prefix> WINEDLLOVERRIDES="version=n,b" WINEDEBUG=-all wine "Dwarf Eats Mountain.exe"
# wait for "YYCVmProbe done" in <game>/yyc_vm_probe.log, then: WINEPREFIX=<prefix> wineserver -k
```

Keep `.gmlo` files out of `mods/`, since the DELTARUNE loader treats every folder there as a loose
mod. Under the loader the runner's stdout is not captured, so the probe hooks `show_debug_message`
to count the messages.

### Limitations (YYC)

- `GmlObjectCompiler` rejects code that creates child entries (`function` declarations inside a
  snippet).
- Only `call.i` is lowered. A script used as a value (`push.i` of a function id, e.g.
  `method(undefined, X)` or `callv` on it) is linked unchanged. It probably reaches the same
  faulting script path when called; this is untested.
- The snippets run with the global instance as `self`. Instance variables on a real instance are
  untested on YYC.
- `RunnerStrings` assumes the runner keeps the image pointer and the table pointer in the exe's
  own data section. If either is missing, the linker refuses string relocations.

## Generalizing

Nothing in `ClrHost`, `RuntimeExports`, or the dump code is DELTARUNE-specific. The DELTARUNE
parts are ImportLooseMod's folder conventions and the CheatMenu/SpriteInspector mods. To embed
this in YYToolkit, it would ship `ClrHost` plus a C API, `YYTK::CompileGml(name, source)`, behind
an optional dependency on the compiler folder.
