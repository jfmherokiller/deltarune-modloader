# yyc_interop

Call and hook compiled GML by name in YYC GameMaker games, from a YYToolkit v5 plugin.
This is the interop layer of the YYC toolchain described in `docs/yyc_il2cpp.md`.

`include/yyc_interop.hpp` is header-only:

- `yyc::Functions().Init(yytk)` builds a name → address map of every compiled script and
  object event. It reads the runner's own `YYGMLFuncs` table, which it locates by following
  any `CScript::m_Functions` pointer. No fixed addresses are involved, so a mod survives game
  updates as long as the GML names it uses still exist.
- `yyc::CallScript("name", self, other, result, {args...})` calls a script directly.
- `yyc::Hook(module, "name", detour, &original)` installs an inline detour (Aurie
  `MmCreateHook`) on a script (`"loc"` or `"gml_Script_loc"`) or an event
  (`"gml_Object_oGUI_Step_0"`). YYC scripts call each other with direct `call`s, so only an
  entry detour sees every call.

Signatures: every script is
`RValue& (CInstance* self, CInstance* other, RValue& result, int argc, RValue** argv)`, and every
event is `void (CInstance* self, CInstance* other)`.

Initialise from the game thread once the game is running, e.g. in the first
`EVENT_OBJECT_CALL` callback. Do not keep `RValue`s in static storage: their constructor
calls into YYTK during CRT init, which fails under Wine.

## Example: `example/HookDemo.cpp` → `YYCHookDemo.dll`

Built for Dwarf Eats Mountain 1.1.0. Build it with `build.sh` (or `build.bat`, which has not
been run on Windows), copy it to `mods/aurie/`, and read `yyc_hook_demo.log` next to the exe.

Verified under Wine (runner 2024.14.4.268):

- The registry finds 5917 compiled scripts/events, and both hooks install.
- The `loc` detour sees about 7 calls per frame, from 7 call sites in the game's code. All of
  them are inside compiled GML (`gml_Object_oGUI_Step_0`, `gml_Object_oGUI_Draw_64`, and
  closures inside them), so the direct calls are intercepted.
- The `gml_Object_oGUI_Step_0` detour runs once per frame.
- `yyc::CallScript("getPriestCount")` and YYTK's `CallGameScript` return the same value.
- With `YYC_DEMO_TAG=1`, every localised string the game draws on screen gets a `MOD `
  prefix (the HUD labels, the tutorial header and the tutorial body). The demo checks this
  with the engine's own `screen_save`.

  Square brackets would not show: the game renders text with Scribble, which reads `[...]`
  as a formatting tag.
