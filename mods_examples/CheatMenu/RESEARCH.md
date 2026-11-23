# CheatMenu: cheat research

This file lists where the game stores things worth cheating, and the cheats that could be added next. The evidence comes from the decompiled code of each chapter (`utmtcli dump chapterN_windows/data.win -c UMT_DUMP_ALL`), kept locally under `RE/code/chapterN/`, which is gitignored. Code entry names are given so each finding can be rechecked after a game update.

The tasks themselves are in `docs/ROADMAP.md` under "CheatMenu".

Contents: [ROADMAP 32 report](#debug-branches-encounters-and-room-warp-roadmap-32) (debug branches, encounter toggle, room warp, recommended features, open questions), then currencies and other cheat candidates.

## Debug branches, encounters and room warp (ROADMAP 32)

This part is the consolidated report for ROADMAP task 32. It condenses three longer notes, which
stay in the repo as appendices because they hold the full per-site catalogs and room tables:

- Appendix A: [`research/debug_branches.md`](research/debug_branches.md), every `scr_debug()` call site (1091), grouped and rated.
- Appendix B: [`research/encounters.md`](research/encounters.md), every `encounterno` writer and reader, and the encounter paths per chapter.
- Appendix C: [`research/room_warp.md`](research/room_warp.md), room-change mechanics, failure modes and save-point whitelists per chapter.

Code references are `obj_<name>:<Event>@<line>` or `gml_GlobalScript_<name>.gml:<line>` in the
local decompile `RE/code/chapterN/`. Line numbers belong to that dump and move when the game
updates; the object and script names do not. Nothing here has been run in the game yet; every
claim comes from the code and from `data.win` structure.

### Verdicts

| Question | Verdict |
|---|---|
| What do the `scr_debug()` branches do in release builds? | **Nothing.** `global.debug` is set to `0` once (`obj_initializer2:Create_0@6`, Ch4 `@7`) and never written again, and Ch2's `scr_debug()` is a hard-coded `return 0`. The only release-live code in this family is 28 inverse guards (`!scr_debug()`) and a few bare `scr_debug` function references. Turning debug on is **not** a usable cheat: it does nothing in Ch2, probably crashes Ch3 board rooms, and rewrites party/plot/flags on entry to about 140 rooms in Ch3–5. A few branches are worth re-implementing as direct calls. |
| Does `global.encounterno` give an encounter toggle? | **No.** It only selects *which* battle `scr_encountersetup()` builds and is written just before every battle starts. A **partial** toggle is possible another way: cancel the shared contact transition `obj_testoverworldenemy` from a Begin Step hook in its ~20-frame window. That covers roaming chasers only. |
| Is room warp safe enough to offer? | **Conditional.** Not as arbitrary `room_goto`. Only as "warp to a save point": a curated whitelist of save-point rooms that are in `scr_get_room_list()`, in the current world, already reached with free control on this save, entered load-style (`interact = 0`, `entrance = 0`) after `drc_close()`. |

### 1. `scr_debug()` branches in release builds

Full catalog: [Appendix A](research/debug_branches.md).

**Definitions** (`gml_GlobalScript_scr_debug.gml`):

| Ch | `scr_debug` body | Release value | `scr_debug_keycheck` |
|---|---|---|---|
| 1 | `return global.debug;` | `0` | not present |
| 2 | `return 0;` (hard-coded) | `0`, even with `global.debug = 1` | always false |
| 3 | `if (global.debug == 1) return 1;` | `undefined` (falsy) | always false |
| 4 | same as Ch3 | `undefined` | empty → `undefined` |
| 5 | `return global.debug == 1;` | `false` | empty → `undefined` |

There is no launch parameter, INI key or `variable_global_set("debug")`;
`scr_init_launch_parameters` does not touch it. Ch4 reads `global.debug` directly in
`obj_solid_floor:Step_0@17` and `obj_dw_church_window_parallax:Step_0@1`; both are dead too.

**Call sites** (the roadmap's "136" is Ch5's 135 entries plus the definition):

| Ch | Call sites | Code entries | Inverse guards | Payload stripped | Live with debug on |
|---|---|---|---|---|---|
| 1 | 15 | 9 | 2 | 5 | 8 |
| 2 | 96 | 80 | 1 | 31 | 64 |
| 3 | 409 | 345 | 3 | 47 | 359 |
| 4 | 415 | 344 | 18 | 72 | 325 |
| 5 | 156 | 135 | 4 | 20 | 132 |
| **total** | **1091** | **913** | **28** | **175** | **888** |

**What runs in release.**

- The 28 inverse guards. They destroy dev-only objects (`obj_snd_maker:Step_0`,
  `obj_andydebug:Create_0`), exit dev key handlers (Ch4 `obj_rotating_tower_controller:KeyPress_49…55`),
  and make the Ch1 castle text unskippable (`obj_darkcastle_event:Step_0@39,67`). Debug on *removes*
  these behaviours.
- Bare `scr_debug` references (a function value, always truthy): Ch3 `obj_b1store:Draw_0@1`,
  `obj_b2bridgepuzzle2:Draw_0@1` (no-op name labels), Ch3 `obj_npc_PAspeaker:Step_0@1` (hold W +
  A/D toggles the speaker sprite; cosmetic), and `scr_turn_skip` / the Ch4 Justice enemies (blocked
  again by the stub keycheck).

**Stubbed helpers** stay dead even with debug on: `scr_debug_keycheck` (all battle F-keys in
`obj_battlecontroller:Step_0`, V turn skip), `scr_84_debug` (the dev menu), `scr_debug_fullheal`,
`scr_debug_premade_save`, `obj_debugProfiler`, `obj_debug_xy`, the `obj_time:Step_1@21` quicksave
(`quicksaved == 22/99`, never set). Ch2's `scr_turn_skip` and `scr_weaken_enemies` are empty.

**Why `global.debug = 1` is not a cheat** (details in Appendix A, "Dangerous branches"):

| Effect | Where |
|---|---|
| No effect at all | Ch2 (`scr_debug` returns 0; needs a code-entry override) |
| Crash: unassigned `global.chemg_show_room` / `chemg_show_val` | Ch3 board controllers `Draw_0` → `scr_board_objname` (137 sites), Ch3 `showval`, `obj_rhythmgame:Draw_0@568`; Ch5 `obj_dw_fcastle_yellow_miniboss:Draw_0@1` calls the undefined `scr_roommenu_debug` |
| Party forced on room entry (SETPARTY ×84) | Ch3–5 room controllers' `Create_0`, e.g. Ch5 `obj_ch5_LW02W:Create_0@3` drops followers |
| Plot/flags/items written on entry (STARTSTATE ×53) | e.g. Ch5 `obj_dw_garden_susiechase:Create_0@20`, `obj_ch5_LWF01:Create_0@92` (removes a light item), Ch4 `obj_ch4_DCA08C:Create_0@1` |
| `global.plot = 0` on held key | Ch4 `obj_dw_church_worshiproom:Create_0@98`, `obj_dw_churchb_rotatingtower:Create_0@12`, `obj_dw_churchb_staircaseintro:Create_0@40`; Home+2 in `obj_mainchara:Step_0` |
| Unprompted single-letter hotkeys | S save anywhere, L load, R restart, M mute, D dialogue box, V/B toggles (`obj_overworldc:Step_0`, `obj_darkcontroller:Step_0`) |
| Softlock | Ch5 `obj_dbulletcontroller:Step_0@1415,1460` (`turntimer = 9999999`), Ch2 `obj_ch2_sceneex2_special:Step_0@3` (`interact = 1`), cutscene `con` jumps (SKIPKEY ×49) |
| Boot change | `obj_initializer2:Step_0@41` `menu_go = 3` if debug is on before it runs |

**Branches worth re-implementing as direct calls** (not by flipping the global):

| Feature | Dev branch | Direct implementation |
|---|---|---|
| Battle win / weaken / heal party / TP | `obj_battlecontroller:Step_0` F-keys (Ch2@1187, Ch3@1530, Ch4@1371, Ch5@1420; behind the stub keycheck) | `scr_wincombat()` (1–5), `scr_weaken_enemies()` (3–5), `scr_raise_party()` / `scr_weaken_party()` (2–5), `global.tension` |
| Noclip | `obj_mainchara:Step_0` Ctrl+Shift+N (Ch5@20; collision skip `@369` is `scr_debug() && noclip`) | needs its own collision bypass; setting `noclip` alone does nothing |
| Unstuck | `obj_mainchara:Step_0` Ctrl+Shift+0 | `global.interact = 0` |
| Game speed | `obj_time:Step_1` `` ` `` (30 ↔ 150) | `room_speed` |
| Hold-C text automash | `obj_writer:Draw_0` (`global.flag[10] == 1 \|\| scr_debug()`) | `global.flag[10] = 1`, no debug needed |
| Hitbox view | `obj_overworld_bulletarea*:Create_0/Step_0`, `obj_bulletdeleter:Draw_0` | `showArea` / `image_alpha`, `draw_self` |
| Rhythm-game win | `obj_rhythmgame:Step_0` F5 | set `points`/`fame` to max and `_skip = true` |
| Dog-check bypass | `scr_dogcheck` returns 0 under debug | only matters for warps outside the whitelist; the recommended warp design does not need it |

### 2. Encounter toggle

Full analysis: [Appendix B](research/encounters.md).

**`encounterno` is a selector, not a gate.** Every writer sets it immediately before a battle
starts, so a value set by the cheat is overwritten:

| Writer | Where |
|---|---|
| Chaser contact | Ch1 `obj_chaseenemy:Other_10@15`; Ch2–5 `obj_overworldenemy_parent:Other_10@52` |
| `scr_battle(id, …)` | `gml_GlobalScript_scr_battle.gml:4` (Ch2–5), used by 15/22/20/14 objects in Ch2/3/4/5 |
| Multi-wave battles | `scr_wincombat.gml:34` (`encounterno = flag[60]`) |
| Ch5 platforming / projectiles | `scr_plat_encounter.gml:90,125`, `obj_encounterable_projectile:Other_10@54` |
| Cutscenes, dojo/fusion menu | e.g. Ch1 `obj_kingcutscene:Step_0@905`, `obj_fusionmenu:Step_0@616..631` |
| Ch3 reset to 0 | `obj_chaseenemy:Create_0@101`, on every chaser create |

Every reader (`scr_monstersetup`, `obj_battlecontroller`, `obj_testoverworldenemy:Other_10` music,
per-enemy checks) branches on *which* battle runs; none decides *whether* one happens. There are
no step-counter random encounters. `global.interact` is the only shared gate (contact counts at 0
or 4), and holding it elsewhere freezes Kris too.

**The chokepoint.** All battles end in `obj_encounterbasic`, which only two paths create:

1. `obj_testoverworldenemy:Step_0` at `con == 5` (Ch1@67, Ch2–4@82, Ch5@88): the shared overworld
   contact path. After contact (`obj_chaseenemy:Step_2`, or the Ch1 Collision event) the chaser
   destroys itself and hands off; the transition object waits `alarm[4] = 8` then `12`, so there
   are **about 20 frames** before the battle is created.
2. `scr_battle()` (`scr_battle.gml:18`): scripted and room-specific battles. These are shared with
   story bosses, so a hook there cannot tell optional from mandatory fights.

**Toggle design ("roaming enemies only").** In the CheatMenu's existing `obj_time:Step_1` (Begin
Step) append, destroy any `obj_testoverworldenemy` with `1 <= con < 5`, restoring `flag[54] = 0`,
`flag[9]`, freeing `global.batmusic[0]`, undoing the Ch1 music fade and setting `interact = 0`.
Exempt it when the previous frame's `interact` was not 0/4 (cutscene-forced), when `washit != 0`,
and for the Ch5 `obj_dw_garden_enemyrush` / `obj_dw_garden_floradinnencounter`. The code sketch is
in Appendix B §4(a). Nothing permanent is written (`encounterflag` is untouched), so the enemy
returns on the next room load.

| Ch | Covered by the toggle | Not covered (calls `scr_battle` or own spawn) | Exemptions needed |
|---|---|---|---|
| 1 | all chasers | cutscenes create `obj_encounterbasic` directly | Clover fight (`obj_cloverpuzzle_event:Step_0@108-119`); `touchflag` already set on contact |
| 2 | roaming chasers | 15 objects (scripted, Berdly, dojo) | none known |
| 3 | roaming chasers | 22 objects, including all TV-board chasers (`obj_chaseenemy_board:Step_0@227-245`) | none known |
| 4 | few: most church encounters have their own chaser | 20 objects (`obj_dw_church_organikchaser`, `obj_dw_churchb_bellchaser`, …) | none known |
| 5 | roaming chasers, `scr_plat_encounter`, projectiles | scarecrow, kawkaw, shinobeetle | enemy rush, Floradinn, encounter 230 (`obj_dw_post_fountain_close:Step_0@446-461`) |

Side effects: skipped enemies give no recruit progress (nothing becomes LOST; only `scr_defeatrun`
sets `flag[600+type] = -1`), no D$, and no violence/spare tallies (`flag[40..45]`).

**Instant win** reuses the dev F5 path, `scr_wincombat()` (Ch5 `obj_battlecontroller:Step_0@1435`):
enemies are recorded as spared (`flag[51+i] = 2`), the encounter flag is written so chasers do not
respawn, no recruit progress, and `flag[60]` multi-wave battles need one call per wave. Allow it
only while `global.myfight == 0`, `global.specialbattle == 0` and no living enemy is in
`scr_drcheat_nospare()`.

**Forcing an encounter** (`scr_battle(id, 0, noone)`) works in Ch2–5 but many ids depend on the
room, flags or scripted endings. Not recommended.

### 3. Room warp

Full analysis and whitelists: [Appendix C](research/room_warp.md).

**How a load enters a room.** `obj_mainchara:Create_0` places the player at an entrance marker
only when `global.interact == 3` (Ch5 `@196`). A save load has `interact = 0`, `entrance = 0`
(`scr_gamestart.gml:334-335`), so the player stays at the room's placed position, the load spawn
the game uses on every load. `scr_load` (Ch5 `:310-374`) then runs `scr_dogcheck()`: a room id
missing from `scr_get_room_list()` (`gml_GlobalScript_scr_get_room_by_id.gml`) loads into an
exit-less dog room forever.

| Ch | Rooms listed / total | Dog room (`scr_load` line) | Save-point whitelist |
|---|---|---|---|
| 1 | 131 / 147 | 131 `PLACE_DOG` (`:350`) | 17 |
| 2 | 277 / 278 | `choose(226, 271)` (`:342`) | 12 |
| 3 | 195 / 246 | 83 `PLACE_DOGCHECK2` (`:359`) | 4 |
| 4 | 243 / 328 | 92 `PLACE_DOGCHECK2` (`:360`) | 14 (3 LW) |
| 5 | **39 / 252** | 98 `PLACE_DOGCHECK_CH5` (`:351`) | 15 |

The dev warps (Insert/Delete/Home+N in `obj_mainchara:Step_0`, Ch5@895-954) and the per-room dev
state setup (SETPARTY, STARTSTATE, `global.tempflag[90]`) are all `scr_debug()`-gated, so a
release warp gets **no** state fix-up. `scr_roommenu` is `exit;`.

**Failure modes of a raw `room_goto`:**

| # | Failure | Code |
|---|---|---|
| F1 | No `obj_mainchara` in the room (16–31 rooms per chapter) → crash or frozen screen | `obj_overworldc:Create_0@13`, `obj_darkcontroller:Create_0@66` |
| F2 | Missing entrance marker: Ch1 crash, Ch2–5 room centre (maybe in a wall) | Ch1 `obj_mainchara:Create_0@86`; Ch5 `@339-374` |
| F3 | Followers rebuilt from the current `global.char[]`, wrong for the room | `obj_darkcontroller:Create_0@46-66` |
| F4 | Room cutscene before its plot, assuming setup that was debug-only | Ch5 `obj_dw_fcastle_post_party_jail:Create_0@11-17` |
| F5 | Plot ratchet on entry, permanent once saved | Ch5 `obj_dw_cliff_climbrefresher` (305), Ch4 `obj_dw_churchc_savepoint` (244.9), `obj_dw_churchc_prepretitan` (249), Ch2 `obj_ch2_room_city_savepoint` (75) |
| F6 | Party forced on entry | Ch4 `obj_dw_churchb_gersonstudy:Create_0@58` (`scr_setparty(1)` every entry) |
| F7 | Wrong world: `darkzone`, sprite scale, weapons/HP not converted; saved | `obj_mainchara:Create_0@32-38`, `scr_become_dark`, `scr_load.gml:45` |
| F8 | Other chapters' rooms unreachable (indices are per `data.win`) | Ch3 `scr_get_room_by_id.gml:49` |
| F9 | Save made after the warp loads into the dog room | §"How a load enters a room" |
| F10 | `room_goto` while the CheatMenu has instances deactivated drops them, including `obj_time` | `obj_drcheat:Step_0@129`, `drc_close` in `obj_drcheat:Create_0@90` |
| F11 | Ch3 board rooms have their own controller, party, camera and save object | `obj_board_controller:Create_0@4` |

**Rules for a v1 "Warp to save point" page:**

1. Targets come only from the per-chapter whitelists (Appendix C §5): save point placed,
   `obj_mainchara` placed, room in `scr_get_room_list()`. Excluded on purpose: Ch3
   `room_dw_castle_town` (unlisted → dog room), Ch3 board rooms, Ch4
   `room_dw_churchb_staircaseintro` and `room_dw_churchb_gersonstudy` (party reset), Ch2 rooms 75
   and 217 until checked in game.
2. A target unlocks only after ~30 frames of `global.interact == 0` in it during normal play,
   stored per chapter and `global.filechoice` in mod storage (`docs/mod_storage.md`) and cleared on
   `scr_gamestart`. This rules out F4–F6.
3. Same world only (`global.darkzone`); Ch4 marks its Light World rooms.
4. Only from free control, checked from the snapshot taken when the menu opened: `obj_mainchara`
   exists, no battle (`global.fighting == 0`), `interact == 0`, no `obj_dialoguer` / `obj_writer`,
   not a Ch3 board room.
5. Sequence: `drc_close()`, `interact = 0`, `entrance = 0`, `flag[21] = -10`, `facing = 0`,
   `snd_free(global.currentsong[0])`, `obj_persistentfadein`, `room_goto(asset_get_index(name))`.
   Do not touch plot, party, flags or `darkzone`. Recipe: Appendix C §4.

### Recommended CheatMenu features

**safe**: no story state can change; **conditional**: safe only with the listed guards;
**avoid**: do not ship.

| Feature | Rating | Implementation / guards | Source |
|---|---|---|---|
| TP set / infinite TP | safe (already shipped) | `global.tension` | A §"Most useful" |
| Enemy HP / mercy edits | safe (already shipped) | `global.monsterhp[i]`, `global.mercymod[i]` instead of the per-boss dev keys | A ENEMYKEY |
| Hold-C text automash | safe | `global.flag[10] = 1`; the game sets this itself on chapter-start saves (`scr_gamestart_chapter_override:44`). It is saved with the file | A FASTTEXT |
| Hitbox view | safe | `showArea` / `image_alpha` on `obj_overworld_bulletarea*`, `draw_self` for `obj_bulletdeleter`; cosmetic | A HITBOX |
| Game speed toggle | safe | `room_speed` 30 ↔ 150, restore on OFF; timers scale with it | A TIME |
| Instant battle win | `scr_wincombat` exists in every chapter (two entries in Ch4) | 1–5 | ROADMAP 30. Guards and side effects in [§2 Encounter toggle](#2-encounter-toggle): menu turn only, `specialbattle == 0`, nothing from `scr_drcheat_nospare`, one call per `flag[60]` wave |
| Weaken enemies / heal or weaken party | conditional | `scr_weaken_enemies()` Ch3–5 only, `scr_raise_party()` / `scr_weaken_party()` Ch2–5; same battle guards as instant win | A STUBKEY |
| No encounters (roaming only) | conditional | Begin Step cancel of `obj_testoverworldenemy` with cutscene/`washit`/Ch5 exemptions; label "roaming enemies only" | B §4(a) |
| Warp to save point | conditional | whitelist + visited set + same world + free control + load-style entry | C §4–5 |
| Unstuck | conditional | `global.interact = 0`; refuse while a cutscene object or `obj_dialoguer` exists, or warn | A PLAYER |
| Noclip | conditional | own collision bypass (the vanilla skip is `scr_debug() && noclip`); OFF restores; warn about walking into triggers out of order | A PLAYER |
| Rhythm-game win (Ch3–5) | conditional | replicate the F5 branch of `obj_rhythmgame:Step_0`; skip story songs if they gate plot | A RHYTHM |
| Reload last save | conditional | `scr_load()` from free control only | A OWHOTKEY |
| `global.debug = 1` toggle | avoid | crash (Ch3), party/plot rewrites (Ch3–5), no effect (Ch2), live hotkeys | A §"Is `global.debug = 1` viable?" |
| Save anywhere | avoid | `obj_savemenu` outside `scr_get_room_list()` makes the save unloadable (Ch5: 213 of 252 rooms); mid-cutscene saves softlock | A OWHOTKEY, C F9 |
| Arbitrary `room_goto`, next/previous room, Home+N warps | avoid | F1–F11; Home+2/3 reset the plot | C §3 |
| Cross-world warp | avoid (v1) | needs `scr_become_light/dark` plus per-chapter party handling | C §4 |
| Force a specific encounter | avoid | `scr_battle(id, …)` ids depend on room/flags/scripted endings | B §4 |
| Full encounter-off (hook `scr_battle` / `obj_encounterbasic`) | avoid | cannot tell story bosses from optional fights | B §2 |
| Room-local dev keys (puzzle autosolve, god modes, cutscene skips) | avoid | each is room-local and skips flags the room expects; `con` jumps softlock | A KEYMISC, SKIPKEY |

### Open questions

All of these need a run in the game or a `data.win` check; none blocks the safe items above.

1. **Instant win**: what does the win screen award after `scr_wincombat()` (D$, `flag[41]`), and
   does any non-`nospare` battle misbehave? (ROADMAP 30 is in progress.)
2. **Encounter toggle**: run the Begin Step hook per chapter. Check that the Ch1 music fade is
   undone, that `batmusic[0]` is freed without a pop, and that no listed exemption is missing
   (search for other cutscenes that poll `obj_testoverworldenemy`).
3. **Warp**: one warp per chapter followed by save and reload; Ch4 LW↔LW; Ch2 rooms 75 and 217;
   that `room_goto` right after `drc_close()` keeps the persistent `obj_time` alive; where the
   visited set lives in mod storage.
4. **Noclip**: the cheapest bypass of the `scr_debug() && noclip` check (a one-frame debug window,
   or a `.patch` on `obj_mainchara:Step_0`).
5. **Debug branches** (Appendix A open questions): confirm the Ch3 `global.chemg_show_room` crash;
   whether `scr_roommenu_debug` exists as a function asset in Ch5; the `scr_debug` asset index
   (decides whether the bare-reference sites such as `obj_npc_PAspeaker` really run); whether
   `room_battletest` / `room_bullettest` still exist; the trophy queued by
   `obj_event_manager:Create_0@290`; whether `scr_rhythmgame_load_chart_from_file` fails cleanly.
6. **Rhythm-game win**: which Ch3–5 songs are story-gated and must be excluded.

## Currencies

The **Money...** page (ROADMAP task 27, done) edits all of these; it replaced the old **Gold** row, which only edited D$.

| Currency | Shown as | Storage | Chapters | Spent at / notes |
|---|---|---|---|---|
| Dark Dollars | `D$` | `global.gold` | 1–5 | Dark World shops |
| Light World money | `$` | `global.lgold` | 1–5 (Light World parts) | Light World shops; `scr_litemuseb` gives some |
| TV points | `PT` | `global.flag[1044]` | 3, 4 | Ch3 TV show; Ch4 Gerson's table shop (`obj_gerson_table_Draw_0` spends it, `obj_gerson_fountain_Draw_0` shows it). The TV Show page edits it in Ch3 only |
| Board points | points | `global.flag[1117]` | 3 (boards), 4/5 (minigames) | Running score of the current board or minigame. `obj_round_evaluation` / `obj_minigame_evaluation` read it into `totalpoints` and reset it to 0 |
| Flowery Dollars | `F$` | `global.flag[1411]` | 5 | Café (`obj_dw_fcastle_cafe_Draw_0`), pink shop (`obj_dw_fcastle_pinkshop_Step_0`), vending (`obj_shop_vending_Draw_0`), Ferrol (`obj_dw_fcastle_pinkroom_ferrol_Step_0`, 50), the yellow cave. Earned from `obj_plat_coin`, `obj_climb_coin`, treasure rooms, flowery gardening (+50) |
| Pink Dollars | `P$` | `global.flag[1312]` | 5 | Pink shop (`obj_dw_fcastle_pinkshop_Step_0`). Earned from `obj_plat_coin`, `obj_climb_coin`, treasure rooms |

### Chapter 5 Flowery Dollar display state, `global.flag[1412]`

This flag is story state, not a currency, so the cheat must not change it.

| Value | Meaning |
|---|---|
| 0 | No Flowery Dollars taken yet |
| 1 | They were taken from the box in the garden enemy rush (`obj_dw_garden_enemyrush_Other_14`). The money display shows `D$ = gold + F$` (`obj_darkcontroller_Draw_0`) |
| 2 | Set when the player "realized that the dollars you took weren't Dark Dollars". `F$` is then shown on its own line |

`P$` gets its own line once `flag[1312] > 0`.

Consequence while 1412 = 1: editing F$ changes the D$ number shown, but shops that take D$ still use only `global.gold`. The money page has to explain this.

### Proposed "Money..." page

Replace the Gold row with a **Money...** page built from a table of entries `{name, chapter range, getter/setter, max}`, so a new currency is one entry. The page shows only the entries for the current chapter, with ←/→ ±10 and Shift ±1000.

- **Caps.** Shops compare money with `>=`, so a negative or huge value has to be clamped. The proposal is 0..99999. This needs checking against each currency's text box: `D$` is drawn at a fixed position.
- **TV points in Chapter 4.** The TV Show page edits PT in Chapter 3 only, but Chapter 4 spends PT at Gerson's table. The money page should offer PT in Chapters 3 and 4.

## Other cheat candidates

| Cheat | Storage / hook | Chapters | Notes |
|---|---|---|---|
| Walk/run speed | `obj_mainchara.bwspeed` (3, or 4 in the Dark World); `wspeed` is derived from it each step (`obj_mainchara_Step_0`: bwspeed, + 1..3 running, + 2..5 running in the Dark World, `ceil(x0.7)` climbing) | 1–5 | **Done** (task 28): "Walk speed" row, `scr_drcheat_speed_apply`. Scales only the vanilla bases 3/4 and keeps the game's value on the instance, so it never compounds and OFF restores it exactly. Scenes that write their own `bwspeed` keep it: Ch5 `obj_ch5_LW20`/`LW21` slow-to-a-stop exits (0..2; LW20 reads it back and waits for 0), Ch3/5 `obj_b3bs_stealth` (rewrites it every End Step). Ch5 side B and plot >= 570 clamp `wspeed` to 2/3 after it is derived, so the cheat has no effect there. Skipped on purpose (own physics, no `bwspeed`): `obj_plat_player` (platformer), `obj_climb_kris` (wall climbing, `climbspeed`), `obj_mainchara_dash`, `obj_mainchara_board`, `obj_homealone_heart` |
| Light World stats | `global.lhp`, `lmaxhp`, `lat`, `ldf`, `llv`, `lxp`, `lweapon` | 1–5 | **Done** (task 29): "Light World..." page, `scr_drcheat_page_lightworld`. The game only writes `lmaxhp` / `lat` / `ldf` / `llv` / `lxp` in `scr_gamestart` and the load scripts; nothing levels up in the Light World, so LV and EXP need no derived stats. `obj_overworldc_Draw_0` shows them (NEXT is a fixed LV 1–19 table); `llv` is also the save file's `Love` ini value. `lhp` is also written by `scr_lrecover`, `scr_litemuseb`, `scr_become_light` (`lhp` = Kris's Dark World HP ratio × `lmaxhp`, clamped 1..`lmaxhp`) and a few Ch4 scenes; `scr_become_dark` (Ch2/4/5) divides by `lmaxhp`, so the page keeps it ≥ 1. `lweapon` is equipment and isn't edited |
| Instant battle win | `scr_wincombat` exists in every chapter (two entries in Ch4) | 1–5 | ROADMAP 30. Guards and side effects in [§2 Encounter toggle](#2-encounter-toggle): menu turn only, `specialbattle == 0`, nothing from `scr_drcheat_nospare`, one call per `flag[60]` wave |
| Encounter control | `obj_testoverworldenemy` cancel in Begin Step; `global.encounterno` is only a selector | 1–5 | **Researched** (ROADMAP 32): conditional, roaming enemies only. See [§2](#2-encounter-toggle) |
| Flag viewer/editor | `global.flag[0..]`, `global.plot` | 1–5 | Power-user page: read any flag, edit with a confirm step. The flag-name table (`scr_flag_names`) is stripped from release builds, so the page uses our own list: `data/flags.json` (1499 flags, `global.plot` milestones per chapter; see `data/README.md`), compiled in as `scr_drcheat_flagnames()` / `scr_drcheat_plotnames()` |
| Room warp | `room_goto` | 1–5 | **Researched** (ROADMAP 32): conditional, save-point whitelist, visited only, same world, load-style entry. See [§3](#3-room-warp) |
| `global.debug` | `scr_debug()`: Ch1 `return global.debug`, **Ch2 hard-coded `return 0`**, Ch3/4 `global.debug == 1` or `undefined`, Ch5 `global.debug == 1`. 1091 call sites in Ch1–5 (Ch5: 135 code entries) | 1–5 | **Researched** (ROADMAP 32): avoid as a toggle; re-implement the useful branches as direct calls. See [§1](#1-scr_debug-branches-in-release-builds) |

## Already covered

These are in the menu now (`README.md`): money (all currencies above), heal, TP, lock HP / no damage / infinite TP, puzzle timer, party stats, Light World stats, inventory, enemies (HP / mercy / tired / auto mercy), recruits, and the Chapter 3 TV show.
