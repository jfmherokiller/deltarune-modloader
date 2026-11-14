# compiler/

Our own GML compiler pipeline, built from UndertaleModLib + Underanalyzer
(`external/UndertaleModTool` submodule, pinned to tag `0.9.2.0`, the same version as the UTMT CLI
the loader used before).

| Path | What |
|---|---|
| `LooseModCompiler/` | `modloader/tools/ImportLooseMod.csx` compiled as a program (the `.csx` is the single source of truth; `ScriptHost.cs` supplies `Data`/`EnsureDataLoaded` and UTMT's default script imports). Same command line, env (`DR_MOD_DIRS`, `DR_OUT_PIPE`) and output as `UndertaleModCli load <data.win> -s ImportLooseMod.csx`. |
| `LooseModCompiler/RuntimeExports.cs` | `[UnmanagedCallersOnly]` entry points (`Ping`, `Compile`) for native hosts. |
| `RunnerCompiler/` | SPIKE: YYToolkit plugin that hosts the same .NET runtime in the game process. See `RUNTIME_COMPILER.md`. |
| `tools/compare_runtime_bytecode.py` | Diffs the runner's in-memory bytecode (spike dump) against the data.win it was served. |
| `tools/runtime_bytecode_fixtures.py` | Pairs a spike named capture with the served data.win: offline/runtime `.bin` files and side-by-side listings. |
| `fixtures/runtime_bytecode/` | Live-captured runtime vs. pre-fixup bytecode for seven GML snippets (ROADMAP 41). |

## Build / install

```
git submodule update --init --recursive
compiler/build.sh            # or build.bat on Windows; needs the .NET 10 SDK
cp -r compiler/build/win-x64/. <game>/mods/tools/compiler/
```

The loader uses `mods\tools\compiler\LooseModCompiler.exe` when present, else
`mods\tools\utmt\UndertaleModCli.exe` (`utmt_cli` in `modloader.ini` overrides both).
`compiler/build.sh linux-x64` builds a native Linux copy.

Release build: self-contained, framework trimmed (`TrimMode=partial`, UndertaleModLib and
Underanalyzer are kept whole because they use reflection) and ReadyToRun. About 94 MB on disk,
about 43 MB zipped.

## Verified

- The output is byte-identical to UTMT CLI + `.csx` for Chapters 2–5 with CheatMenu,
  SpriteInspector and LocalMultiplayerLite. `ImportLooseMod.csx` now strips ImageMagick's
  `date:*` / `tIME` PNG chunks, so both tools are deterministic. Before this change, UTMT itself
  produced 15–18 different bytes on every run.
- Chapter 4 timings under Wine: UTMT 4.7–4.9 s, LooseModCompiler 2.3–2.8 s. Most of the
  remaining time goes to loading and saving data.win, not to compiling.

## License

The binaries link UndertaleModLib (GPL-3.0) and Underanalyzer (MPL-2.0), so the
`compiler/build/` output is distributed under GPL-3.0. The rest of the repo stays MIT. Our
sources in `compiler/` are MIT and GPL-compatible.
