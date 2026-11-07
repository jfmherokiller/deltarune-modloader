# YYCRuntimeDump

A YYToolkit v5 plugin that records the names the runner only knows at run time. It was
written for YYC games, whose exe has no symbols. About 10 s after the first frame it writes
`<game>/yyc_runtime/`:

| File | Contents |
|---|---|
| `runtime.json` | exe base and size; every script (`GetScriptData`: name, `YYGMLFuncs` entry, native function); built-in variables (`GetBuiltinVariableInformation`: name, getter, setter); built-in functions; the raw `YYRunnerInterface` slots |
| `data_section.bin` | the exe's `.data` after startup. YYVAR ids are assigned at startup, so this is where they can be read. |
| `dump.log` | progress. It ends with `YYCRuntimeDump done`, or names the step that faulted. |

There is no API to list built-in functions by index. Instead, the plugin passes every
identifier-shaped string in the exe's `.rdata` to `GetNamedRoutineIndex`: each registered
name is a literal there. Addresses inside the exe are written as RVAs; anything else is
written as `abs:0x...`.

Merge the dump with the static one:

```sh
python3 tools/yyc_dump.py "<game>/<Game>.exe" --out <dir> --runtime "<game>/yyc_runtime"
```

This adds `script_index`, `runtime_id` (on YYVAR records), `builtin_functions`,
`builtin_variables` and `runner_interface` to `symbols.json`. `ida_names.py` then also names
`F_<builtin>`, `GV_`/`SV_<variable>` and the runner-interface functions, and applies
script/event/built-in prototypes.

Dwarf Eats Mountain 1.1.0 under Wine took 41 ms for the built-in scan. It found:
- 5690 scripts. All 5690 match the static table's addresses; the 227 object events account
  for the rest of the 5917.
- 2864 built-in functions. One index, 2428, has no name string in the exe.
- 210 built-in variables and 100 runner-interface slots.
- Runtime ids for all 8339 YYVAR records.
