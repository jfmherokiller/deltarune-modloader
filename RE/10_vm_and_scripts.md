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

## Script-to-script calls bypass `Code_Execute`: `VM_PushFrame` — CONFIRMED live (2026-11-08)

With `log_scripts=1`, `Code_Execute` sees only object events and `gml_GlobalScript_*` bodies
on the chapter-select screen: 74 first runs, none of them `gml_Script_*`. Function calls
take another path.

| Address | Name | What it does |
|---|---|---|
| `0x1401CC280` | `DoCall` (string `"DoCall :: Execution Engine type error"`) | Bytecode call handler (`call.i` / `callv`) |
| `0x1401CFCF0` | `VM::Exec` | Called from `Code_Execute`; runs the interpreter loop (`0x1401D0550`, or `0x1401D0160` under the debugger; see "Return / frame-pop path") |
| `0x1401CF3F0` | `VM_PushFrame` (named here) | Builds the callee's frame; does not run the callee |
| `0x1400BA7A0` | `Script_GetById` | Returns `g_ScriptCodeTable[i]`, a `CScript*` despite the `CCode` typing in the IDB |

`VM_PushFrame` is the only function referencing the trace string `"script call = %s\n"`.
That string is printed when the flag at `0x1408CA00E` is set.

`DoCall` handles the call kinds as follows:

- **Script ids 100000-500000:** `Script_GetById(id - 100000)` and then
  `VM_PushFrame(script, argc, sp, vm, 0)`.
- **Ids above 500000:** extension functions.
- **Ids below 100000:** built-ins, through the table `unk_1408C9E40` (24 bytes per entry,
  function at +8).
- **Method calls (`callv`, kind `0x50000`):** `VM_PushFrame` again, with the method's
  `CScript` (`[[BoundArgs]]` are prepended).

`VM_PushFrame(CScript* s, int argc, RValue* sp, VMExec* vm, YYObjectBase* locals)`:

- It saves the caller's state in a frame record on the VM stack:
  - self/other at `vm+0x28`/`+0x30`;
  - code at `vm+0x38`;
  - the PC and other fields.
- It points `vm` at the callee's `CCode` (`s->m_Code`), creates the "Locals" object and
  returns the new stack pointer.
- The interpreter loop then continues inside the callee. The callee's `ret` pops the frame.

Consequences for hooking:

- **One hook covers every bytecode script call.** An inline hook on `VM_PushFrame` sees
  `call.i`, method calls and `script_execute`. Replacing its first argument redirects the call
  to another `CScript`.
- **There is no after-call point.** The callee runs after the hook returns; seeing its end
  needs a hook on the `ret` path, located in "Return / frame-pop path" below.
- **It is irrelevant on YYC.** YYC runners link the same routine (Dwarf Eats Mountain
  `exe+0xEF1350`, found by the same string), but compiled scripts call each other natively and
  never reach it.

Verified live on the DELTARUNE launcher with `modloader/yyc_interop/include/vm_hooks.hpp`:
- 5300 script calls per 600 frames across 15 scripts;
- a redirect of the version label's anonymous function to `scr_is_switch_os` visibly dimmed
  the label (see `modloader/yyc_interop/README.md`).

## Return / frame-pop path

Static pass, 2026-11-19, on the shipped `DELTARUNE.exe` (SHA-256 `b882cc1879ad8f24c63fa6e4f47a8fef9a01065ee61ad6220beed9932b04e7ed`, the build every
address in this file refers to). Evidence: Hex-Rays and disassembly of the functions below, from a
scratch copy of `DELTARUNE.exe.i64` (`idat -A -S`, changes discarded); the byte patterns were checked
for uniqueness against `.text` with pefile. Nothing here has been exercised live yet; the hook design
in the last subsection is the plan for the implementation task (ROADMAP task 1).

### Two interpreter loops, one opcode table

