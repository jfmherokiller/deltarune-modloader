# YYCVmProbe (experiment)

This plugin asks whether a YYC runner can still run GameMaker bytecode.

The YYC exe still links the VM. `Code_Execute` checks whether the `CCode` has a compiled
function (`+0x90`). If it does not, but it has a `VMBuffer` (`+0x68`) of non-zero size,
`Code_Execute` calls `VM::Exec`. `VM::Exec` builds the threaded form of the bytecode
on first use (`VMBuffer` `+0x20`/`+0x28`).

The probe builds a `CCode` and a `VMBuffer` by hand around small bytecode programs and runs
them through the runner's original `Code_Execute`, via YYToolkit `ExecuteCodeObject` (which
calls YYToolkit's `ExecuteIt` hook trampoline), so no address is hard-coded. The results are
written to `yyc_vm_probe.log`.

Results on Dwarf Eats Mountain 1.1.0 (runner 2024.14.4.268, Wine):

| Program (bytecode 17 encoding) | Result |
|---|---|
| `push.d 42; conv.d.v; ret.v` | `42` |
| `push.d 7; conv.d.v; call.i sqr(argc 1); ret.v` | `49`. The operand is the runtime built-in index (234). |
| `push.d <script id>; conv.d.v; call.i script_execute(argc 1); ret.v` | runs the compiled `getPriestCount` and returns its value (`0`, which is what a direct call returns too) |
| `push.v global.<slot>; ret.v` | `60`, the same value YYTK reads for `global.sparkle_budget_cap`. The slot (106899) comes from YYTK `GetVariableSlot`. |
| `push.d 123; conv.d.v; pop.v.v global.<slot>; exit.i` | YYTK then reads `123`. A second write restores `60`. |
| `call.i <script id>` (raw) | access violation, caught by SEH. The VM's call handler expects the script's `CCode` to hold bytecode, and YYC scripts have none. |

So the interpreter is live on YYC. With runtime ids in the operands, bytecode can read and
write variables and call built-ins, and it reaches compiled scripts through `script_execute`.

## Linked GML (`gmlo/`)

After the hand-encoded programs, the probe links and runs every `<game>\gmlo\*.gmlo`: GML
compiled by `compiler/GmlObjectCompiler`, linked against the live game by
`modloader/include/gmlo_link.hpp` (variable, built-in, function and string ids from the runner;
compiled-script calls lowered to `script_execute`). Each object runs 100 times in each of 3 rounds
on separate frames. The checks in its `.expect` file read the side effects back through YYTK:

| Check | Meaning |
|---|---|
| `result <text>` | the code's return value |
| `global <name> <text>` | `global.<name>` |
| `global_eq_script <name> <script> <args>` | `global.<name>` equals the script called natively |
| `global_eq_builtin <name> <builtin>` | `global.<name>` equals the built-in variable |
| `debug_message <text>` | `show_debug_message(<text>)` ran once per run (the probe hooks the built-in) |
| `repeat <n>` | runs per round (default 100) |

`gmlo/compile.sh <game data.win> <game>/gmlo` builds the objects. On Dwarf Eats Mountain all four
snippets passed every round (`gmlo/dwarf_eats_mountain.log`). Results, layout and the YYC fixup
differences: `compiler/RUNTIME_COMPILER.md`, "YYC: running linked bytecode".

The probe needs the repo's YYToolkit fork (`ExecuteCodeObject`) and the current loader build.

The `CCode` offsets are taken from this runner's `Code_Execute`/`VM::Exec` and are not
version-checked. This is an experiment, not a mod.
