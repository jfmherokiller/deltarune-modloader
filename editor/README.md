# Runner Studio — runtime editor foundation

Standalone desktop tooling for compatible GameMaker games. This lives alongside the
DELTARUNE loader for now, but the editor and bridge must not depend on DELTARUNE's
executable name, global variables, object names, or addresses.

## Run

Requires Node.js/npm on Windows for development.

```bat
cd editor
npm ci
npm start
```

Or double-click `start.bat` after installing dependencies. To open an existing mod:

```bat
npm start -- --workspace="C:\path\to\mods\MyMod"
```

Open the mod directory itself, not a game install or `data.win`. Root-level
`code/`, `sprites/`, and `sounds/` are the generic layout. Existing DELTARUNE
`all_chapters/` and `chapterN_windows/` folders also work. The editor does not
install or inject a loader, launch a game, or modify any `data.win`.

## First slice

- Modern GameMaker-style workspace arrangement: Inspector at left, tabbed workspace
  in the middle, Asset Browser at right, Output/Search Results below.
- GML editing with Ace, object events grouped together, syntax highlighting, undo,
  local word/resource completion, function/region outline, and quick file opening.
- PNG pixel previews with zoom; local OGG/WAV playback.
- Literal search across saved text resources.
- Save existing loose text files with conflict detection. Saves do **not** apply to
  the running game. Binary files are preview-only. Close warns about dirty buffers.
- A separate read-only YYToolkit bridge, described in `runtime/README.md`.
- Runtime Explorer: paged globals, instances grouped by object, typed builtin/dynamic
  fields, loaded-row filtering, manual refresh, and sample age. No mod folder needed.
- No live code replacement, live asset replacement, arbitrary evaluation, or debugger
  control yet. **Apply live** is intentionally disabled.

This is not a full GameMaker IDE clone: no room canvas, docking/floating workspace
windows, resource creation, pixel painting, or debugger in this first slice.

## Keyboard

| Shortcut | Action |
|---|---|
| Ctrl+P | Go to resource |
| Ctrl+S | Save current loose text file |
| Ctrl+Shift+F | Search saved text files |
| Ctrl+F | Ace's search in the current buffer |
| Ctrl+Space | Local word/resource suggestions |

## GMEdit relationship

GMEdit is the coding-workflow reference, not a bundled dependency at this stage.
Its source uses Ace plus custom Haxe/GML integrations. This editor uses the
package-managed Ace build; sharing the editor engine does **not** provide GMEdit's
semantic analysis, syntax extensions, combined object documents, or plugin API.
Those need an explicit adapter/integration and tests. No GameMaker proprietary
assets or GMEdit binaries were copied.

Reference: <https://github.com/YellowAfterlife/GMEdit>, inspected at upstream HEAD
`4dbbf176990ceea16dcd9bb7668abd7a12c71753`. The source repository is MIT; its README
also mentions extra non-MIT components in prebuilt distributions. Review individual
component licenses before importing code or shipping those components.

## Live-variable fallback

The [Live Inspector plan](LIVE_INSPECTOR_PLAN.md) makes UnityExplorer-style runtime
variable browsing/editing a core track even if live asset or script replacement is
unavailable. It retains the GameMaker-style desktop UI: Globals/Instances explorer,
typed fields, explicit Apply, readback, watches, and conditional undo.

**Inspection implemented; editing not enabled.** Open **Connect**, select a process,
then **Runtime Explorer → Globals / Instances**. Use **Load more** for further pages;
filters search loaded rows only. Instance buttons open their typed fields. Refresh
resamples rather than modifying the game. Arrays/structs remain opaque summaries.

Verified live in DELTARUNE on 2026-10-09: launcher (110 globals, 8 instances), then
Chapter 3 `room_dw_green_room` (1,555 distinct globals, 76 distinct instances). Actual
Electron UI browsing loaded all globals and inspected first-page fields on 64
instances, with no renderer errors and values cleared on disconnect. Dwarf remains
unverified. The installed connector was upgraded with a backup; no data.win or
save files were written by this inspector.

Writes require a separately authorized command channel, lifetime/generation checks,
and separate live tests for VM and YYC games. An
in-game edit can be persisted by the game's own autosave; restart is not guaranteed
rollback.

## Game-independent architecture

See [ARCHITECTURE.md](ARCHITECTURE.md). A successful YYToolkit injection does not
imply every editing feature works on that runner. VM and YYC code replacement need
different backends. New games must never inherit DELTARUNE's pinned hook offsets.

## Tests

```bat
npm test
npm run test:ui
```

Backend tests use temporary files; UI tests launch actual Electron windows and use
synthetic mod fixtures. Named-pipe fixtures test the protocol, **not** in-game
compatibility. Real-game checks must be reported separately. Test fixtures do not
redistribute decompiled game code.

## Security boundaries

The renderer is sandboxed, has no Node integration, and uses a narrow preload API.
New windows, navigation, and browser permission requests are denied. The selected
workspace is the file boundary; IPC cannot select arbitrary save targets. Search
and preview do not execute GML. Runtime transport is local-only and read-only.

Dependencies are version-pinned in `package-lock.json`. Source follows the parent
repository's license; dependencies keep their own licenses. The YYToolkit bridge
uses the existing SDK, with the licensing obligations documented by that SDK.