| Address | Name (proposed) | Role |
|---|---|---|
| `0x1401CFCF0` | `VM::Exec(CCode*, self, other, RValue* result, locals, argc, RValue* args)` | Native → VM boundary. Builds a `VMExec` on the native stack, runs a loop, copies the result out |
| `0x1401D0550` | `VM_Loop` | **The loop used in normal play.** Calls pre-decoded handlers from `VMBuffer+0x20` |
| `0x1401D0160` | `VM_LoopDebug` | Used only while the GML debugger is attached (`unk_1406835F8`, written only by the debug-server code in `sub_1401FF5E0` / `sub_1400516C0`). Same body plus breakpoint checks; it indexes the opcode table directly |
| `0x1405FCD70` (`.data`) | `g_VMOpHandlers[32]` | Opcode handlers, indexed by `(instr >> 24) & 0x1F`. Entry `0x19` is `DoCall` |
| `0x1401DA640` | `VMBuffer::Prepare` | Fills `VMBuffer+0x20` (one handler per instruction) from `g_VMOpHandlers`, plus `VMBuffer+0x28` (pc/4 → instruction index). It substitutes fast handlers only for opcode `0x05`, opcode `0x07` and `call.i` (`0x1401CF260` for scripts, `0x1401CF2C0` for built-ins). `ret`/`exit` always get the generic table entries |

The earlier statement that the main loop is `0x1401D0160` is therefore only true under the
debugger. Both loops call the same `ret`/`exit` handlers, so a detour on those handlers covers
both loops.

Handler ABI (both loops): `RValue* Handler(uint32 instr, RValue* sp, uint32* operands, VMExec* vm)`
returns the new stack pointer. Win64: `ecx` = instruction word, `rdx` = `sp` (VM stack grows
down, `sp` = top RValue), `r8` = operand words (`pc + 4`), `r9` = `vm`.

### `VMExec` fields used on this path

Offsets come from `VM::Exec`'s stack-local initialisation and from `VM_PushFrame` / the pop code.
`vm_hooks.hpp` already uses `+0x28` and `+0x30`.

| Off | Field | Notes |
|---|---|---|
| `+0x08` | `prev` | Enclosing `VMExec` (chain head `g_CurrentVMExec` = `0x1408D02A0`) |
| `+0x10` | `stackBase` | VM stack block. It is **reallocated** (+0x4000, contents moved to the top) when `sp` gets within 0x400 bytes of the base, so raw stack addresses are not stable. Offsets from `stackBase + stackSize` are |
| `+0x18` | `localsBorrowed` | 0 = `locals` was created for this frame and is freed on pop |
| `+0x20` | `locals` | `YYObjectBase` "Locals" |
| `+0x28` / `+0x30` | `self` / `other` | Current code's `CInstance*`. `DoCall` writes a method's bound self here after `VM_PushFrame` |
| `+0x38` | `code` | **`CCode*` of the running code**: the callee while it runs, restored to the caller on pop |
| `+0x40` / `+0x48` | `args` / `argc` | Mirrors of the globals `g_VMArgs` (`0x1408C9EC0`, `RValue*`) and `g_VMArgCount` (`0x1408CE290`, `int`) |
| `+0x50` | `bytecode` | `VMBuffer->bytecode` |
| `+0x58` | `frame` | Current frame record (below) |
| `+0x60` | `vmBuffer` | `CCode+0x68` |
| `+0x70` | `name` | `CCode+0x80`, e.g. `gml_Script_scr_foo` |
| `+0x88` | `stackSize` | |
| `+0x8C` | `pc` | Byte offset into `bytecode` |
| `+0x90` | `handlerIndex` | Index into `VMBuffer+0x20` |
| `+0x94` | `depth` | Script frames pushed by `VM_PushFrame` in this `VMExec`. 0 = the code `VM::Exec` was entered with |
| `+0x98` | `codeLength` | Loop ends when `pc >= codeLength` |
| `+0xA0` / `+0xA8` | `handlers` / `pcToIndex` | `VMBuffer+0x20` / `+0x28` |

### Frame record (`VM_PushFrame`, 0x78 bytes, directly below the callee's arguments)

`VM_PushFrame` pads the arguments up to `CCode+0xA4` (argument count) with `undefined`, sets
`g_VMArgs` to the first argument, and builds the record at `args - 0x78` (so `frame + 0x78 == args`).
It then sets `vm->frame` to the record and increments `vm->depth`. Its return value is the record
address.

