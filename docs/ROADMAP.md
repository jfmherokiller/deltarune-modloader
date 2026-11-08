# Roadmap: a BepInEx-style mod framework for GameMaker (VM and YYC)

Goal: a single install that works in any GameMaker game:
- plugins and loose mods on both runner types, VM (bytecode) and YYC (native);
- name-based hooks, so mods survive game updates;
- the RunnerEditor pipe, so Runner Studio and other tools can attach to the running game.

This file is the task list. Evidence for the YYC parts is in `docs/yyc_il2cpp.md`.
Labels used below:
- **[bug]**, **[feature]**, **[test]** for the kind of task;
- size: **S** under a day, **M** a few days, **L** a week or more;
- **(upstream)** for work that belongs in YYToolkit/Aurie rather than this repo.

## Architecture (proposed)

| Layer | BepInEx equivalent | Lives in | State |
|---|---|---|---|
| `version.dll` bootstrap proxy | Doorstop | this repo, `modloader/src/bootstrap.c` | only parks the entry point and loads AurieCore, plus the fixes AurieCore needs (Wine early-launch, cwd, console) |
| LooseMods | (game-specific patchers) | this repo, `modloader/loosemods` | file overrides, loose GML, `redirects.txt`; file hooks only for DELTARUNE (fixed RVAs) |
| Aurie | (preloader) | upstream | works under Wine with our loader fix; see Aurie #23 |
| YYToolkit | runtime glue / Il2CppInterop core | fork `deltarune`, PRs upstream | runner detection, built-ins, `ExecuteCodeObject` (PR #91) |
| script registry + hooks by name | Harmony + interop | `modloader/yyc_interop` (`script_hooks.hpp`) | observe/redirect by name on VM and YYC, verified on both; full detours on YYC only |
| plugin host: config, logging, load order, dependencies | BepInEx chainloader | `modloader/modhost` (ModHost) | manifests, runner/game filters, dependencies, config path, shared log; verified on VM and YYC |
| tool bridge (pipe/TCP relay) | (UnityExplorer, debuggers) | `editor/runtime` (RunnerEditor) | read-only telemetry and inspector; verified on VM and YYC |
| loose GML at runtime | (ScriptEngine) | `compiler/` + `ExecuteCodeObject` | bytecode runs on both runners; operand linking missing |

What belongs upstream: only generic, runner-agnostic primitives (execute code, runner type, script
lookup/hook API) are offered to YYToolkit as PRs. The plugin host and tool bridge stay here, the way
BepInEx sits on top of its runtime libraries.

## Done (2026-11-07)

- YYC symbol dump, runtime dump, interop header, bytecode-on-YYC probe (`docs/yyc_il2cpp.md`).
- YYToolkit:
  - `FWCodeEvent` argument order (PR #88);
  - frame/resize events (PR #89);
  - `ExecuteCodeObject` + `IsYYCRunner` (PR #91). All are on the fork's `deltarune` branch; #89 was
    already there.
- RunnerEditor reports `runnerType` (`vm`/`yyc`, from `code_is_compiled()`). The client validates it
  and the UI shows it. Verified live on Dwarf Eats Mountain (YYC) under Wine, the first live run of
  RunnerEditor on a YYC game: snapshot, globals, instances and instance fields all read.

## Done (2026-11-08)

- **VM script hooks** (`modloader/yyc_interop/include/vm_hooks.hpp`). Script-to-script calls on
  VM runners bypass `Code_Execute` and go through one routine, `VM_PushFrame`. The routine is
  found at run time by the trace string it alone references; one hook on it gives observers and
  redirects by name. Verified on the DELTARUNE launcher under Proton. RE notes:
  `RE/10_vm_and_scripts.md`.
- **One API for both runners** (`script_hooks.hpp`): `gm::Init`, `gm::Observe` and
  `gm::Redirect`. One DLL (`ScriptHookDemo`) ran on DELTARUNE (VM) and on Dwarf Eats Mountain
  (YYC).
- **ModHost** (`modloader/modhost`):
  - reads `mods/plugins/*/plugin.ini` (id, version, dll, runner, game, depends, enabled);
  - resolves the load order deterministically and logs every skip with its reason;
  - gives plugins their folder, config path, a shared log and the runner type;
  - the manifest rules have native unit tests.

  Verified on both games with a seven-plugin test layout.

## Done (2026-11-08, later)

- **Thin proxy.** `version.dll` was cut down to a Doorstop-style bootstrap of about 250 lines
  (`modloader/src/bootstrap.c`, was 794 lines). Every game-facing feature moved into the Aurie
  module LooseMods.
- **Less duplicated code:**
  - LooseMods hooks with Aurie's `MmCreateHook`, not a second MinHook copy.
  - Event redirects and `log_scripts` reuse YYToolkit's `Code_Execute` hook through
    `EVENT_OBJECT_CALL` instead of hooking it again.
  - Script redirects use the shared script-hook layer.
  - The proxy loads only AurieCore; AurieCore loads the rest of `mods\native` itself.
- **One script-hook set per process.** `script_hooks.hpp` publishes the hooks as the Aurie
  interface `GmScriptHooks`. Later plugins attach to it instead of hooking the same routine
  again; before this, two plugins' hooks saw each other's redirects in load order.
- **New bug found and fixed.** A `/MD` module that `fopen`s its log before the game's CRT sets up
  stdio receives the game's stdout. Shared fix: `modloader/include/append_log.hpp`.
- **Task 7 fixed.** On other games, LooseMods logs "does not match this exe ...: file overrides
  and loose GML are off" instead of "version check FAILED".

## Framework tasks

1. **[feature] M: after-call hooks on VM.** `VM_PushFrame` only sees the call start. Seeing the
   end needs the interpreter's `ret`/frame-pop path, which is not yet located; the interpreter
   loop is `0x1401D0160` in DELTARUNE. With it, `gm::` could offer post-observers and result
   replacement on VM, as `yyc::Hook` already does on YYC.
2. **[feature] M: native replacement of VM scripts.** On VM, run a C++ function instead of a
   script, the counterpart of `yyc::Hook`. Possible routes:
   - a stub `CScript` whose code is a one-instruction bytecode thunk calling a registered
     built-in;
   - a redirect to a script compiled at run time (task 4).

   `CallScript` on VM, through `script_execute`, belongs here too.
3. **[feature] S: ModHost follow-ups.**
   - plugins that must load in Aurie's early-launch phase (today they stay in `mods/aurie`);
   - `MdUnmapImage` on request, for the editor;
   - a per-plugin log file option;
   - listing loaded plugins over the RunnerEditor pipe.
4. **[feature] L: runtime linking of compiled GML.** Turn Underanalyzer output into a runnable
   `CCode`: rewrite variable, built-in and string operands to runtime ids, and route script calls
   through `script_execute`. Shared by VM and YYC (`compiler/RUNTIME_COMPILER.md`,
   `modloader/yyc_vm_probe`).
5. **[feature] M: generic loose-asset overrides.** Find `ReadEntireFile` by signature instead of
   by RVA, so the data.win redirect works in other VM games. YYC data.win still holds sprites,
   sounds and strings, so asset mods would also work on YYC games.
6. **[feature] S: version and feature checks.** Plugins built against new YYToolkit methods must
   not call them on an older YYToolkit. Bump `YYTK_MINOR` in the fork (and suggest it upstream),
   and gate in `yyc_interop`.

## Loader / this repo

7. ~~**[bug] S: misleading log on other games.**~~ Fixed with the thin proxy (see Done).
8. **[test] S: CI.** Build the YYC plugins and run the editor's native tests under Wine in CI;
   release CI is still Windows-only (`RE/TODO.md`).
9. **[feature] S: a release flavour without the DELTARUNE parts.** Loader, Aurie, YYToolkit and
   RunnerEditor only, installable into any GameMaker game.
10. **[test] M: second YYC game, second runner version.** Every YYC result so far comes from one
    game.
11. **[bug] S: version checks in `yyc_vm_probe`.** It hard-codes `CCode` offsets from 2024.14;
    check the runner version before use, or derive the offsets from `Code_Execute`.
12. Open items carried over from `RE/TODO.md`: CheatMenu battle test, streamed `.ogg` audio
    overrides, sprite size-change behaviour, DialogueBacklog in-game test.

## Runner Studio editor

13. **[feature] M: Tier B scalar writes** (`LIVE_INSPECTOR_PLAN.md`). The design is done:
    - a separate write capability;
    - expected-value checks;
    - writes and readback on the runner thread;
    - conditional undo.

    YYToolkit `GetInstanceMember`/`SetBuiltin` and GML `variable_*_set` are the routes; verify
    them on VM and YYC separately.
14. **[test] S: chapter-transition reconnection on DELTARUNE** (unverified,
    `editor/runtime/README.md`).
15. **[feature] M: a script panel.** List compiled scripts and events with their runtime index
    (from the registry), call counts from an opt-in counting hook, and on YYC the native address.
    This is the editor's view of the hook layer. `gm::Observe("")` gives the VM call counts now;
    on YYC the counts need opt-in per-script observers.
16. **[feature] L: live code.** Once task 4 works, "Run GML" and "replace script" behind their own
    capability, VM and YYC tested separately.
17. **[feature] M: arrays/structs, watches, freezes** (inspector tiers C/D).
18. **[bug] S: unload safety.** The pipe workers are pinned until exit because YYToolkit's callback
    vector has no dispatch/removal synchronization (see YYToolkit task 21). Keep "disconnect only"
    until that is fixed.
19. **[feature] L: GMEdit integration** (roadmap item 5 in `ARCHITECTURE.md`).

## YYToolkit / Aurie (upstream)

20. **[feature] S: review state.** PRs #85–#89 and #91, plus issue #90, are waiting for review.
    Upstream has not merged anything since February. Keep the fork `deltarune` branch as the
    integration branch.
21. **[bug] M: callback dispatch vs `RemoveCallback`/unload race.** `YkDispatchCallbacks`
    (`MI.hpp`) range-iterates `m_RegisteredCallbacks` on the game thread. `YkRemoveCallbackFromList`
    (`std::erase_if`) and `YkAddToCallbackList` (`emplace_back` + `sort`) modify it with no lock,
    as confirmed in the `experimental` source. Removing or adding a callback from another thread,
    or from inside a callback, invalidates the iterator. A lock plus deferred removal during
    dispatch would fix it.
22. **[bug] S: `EnumInstanceMembers` returns `AURIE_OBJECT_NOT_FOUND` after a full enumeration.**
    Probe log: `members=2565`, status `OBJECT_NOT_FOUND`. Callers cannot tell "empty" from
    "finished". Document it, or return success when the callback never asked to stop.
23. **[bug] Aurie #23.** Modules that call Aurie from CRT init fail to load under Wine. Workaround
    in our plugins: no static `RValue`.
24. **[feature] M: public script lookup/hook API** (`script_hooks.hpp` is the reference) and a public
    built-in-by-index enumeration. `YYCRuntimeDump` currently scans `.rdata` strings because there
    is no index API; YYToolkit already has `YkExtractFunctionEntry` internally.
25. **[feature] S: `YYTK_MINOR` bump plus a feature query**, so plugins can detect new methods
    (framework task 6).

26. **[bug] S: `screen_save` ends DELTARUNE under Proton.** `screen_save`, called through
    `CallBuiltin` from an `EVENT_OBJECT_CALL` callback, writes the PNG and then the DELTARUNE
    process ends. Nothing is logged after the call. The same call works on Dwarf Eats Mountain
    under Wine. The cause is unknown.

## What goes upstream (proposed policy)

Decided 2026-11-08: ModHost lives in this repo.

Send upstream (YYToolkit/Aurie) only what is:
- generic, i.e. useful to any YYToolkit user and not tied to DELTARUNE or to our loader;
- small;
- verified in a game.

How:
- **Bug fixes:** PR directly, with the evidence (done for #85–#89).
- **New API:** open an issue first, then the PR (done for #90 → #91).
- **Pacing:** do not open more PRs while #85–#91 have had no response. Keep stacking them on the
  fork's `deltarune` branch instead, which this repo already pins.

| Work | Where | Why |
|---|---|---|
| Callback dispatch race (task 21) | upstream PR, once the open ones get a response | bug in YYToolkit itself |
| `EnumInstanceMembers` status (task 22) | upstream issue first | behaviour change; maintainer's call |
| `YYTK_MINOR` bump + feature query (task 25) | upstream PR together with #91 | only matters once new methods exist |
| Script lookup/hook API (task 24) | upstream issue with `script_hooks.hpp` as the reference; PR only if wanted | large, opinionated API |
| Aurie #23 fix | upstream PR if the maintainer picks an approach | design choice in Aurie |
| ModHost, RunnerEditor, loader, interop headers, DELTARUNE parts | this repo | framework layer on top of YYToolkit, like BepInEx |
| `yyc_vm_probe`, dumpers | this repo | research tools |
