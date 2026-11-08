# yyc_interop

Call and hook GML scripts by name from a YYToolkit v5 plugin. This is the interop layer
of the toolchain described in `docs/yyc_il2cpp.md`. All headers are header-only:

| Header | Runner | What |
|---|---|---|
| `include/script_hooks.hpp` | VM and YYC | `gm::Observe` and `gm::Redirect` by name; picks the backend at run time |
| `include/yyc_interop.hpp` | YYC | registry of compiled scripts/events, `CallScript`, inline detours |
| `include/vm_hooks.hpp` | VM | observers and redirects on the interpreter's script calls |

## script_hooks.hpp (both runners)

```cpp
gm::Init(module, yytk);                  // once, on the game thread
gm::Observe("scr_foo", fn, user);        // fn(const gm::CallInfo&, void*) before each call
gm::Observe("", fn, user);               // every script call (VM only)
gm::Redirect("scr_foo", "scr_bar");      // calls of scr_foo run scr_bar
```

`CallInfo` gives the script name, `self`, `other` and the argument count. `gm::Init` asks
the built-in `code_is_compiled()` which runner it is in.

- **YYC backend.** Observers are detours on the compiled function, drawn from a pool of 64
  thunks. A redirect detours the script's entry to the replacement.
- **VM backend.** One hook on the interpreter's frame-push routine. Observers then fire
  for observed scripts, and a redirect swaps the `CScript*` the frame is built for.

On both runners, a redirected call fires the replacement's observers.

### Limits

- On VM, observers run before the callee starts. There is no after-call point: the callee
  runs later in the same interpreter loop.
- On VM, a redirect cannot change arguments or wrap the result.
- On YYC, observe-all is not offered: it would mean detouring all of the game's compiled
  scripts.
- For full pre/post control on YYC, use `yyc::Hook`.
- Object events are not covered on either runner. They run through `Code_Execute`, and
  YYToolkit's `EVENT_OBJECT_CALL` already handles them.

## vm_hooks.hpp (VM)

Script calls on a VM runner never pass through `Code_Execute`. The interpreter's call
handler (DoCall) builds a new frame with an internal routine instead:

```
RValue* VM_PushFrame(CScript*, int argc, RValue* sp, VMExec*, YYObjectBase* locals)
```

The interpreter then keeps running in the same loop. Every bytecode script call reaches
that routine: `call.i`, method calls, `script_execute`. The routine is located at run time
as the only function that references the runner's trace string `"script call = %s\n"`. It
is not found by address. `RE/10_vm_and_scripts.md` covers the reverse engineering.

`self`/`other` are read at `VMExec+0x28`/`+0x30`. Those are the offsets DoCall itself
uses in DELTARUNE (GM 2024.x).

## yyc_interop.hpp (YYC)

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

## Example: `example/ScriptHookDemo.cpp` → `ScriptHookDemo.dll` (both runners)

The same DLL runs on both runner types:

- On VM it counts every script call for 600 frames and logs the most-called scripts.
- It observes the script named by `watch=` and redirects the one named by `redirect=from=to`.
  Both options go in `[demo]` of `script_hook_demo.ini` next to the exe.
- It logs to `script_hook_demo.log`.

Verified on 2026-11-08:

| Game | Runner | Frame-push routine | Result |
|---|---|---|---|
| DELTARUNE launcher, Proton | VM | found at exe+0x1CF3F0, the address IDA shows; 237 scripts | ~5300 calls per 600 frames over 15 scripts |
| Dwarf Eats Mountain, Wine | YYC | found at exe+0xEF1350 | 0 calls, as expected: compiled scripts never use it; `gm::Observe("loc")` used the YYC backend and saw ~4200 calls |

The redirect test on the DELTARUNE launcher swapped the version label's anonymous function
(`gml_Script_anon_..._obj_ui_version_Create_0_533_...`) for `scr_is_switch_os`:
- the bottom-left "DELTARUNE v23" text is drawn almost black, where it is normally grey
  (checked with `screen_save` screenshots);
- the replacement's observer count rose by exactly the redirected calls.

Note: `screenshot=1` (`screen_save`) writes the PNG but then ends the DELTARUNE process
under Proton. The same call works on Dwarf Eats Mountain. The cause is not known yet.

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
