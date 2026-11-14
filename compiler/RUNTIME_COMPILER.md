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
3. **Linking and injection** (TODO, needs RE). The runner rewrites bytecode operands while it
   loads the CODE chunk: variable and function references become runtime ids, and strings become
   runtime indices. Injected bytecode has to get the same fixup. After that, `CCode->m_VmInstance`
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

## Generalizing

Nothing in `ClrHost`, `RuntimeExports`, or the dump code is DELTARUNE-specific. The DELTARUNE
parts are ImportLooseMod's folder conventions and the CheatMenu/SpriteInspector mods. To embed
this in YYToolkit, it would ship `ClrHost` plus a C API, `YYTK::CompileGml(name, source)`, behind
an optional dependency on the compiler folder.
