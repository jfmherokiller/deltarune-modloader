# Linux (Steam + Proton)

Everything runs on Linux: the game and loader under Proton, the build, the mod tools, and the
Runner Studio editor natively.

## Play with mods

1. Install the release zip into the game folder as on Windows.
2. Make Proton load the proxy DLL, once:
   `python3 tools/proton_setup.py` (close the game first; `--check` / `--remove` to inspect or undo),
   or set the launch option `WINEDLLOVERRIDES="version=n,b" %command%`.
3. Loose GML / sprite / sound mods work with the **Windows** UTMT CLI in `mods/tools/utmt/`
   (the loader starts it inside Proton).

Under Wine the loader also:
- rewrites the chapter relaunch command line to an absolute `data.win` path;
- answers `NtQueryInformationThread(ThreadSystemThreadInformation)` for the parked main thread.
  Wine does not implement that class, so AurieCore could not tell it was in early launch, mapped
  YYToolkit late, and YYToolkit crashed. Logged as
  `hook NtQueryInformationThread (Wine: Aurie early-launch detection)` in `mods/modloader.log`.

## Build (clang-cl, MSVC ABI)

Needs `clang`, `lld`, and an MSVC + Windows SDK sysroot:

```sh
# from a mounted Windows install (symlinks, nothing copied)
python3 tools/msvc_sysroot.py --windows-root /mnt/c --output ~/.cache/msvc-sysroot
# or download it (accepts Microsoft's license): xwin --accept-license splat --output ~/.xwin

modloader/build.sh                 # version.dll
modloader/yytk_plugin/build.sh     # DeltaruneYYTK.dll
modloader/yytk_probe/build.sh      # YYTKProbe.dll
tools/build_yytoolkit.sh out/yytk  # YYToolkit.dll from the submodule
editor/runtime/build.sh [test]     # RunnerEditor.dll; `test` runs the native tests under wine
```

`MSVC_SYSROOT` overrides the sysroot location. Exports and imports match the MSVC builds.

## Mod tools

- `mods/tools/export_sprites.sh <chapter> <Mod> <sprite...>`: Linux version of `export_sprites.bat`.
- `xdelta_to_gml.py`, `gml_to_patches.py` find the CLI through `utmt_cli.py`, in this order:
  `$UTMT_CLI` (an `.exe` runs through wine), a native `utmtcli` / `UndertaleModCli` on PATH, or
  `mods/tools/utmt/UndertaleModCli.exe` through `$WINE`. The native Linux CLI is much faster.

## Runner Studio editor

`cd editor && npm ci && npm start`. If `node_modules` was installed on Windows (shared drive), the
matching Linux Electron is downloaded once into `editor/.electron-linux-x64/` (pinned version,
checksum-verified); `RUNNER_STUDIO_ELECTRON` overrides it.

Native Linux processes cannot open Wine named pipes, so under Wine `RunnerEditor.dll` also listens
on `127.0.0.1` behind a random 256-bit token and writes
`~/.cache/runner-studio/RunnerEditor-<pid>.json` (`$XDG_CACHE_HOME` and
`$RUNNER_STUDIO_RUNTIME_DIR` are honoured; `XDG_RUNTIME_DIR` is not, because Steam's container gives
the game a private one). The relay only forwards to the existing read-only pipes; the Cheat Engine
pipe is not exposed. `RUNNER_EDITOR_RELAY=0` disables it. Entries record the Unix pid and start
time, because wineserver keeps an exited game's listening socket open; the editor prunes entries
whose process is gone.