| Off | Saved | | Off | Saved |
|---|---|---|---|---|
| `+0x00` | magic `0xAABBCCDD` (checked with `int 3`) | | `+0x30` | caller `vm->code` (`CCode*`) |
| `+0x04` | caller `pc` (return address) | | `+0x38` | caller `vm->vmBuffer`. 0 only in `VM::Exec`'s own zeroed record |
| `+0x08` | caller `vm+0x68` (int, purpose not traced) | | `+0x40..+0x50` | caller `vm+0x78`, `+0x80`, `+0x70` (name) |
| `+0x0C` | caller `argc` | | `+0x58` | try-block record (48 bytes, set by GML `try`; 0 if none) |
| `+0x10` | caller frame, as offset from stack top (-1 = none) | | `+0x60` | caller `locals` |
| `+0x14` | caller `localsBorrowed` | | `+0x70` | saved `self+0x20` (restored if the callee's `CCode+0xA8 < 0`) |
| `+0x18` | caller `args`, as offset from stack top | | `+0x78` | **callee argument 0** |
| `+0x20` / `+0x28` | caller `self` / `other` | | | |

### Every place a script frame is popped

| # | Where | Trigger | Return value the caller gets |
|---|---|---|---|
| 1 | **`ret` handler `0x1401CD6F0`** (`g_VMOpHandlers[0x1C]`, opcode `0x9C`) | GML `return x;` | The top-of-stack RValue, moved (not copied) into the caller's stack |
| 2 | **`exit` handler `0x1401C1920`** (`[0x1D]`, opcode `0x9D`) | GML `exit;` and the implicit end of every function body | `undefined` (kind 5) |
| 3 | Loop end, `0x1401D0781` in `VM_Loop` (`0x1401D0468` in `VM_LoopDebug`) | Callee ran past the end of its buffer with `depth > 0` | Real `0` (kind 0), the pre-2.3 script convention |
| 4 | `VM_PopFrame` `0x1401D4810` called from `sub_14000D6B0` (throw unwinder) | GML `throw` / a runtime `YYGMLException` caught by a GML `try` further up | None; the frames between the throw and the `try` are discarded |

`VM_PopFrame(RValue* sp, VMExec* vm)` (`0x1401D4810`) is the shared pop routine for #2–#4. It
returns `args + 16*argc` of the dead frame, the caller's stack top before the arguments were
popped. `ret` (#1) does not call it; it inlines the same restore sequence.

Pop sequence (identical in `ret` and `VM_PopFrame`): it decrements `vm->depth` and checks the magic.
It restores `self`, `other`, `code`, `pc`, `vmBuffer`, `name`, `frame`, `bytecode` and `codeLength`
from the record, frees the callee's `locals` unless borrowed, and restores `locals`. It re-runs
`VMBuffer::Prepare` for the caller's buffer, frees the try record, and frees and blanks the callee's
`g_VMArgCount` arguments to `undefined`. Last, it restores `g_VMArgs`/`g_VMArgCount`/`vm->args`/
`vm->argc` and the current-self global `0x1408C9E50`.

Bytecode evidence that #1 and #2 are the normal paths: every function body in DELTARUNE ends
`ret.v` (`9C050000`) followed by `exit.i` (`9D020000`) (fixture listing
`compiler/fixtures/runtime_bytecode/launcher/listings/gml_GlobalScript_rcfx_scripts.txt`). A raw
scan of all six data.win CODE chunks finds no other `ret`/`exit` type (`ret.v` 127–1293 and
`exit.i` 210–8107 per file). So the `ret` handler only ever runs its type-5 case: 16 bytes are
moved, and `sp` points at a real RValue. Path #3 cannot be reached from compiled 2.3 function
bodies; it is a fallback.

### The `ret` handler in detail (`0x1401CD6F0`)

At entry, `depth = vm->depth`, return type `= (instr >> 16) & 0xFF` (`5` = `.v`):

- **`depth == 0`**: this is the code `VM::Exec` was entered with (an object event, or a function
  started from native code). The handler only sets `pc = codeLength` and returns `sp` unchanged.
  The loop exits, and `VM::Exec`'s epilogue copies `*sp` into its `result` argument (release
  `0x1401D07DA`: `movups xmm0,[rdi]` / `movups [rax],xmm0`, only if `sp < vm->frame`). This is
  the native boundary. The `result` reaches `Code_Execute`'s **4th argument**: `Code_Execute`
  passes `r9` straight to `VM::Exec` as `result`, and on its YYC/native branch passes it as the
  `result` of `fn(self, other, result, 0, nullptr)`. The 4th argument YYToolkit calls `Arguments`
  is in fact the result slot; arguments come from `g_VMArgs`.
- **`depth > 0`**: it pops the frame as above, then `memmove(frame + 0x68 + 16*argc, sp, 16)` and
  returns that address. Here `argc` is the callee's padded `g_VMArgCount`, and the destination is
  the slot of the callee's last argument, already blanked. The caller's stack therefore has the
  return value on top, in place of the arguments.

### What is reachable at the hook point

At the `ret`/`exit` handler entry, before the original runs, the callee's frame is still intact:

| What | Where |
|---|---|
| Return value (`ret`) | `*(RValue*)rdx` (`sp`) |
| Returning code | `vm->code` (`[r9+0x38]`, a `CCode*`). Name: `vm->name` (`[r9+0x70]`) = `CCode+0x80`. Script index: `CCode+0x88`. `CScript*` is not stored anywhere in the frame. Map it by `CScript::m_Code` (`Script_GetById(i)->m_Code`), or carry it from the `VM_PushFrame` hook |
| Arguments | `g_VMArgs` / `g_VMArgCount` (= `[r9+0x40]` / `[r9+0x48]`; also `frame + 0x78`). Freed by the original, so read them before calling it |
| self / other | `[r9+0x28]` / `[r9+0x30]` (the callee's, including a method's bound self) |
| Caller | `frame+0x30` (`CCode*`), `frame+0x04` (return pc), `frame+0x20/+0x28` (caller self/other) |
| Frame identity | `vm->depth` (`[r9+0x94]`) is the same value `VM_PushFrame` left after incrementing. `vm->frame` (`[r9+0x58]`) is the record `VM_PushFrame` returned, but as an absolute address it moves if the stack is reallocated. Use `(vm, depth)` or the frame's offset from the stack top to pair calls |

After the original returns (`depth > 0`), `rax` points at the caller-side slot that holds the
return value: the moved RValue for `ret`, `undefined` for `exit`.

### Abnormal exits

- **GML `throw` / runtime `YYGMLException`.** The loops catch `YYGMLException*`. If
  `sub_1401D2970` finds a try record in any frame of the `VMExec` chain, `sub_14000DB10` →
  `sub_14000D6B0` pops frames with `VM_PopFrame` until it reaches the frame owning the try record,
  in this and enclosing `VMExec`s. It sets the unwind globals: `0x14067DA31` (unwinding flag),
  `0x14067DA50` (target `VMExec`), `0x14067DA38` (target sp), `0x14067DA40` (exception RValue),
  `0x14067DA58`/`0x1405F400C` (args/argc), and the target `pc` (catch/finally). The loop then
  resumes there, or `break`s out to return to native code if the target is an outer `VMExec`;
  `VM::Exec` then skips copying the arguments back. The discarded frames get **no** `ret`/`exit`.
  `VM_PopFrame` runs once per discarded frame, so it is the place to drop their bookkeeping.
- **No GML handler.** The exception is rethrown out of the loop with no frames popped. It ends in
  `YYError("Unable to find a handler for exception …")`, which is fatal.
- **Non-GML C++ exceptions** are not caught by the loop at all; frames are not popped. Not traced
  further.
- **`depth == 0` `ret`/`exit`** do not pop a frame (see above). The value goes to `VM::Exec`'s
  `result` and then to `Code_Execute`'s 4th argument.
- **Fall-through past the buffer end** (#3) pops through `VM_PopFrame` and pushes real 0 inline in
  the loop. There is no handler to detour, and the 2.3 bytecode never reaches it.

### Recommended hook (for ROADMAP task 1's implementation)

Inline-detour three functions, all located through the opcode table:

1. **`ret` handler** (`g_VMOpHandlers[0x1C]`). Pre: if `vm->depth > 0` and the type is `.v`, pop
   the side-stack entry for `(vm, depth)` (the entry was pushed by the existing `VM_PushFrame`
   hook). Capture self/other/args. Call the original. Post: run the post-observers with
   `*(RValue*)rax` as the result; they may replace it. Replacement is a normal RValue assignment:
   free the old payload, then write the new value. If `depth == 0` before the call, the value goes
   to `Code_Execute`'s result instead; leave that to a `Code_Execute` post-hook.
2. **`exit` handler** (`g_VMOpHandlers[0x1D]`). Same as `ret`; the result at `rax` is `undefined`.
3. **`VM_PopFrame`** (`0x1401D4810`). Pre: if a side-stack entry for `(vm, vm->depth)` is still
   present, drop it without calling observers (an exception unwind, or the loop-end fallback). The
   `exit` handler calls `VM_PopFrame` itself. By then its detour has already taken the entry, so
   nothing fires twice.

This keeps the fast path to a counter check: when no post-observers exist, the `VM_PushFrame` hook
pushes nothing, and the three detours test only `depth` against the side-stack top. Why the other
options were rejected:

- A mid-function hook in the loop would need two copies (release and debug loop) and gains
  nothing over the handler detours.
- Swapping the `g_VMOpHandlers` entries does not reach already-prepared `VMBuffer`s, which keep a
  copy of the pointer.
- Wrapping call sites is impossible, because the callee runs later in the same loop.

**Locating it without fixed addresses (any GM 2.3+ VM runner):**

- `DoCall`: the only function referencing `"DoCall :: Execution Engine type error"` (string at
  `0x140510B58`).
- `g_VMOpHandlers`: the only 8-aligned run of 32 qwords in `.data` whose every entry points into
  `.text` and whose entry `0x19` equals `DoCall`. The scan finds exactly one: `0x1405FCD70`. Also
  in DELTARUNE, entry `0x1E == 0x1F`.
- `ret` = entry `0x1C`, `exit` = entry `0x1D` (opcodes `0x9C`/`0x9D`, fixed since bytecode 15).
- `VM_PushFrame`: `"script call = %s\n"` (unchanged, `vm_hooks.hpp`).
- `VM_PopFrame`: the `call` target in the `exit` handler (first `E8` after the `depth` test), or the
  pattern below.

**Byte patterns** (DELTARUNE, each unique in `.text`; use them as a cross-check of the table lookup):

```
ret          0x1401CD6F0  40 56 57 48 83 EC 28 FF 05 ?? ?? ?? ?? 49 8B F9 45 8B 89 94 00 00 00 4C 8B C2 C1 E9 10
exit         0x1401C1920  48 83 EC 28 41 83 B9 94 00 00 00 00 4C 8B D2 7E ?? 49 8B 49 58 49 8B D1 E8
VM_PopFrame  0x1401D4810  40 56 57 41 56 48 83 EC 20 FF 05 ?? ?? ?? ?? 48 8B FA 80 3D ?? ?? ?? ?? 00 74 ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 77 58
loop-end pop 0x1401D0781  8B B3 94 00 00 00 85 F6 7E ?? 48 8B D3 48 8B CF E8 ?? ?? ?? ?? 48 8D 78 F0   (2 hits: release + debug loop)
```

The detour prologues are safe for MinHook/Aurie:
- `ret`: `push rsi; push rdi; sub rsp,28h`, then a RIP-relative `inc` that the trampoline relocates.
- `exit`: `sub rsp,28h; cmp dword [r9+94h],0`.

The `VMExec` offsets (`+0x28`, `+0x38`, `+0x40`, `+0x48`, `+0x58`, `+0x70`, `+0x94`) are the
version-specific part. Check them against the `ret` handler's own loads on a new runner: they appear
as `[r9+94h]` and `[rdi+58h]` right in its first instructions.

## Not yet done / next steps
- A real mod-loader would need to *load new bytecode from a loose file* rather than
  only redirecting between already-compiled in-`data.win` scripts — this test proves
  the hook/redirect mechanism, not bytecode authoring/compilation, which remains a
  much larger, separate problem (see `09_datawin_loader.md`'s note on the `CODE`
  chunk's inline fixup pass being the natural next drill-down for that).
- Disambiguate `CCode`'s branch-dependent fields (`field_80`, `field_90`, `field_9C`)
  — would need per-branch (debug-build vs. normal) tracing to pin down exactly.
- The `unk_1408C9E40` builtin-function dispatch table used by `script_execute`'s low
  branch (script id < 100000) — likely already covered by the P4 builtin-variable work
  (`07_globals_and_variable_storage.md`) via a different name; not cross-checked here.
- The per-instruction loop and the opcode handler table are now located (see "Return /
  frame-pop path"). Only `ret`/`exit` and the call handlers are named; the other 28 handlers
  are not individually identified.
