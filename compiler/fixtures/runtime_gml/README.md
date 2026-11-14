# Runtime GML smoke test (ROADMAP 4 and 41)

One GML source, compiled, linked and run inside the game by `gmlrt::Runtime::CompileAndRun`
(`modloader/include/gml_runtime.hpp`) through RunnerCompiler link mode, on both runner kinds
(2026-11-14). See `compiler/RUNTIME_COMPILER.md`, "Runtime GML API".

| Path | What |
|---|---|
| `gmlrt_smoke.gml` | the snippet: only names every GameMaker game has |
| `gmlrt_noop.gml` | memory control (`return 0;`) |
| `link.txt`, `link_noop.txt` | the plans (copy to `mods/aurie/runnercompiler/` with the `.gml`) |
| `objects/gmlrt_smoke.vm.gmlo` | what `RuntimeExports.CompileObject` produced against the DELTARUNE launcher data.win (md5 `83a5a14f9b92a20f21fb9ec6c8528469`) |
| `objects/gmlrt_smoke.yyc.gmlo` | the same against Dwarf Eats Mountain 1.1.0's data.win (YYC) |
| `linked/gmlrt_smoke.{vm,yyc}.bin` | the bytes the game linked and ran (`runnercompiler/linked/`) |
| `logs/vm_deltarune_launcher.log` | `link.log`, DELTARUNE launcher, Proton CachyOS 11 |
| `logs/yyc_dwarf_eats_mountain.log` | `link.log`, Dwarf Eats Mountain, Wine 11.21 |
| `logs/yyc_dwarf_eats_mountain_noop.log` | the control run on Dwarf Eats Mountain |

The objects were written by `compiler/RunnerCompiler/build/clrhost-test <compiler dir> <data.win>
gml_GlobalScript_gmlrt_0 gmlrt_smoke.gml object` (the same export the game calls; the code name is
the one the game used). `compiler/tests/test_gml_runtime.py` checks them against the linked bytes
and the logs.
