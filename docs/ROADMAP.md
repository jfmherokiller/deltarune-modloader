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
| loose GML at runtime | (ScriptEngine) | `compiler/` + `ExecuteCodeObject` | compiled snippets are linked and run on both runners (`gmlo_link.hpp`); one entry point and script replacement missing |

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

## Done (2026-11-14)

- **Runtime GML on VM and YYC (tasks 4 and 41).** One API, `gmlrt::Runtime::CompileAndRun(source,
  self, result, err)` in `modloader/include/gml_runtime.hpp`, compiles GML in the game
  (`RuntimeExports.CompileObject`, the same Underanalyzer code as `GmlObjectCompiler`), links it
  against the live runner with the shared `gmlo::Linker` (`gmlo_link.hpp`; it detects YYC and
  lowers script calls there) and runs it through `Code_Execute`. Compile and link results are
  cached by source. Smoke test: `compiler/fixtures/runtime_gml/gmlrt_smoke.gml` (locals, loop,
  built-in calls, a global, an instance variable on the running instance, `room_speed`, a new
  string, `show_debug_message`) ran 1000 times through RunnerCompiler link mode on the DELTARUNE
  launcher (VM, Proton) and on Dwarf Eats Mountain (YYC, Wine 11). All 5 checks passed on both,
  with no fault and no growth per run against an empty-snippet control.
  `compiler/tests/test_gml_runtime.py` checks the committed objects, linked bytes and logs.
  Architecture and limits: `compiler/RUNTIME_COMPILER.md` "Runtime GML API". Follow-ups: tasks
  42–46.

## Done (2026-11-13)

