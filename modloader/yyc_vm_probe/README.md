# YYCVmProbe (experiment)

This plugin asks whether a YYC runner can still run GameMaker bytecode.

The YYC exe still links the VM. `Code_Execute` checks whether the `CCode` has a compiled
function (`+0x90`). If it does not, but it has a `VMBuffer` (`+0x68`) of non-zero size,
`Code_Execute` calls `VM::Exec`. `VM::Exec` builds the threaded form of the bytecode
on first use (`VMBuffer` `+0x20`/`+0x28`).

The probe builds a `CCode` and a `VMBuffer` by hand around small bytecode programs and runs
them through the runner's original `Code_Execute`. It gets that function from YYToolkit's
`ExecuteIt` hook trampoline, so no address is hard-coded. The results are written to
`yyc_vm_probe.log`.

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

Compiled GML as a whole still needs linking against the running game:
- variable operands need the runtime slots: `GetVariableSlot`, or the YYVAR ids from
  `YYCRuntimeDump`;
- string operands need entries in the runtime string table;
- script calls need to go through `script_execute` or a native thunk.

That linking is the same work `compiler/RUNTIME_COMPILER.md` lists for VM games.

The `CCode` offsets are taken from this runner's `Code_Execute`/`VM::Exec` and are not
version-checked. This is an experiment, not a mod.
