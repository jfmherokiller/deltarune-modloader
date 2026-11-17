# Room warp for the CheatMenu: safety assessment (ROADMAP 32, third bullet)

Read-only research against the local decompile in `RE/code/chapterN/` (UndertaleModTool dump, not
committed) and the room tables in each `chapterN_windows/data.win`. File references are
`chapterN/<file>:<line>` under `RE/code/`. Room indices and placed instances come from parsing the
`ROOM`/`OBJT` chunks (method in the appendix), because the UMT dump has no room files.

## Verdict: conditional

**Don't offer arbitrary `room_goto`.** A **curated warp to save-point rooms** is safe enough if
it meets all of these conditions:

1. The warp enters the room the way **loading a save** does: `global.interact = 0` and
   `global.entrance = 0`, so the room's own placed `obj_mainchara` is the spawn point.
2. The target is a save-point room that is also in the chapter's `scr_get_room_list()`. Otherwise
   a save made there reloads into the dead-end dog room.
3. The target is in the **current world** (Dark/Light), unless the warp performs the game's own
   world switch.
4. The target has already been **reached in normal play** on this save. A per-save "visited" set
   blocks warps ahead of the story.
5. The warp only runs from free overworld control: no battle, no cutscene, no dialogue.

With these conditions a warp is effectively a "load the save point you already reached". Without
them, the code shows these failure modes:

- Crashes: Ch1 entrance markers, and rooms that have no `obj_mainchara`.
- Story skips that become permanent once saved. Room-entry code ratchets `global.plot` forward.
- Party changes.
- Saves that can never be loaded past the dog screen.

Full warp with only a warning is **not** recommended. The worst outcome is silent: a save that
looks fine but loads into the dog room every time, or a plot value past content the player never
saw.

---

## 1. How the game changes rooms

### Doors (every chapter)

Doors run the same three steps:

| Step | Code | State set |
|---|---|---|
| Touch | `chapter5/gml_Object_obj_doorA_Other_19.gml:1-12` | deactivates overworld bullets; `global.interact = 3` (frozen, "in transition"); creates `obj_fadeout`; `alarm[2] = 8`; `event_user(8)` (per-door hook) |
| Door parameters (Ch5 `obj_doorparent`) | `chapter5/gml_Object_obj_doorparent_Other_17.gml:1-27` | `global.entrance` (number, or letter → 1..), `global.facing`, `global.flag[21]` = post-entry freeze frames (`-10` = none), door sound |
| Leave | `chapter5/gml_Object_obj_doorA_Alarm_2.gml:1-4`, `obj_doorAny_Alarm_2.gml:1-4` | `global.entrance = 1` (A) or the door's value; `event_user(7)`; `instance_create(obj_persistentfadein)`; `room_goto_next()` / `room_goto(doorRoom)` |

`obj_doorB…X` and the `_musfade` variants do the same, with a different entrance and a music
fade. `scr_shortcut_door` and `obj_shortcut_door` (Ch1 forest, Ch2/3/5 hubs) are plot-gated
fast-travel doors. Their gates are `chapter2/gml_Object_obj_shortcut_door_Create_0.gml:20,28` (plot < 60 / < 100)
and `chapter5/gml_Object_obj_shortcut_door_Create_0.gml:23` (plot < 440).

### Arrival: `obj_mainchara` Create

Every playable room has `obj_mainchara` placed in the room editor. On arrival:

- `global.currentroom = room` (`chapter5/gml_Object_obj_mainchara_Create_0.gml:2`; Ch1–3 store
  the stable room id via `scr_get_id_by_room_index`). **This is the room the next save writes.**
- `darkmode = global.darkzone` decides the 2× Dark World sprite scale (`:32-38`).
- The marker placement runs **only when `global.interact == 3`** (`:196`). `global.entrance` picks
  the marker:

  | Entrance | Marker |
  |---|---|
  | 1–6 | `obj_markerA–F` |
  | 18–23 | `obj_markerr–w` |
  | 24 | `obj_markerX` |

  If none matches, the player goes to the `obj_markerAny` whose `image_index == entrance`
  (`:342-368`). If that also fails, the player goes to the **room centre** (`:369-373`), which can
  be inside a wall.
- `flag[21] <= 0` releases control at once (`:201-205`). Otherwise
  `chapter5/gml_Object_obj_mainchara_Step_0.gml:54-67` counts `flag[21]` down and then sets
  `global.interact = 0`.