- **CheatMenu Light World page (task 29).** F7 → **Light World...** edits `global.lhp`
  (1..`lmaxhp`, no overheal), `lmaxhp` (1..999), `lat` and `ldf` (0..999), `llv` (1..20) and `lxp`
  (0..99999). It lives in `scr_drcheat_page_lightworld`, and the side effects are in the page
  header and in RESEARCH.md: nothing levels up in the Light World, and `scr_become_light`
  recalculates `lhp` when Kris returns from the Dark World. Verified in Ch4 under Proton:
  - Light World (a temporary save with `darkzone` 0 in Toriel's house): the page showed the STAT
    screen's values (LV 1, HP 20/20, AT 10, DF 10, EXP 0). HP stopped at Max HP, and lowering Max
    HP to 10 took HP with it. After the edits, the game's STAT screen showed LV 5, HP 10/30, AT 12,
    DF 7, EXP 30 and NEXT 170.
  - Dark World save: the page opens and edits without errors and shows the HP note.
  - Ch1–5 compile with LooseModCompiler. The other chapters weren't run in-game.

- **CheatMenu Money page (task 27).** F7 → **Money...** replaces the Gold row. The rows come
  from the `drc_money` table (now in `scr_drcheat_page_money`): D$ (`global.gold`), Light World $
  (`global.lgold`), PT (`flag[1044]`, Ch3–4, 0..9999), Board points (`flag[1117]`, Ch3), F$
  (`flag[1411]`, Ch5) and P$ (`flag[1312]`, Ch5). Rows from other chapters are hidden. The page
  never writes the Ch5 F$ reveal state `flag[1412]`. Verified in-game under Proton, with a
  test-only harness that logged every `global.flag` change:
  - Ch3: PT and board points change by the table's steps. PT stops at 9999, board points at 0 and
    D$ at 99999. The TV Show page shows the same PT. The game's own completion save
    (`scr_complete_save_file`) wrote the edited PT into `filech3_3`, the file Ch4's
    `scr_load_chapter3` copies `global.flag` from.
  - Ch4: PT is shown and editable, and the board row is hidden.
  - Ch5: F$ and P$ edits show up at once in the dark-menu money box. A save with D$ 107598
    (above the cap) keeps its value on →, and ← lowers it normally. `flag[1412]` stayed 2.
  - Ch2: only D$ and Light World $ are shown, and both work.
  - The only flags that changed after each session were the edited currency flags. Nothing
    crashed and no GML errors appeared.
  - Not covered: spending PT at Gerson's table in Ch4, and the `flag[1412] == 1` note, which
    needs a save from before the F$ reveal.
- **RunnerCompiler live run (task 41, first half).** The spike plugin can now capture named code
  entries: `capture.txt` holds name prefixes, and the plugin walks the runner's CCode list on the
  first object event. Seven fixture snippets cover globals, instance variables, locals,
  built-ins, script calls, strings and arrays. They were compiled into the launcher data.win as a
  loose mod and captured live under Proton. The game did not crash. The pre-fixup and runtime
  pairs, word-by-word listings and logs are in `compiler/fixtures/runtime_bytecode/`. Only operand
  words of `pop`/`push`/`call`/`pushloc`/`pushglb`/`pushbltn` differ. Reproduce with
  `compiler/RUNTIME_COMPILER.md` ("Live run and fixtures").

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
4. ~~**[feature] L: runtime linking of compiled GML.**~~ Done (see Done 2026-11-14):
   `gmlrt::Runtime` (`modloader/include/gml_runtime.hpp`) compiles, links and runs GML source on
   VM and YYC runners. Design, per-runner details and limits: `compiler/RUNTIME_COMPILER.md`
   ("Runtime GML API", "YYC: running linked bytecode", "VM: running linked bytecode").
   Follow-ups: 42–46.
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
12. Replaced by the specific tasks 33–40 below (2026-11-08).

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

## Mods and tools (2026-11-08 scan)

The CheatMenu items come from `mods_examples/CheatMenu/RESEARCH.md`, which has the storage
locations and code-entry evidence for each one.

27. ~~**[feature] M: CheatMenu "Money..." page.**~~ Done and verified in-game in Chapters 2–5
    (see Done, 2026-11-13).
28. **[feature] S: CheatMenu walk/run speed.** Multiply `obj_mainchara.bwspeed`; the game derives
    `wspeed` from it every step. Restore it on toggle-off and on room change. Platformer and
    climbing sections use other objects; check them separately.
29. ~~**[feature] S: CheatMenu Light World page.**~~ Done and verified in-game in Chapter 4
    (see Done, 2026-11-13).
30. **[feature] M: CheatMenu instant battle win.** Call `scr_wincombat`, which exists in every
    chapter. Skip the enemies in `scr_drcheat_nospare` (scripted fights). Check the rewards and flags
    the win screen sets.
31. **[feature] M: CheatMenu flag viewer/editor.** Show `global.flag[n]` and `global.plot`, with a
    confirm step before writes. The game's flag-name table is stripped from release builds, so ship
    our own list of known flags, starting with the ones in RESEARCH.md.
32. **[research] S: `global.debug`, encounters, room warp.**
    - `scr_debug()` gates 136 Ch5 code entries, while `scr_debug_keycheck` is an empty stub in
      release builds; find out what the remaining branches do.
    - Check whether `global.encounterno` gives a usable encounter toggle.
    - Room warp (`room_goto`) is risky for story state; decide whether it is worth offering.
33. ~~**[test] S: CheatMenu inside a battle**~~ Done: confirmed in-game by the owner (2026-11-14),
    including the freeze during the bullet phase.
34. ~~**[test] S: DialogueBacklog in-game test**~~ Done: confirmed in-game by the owner (2026-11-14):
    open, scroll and close in overworld and battle.
35. ~~**[feature] S: cache the loose-GML compile.**~~ Done (2026-11-14): `mods\cache\loosegml\`,
    keyed on mod file contents, mod order, vanilla `data.win`, compiler and LooseMods build
    (`modloader/loosemods/source/GmlCache.hpp`). Ch5 with three mods: 2.8–4.9 s compile → ~0.1 s.
36. **[feature] M: streamed `.ogg` overrides.** Music beside `data.win` and in `mus\` is opened by
    the audio engine, not `ReadEntireFile`.
37. **[feature] S: sprite size-change behaviour.** Decide the resize/origin rules for frames whose
    size differs from the original.
38. **[test] S: Windows builds and the release workflow.** Not run since the thin-proxy change:
    `modloader/build.bat`, `loosemods/build.bat`, `modhost/build.bat`, `yyc_interop/build.bat`,
    `.github/workflows/release.yml`.
39. **[feature] S: `log_scripts` for scripts too.** LooseMods logs only what passes through
    `Code_Execute` (events, global scripts). `gm::Observe("")` would add script calls on VM.
40. **[feature] M: Cheat Engine dissector follow-ups.** Symbol lookup callback (`global.gold`) and
    array element expansion (`RE/TODO.md`).
41. ~~**[test] S: RunnerCompiler spike live run.**~~ Done: live capture (Done 2026-11-13,
    `compiler/fixtures/runtime_bytecode/`), the load-time operand fixup derived from it
    (`compiler/RUNTIME_COMPILER.md` "Load-time operand fixup"), and the end-to-end smoke test on
    both runner kinds (Done 2026-11-14, `compiler/fixtures/runtime_gml/`).

## Runtime GML follow-ups (from tasks 4 and 41)

42. **[feature] M: script replacement with linked code.** Point an existing script's `CCode` (VM)
    or its YYC function at a linked buffer. Prerequisite for task 2 (second route) and task 16.
43. **[feature] M: function declarations in snippets.** `ObjectCompiler.Compile` rejects code that
    creates child entries (`function`, constructors, `static`). Needs child `CCode`s sharing the
    parent buffer, and `push.i` function-id relocations to them.
44. **[feature] M: compile context from the live runner.** The compiler loads the data.win on disk.
    With loose mods active the game runs a patched data.win, so names that only mods add are
    unknown to the compiler. Implement Underanalyzer's `IGameContext` over YYTK lookups (asset,
    script and function names), then no data.win is needed at all (RUNTIME_COMPILER.md piece 2).
45. **[test] S: YYC script values.** Only `call.i` of a script is lowered. A script used as a value
    (`push.i` of its id, `method()`, `callv`) is linked unchanged and untested on YYC.
46. **[feature] S: a GmlRuntime plugin with a public interface.** Today the API is a header used by
    one plugin (RunnerCompiler). Publish one `gmlrt::Runtime` per process as an Aurie interface
    (like `GmScriptHooks`), so mods and the editor (task 16) share the CLR, the cache and the
    string table instead of each hosting their own.
47. **[feature] M: per-mod config and data folders, BepInEx-style.** The DialogueBacklog transcript
    is written to the game's save folder, which under Proton is deep inside the Wine prefix; the owner
    found that the one real problem (2026-11-14). Generalise it so any mod gets local storage:
    - Layout, like BepInEx's central `BepInEx/config/` (so updating or deleting a mod's folder never
      wipes user settings): `mods/config/<ModId>.ini` for settings and `mods/data/<ModId>/` for
      everything else a mod saves (`logs/`, transcripts, caches). The loader creates them on demand.
    - Native GML functions registered by the loader, so GML mods can use them on VM and YYC games,
      e.g. `modloader_config_path(mod_id)` and `modloader_data_dir(mod_id)`, plus write helpers if
      GameMaker's file sandbox blocks paths outside the save folder. Built-ins are added with the
      runner's `Function_Add` (RE/04_builtin_functions.md); the runtime-GML linker already resolves
      built-ins by name, so code compiled at run time can call them too.
    - ModHost's native plugins get the same paths (`GetPluginInfo().configPath` moves to
      `mods/config/<id>.ini`; add a data dir), so GML mods and native plugins share one convention.
    - First user: DialogueBacklog (transcripts to `mods/data/DialogueBacklog/logs/`, settings to
      `mods/config/DialogueBacklog.ini`, migrating the old files from the save folder once).
    Cards: research t_36d7f648 -> implementation t_bd9dfca1 -> DialogueBacklog port t_33d3cfc6
    (all parked; start manually).
48. **[idea] M (optional, later): DialogueBacklog character portraits** next to speaker names.

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
