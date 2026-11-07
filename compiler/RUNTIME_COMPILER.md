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
  and `*.bin`.

To disable it, rename `mods/aurie/RunnerCompiler.dll` to `RunnerCompiler.dll.off`.

The served data.win is the vanilla `-game` file only when no loose mods are active. When loose
mods are active, the game reads the patched data from the loader's pipe. Compare against
`DR_OUT_PIPE` output, or run the dump with loose mods disabled.

## Generalizing

Nothing in `ClrHost`, `RuntimeExports`, or the dump code is DELTARUNE-specific. The DELTARUNE
parts are ImportLooseMod's folder conventions and the CheatMenu/SpriteInspector mods. To embed
this in YYToolkit, it would ship `ClrHost` plus a C API, `YYTK::CompileGml(name, source)`, behind
an optional dependency on the compiler folder.