- If `global.interact != 3`, none of this runs and the player stays at the editor position. That
  is exactly what a **save load** does: `scr_gamestart` sets `interact = 0` and `entrance = 0`
  (`chapter5/gml_GlobalScript_scr_gamestart.gml:334-335`), and `scr_load` just calls `room_goto`.
  Every save room's editor position is the load spawn, which the game exercises on every load.
- **Ch1 differs:** `chapter1/gml_Object_obj_mainchara_Create_0.gml:86-…` reads
  `obj_markerA.x` and the other markers **without an existence check**. A missing marker there
  is a "can't find instance" crash. Ch1 also maps entrance 4 to marker C, not 3.

### Followers, camera, music

- **Followers** are not carried over. The room's controller recreates them from `global.char[]`:
  - `obj_darkcontroller` Create in the Dark World (`chapter5/gml_Object_obj_darkcontroller_Create_0.gml:46-66`;
    Ch1 `:62`).
  - `obj_overworldc` Create in the Light World, Ch2+, Susie/Noelle only
    (`chapter5/gml_Object_obj_overworldc_Create_0.gml:9-20`).

  So followers follow whatever party is current. A raw warp needs no follower setup, but it also
  gets no party fix-up.
- **Camera:** `obj_mainchara` End Step re-centres and clamps the camera every frame
  (`chapter5/gml_Object_obj_mainchara_Step_2.gml:~80-121`, `camerax_set`/`cameray_set`). No setup
  is needed.
- **Music** belongs to room objects: `obj_musicer_*`, or room controllers that check
  `snd_is_playing(global.currentsong[1])` (e.g. `chapter4/gml_Object_obj_dw_churchb_gersonstudy_Create_0.gml:47-56`).
  World transitions free the old song first (`snd_free(global.currentsong[0])`,
  `chapter5/gml_Object_obj_dw_leave_Draw_0.gml:76`). After a raw warp the old track keeps
  playing until a room restarts it. This is cosmetic.

### World switches (Light ↔ Dark)

The game's own recipe:

- **Leaving the Dark World** (`chapter5/gml_Object_obj_dw_leave_Draw_0.gml:70-82`):
  1. `obj_persistentfadein`
  2. `global.interact = 3`, `global.entrance = 24`
  3. `snd_free(global.currentsong[0])`
  4. `scr_become_light()`
  5. Ch2 only: `scr_losechar()` at plot 16
  6. `room_goto(nextroom)`
- **Entering the Dark World** (`chapter5/gml_Object_obj_dw_transition_Draw_0.gml:551-553`):
  `scr_become_dark(); room_goto(nextroom)`.

What the two scripts do:

- `scr_become_dark` (`chapter5/gml_GlobalScript_scr_become_dark.gml:1-25`) sets
  `global.darkzone = 1`, converts the equipped LW weapon, copies LW weapons into the DW
  inventory, and scales `hp[1]` from `lhp`.
- `scr_become_light` (`:26-…`) does the reverse and adds or removes the Light World key items.

`global.darkzone` is saved and loaded (`chapter5/gml_GlobalScript_scr_load.gml:45`). A mismatch
survives a save.

### Save / load and the "dog check"

- **Save:**
  - `obj_savemenu` Create sets `global.currentroom = scr_get_id_by_room_index(room)`
    (`chapter5/gml_Object_obj_savemenu_Create_0.gml:47`; Ch2 `:45`).
  - `scr_saveprocess` writes it (`chapter5/gml_GlobalScript_scr_saveprocess.gml:215`).
  - The ini "Room" field is written at `chapter5/gml_GlobalScript_scr_save.gml:23`.
- **Load** (`chapter5/gml_GlobalScript_scr_load.gml:310-374`): it reads `global.plot` and
  `currentroom`, maps the id back to an index (`scr_get_room_by_id`), and then calls
  **`scr_dogcheck()`**. If the room id is not in the chapter's `scr_get_room_list()`, the load
  goes to the dog room instead:

  | Chapter | `scr_load` line | Dog room |
  |---|---|---|
  | 1 | `:350` | 131 `PLACE_DOG` |
  | 2 | `:342` | `choose(226, 271)` |
  | 3 | `:359` | 83 `PLACE_DOGCHECK2` |
  | 4 | `:360` | 92 `PLACE_DOGCHECK2` |
  | 5 | `:351` | 98 `PLACE_DOGCHECK_CH5` |

  The dog room has no exit. `chapter5/gml_Object_obj_dogcheck_ch5_Create_0.gml` only plays music
  and draws the dog, and the saved room id never changes. **The file is stuck for vanilla play.**
