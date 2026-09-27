# DELTARUNE mod loader (native Windows)

`version.dll` proxy + MinHook. Drop-in: no exe patching, no data.win edits.
Ported from the Wine-only prototype `mod_test/aurie_build/proxy/dllmain.c` (the `mod_test/` rig was deleted 2026-09-27; everything lives here now).

## Build

```
build.bat          → build\version.dll   (MSVC 2022, /MT, only imports KERNEL32)
```

## Install / uninstall

Copy `build\version.dll` next to `DELTARUNE.exe` and create a `mods\` folder.
Without `mods\` the DLL loads but does nothing. Uninstall = delete `version.dll`.
Wine/Proton additionally needs `WINEDLLOVERRIDES="version=n,b"`.

## Mods folder

```
DELTARUNE\
  version.dll
  mods\
    modloader.ini          optional settings
    modloader.log          written by the loader
    MyMod\
      chapter1_windows\lang\lang_en.json    ← replaces the game's file
      chapter2_windows\options.ini
      redirects.txt                         ← optional script redirects
    _DisabledMod\          ← '_' or '.' prefix = ignored
```

* **File overrides**: any file the runner reads through `ReadEntireFile` from inside
  the install folder can be replaced by the same relative path under
  `mods\<Mod>\`. Mods load in alphabetical order, and the last one wins. This covers
  each chapter's `data.win`, `lang\*.json`, `options.ini`, and `audiogroup*.dat`.
  Loose `.ogg` music/SFX is **not** covered yet, because it's streamed by another reader.
* **Script redirects** (`redirects.txt`): `target = substitute`, one per line, `#` comments.
  When the VM executes `target` it runs the already-compiled `substitute` instead.
  Both must exist in the running chapter's data.win. Names are the `code/` dump names,
  e.g. `gml_Object_obj_ui_version_Draw_0`.

`modloader.ini`:

```ini
[loader]
enabled=1
log_files=0     ; 1 = log every file read (research)
log_scripts=0   ; 1 = log each GML script/event name the first time it runs
```

## Loose GML (script replacement without xdelta)

```
mods\<Mod>\chapter2_windows\code\gml_GlobalScript_button1_p.gml
mods\<Mod>\chapter2_windows\code\gml_Object_DEVICE_MENU_Draw_0.gml
mods\<Mod>\code\...            <- launcher (root data.win)
```

Each file is decompiled GML named after its UTMT code entry. When the game reads a
`data.win` that any enabled mod has `.gml` files for, the loader runs UTMT's own
compiler on the vanilla file, applying all mods' folders in load order (a later mod
wins on the same name). It then hands the result to the game from memory. The
`data.win` on disk is never modified and no patched copy is written. On a compile
error the game runs vanilla, and the errors go to `mods\modloader.gml.log`.

