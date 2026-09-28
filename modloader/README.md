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
    load_order.txt         optional explicit load order (see below)
    modloader.log          written by the loader
    MyMod\
      chapter1_windows\lang\lang_en.json    ← replaces the game's file
      chapter2_windows\options.ini
      redirects.txt                         ← optional script redirects
    _DisabledMod\          ← '_' or '.' prefix = ignored
```

* **Load order** is deterministic. Mods named in `mods\load_order.txt` (one folder name per
  line, `#` comments, case-insensitive) load first, in that order. Every other enabled mod
  follows alphabetically by folder name (ordinal, case-insensitive, the same on every
  machine and locale). **Later mods win**: file overrides and full `.gml` replacements come
  from the last mod that has them, and patches are applied in this order, each on top of the
  previous. `modloader.log` prints the final order (`mod #0`, `#1`, …), and names in
  `load_order.txt` that aren't installed are logged and ignored. Within one mod,
  `all_chapters\` applies before `chapterN_windows\`, and files are processed in name
  order. `mods\native\*.dll` also load alphabetically.
* **File overrides**: any file the runner reads through `ReadEntireFile` from inside
  the install folder can be replaced by the same relative path under
  `mods\<Mod>\`. The last mod in the load order wins. This covers
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

## Loose GML, sprites and sounds (no xdelta, no rebuilt data.win)

```
mods\<Mod>\chapter2_windows\code\gml_Object_DEVICE_MENU_Draw_0.gml   decompiled GML (UTMT code entry name)
mods\<Mod>\chapter2_windows\sprites\spr_heart_0.png                   replace frame 0 of spr_heart
mods\<Mod>\chapter2_windows\sprites\spr_mything_0.png ... _3.png      new sprite (frames from 0, no gaps)
mods\<Mod>\chapter2_windows\sprites\spr_mything.origin.txt            "x y" origin for a new sprite (default 0 0)
mods\<Mod>\chapter2_windows\sounds\snd_menumove.ogg                   replace a sound (.ogg or .wav)
mods\<Mod>\chapter2_windows\sounds\snd_mynew.ogg                      new sound, usable as snd_mynew in GML
mods\<Mod>\code\... sprites\... sounds\...                            launcher (root data.win)
```

`mods\<Mod>\all_chapters\` has the same layout and is applied to every `chapterN_windows`
data.win (before that chapter's own folder).

### Replacing a sprite (step by step)

1. **Find its name.** Install `mods_examples/SpriteInspector` (copy it into `mods\`) and press
   **F9** in game. Every sprite on screen gets a yellow label (`spr_city_mice_bell [0/2]`
   = frame 0 of 2), and a panel lists the room and the unique names. **Shift+F9** copies
   that list to the clipboard. Disable the mod again (`_SpriteInspector`) when you're done.
2. **Get the original frames** into your mod, already named correctly:
   ```
   mods\tools\export_sprites.bat 2 MyMod spr_city_mice_bell
   mods\tools\export_sprites.bat 1 MyMod spr_krisd*          (wildcards work)
   ```
   This writes `mods\MyMod\chapter2_windows\sprites\spr_city_mice_bell\spr_city_mice_bell_0.png`,
   `_1.png`, and so on. It never overwrites files, and it suggests close names if one
   doesn't exist. Subfolders under `sprites\` are optional; flat files work the same.
3. **Edit the PNGs.** Keep the file names and, ideally, the size.
4. **Start the game** and read `mods\modloader.gml.log`:
   ```
   [DR] sprite spr_city_mice_bell: replacing frame(s) _0.._1 of 2 (MyMod)
   ```
   Mistakes are reported there instead of silently doing nothing:
   - a misspelled name, which would create an unused new sprite: *"did you mean …?"*;
   - frames past the original count with a gap (the sprite is skipped), or without one (a
     warning, because the game's code may never show extra frames);
   - a frame whose size differs from the original, which may look shifted;
   - PNGs in a wrongly named folder (e.g. `Sprite\` from a UTMT export).
   New sprites (a name the game doesn't have) need frames `_0`, `_1`, … with no gaps. Add
   `<name>.origin.txt` to set their origin, which also marks the new name as intentional.

### Patching instead of replacing

Full `.gml` files replace a script entirely (a later mod wins). To change *part* of a script, so that
several mods can touch the same one, put a patch next to it in `code\`. Patches are applied
after all full replacements, in mod load order, on top of whatever the script is at that
point (decompiled with UTMT):

| File | Effect |
|---|---|
| `<entry>.append.gml` | code added at the end |
| `<entry>.prepend.gml` | code added at the start |
| `<entry>.patch` | `>>> find` / `>>> replace` / `>>> end` blocks (see below) |
| `*.diff` | `git diff` / unified diff output against decompiled GML (`+++ b/<entry>.gml`). Each hunk is applied at the nearest match to its `@@ -N` line (like `git apply`), with indentation ignored for matching and kept from the diff |

```
>>> find
if (quit_timer >= 1)
>>> replace
draw_text(8, 452, "hello");
if (quit_timer >= 1)
>>> end
```

`find` matches whole lines and ignores indentation. Every match is replaced; use `>>> find first` for only the first,
or `>>> find regex` for a .NET regex. Diff hunks are matched the same way (context and `-` lines, with
line numbers ignored), so a diff made against one UTMT decompile still applies after other mods
have shifted lines. `--- /dev/null` creates a new entry. A patch whose text isn't found
is skipped with a `WARN` in `mods\modloader.gml.log`; everything else still loads.
Example: `mods_examples/CheatMenu`.

When the game reads a `data.win` that any enabled mod has loose assets for, the loader
runs UTMT's own library (`mods\tools\ImportLooseMod.csx`) on the vanilla file, applying
all mods in load order (a later mod wins on the same name). Sprites and sounds are imported
before code, so GML can reference new asset names. The patched data goes to the game
from memory. The `data.win` on disk is never modified and no patched copy is written. On
any error the game runs vanilla, and the details go to `mods\modloader.gml.log`.

- Sprite PNGs are packed onto new texture pages. A replaced frame 0 with a different size
  resizes the sprite. Precise collision masks are regenerated only for sprites that
  already had one.
- Sounds are embedded in `data.win` (default audio group) and play through the normal
  `snd_play`. Streamed music (the `.ogg` files next to `data.win` and in `mus\`) is
  opened by the audio engine, not `ReadEntireFile`, so it can't be overridden yet.

Needs `mods\tools\utmt\` (UndertaleModTool CLI 0.9.2.0, `UTMT_CLI_*-Windows.zip`) and
`mods\tools\ImportLooseMod.csx` (from `tools/`; `ImportGMLFolder.csx` is the older
code-only version and is used if `ImportLooseMod.csx` is absent). You can override the CLI
path with `utmt_cli=` in `modloader.ini` and the timeout with `gml_timeout_ms=` (default 300000).
Ch2 with 216 GML files plus a few assets takes about 6 s.

**Shrinking a loose-GML mod into patches:** `python tools/gml_to_patches.py <game> <src_mod> <out_mod>`
decompiles vanilla for every `.gml` the mod replaces and writes a `.diff` wherever that's
smaller (new or heavily rewritten entries stay as full `.gml`). For Local Multiplayer v19 this
gives 25 MB → 4 MB, 1759 of 2812 entries as diffs, and a build identical to the full mod
(all 2812 entries decompile the same). Because patches only touch the changed lines, other
mods (e.g. CheatMenu's `obj_time_Step_1` append) stack on the same scripts.

**Converting an xdelta mod:** `python tools/xdelta_to_gml.py <game> <ModName>
chapter2_windows=patch.xdelta ...` keeps only the code entries the patch changed and
warns about asset differences (export those with UTMT into `sprites\`/`sounds\`).

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