- `scr_dogcheck` is `chapterN/gml_GlobalScript_scr_dogcheck.gml`. Ch2–5 skip it only under
  `scr_debug()`, which is dead in release builds; Ch1 has no bypass.
- `scr_get_room_list()` is in `chapterN/gml_GlobalScript_scr_get_room_by_id.gml`:

  | Chapter | Line | Listed rooms / total rooms | Unlisted |
  |---|---|---|---|
  | 1 | 50 | 131 / 147 | 16 |
  | 2 | 43 | 277 / 278 | 1 |
  | 3 | 49 | 195 / 246 | 51 |
  | 4 | 51 | 243 / 328 | 85 |
  | 5 | 64 | **39 / 252** | 213 |

  In Ch5, saving almost anywhere outside the official save rooms makes the save unloadable.

## 2. Existing debug warps (all dead in release)

`global.debug` is never set to 1 in release, and Ch2's `scr_debug()` is `return 0`. See
`debug_branches.md`.

- **`obj_mainchara` Step, under `scr_debug()`** (`chapter5/gml_Object_obj_mainchara_Step_0.gml:895-954`;
  Ch2 `@733`, Ch3 `@992`, Ch4 `@870`):

  | Key | Effect | Line |
  |---|---|---|
  | **Insert** | `room_goto_next()` | `:907-917` |
  | **Delete** | `room_goto_previous()` | `:918-924` |
  | **Home+2** | `room_goto(room_krisroom)` plus `global.plot = 0; global.darkzone = 0` | `:927-932` |
  | **Home+3** | `room_dw_castle_area_1` plus `plot = 7; darkzone = 1` | `:933-938` |
  | **Home+7/8/9** | legend, `room_battletest`, `room_bullettest` | |

  These are raw `room_goto` calls, with only `darkzone` (and a plot reset) fixed up. The player
  lands at the editor position.
- The developers' state setup for warped-in testing is **per-room debug code**, keyed on held
  keys or `global.tempflag[90]` (the "dev warp" marker). One example is
  `chapter5/gml_Object_obj_dw_garden_intro_Create_0.gml:2-13`: P plus tempflag 90 sets
  `plot = 199`. The debug-gated `scr_setparty(…)` calls in room controllers are another example;
  `debug_branches.md` counts 84 SETPARTY and 53 STARTSTATE sites. **All of it is stripped in
  release**, so a release-build warp gets none of that per-room setup. This is the main reason
  arbitrary warp is unsafe.
- The room-select menu is stubbed: `chapter5/gml_GlobalScript_scr_roommenu.gml:1` is `exit;`,
  `chapter1/gml_Object_obj_debugcontroller_Step_0.gml:1` is `exit;`, and `scr_roommenu_debug`
  (called from `obj_dw_fcastle_yellow_miniboss_Draw_0`) has no definition.

## 3. Failure modes of a raw `room_goto`

