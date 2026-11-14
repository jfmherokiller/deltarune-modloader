# LooseMods

LooseMods is the Aurie module behind the loose-mod features in `modloader/README.md`:
- file overrides;
- loose GML, sprites and sounds;
- `redirects.txt`;
- `log_files` / `log_scripts`;
- the Wine chapter-relaunch fix.

These used to live in the `version.dll` proxy. The proxy is now only the bootstrap; see "Bootstrap proxy and Aurie" there.

It installs as `mods\aurie\LooseMods.dll` and reads `[loader]` in `mods\modloader.ini`. It writes to `mods\modloader.log`, on lines tagged `[loosemods]`.

## When each part starts

| Part | Started from | Why |
|---|---|---|
| `ReadEntireFile` hook (file overrides, loose GML) | `ModulePreinitialize` | Aurie runs it while the main thread is still parked at the entry point, so the hook exists before the game reads `data.win` |
| Wine `CreateProcessW` fix | `ModulePreinitialize` | Same early point |
| `EVENT_OBJECT_CALL` callback (event redirects, `log_scripts`) | `ModuleInitialize` | YYToolkit is fully up by then |
| Script redirects | first object event | `script_hooks.hpp` must be set up on the game thread. `ModuleInitialize` runs on Aurie's thread while the game is already running; calling it from there hung the game's module initialization |

Hooks use Aurie's `MmCreateHook`. Event redirects and script logging reuse YYToolkit's `Code_Execute` hook instead of hooking it a second time.

Script redirects use the shared `GmScriptHooks` interface (`yyc_interop/include/script_hooks.hpp`). It holds one hook on the interpreter's frame-push routine for every plugin in the process.

## Game-specific parts

| Part | Status outside DELTARUNE build `0x685E7007` |
|---|---|
| `ReadEntireFile` hook | Off. Fixed RVA, checked against a 16-byte prologue; the log says so |
| Runner allocator | Off. Fixed RVA, checked against a 16-byte prologue |
| `CCode` list head, used to find redirect substitutes by name | Off. Fixed RVA |
| Everything else | Works in any GameMaker game |

## Loose-GML compile cache

`source/GmlCache.hpp` keeps each chapter's compiled `data.win` in `mods\cache\loosegml\<chapter>\`
with a plain-text `manifest.txt` key (mod file hashes, mod order, vanilla `data.win`, compiler,
this DLL). An unchanged key skips the compiler; a miss logs the first key line that changed and
rewrites the cache from a background thread. `gml_cache=0` / `DR_GML_CACHE=0` bypasses it. The
XXH64 used for the key and the integrity check is `modloader/include/xxh64.hpp`.

## Logging

Logging uses Win32 file handles (`modloader/include/append_log.hpp`), not `fopen`.

A module built with the shared CRT (`/MD`) shares the game's file-descriptor table. Opening a `FILE*` before the game's CRT set up stdio took descriptor 1, so the game's stdout went into the log. This was seen with Dwarf Eats Mountain.

## Build

`build.sh` (Linux, clang-cl) or `build.bat` (MSVC). Both go through `../build_yytk_plugin.sh` / `.bat`.

## Verified (2026-11-08)

**DELTARUNE under Proton, launcher and chapter 5**

| Feature | Result |
|---|---|
| File override (`options.ini`) | Worked |
| Loose GML on the launcher | Worked: a replaced `obj_ui_version_Draw_0` drew "THIN PROXY OK" |
| Event redirect | Worked: the version label disappeared |
| Script redirect | Worked: `down_p` → `up_p` doubled the `up_p` calls seen by a second plugin through the shared hook |
| `log_scripts` | Worked |
| Chapter 5 relaunch | Worked: the Wine fix was applied and the working directory restored. CheatMenu, DialogueBacklog and SpriteInspector compiled into chapter 5's `data.win` |

**Dwarf Eats Mountain (YYC) under Wine**
- LooseMods stays off with the "does not match this exe" line.
- ModHost and its plugins load as before.
