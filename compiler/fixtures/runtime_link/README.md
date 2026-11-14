# Runtime-link fixtures (ROADMAP 4, VM half)

GML snippets compiled with `compiler/GmlObjectCompiler` against the vanilla DELTARUNE launcher
data.win (md5 `83a5a14f9b92a20f21fb9ec6c8528469`), and what the in-game linker made of them in a
live run (RunnerCompiler link mode, `gmlo::Linker`, 2026-11-14, Proton). See
`compiler/RUNTIME_COMPILER.md`, "VM: running linked bytecode".

| Path | What |
|---|---|
| `src/*.gml` | the snippets |
| `objects/*.gmlo` | GmlObjectCompiler output (install these) |
| `link.txt` | the test plan: 1000 runs each, with side-effect checks |
| `link_noop.txt` | memory control: an empty snippet, 1000 runs |
| `linked/*.bin` | the linked bytecode the plugin wrote (`runnercompiler/linked/`), identical in three runs |
| `link.log`, `link_noop.log` | `runnercompiler/link.log` of those runs |

`compiler/tests/test_gml_object.py` checks that only relocated operand words changed, that an
offline link (`compiler/tools/gml_object.py`) with the ids the runtime chose reproduces these
bytes, and, with `DR_LAUNCHER_WIN` set, that the ids follow the data.win load-order rules.