| # | Failure | Where it bites | Code |
|---|---|---|---|
| F1 | **No player**: room has no `obj_mainchara` (menus, transitions, cutscene-only, battle-test rooms). Controllers dereference `obj_mainchara.x` at once → crash or a frozen screen | Rooms without a placed `obj_mainchara`: Ch1 16, Ch2 28, Ch3 31, Ch4 29, Ch5 31 | `chapter5/gml_Object_obj_overworldc_Create_0.gml:13`, `obj_darkcontroller_Create_0.gml:66` |
| F2 | **Missing marker** when warping with `interact = 3` and an entrance the room lacks | Ch1 crash; Ch2–5 fall back to `markerAny`, then to room centre (possibly inside a wall) | `chapter1/gml_Object_obj_mainchara_Create_0.gml:86-…`; `chapter5/…Create_0.gml:339-374` |
| F3 | **Followers wrong**: rebuilt from `global.char[]`, so the warp keeps the current party in a room that expects another one. Cutscenes then run with the wrong cast or call `scr_losechar` on someone who isn't there | All chapters | `obj_darkcontroller_Create_0.gml:46-66` |
| F4 | **Cutscene room before its plot**: room controllers start their intro when `global.plot` is below a threshold, and they assume the *previous* room set the party or position (that setup was debug-only, see §2) | e.g. Ch5 `obj_dw_fcastle_post_party_jail` | `chapter5/gml_Object_obj_dw_fcastle_post_party_jail_Create_0.gml:11-17`: cutscene at plot < 399, solo-Kris party set only under `scr_debug()` |
| F5 | **Plot ratchet on entry**: release-live `if (plot < N) plot = N` in room Create. Warping ahead skips story content, permanently once saved | Ch5 `obj_dw_cliff_climbrefresher` (305), `obj_dw_garden_aqua` (290), `obj_dw_fcastle_seth_encounter` (449), `obj_dw_fcastle_asgore` (`max(plot,435)`), `obj_dw_garden_enemyrush`/`_diner`/`_ralseicupboard` (minplot); Ch4 `obj_dw_churchc_savepoint` (244.9), `obj_dw_churchc_prepretitan` (249), `obj_dw_churchb_staircaseintro` (231); Ch2 `obj_ch2_room_city_savepoint` (75) | `chapter5/…climbrefresher_Create_0.gml:2-4`, `…garden_aqua_Create_0.gml:26-28`, `…seth_encounter_Create_0.gml:15-17`, `…asgore_Create_0.gml:91`, `…enemyrush_Create_0.gml:15-17`; `chapter4/…churchc_savepoint_Create_0.gml:5-8`, `…churchc_prepretitan_Create_0.gml:5-9`, `…churchb_staircaseintro_Create_0.gml:63-70`; `chapter2/…city_savepoint_Create_0.gml:2-5` |
| F6 | **Party forced on entry** (release-live) | Ch4 `obj_dw_churchb_gersonstudy` calls `scr_setparty(1)` unconditionally; Ch4 `obj_dw_churchb_staircaseintro` calls `scr_setparty(1)` at plot < 231; Ch5 `obj_ch5_dw_castle_town` calls `scr_setparty(0,1,0)` at plot < 50 | `chapter4/…churchb_gersonstudy_Create_0.gml:58`, `…staircaseintro_Create_0.gml:66-70`, `chapter5/…obj_ch5_dw_castle_town_Create_0.gml:3-6` |
| F7 | **Wrong world**: `global.darkzone` is not changed by `room_goto`. LW Kris at 1× scale in a DW room, or the reverse; DW weapons and HP not converted; the mismatch is saved | Any cross-world warp | `obj_mainchara_Create_0.gml:32-38`, `scr_become_dark.gml`, `scr_load.gml:45` |
| F8 | **Wrong chapter**: room indices are per `data.win`, so `room_goto` can only reach the current chapter. Other chapters' Light World rooms exist as copies with *different* indices and ids | Ch3's list even holds Ch2 ids (`room_krisroom=20028`) | `chapter3/gml_GlobalScript_scr_get_room_by_id.gml:49` |
| F9 | **Unloadable save** (dog room): saving after a warp into a room missing from `scr_get_room_list()` | Ch5 213 rooms, Ch4 85, Ch3 51 (including Ch3 `room_dw_castle_town`, which *has* a save point), Ch1 16 | §1 "Save / load" |
| F10 | **Warping out of a frozen state**: the CheatMenu calls `instance_deactivate_all(true)` when it opens. A `room_goto` while instances are deactivated drops them, including persistent ones like `obj_time` that keep the mod alive. Warping mid-cutscene can also carry over persistent cutscene objects, `interact = 1`, or `obj_persistentfadein` | CheatMenu itself | `mods_examples/CheatMenu/all_chapters/code/gml_Object_obj_drcheat_Step_0.gml:129`, `drc_close` in `…obj_drcheat_Create_0.gml:90-` |
| F11 | **Ch3 board rooms**: own controller, own party (`scr_getchar(0)`), own camera, own save object (`obj_b2savelancer`) | `room_board_*` | `chapter3/gml_Object_obj_board_controller_Create_0.gml:4`; see `debug_branches.md` on `chemg_show_room` |

## 4. Recommendation and recipe

### Rule set

1. **Targets**: only rooms from the per-chapter whitelists in §5. Those rooms have a save point
   placed, a placed `obj_mainchara`, and membership in `scr_get_room_list()`.
