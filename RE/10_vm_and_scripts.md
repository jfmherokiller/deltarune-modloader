# Compiled scripts and the GML VM: `CCode`, `VMBuffer`, and script invocation

New pass, started 2026-09-17, directly targeting the user's stated goal for the
mod-loader work: **script replacement / hooking the VM**, as opposed to the asset
(texture/audio/localization) override work in
[09_datawin_loader.md](09_datawin_loader.md). Same evidence rules apply — see
[01_methodology.md](01_methodology.md).

## The `CCode` class — one per compiled script

The runner retains full MSVC RTTI (as already used in the P2 `YYObjectBase` hierarchy
work — see [07_globals_and_variable_storage.md](07_globals_and_variable_storage.md)).
Searching that RTTI symbol set for VM-related classes surfaced `??_7CCode@@6B@` — a
real C++ class, previously unexplored.

`CCode_Construct` (`0x1401BABD0`, renamed from `sub_1401BABD0`) is `CCode`'s
constructor, found and confirmed the same way `YYObjectBase_Construct` was in P2: its
own vftable-assignment instruction (`*(QWORD*)a1 = &CCode::vftable`). One `CCode`
instance exists per compiled script/code entry (184 bytes, confirmed by
`CCode_Destruct`'s free-size argument). All live instances are tracked in a global
singly-linked list — head `unk_1408C9EC8`, count `unk_1408C9EBC` — built by prepending
(`CCode_Construct` sets `self+8 = old_head; g_head = self`).

Declared `struct CCode` in the IDB (184 bytes, `0xB8`). Confirmed fields:

| Offset | Field | Notes |
|---|---|---|
| `+0` | `vftable` | `CCode::vftable` |
| `+8` | `next` | Singly-linked list of all live `CCode` instances |
| `+16` | `field_10` | Small int, `(ctor_arg3) + 1` — purpose unconfirmed (refcount? state?) |
| `+20` | `isValid` | Set to `1` unconditionally in the constructor |
| `+32`–`+79` | *(zeroed)* | 48 bytes, purpose not traced (likely embedded `RValue`-shaped default-return slots, unconfirmed) |
| `+100` | `sentinelFFFFFF` | Always `0xFFFFFF` — matches the `RValue.kind` 24-bit-mask convention seen elsewhere in this codebase; purpose here unconfirmed |
| `+104` | `vmBuffer` | **`VMBuffer*` — the actual compiled bytecode for this script.** See below |
| `+112` | `debugVmBuffer` | A second, conditional `VMBuffer*` (only allocated if `unk_1406A9E08` is set — likely a debug-build/unoptimized bytecode variant) |
| `+120` | `localeOrUnresolved` | Holds the global `Locale` pointer, **not** the name (originally mislabeled `name`; see the correction below) |
| `+128` | `name` | **Resolved script name string pointer** (`gml_Object_..._Draw_0` etc.) — confirmed live on Wine and native Windows |
| `+136` | `scriptIndex` | The compiled script's asset index |
| `+144` | `field_90` | Pointer to a 3-field-per-entry (24-byte stride) metadata table at `unk_1406A9DD8[5]`, or the raw CODE-chunk entry header pointer (`v17`), depending on branch |
| `+152` | `doNotFreeFlag` | Checked in the destructor to decide whether to unlink from the list |
| `+156` | `field_9C` | Branch-dependent meaning (a debug-build "is this a `gml_Script`/`gml_GlobalScript`-prefixed name" boolean in one branch, a raw bytecode-header field in another) — **do not assume one meaning**, this field is overloaded across the 3 construction paths |
| `+160` | `localCount` | From the CODE-chunk entry header, 16-bit sourced |
| `+164` | `argCountMasked` | From the header, masked `& 0x1FFF` (13 bits) — likely the real argument-count field, GameMaker packs flags into the high bits of the same word |
| `+168` | `compileFlags` | Top 3 bits of the same header word, combined with a sign bit from the constructor's own `scriptIndex` argument |
| `+176` | `field_B0` | Zeroed at construction, not traced further |

## `VMBuffer` — the actual bytecode container

A second, independent RTTI class, `??_7VMBuffer@@6B@` — not derived from `CCode` or
`YYObjectBase`. 48 bytes (`0x30`). Declared in the IDB:

```c
struct VMBuffer {
    void* vftable;
    unsigned int length;       // +8:  bytecode length in bytes
    unsigned int field_0C;     // +12: from the CODE-chunk header, 16-bit sourced
    unsigned int field_10;     // +16: from the CODE-chunk header, 16-bit sourced
    unsigned int _pad14;
    unsigned __int8* bytecode; // +24: THE ACTUAL EXECUTABLE BYTECODE POINTER
    __int64 field_20;          // +32: unused/reserved (zeroed)
    __int64 field_28;          // +40: unused/reserved (zeroed)
};
```

`bytecode` at `+24` is set from the CODE chunk's own fixed-up data — either
`v17 + v17[3] + 12` or `v17 + 8` depending on a global format-version flag
(`MEMORY[0x1406A9CF5]`), where `v17` is the CODE-chunk entry resolved the same
`unk_1406A9DE8 + offset` way every other chunk's references resolve. This ties
directly into [09_datawin_loader.md](09_datawin_loader.md)'s note that `CODE` is
handled *inline* during chunk loading (a bytecode operand-fixup pass) — this is where
that fixed-up bytecode ends up living for later execution.

**This is the concrete, in-memory home of one script's executable GML bytecode.**
Swapping `VMBuffer.bytecode`/`VMBuffer.length` for a different, self-consistent
bytecode buffer (built by some other means — not attempted here) would be the most
literal form of "script replacement": the VM would then execute different code under
the same script id, with zero changes to `data.win` itself.

## Script invocation chain

```
gml_script_execute / gml_script_execute_ext   (already-named GML builtins)
  ├─ script_id < ~100000 (builtin-function id space):
  │    directly calls unk_1408C9E40[3*id + 1]  -- the Function_Add-style builtin table,
  │    NOT a GML script; script_execute() on a low id just calls a builtin by id.
  └─ script_id >= 100000 (GameMaker's user-script-asset id convention):
       Script_Exists(id)                        (0x1400BA7C0, renamed)
         -> bounds-checks id-100000 against g_ScriptCodeTable
       Script_InvokeById(id, self, other, argc, &result, &args)  (0x1400BB020, renamed)
         -> looks up g_ScriptCodeTable[id-100000]  (a CCode*)
         -> the common path calls Script_InvokeCode(...)          (0x1401BB740, renamed)
              -> sub_1401BBC90 (NOT YET NAMED - see below)
```

- **`g_ScriptCodeTable`** (`0x1406AAE68`, renamed from `unk_1406AAE68`, typed
  `struct CCode**`) — the master script-id → `CCode*` dispatch table, bounded by
  `g_ScriptCodeTableCount` (`0x1406AAE58`). **This is the primary candidate hook point
  for script replacement at the table level**: swapping a specific index's `CCode*`
  (or that `CCode`'s `vmBuffer` contents) redirects every future call to that script id.
- **`Script_InvokeById`** (`0x1400BB020`) — **the primary candidate hook point at the
  call level**: every invocation of a user GML script (by id ≥ 100000) passes through
  here with the raw script id, `self`/`other` context, argc, and args — the natural
  place to intercept-and-redirect, intercept-and-wrap (call original then
  post-process), or fully replace with native code, matching how YYToolkit's own
  "script hooking" feature works conceptually (see `TODO.md`'s Track B history for why
  YYToolkit itself isn't used here — Aurie's own hooking primitive is broken under
  Wine; the proven proxy-DLL + MinHook pipeline is a direct substitute).

## `Code_Execute` — CONFIRMED live, the real general-purpose hook point (2026-09-17)

`Script_InvokeCode` (`0x1401BB740`) is a thin wrapper (confirmed, fully decompiled)
around `Code_Execute` (`0x1401BBC90`, renamed from `sub_1401BBC90`), which **Hex-Rays
fails to decompile outright** (`Decompilation failed at 0x1401bbc90`).

Static analysis alone left this ambiguous — the function is small (382 bytes) with
only 2 direct-call xrefs, too small to obviously be a giant opcode-dispatch loop, but
too central-looking to ignore. **Live-hook testing resolved it decisively**: extended
the proven proxy-DLL + MinHook pipeline (same one used for `ReadEntireFile` and
`Texture_DecodePNGAndUpload` in `09_datawin_loader.md`) with hooks on both
`Script_InvokeById` and `Code_Execute` simultaneously, ran the scratch rig's launcher
for ~10 seconds sitting idle on the chapter-select screen, and logged:

- **`Script_InvokeById` fired zero times.** It only handles the explicit
  `script_execute()`/`script_execute_ext()` GML *builtins* — rarely used in practice;
  ordinary script-to-script and object-event calls (the overwhelming majority of real
  GML execution) never go through it.
- **`Code_Execute` fired 52 times** in the same ~10 idle seconds, entirely from normal
  object Step/Draw event activity — exactly the behavior expected of "the thing that
  runs one piece of compiled GML code," called once per event per object per frame.

**Why Hex-Rays fails on it**: raw-byte inspection of `Code_Execute`'s cross-reference
addresses (`0x1405B5220`–`0x1405B5290` in `.rdata`, `0x1408F88D0`–`0x1408F8900` in
`.data`) decodes as RVA triplets matching the classic MSVC x64 C++ exception-handling
funclet table format (`try_start`/`try_end`/`handler`), with one entry pointing
exactly at `Code_Execute`'s address. This is GML's `try`/`catch`/`finally` compiled to
nested SEH — a known Hex-Rays weak spot — not an unusually complex opcode switch. Only
2 *static* direct-call xrefs exist (`Script_InvokeCode`, `sub_1401BB700`), but the
function is also reached through indirect (function-pointer/vtable-style) calls not
visible to static callgraph analysis, explaining the gap between "2 callers on paper"
and "fires 52 times in 10 idle seconds" in practice.

**This is the real, general-purpose hook point for both script replacement and VM
hooking** — superseding `Script_InvokeById` as the recommendation.

**Signature CONFIRMED by cross-referencing `AurieFramework/YYToolkit`'s own source**
(`Module Internals/Hooks/Hooks.cpp`), which independently identified and hooks this
exact function under the well-known community name **`ExecuteIt`**:

```c
bool ExecuteIt(CInstance* SelfInstance, CInstance* OtherInstance,
               CCode* CodeObject, RValue* Arguments, int Flags);
```

This matches this project's own live-fire observations exactly: `SelfInstance`/
`OtherInstance` stayed constant within a burst of calls (consistent with a stable
instance context), `CodeObject` varied every call (consistent with `CCode*` — a
different compiled-code object per event/script), `Arguments` stayed constant within a
burst (a reused per-frame stack buffer), `Flags` was observed as `0` throughout. This
is not a DELTARUNE-specific name — `ExecuteIt` is the standard name this function goes
by across the broader GameMaker RE/modding community; independently landing on the
same function via pure static analysis of this specific binary is a strong
cross-validation of both efforts.

**This also fully explains why hooking scripts never worked via Aurie/YYToolkit in
this project's earlier Track B attempts (see `TODO.md`)**: YYToolkit hooks `ExecuteIt`
via `MmCreateHook` — the exact Aurie hooking primitive this project independently
proved hangs indefinitely under Wine on *any* target (not GameMaker- or
DELTARUNE-specific), reported upstream at
[AurieFramework/Aurie#22](https://github.com/AurieFramework/Aurie/issues/22). The
target function itself (`ExecuteIt`/`Code_Execute`) is perfectly compatible and
reachable — it's purely the hooking primitive that's broken here. This project's own
proxy-DLL + MinHook pipeline is a direct, already-proven substitute for exactly this
one hook, without needing YYToolkit or Aurie's own hooking engine at all.

## Name resolution and live script replacement — CONFIRMED end-to-end (2026-09-17)

**Correction first**: `CCode`'s resolvable name field is at **`+128`**, not `+120` as
originally labeled — `+120` (`localeOrUnresolved`) actually holds the global `Locale`
pointer in 2 of `CCode_Construct`'s 3 branches (or a secondary table entry in the
third), not a script name; the `+128` field is what the constructor's debug branch
`strncmp`s against `"gml_Script"`/`"gml_GlobalScript"` prefixes, which is the real tell.

Reading `CodeObject+128` from a live `ExecuteIt` hook resolved a real, human-readable
name for **every single call**, exactly matching this project's own established GML
dump naming convention — sampled live from the scratch rig's chapter-select screen:

```
gml_Object_obj_ui_choice_Step_0      gml_Object_obj_CHAPTER_SELECT_Step_0
gml_Object_obj_ui_choice_Draw_0      gml_Object_obj_ui_version_Draw_0
gml_Object_obj_screen_start_Step_0   gml_Object_obj_screen_start_Draw_0
gml_Object_obj_input_Step_1          gml_Object_obj_init_pc_Draw_77
gml_Object_obj_gamecontroller_Step_1 gml_GlobalScript_scr_wrap
gml_GlobalScript_strlen              gml_GlobalScript_scr_os_checks
...
```

**Then live-tested an actual redirect, not just observation**: retargeted
`gml_Object_obj_ui_version_Draw_0` (the event that draws the "DELTARUNE v23" version
text visible in the bottom-left corner of the chapter-select screen) to
`gml_Object_obj_screen_start_Draw_0` — a completely different object's Draw event —
by matching `CodeObject`'s resolved name and substituting a different, already-loaded
`CCode*` (found by walking the global `CCode` linked list, head `unk_1408C9EC8`,
matching by the same `+128` name field) before calling through to the real
`ExecuteIt`. Same "substitute an identifying value, let the real implementation do
the rest" tactic already proven on `ReadEntireFile`.

**Result: conclusive, in the game's own words.** GameMaker's own runtime error
reporting fired:

```
ERROR in
action number 1
of Draw Event
for object obj_ui_version:

Variable obj_ui_version.init(100264, -2147483648) not set before reading it.
at gml_Object_obj_screen_start_Draw_0
```

This is about as strong a confirmation as this kind of test can produce: the error is
explicitly attributed to `obj_ui_version`'s Draw event, but names
`gml_Object_obj_screen_start_Draw_0` as the code that was actually executing — the
runtime's own diagnostics confirm, unprompted, that the substitution took effect
exactly as intended (the error itself is just `obj_screen_start`'s code referencing an
instance variable that only exists on `obj_screen_start` instances, not on
`obj_ui_version`'s — an expected, harmless consequence of cross-object substitution,
not a crash; matches this build's established pattern of graceful GML-level error
reporting rather than hard process crashes seen elsewhere in this project's dynamic
testing).

**This is full, live, end-to-end proof that GML script/event replacement via the
`ExecuteIt` (`Code_Execute`) hook works.** The redirect was disabled again in the
scratch rig's default build afterward (so a future run doesn't throw this error
dialog unexpectedly) — re-enable `REDIRECT_TARGET_NAME`/`REDIRECT_SUBSTITUTE_NAME` in
`modloader/src/modloader.c` (the ExecuteIt redirect hook) to reproduce.

## Clean redirect, no error dialog — CONFIRMED 2026-09-17

Following up on the note above: picked a genuinely safe substitution pair by dumping
the real GML source of both candidates first (`utmtcli dump <data.win> -c
<code_name>`) instead of guessing. `obj_ui_version`'s own `Create_0` event only
*writes* instance variables (literals + `get_version()`) and defines two function
closures — it never *reads* anything, so it can safely run in place of `Draw_0` on
the exact same instance with zero risk of an "undefined variable" error (the class of
error the first test hit). Since `Create_0` does no drawing, the expected visible
effect is simply that `Draw_0`'s output stops appearing.

Redirected `gml_Object_obj_ui_version_Draw_0` → `gml_Object_obj_ui_version_Create_0`
(same object, not cross-object this time) and verified live: fired repeatedly with
**zero error dialogs**, and the "DELTARUNE v23" version text visibly disappeared from
the chapter-select screen (screenshot-confirmed) while everything else rendered
normally. This is the clean demonstration of the same underlying capability — real
script/event replacement via the `ExecuteIt` hook, with no side-effect noise.

**General lesson for picking future substitution pairs**: check the real GML source
first (`utmtcli dump`) rather than guessing — a substitute is safe if it doesn't read
any instance variable the target's instance doesn't already have. Same-object
substitutions (different event on the identical instance) are inherently safer than
cross-object ones, since all of the object's own instance variables are guaranteed
present either way.

## Not yet done / next steps
- A real mod-loader would need to *load new bytecode from a loose file* rather than
  only redirecting between already-compiled in-`data.win` scripts — this test proves
  the hook/redirect mechanism, not bytecode authoring/compilation, which remains a
  much larger, separate problem (see `09_datawin_loader.md`'s note on the `CODE`
  chunk's inline fixup pass being the natural next drill-down for that).
- The literal per-*instruction* opcode loop (if separate from `Code_Execute`/
  `ExecuteIt` at all) is still not individually located — not needed for whole-script
  replacement, which is now fully proven end-to-end without it.
- Disambiguate `CCode`'s branch-dependent fields (`field_80`, `field_90`, `field_9C`)
  — would need per-branch (debug-build vs. normal) tracing to pin down exactly.
- The `unk_1408C9E40` builtin-function dispatch table used by `script_execute`'s low
  branch (script id < 100000) — likely already covered by the P4 builtin-variable work
  (`07_globals_and_variable_storage.md`) via a different name; not cross-checked here.
- The literal per-*instruction* opcode fetch-decode-execute loop (if it exists as a
  separate function at all, rather than being folded into `Code_Execute` itself) is
  still not individually located — not needed for whole-script/whole-event
  replacement (which `Code_Execute` already answers), only for true
  instruction-level tracing/hooking, a narrower and lower-priority goal.
