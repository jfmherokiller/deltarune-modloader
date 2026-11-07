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
| `version.dll` loader | Doorstop | this repo, `modloader/` | works; loose-mod file overrides only for DELTARUNE (fixed RVAs) |
| Aurie | (preloader) | upstream | works under Wine with our loader fix; see Aurie #23 |
| YYToolkit | runtime glue / Il2CppInterop core | fork `deltarune`, PRs upstream | runner detection, built-ins, `ExecuteCodeObject` (PR #91) |
| script registry + hooks by name | Harmony + interop | `modloader/yyc_interop` today; generic part to YYToolkit | YYC verified; VM backend missing |
| plugin host: config, logging, load order, dependencies | BepInEx chainloader | new, this repo | missing (Aurie loads every DLL in `mods/aurie`) |
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

## Framework tasks

1. **[feature] M: VM backend for name-based script hooks.** On VM runners every script runs through
   `VM::Exec(CCode*, ...)`. One hook on it, filtered by `CCode` name, gives pre/post hooks per
   script; `yyc_interop`'s registry API stays the same.
   - Locate `VM::Exec` from `Code_Execute`'s tail call (shown on the YYC runner:
     `0x140EF1C30`). Then verify that nested script-to-script calls reach it on a VM game.
   - Needs a live DELTARUNE run.
2. **[feature] M: one hook API for both runners.** `Hook("name", pre, post)` with a native-detour
   backend on YYC and a `VM::Exec` filter backend on VM, plus `CallScript` on VM through
   `script_execute`. Then propose the API to YYToolkit as `CreateScriptHook`/`CallScript`
   (upstream).
3. **[feature] M: plugin host.** A loader-side manifest per plugin covering name, version,
   dependencies and target game/runner. It also provides a config file per plugin, a log per
   plugin (instead of each DLL opening its own file) and a deterministic load order. Plugins with
   unmet dependencies or the wrong runner type are skipped with a log line.
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

7. **[bug] S: misleading log on other games.** `modloader.log` on Dwarf Eats Mountain says
   "version check FAILED ... unknown DELTARUNE.exe build". That is correct but alarming; it should
   say "not DELTARUNE, game-specific hooks off".
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
    This is the editor's view of the hook layer.
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
24. **[feature] M: public script lookup/hook API** (framework task 2) and a public
    built-in-by-index enumeration. `YYCRuntimeDump` currently scans `.rdata` strings because there
    is no index API; YYToolkit already has `YkExtractFunctionEntry` internally.
25. **[feature] S: `YYTK_MINOR` bump plus a feature query**, so plugins can detect new methods
    (framework task 6).

## Needs a decision from the owner

- Permission to run DELTARUNE under Wine/Proton for the VM-backend tests (tasks 1, 2, 14).
- Whether the plugin host is a new component here (recommended) or goes into YYToolkit.
- Which tasks go upstream as PRs and which stay on the fork.