2. **Unlock**: a target is offered only after the player has had **free control in it** during
   normal play on this save. Record `room_get_name(room)` once `global.interact == 0` for about
   30 frames in a whitelisted room. This is cheap in the `obj_time` Step hook the CheatMenu
   already uses. Store the set per chapter and per `global.filechoice` in the mod's storage
   (`docs/mod_storage.md`). This rules out F4, F5 and F6: those rooms' entry cutscenes have
   already run, so their `plot < N` gates are false.
3. **Same world only** in v1: offer a room only when its world equals `global.darkzone`. Ch4 is the
   only chapter with Light World save rooms; they are listed with a `LW` tag.
4. **Preconditions**, checked from state captured when the menu opened (`instance_exists` is
   false for deactivated instances, so check before `instance_deactivate_all` or by scanning
   `drc_frozen`):
   - `obj_mainchara` exists.
   - `!instance_exists(obj_battlecontroller)` and `global.fighting == 0`.
   - `global.interact == 0`.
   - No `obj_dialoguer` / `obj_writer`.
   - Not a Ch3 board room.
5. **Label** the page "Warp to save point", and confirm with Z twice like the Flags page does.

### Minimal state before `room_goto`

```gml
/// scr_drcheat_warp(room_name) -> "" or an error string
function scr_drcheat_warp(_name)
{
    var _r = asset_get_index(_name);              // names resolve per chapter's data.win
    if (_r < 0 || !room_exists(_r)) return "no such room";
    if (!drc_open_ok) return "only from free overworld control";   // snapshot taken at menu open
    drc_close();                     // 1. reactivate every frozen instance BEFORE leaving (F10)
    global.interact = 0;             // 2. load-style entry: spawn at the room's placed obj_mainchara
    global.entrance = 0;
    global.flag[21] = -10;           //    no post-door freeze countdown
    global.facing = 0;               //    face down, as after a load
    snd_free(global.currentsong[0]); // 3. stop the old track; the target room starts its own
    instance_create(0, 0, obj_persistentfadein);  // 4. same fade-in doors use (cosmetic)
    room_goto(_r);                   // 5. takes effect at the end of this step
    return "";
}
```

Notes on the recipe:

- **Use load-style entry, not a door entrance.** `interact = 0` skips the marker code entirely
  (`obj_mainchara_Create_0.gml:196`), so F2 can't happen. The spawn is the same point a save load
  uses in that room.
- **Do not touch** `global.plot`, `global.char[]`, flags or `global.darkzone`. Rule 2 makes those
  already correct for the target.
- If cross-world warps are ever added (v2), copy the game's own sequence:
  - To the Light World: `scr_become_light()` before `room_goto`.
  - To the Dark World: `scr_become_dark()` before `room_goto`.

  This still leaves party composition: Light World scenes usually run with Kris alone. That needs
  per-chapter handling, so keep it out of v1.
- `snd_free` and `obj_persistentfadein` exist in all five chapters (`gml_GlobalScript_snd_free`,
  `obj_persistentfadein_Create_0`). `global.currentsong[0]` is used in all five.

## 5. Candidate whitelists (save-point rooms)

Each list is the set of rooms with `obj_savepoint` (or a chapter save variant) placed, joined with
`scr_get_room_list()` membership and `scr_roomname` naming. In the tables:

- **idx** is the room index in the current `data.win`. Use the name with `asset_get_index`, not
  the index.
- **Notes** flags rooms whose entry code reads or writes the plot. Rule 2 (visited only) makes
  them safe.
- **excl** marks rooms to leave out.

### Chapter 1 (all Dark World; 17 rooms, all in the list)

| idx | room | notes |
|---|---|---|
| 35 | `room_dark1a` | |
| 40 | `room_dark_eyepuzzle` | |
| 45 | `room_castle_town` | |
| 49 | `room_field_start` | |
| 56 | `room_field_maze` | Hathy event at plot < 36 (`obj_hathyfightevent`) |
| 59 | `room_field_shop1` | |
| 68 | `room_field_checkers3` | Lancer scare event at plot < 55 |
| 71 | `room_field_checkers7` | |
| 73 | `room_forest_savepoint1` | shortcut door gated at plot < 60 / < 154 |
| 82 | `room_forest_savepoint2` | bake-sale event at plot < 80 |
| 90 | `room_forest_savepoint_relax` | |
| 96 | `room_forest_savepoint3` | |
| 107 | `room_cc_prison_to_elevator` | |
| 111 | `room_cc_prison_prejoker` | |
| 114 | `room_cc_1f` | |
| 123 | `room_cc_5f` | |
| 126 | `room_cc_throneroom` | |