Needs `mods\tools\utmt\` (UndertaleModTool CLI 0.9.2.0, `UTMT_CLI_*-Windows.zip`) and
`mods\tools\ImportGMLFolder.csx` (from `tools/`). You can override the CLI path with
`utmt_cli=` in `modloader.ini` and the timeout with `gml_timeout_ms=` (default 300000).
Compile time is about 4 s for Ch2 with 216 files.

**Converting an xdelta mod:** `python tools/xdelta_to_gml.py <game> <ModName>
chapter2_windows=patch.xdelta ...` keeps only the code entries the patch changed and
warns about non-code (asset) differences, which loose GML can't carry yet.

## Aurie / YYToolkit

The loader also boots the [Aurie](https://github.com/AurieFramework/Aurie) +
[YYToolkit](https://github.com/AurieFramework/YYToolkit) stack, so AuriePatcher is
not needed and `DELTARUNE.exe` stays unmodified:

```
mods\native\AurieCore.dll      Aurie v2.0.2
mods\aurie\YYToolkit.dll       YYToolkit v5 (patched build, see yytk_plugin/)
mods\aurie\DeltaruneYYTK.dll   example plugin (yytk_plugin/)
```

It detours the exe's entry point and parks the main thread before any game code runs,
the same state AuriePatcher's trampoline produces. It then loads `mods\native\*.dll`.
AurieCore runs YYTK's early init, then resumes the game. If nothing resumes the game
within `aurie_timeout_ms` (30 s), the loader resumes it itself.

**Chapter loop fix:** AurieCore does `SetCurrentDirectoryW(<exe folder>)` at startup.
The launcher starts each chapter as `DELTARUNE.exe -game data.win`, with the working
directory set to `chapterN_windows\`. After Aurie's chdir, that relative `data.win`
resolves to the root launcher's `data.win`, so pressing Play loops back to chapter
select. The loader saves the start directory and restores it before the game's entry
point continues.

`modloader.ini` → `load_aurie=0` disables all of this.

### YYToolkit patch (DELTARUNE runner)

The stock `YYToolkit.dll` v5.0.0c fails init on this game with `Failed to locate room
data!` and gets purged. Its `Zeus::VM::FindRoomData` expects the room lookup to be
inlined in `F_RoomInstanceClear`. In DELTARUNE's runner that function calls a separate
`Room_Data(index)` (`0x1400B6330`, room array `0x1405FC130`).
`third_party/yytk_patch/0001-vm-room-data-non-inlined.patch` (base commit in
`BASE_COMMIT.txt`, the `experimental` branch) teaches Zeus to follow that call. The
built DLL is `third_party/aurie_bin/YYToolkit-5.0.0-deltarune.dll`, which installs as
`mods\aurie\YYToolkit.dll`.

Rebuild: clone YYToolkit `experimental` at the base commit, `git apply` the patch, then
`MSBuild YYToolkit\YYToolkit.vcxproj -p:Configuration=Release -p:Platform=x64`.

### Known YYToolkit 5.0.0 limitations here

* `EVENT_FRAME` / `EVENT_RESIZE` never fire. YYTK 5.0.0 has no code path that installs
  its Stage 3 D3D11 `Present` hook. Drive per-frame work from `EVENT_OBJECT_CALL`
  instead (the example plugin keys off `gml_Object_obj_gamecontroller_Step_1`).
* `GetInstanceMember(<global CInstance>, name)` crashes in the runner's `YYGetString`
  (`0x1401BF58C`). Read globals with `CallBuiltin("variable_global_get", {name})`.
* Aurie normally opens a console window ("Aurie Framework Log") in every chapter
  process. The loader suppresses it (`AllocConsole` hook); set `aurie_console=1` in
  `modloader.ini` to get it back. Logs still go to `aurie.log` in the game folder.

### Example plugin: `yytk_plugin/`

`build.bat` builds `DeltaruneYYTK.dll`, which goes in `mods\aurie\`. It logs the first
GML events, then about once a second logs the room, `global.chapter`, `global.gold`, and
a running count of GML events. Its headers are YYTK's v5 `Shared/` headers
(`include/YYTK_SOURCE_COMMIT.txt`). The old `mod_test/aurie_build` plugins used v4.0.1 headers, which don't match the
v5 DLL (v5 "breaks all v4 mods"); they were deleted.

## Safety

The loader is pinned to DELTARUNE.exe build `TimeDateStamp 0x685E7007`
(md5 `7bf3cccc2e54481ced3a149e1a083684`). It checks the first 16 bytes of both hooked
functions first. After a game update that moves them, it logs
`version check FAILED` and installs no game hooks, so the game still runs unmodded.

## Addresses

| What | RVA | Doc |
|---|---|---|
| `ReadEntireFile(const char* utf8, unsigned* size)` | `0x92C40` | RE/09 |
| `ExecuteIt(self, other, CCode*, RValue* args, int flags)` | `0x1BBC90` | RE/10 |
| CCode list head (`next` @ +8, `name` @ +128) | `0x8C9EC8` | RE/10 |