### Chapter 2 (all Dark World)

| idx | room | `scr_roomname` | notes |
|---|---|---|---|
| 55 | `room_castle_town` | Castle Town | `obj_ch2_scene4` at plot 8–9 |
| 61 | `room_dw_castle_area_2` | Castle Town | same scene |
| 62 | `room_dw_castle_area_2_transformed` | My Castle Town | `obj_ch2_scene6` at plot 10–15 (party swap) |
| 78 | `room_dw_cyber_savepoint` | Cyber Field - Entrance | |
| 89 | `room_dw_cyber_musical_shop` | Cyber Field - Music Shop | |
| 112 | `room_dw_city_entrance` | Cyber City - Entrance | `obj_cityEntrance_event` at plot < 67 |
| 121 | `room_dw_city_savepoint` | Cyber City - Music Shop | **ratchet to plot 75 on entry** (F5) |
| 133 | `room_dw_city_postbaseball_1` | Cyber City - Heights | |
| 152 | `room_dw_mansion_lightner_hallway` | Queen's Mansion - Guest Hall | `obj_ch2_scene18` at plot < 101 |
| 157 | `room_dw_mansion_entrance` | Queen's Mansion - Entrance | |
| 171 | `room_dw_mansion_b_central` | Queen's Mansion - Basement | |
| 199 | `room_dw_mansion_prefountain` | Queen's Mansion - Rooftop | |

Excluded:

- `room_dw_cyber_intro_connector` (75) and `room_dw_mansion_east_4f_a` (217): they have a save
  point placed, but `scr_roomname` doesn't name them. Possibly unused; verify in game first.
- `room_dw_cyber_savepoint_original` (245) and `room_dw_city_traffic_5_old` (257): developer
  leftovers.

### Chapter 3 (Dark World)

| idx | room | `scr_roomname` | notes |
|---|---|---|---|
| 98 | `room_dw_couch_overworld_intro` | Dark World | chapter opening, plot < 10 |
| 109 | `room_dw_snow_zone` | Cold Place | several plot-banded scenes (160–340) |
| 195 | `room_dw_teevie_intro` | TV World - Entrance | |
| 208 | `room_dw_teevie_rhythm` | TV World - Concert | |

Excluded:

- `room_dw_castle_town` (56): it has a save point, but it is **not in Ch3's
  `scr_get_room_list()`**, so a save there loads into the dog room (F9).
- `room_board_2` (141) and `room_board_tests` (129): board mode (F11).

### Chapter 4

| idx | room | world | notes |
|---|---|---|---|
| 15 | `room_krisroom` | LW | chapter intro at plot 0 |
| 24 | `room_town_mid` | LW | |
| 49 | `room_lw_noellehouse_kitchen` | LW | home-alone sequence at plot 60–66 |
| 65 | `room_dw_castle_town` | DW | |
| 298 | `room_dw_castle_tv` | DW | |
| 115 | `room_dw_church_gersonstudy` | DW | DCA07/DCA10 scenes at plot 160–190 |
| 177 | `room_dw_church_savepoint` | DW | |
| 218 | `room_dw_church_pianopuzzle` | DW | |
| 219 | `room_dw_church_nwconnect` | DW | |
| 251 | `room_dw_churchb_savepoint` | DW | |
| 270 | `room_dw_churchc_superprophecies` | DW | |
| 277 | `room_dw_churchc_savepoint` | DW | **ratchet to 244.9** (F5) |
| 280 | `room_dw_churchc_prepretitan` | DW | **ratchet to 249** (F5) |
| 304 | `room_dw_churchb_darkclimb` | DW | climbing room |

Excluded:

- `room_dw_churchb_staircaseintro` (247): sets plot to 231 and calls `scr_setparty(1)` at
  plot < 231.
- `room_dw_churchb_gersonstudy` (257): calls `scr_setparty(1)` on **every** entry (F6).

Both are story-sequence rooms. Even when visited, a warp there resets the party.

### Chapter 5 (all Dark World; 15 rooms, all in the 39-room list)

| idx | room | `scr_roomname` | notes |
|---|---|---|---|
| 54 | `room_dw_castle_town` | My Castle Town | arrival cutscene with `scr_setparty` at plot < 50 |
| 112 | `room_dw_castle_tv` | Castle Town - TV Building | |
| 122 | `room_dw_garden_floradinnencounter` | Garden - Beginning | |
| 141 | `room_dw_garden_aquahole` | Garden - Shrine | |
| 144 | `room_dw_garden_cliffexit` | Cliffs - Beginning | `obj_ch5_DW10` scene at plot < 300 |
| 161 | `room_dw_cliff_shop` | Cliffs - Shop | |
| 167 | `room_dw_cliff_verticalwind_post` | Cliffs - Below Castle | |
| 177 | `room_dw_fcastle_cafe` | Flower Castle - Cafe | |
| 179 | `room_dw_fcastle_post_party_jail` | Flower Castle - Jail | jail scene at plot < 399, expects solo Kris (F4) |
| 189 | `room_dw_fcastle_onsen` | Flower Castle - Left End | |
| 202 | `room_dw_fcastle_second_diner` | Flower Castle - Right Diner | |
| 205 | `room_dw_fcastle_right_penultimate` | Flower Castle - Right View | |
| 207 | `room_dw_fcastle_right_endingscene` | Flower Castle - Right End | |
| 222 | `room_dw_fcastle_top_entrance` | Top of Castle - Beginning | |
| 225 | `room_dw_fcastle_final_save` | Top of Castle - Castle Top | `obj_shortcut_door` gated at plot < 440 |

Ch5's `scr_get_room_list()` also includes `scr_roomname`-named rooms without a placed save point
(`room_krisroom`, `room_town_south`, `room_dw_garden_intro`, `room_dw_garden_enemyrush`,
`room_dw_garden_diner`, `room_dw_garden_aquadash`, `room_dw_fcastle_left_twodoors`,
`room_dw_fcastle_blueroom`, `room_dw_fcastle_green_checkpoint`, `room_dw_fcastle_pinkroom`). These
are loadable, so saving there is safe, but several ratchet the plot on entry (`enemyrush`,
`diner`). Leave them out of v1.

## 6. Remaining risks with the recommended design

- **Visited-set bookkeeping**: a stale or shared set (wrong slot, wrong chapter) re-opens F4–F6.
  Key it by chapter and `global.filechoice`, and clear it when a new game starts
  (`scr_gamestart`).
- **Mid-room state**: some save rooms hold puzzle or NPC state in `global.flag[]`. Warping back
  restores the room as a load would, which the game already supports.
- **Sequence-broken backtracking**: warping *back* to an earlier hub is fine for the room itself.
  Doors out of it may lead into rooms whose plot gates assume forward progress (e.g. one-way story
  corridors). This is the same exposure as walking back, which the game mostly blocks with
  `obj_tempblock` and `obj_forcefield` (`plot >= disableplot`).
- **Music** may be silent until the next musicer room. This is cosmetic.
- **Untested in game.** Every claim here comes from code and `data.win` structure. Before
  shipping, test one warp per chapter, then save and reload. Also check:
  - Ch4 LW↔LW warps.
  - Chapter-2 rooms 75 and 217.
  - That `room_goto` straight after `drc_close()` keeps `obj_time` alive.

## Appendix: room dump method

UMT's code dump has no room contents, so the tables above come from parsing `data.win` directly:

- The `FORM` header is followed by chunks: a 4-byte name, then a u32 size.
- `OBJT` and `ROOM` each hold a u32 count followed by absolute pointers. An object entry's word 0
  points to its name string (NUL-terminated).
- A room entry's word 0 is a pointer to its name; word 12 (offset 48) points to the
  `GameObjects` pointer list.
- Each instance record starts with `x`, `y` and an `i32` object index.

A ~40-line Python script over these offsets produced, per chapter:

- room index → name;
- placed objects per room;
- the `obj_savepoint`, `obj_mainchara` and `obj_darkcontroller`/`obj_overworldc` (world) columns
  used in §3 and §5.

Room totals match UMT (Ch1 147, Ch2 278, Ch3 246, Ch4 328, Ch5 252). Indices match the hard-coded
dog-room indices in `scr_load` and the `scr_roomname` indices (e.g. Ch5 112 = `room_dw_castle_tv`).
