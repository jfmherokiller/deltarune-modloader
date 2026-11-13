# `scr_debug()` branches: what they do and whether release builds reach them

ROADMAP task 32, first bullet. This is read-only research against the local decompile in
`RE/code/chapterN/`, which is gitignored. Each `file:event@line` reference points at the
decompiled file `RE/code/chapterN/gml_Object_<obj>_<event>.gml` (or `gml_GlobalScript_<name>.gml`
for scripts), at the line of the `scr_debug()` call. Line numbers belong to this decompile and
will move when the game updates.

## Summary

All five chapters contain **1091 `scr_debug()` call sites in 913 code entries**. The "136" in
the roadmap is Chapter 5 on its own: 135 entries with calls, plus the `scr_debug` definition.
**No positive `scr_debug()` branch can run in a release build.** `global.debug` is set to `0` once
at boot and nothing in the shipped code ever writes it again. Chapter 2's `scr_debug()` returns a
constant `0` regardless of `global.debug`. The only release-reachable code in this family is:
- the 28 inverse guards (`!scr_debug()` / `scr_debug() == 0`), which run on every release build;
- 8 places that use a bare `scr_debug` without parentheses (a function reference, which is always
  truthy). These are listed separately below.

Of the 888 positive branches that survived stripping, a few are worth having. Noclip, hitbox
views, the speed toggle, the battle helpers, hold-C text automash and save/load anywhere are good
cheat-menu material. The *battle* F-key cheats are not among them: they sit behind
`scr_debug_keycheck`, which is a stub that always returns false. So turning debug on does not
bring them back.

**Simply setting `global.debug = 1` is not viable as a user-facing cheat.** It does nothing in
Ch2. It probably crashes Ch3 in the first board room, because the code reads a global that is
never assigned. In Ch3–5 it silently rewrites party, plot and flags on room entry in about 140
rooms. It also arms many unprompted single-letter hotkeys (S, L, R, D, M, V, P, W+digit,
Home+digit), and some of those reset `global.plot` to 0. The CheatMenu should call the useful
target scripts directly rather than flip the global.

## Release-build behaviour (with code references)

### Definitions

| chapter | `scr_debug` (`gml_GlobalScript_scr_debug.gml`) | release value | `scr_debug_keycheck` |
|---|---|---|---|
| 1 | `return global.debug;` | `0` | not present (no callers) |
| 2 | `return 0;` (**hard-coded**) | `0`, even if `global.debug = 1` | `var is_pressed = false; return is_pressed;` → always false |
| 3 | `if (global.debug == 1) { return 1; }` | `undefined` (no else branch) | same as Ch2 → always false |
| 4 | same as Ch3 | `undefined` | empty body → `undefined` |
| 5 | `return global.debug == 1;` | `false` | empty body → `undefined` |

- `global.debug = 0;` is set in `obj_initializer2:Create_0` (line 6 in Ch1/2/3/5, line 7 in Ch4).
  No other assignment to `global.debug` exists in any chapter. There is no launch parameter,
  INI key or `variable_global_set("debug")`; `scr_init_launch_parameters` does not touch it.
- Ch4 also reads the global directly, outside `scr_debug()`, in two places:
  `obj_solid_floor:Step_0@17` (`keyboard_check(ord("B")) && global.debug == true`) and
  `obj_dw_church_window_parallax:Step_0@1`. Both are dead in release.
- In Ch3/Ch4, `undefined` in an `if` evaluates to false, and `!undefined` to true. That is
  observable: those chapters run every frame without errors, and their inverse guards (for example
  `obj_snd_maker:Step_0`, which destroys itself) behave as release code.
- `RESEARCH.md` row `global.debug` says "`scr_debug()` returns `global.debug == 1` … 1–5". That
  holds for Ch5 only; Ch2 needs a patched `scr_debug` (see below).

### Debug helper scripts that are stubs in release

These were found by scanning `gml_GlobalScript_*` for empty or constant bodies. With
`global.debug = 1` they still do nothing, so any branch that depends on them stays dead:

| helper | chapters | effect on branches |
|---|---|---|
| `scr_debug_keycheck(key)` | 2–5 | false/undefined: **every battle F-key cheat** (`obj_battlecontroller:Step_0`), `scr_turn_skip` (V), Ch4 `obj_hammer_of_justice_enemy:Step_0@1033` and `obj_sound_of_justice_enemy:Step_0@546` (V skip) |
| `scr_debug_kb_check_pressed`, `scr_debug_number_pressed`, `scr_debug_numpad_pressed` | 3–5 | empty / `-1` |
| `scr_debug_fullheal`, `scr_debug_premade_save`, `scr_debug_soundboard`, `scr_debugallvals`, `scr_debug_type_string` | 2–5 | empty |
| `scr_84_debug` (the dev debug-menu UI) | 1–5 | empty: the in-game debug menu was stripped |
| `scr_debug_print`, `debug_print`, `print_message`, `debug_message`, `scr_debug_print_persistent`, `draw_text_debug`, `scr_debug_save_trophies`/`load_trophies` | 1–5 | empty (Ch5 `scr_debug_print` only contains its own `if (!scr_debug()) exit;`) |
| Ch2 only: `scr_turn_skip`, `scr_weaken_enemies` | 2 | empty (they are real in Ch3–5) |

Debug-only objects are also gutted: every event of `obj_debugProfiler` (Ch2–5) and
`obj_debug_xy` (Ch2–5) is just `exit;`. The branches that create them
(`obj_initializer:Create_0@140` in Ch5, `obj_initializer2:Step_0@74` in Ch3/4, middle mouse in
`obj_time:Step_1`) are therefore harmless and useless.

### Bare `scr_debug` references (release-reachable)

These sites use the function as a value, not a call. They are not in the 1091 count. A script
reference is non-zero, so the condition reduces to whatever follows the `&&` and **does run in
release** unless the script's asset index happens to be 0. That has not been confirmed in
`data.win` (see Open questions).

| chapter | site | trigger | effect | risk |
|---|---|---|---|---|
| 3 | `obj_b1store:Draw_0@1` `if (scr_debug)` | always, in that board room | calls `scr_board_objname()`, which then checks the *real* `scr_debug()` → nothing | none |
| 3 | `obj_b2bridgepuzzle2:Draw_0@1` `… (active == true && scr_debug)` | puzzle active | same: board name label (no-op) | none |
| 3 | `obj_npc_PAspeaker:Step_0@1` `scr_debug && keyboard_check(ord("W"))` | hold W + A / D near the TV-world PA speaker | toggles the speaker sprite "talkmode" and a debug box flag | cosmetic; a real release easter-egg-by-bug |
| 3, 4, 5 | `scr_turn_skip` (script) `scr_debug && scr_debug_keycheck(ord("V"))` | — | keycheck stub → never | none |
| 4 | `obj_hammer_of_justice_enemy:Step_0@1033`, `obj_sound_of_justice_enemy:Step_0@546` | — | keycheck stub → never | none |

## Per-chapter count

"Stripped" means that after the release strip the body contains only stub calls
(`scr_debug_print`, `show_debug_message`…), `var` declarations, or empty `if {}` blocks. The
compiler left the guard and removed the payload.

| chapter | `scr_debug()` call sites | code entries with a call | inverse guards | stripped (no effect) | live branches |
|---|---|---|---|---|---|
| 1 | 15 | 9 | 2 | 5 | 8 |
| 2 | 96 | 80 | 1 | 31 | 64 |
| 3 | 409 | 345 | 3 | 47 | 359 |
| 4 | 415 | 344 | 18 | 72 | 325 |
| 5 | 156 | 135 | 4 | 20 | 132 |
| **total** | **1091** | **913** | **28** | **175** | **888** |

The roadmap's "136 code entries" means Ch5's 135 entries with a call, plus `gml_GlobalScript_scr_debug` itself.

## Catalog

Each row is either one branch or a clearly counted group. Trigger notation: `S` means
`keyboard_check_pressed(ord("S"))` (or `sunkus_kb_check_pressed`, its real-keyboard wrapper in
Ch3–5, which is *not* a stub). `hold P@Create` means the key must be held at the moment the room
is created. `KeyPress_N` / `Mouse_60/61` are GameMaker key-press / wheel-up/down events, so the
event itself is the trigger (79 = O, 89 = Y, 32 = Space, 17 = Ctrl, 8 = Backspace, 49–55 = 1–7,
68 = D, 72 = H, 105 = Numpad9). "Ch2 dead" applies to every Ch2 row, because Ch2 `scr_debug()`
returns 0.

Useful? rates the branch as cheat-menu material. Risk is what happens if the branch fires
(with `global.debug` forced on).

| chapter | file:event | trigger | effect | useful? | risk |
|---|---|---|---|---|---|
| 1–5 | **INV ×28**: `obj_snd_maker:Step_0`, `obj_spritecomparer:Draw_0`, `obj_andydebug:Create_0`, `obj_platswap_statue:Create_0` (Ch5), ch4 `obj_rotating_tower_controller{,_new,_old}:KeyPress_49…55`, `obj_tower_dog_controller:KeyPress_32` | always (inverse) | **Runs in release.** Destroys dev-only objects / `exit`s dev key handlers; Ch1 `obj_darkcastle_event:Step_0@39,67` makes the writer unskippable; Ch2 `obj_rouxls_enemy:Create_0@71` hides simtown makers; Ch3 `obj_ch3_GSB02:Step_0@7` hides performers; Ch5 `obj_ch5_LW20:Create_0@53` `scene_mode = 0`; Ch5 `scr_debug_print` exit | no | debug=1 *removes* these: dev objects stay alive, tower rotation keys 1–7 go live, Ch1 castle text becomes skippable |
| 1–5 | **NOOP ×175** (see appendix) | — | payload stripped; prints, `var` decls, empty ifs, Ch2 `obj_overworldc:Step_0@261` (all keycheck stubs) | no | none |
| 3 | **BOARDNAME ×137**: every board-room controller `obj_b0…/obj_b1…/obj_b2…/obj_b3…:Draw_0` `if (active && scr_debug())` → `scr_board_objname()` | always (Draw) | draws the object name in aqua if `global.chemg_show_room` | no | **crash**: `global.chemg_show_room` is never assigned anywhere in Ch3 (it lived in the stripped `scr_84_debug` menu), so the read raises "not set before reading" on the first board frame with `obj_board_camera.con == 0` |
| 2–5 | **STUBKEY ×4**: `obj_battlecontroller:Step_0` (Ch2@1187, Ch3@1530, Ch4@1371, Ch5@1420) | `scr_debug_keycheck`: V skip turn, I/F9/F10 TP, F2 heal, F3 raise party, F5 win, F6 weaken enemies, F8 weaken party, M music | battle cheats | **yes**, but call the targets directly: `scr_wincombat` (1–5), `scr_weaken_enemies` (3–5), `scr_raise_party`/`scr_weaken_party` (2–5), `global.tension` | dead even with debug (stub keycheck); `scr_debug_fullheal` itself is a stub too |
| 2–5 | **NOTEXT ×21**: `obj_npc_facing/obj_npc_room/obj_npc_room_animated/obj_npc_sign/obj_readable_room1:Other_10`, ch4 `obj_readable_nubert:Other_10` | interact with an NPC that has no text | `global.msg[0] = "* [NO TEXT] (obj_…)"` | no | none (only replaces the default line) |
| 3–5 | **SETPARTY ×84**: room-controller `Create_0` with `scr_setparty(…)`, e.g. Ch5 `obj_ch5_DWCL00`, `obj_dw_garden_exit`, `obj_dw_fcastle_onsen`; Ch5 `obj_ch5_LW02W:Create_0@3` `scr_losechar()` + destroys followers | room entry | forces party composition so a warped-in tester has the right party | no | **story break**: changes the party on every entry; e.g. Ch4 `scr_setparty(1)` makes Kris solo, Ch5 LW02W drops followers |
| 3–5 | **STARTSTATE ×53**: Create-time plot/flag setup (`obj_dw_church_*`, `obj_ch4_DCA0*`, ch5 `obj_dw_garden_*`) | room entry, often `hold 1/2/P/W@Create` or `global.tempflag[90]` (dev warp marker) | sets `global.plot`, `global.flag[…]`, positions, items | no | **save corruption**: several are unconditional: Ch5 `obj_dw_garden_susiechase:Create_0@20` (plot ≥ minplot, flag 1311, party), Ch5 `obj_ch5_LWF01:Create_0@92` removes light item 21, Ch5 `obj_ch5_LW11A:Create_0@7` teleports Kris and drops Noelle, Ch4 `obj_ch4_DCA08C:Create_0@1` (plot ≥ 170, flag 850), `obj_dw_churchc_angelprophecy_encounter` (plot ≥ 244), `obj_dw_churchc_prophecies`/`obj_climbstartertrig` (flag 23), `obj_room_castle_tv_zone_battle` (flag 1695 = 0); held-key ones set **`global.plot = 0`** (Ch4 `obj_dw_churchb_rotatingtower:Create_0@12` P, `obj_dw_churchb_staircaseintro:Create_0@40` 1) |
| 1–4 | **TROPHY ×5**: `obj_event_manager:Create_0` | trophy events, PC only (`!global.is_console`) | with ≥ 29 trophies, queues trophy 0; Ch4 also starts the climb timer; `scr_debug_save_trophies` is a stub | no | may unlock a Steam achievement as a side effect |
| 2–5 | **CUTCMD ×11**: `c_instant`, `c_saveload`, `scr_cutscene_saveload_extra` (scripts) | cutscene code calling them | enables cutscene state snapshot/restore and instant commands | no | low: the only callers are the definitions themselves (no shipped cutscene calls them) |
| 1–5 | **FASTTEXT ×6**: `obj_writer:Draw_0` (Ch1@12, Ch2@13, Ch3@15, Ch4@13, Ch5@17), Ch3 `obj_writer_quiz:Other_10@13` | hold C (`button3_h`) | auto-mashes Z/X through text (alternating frames) | **yes** | low. The guard is `global.flag[10] == 1 \|\| scr_debug()`, so setting `global.flag[10] = 1` gives the same thing *without* debug (`scr_gamestart_chapter_override:44` already sets it on chapter-start saves) |
| 2–5 | **DOGCHECK ×4**: `scr_dogcheck` (script) | room load | `return 0` → skips the "room not in valid room list" dog screen | **yes**, as a prerequisite for any room-warp feature (task 32, third bullet) | low; but warping out of order is itself risky |
| 2–5 | **HITBOX ×42**: `obj_overworld_bulletarea{,_no_dark}:Create_0/Step_0`, `obj_overworld_darkness_bulletarea`, `obj_act_hitbox:Create_0`, `obj_bulletdeleter:Draw_0` (`draw_self`), Ch4 `obj_climb_*:Draw_0` (V toggles `dodraw`), `obj_climbloc` (V), `obj_dw_church_pushableshelf*:Step_2`, Ch4 `scr_makemarker`, Ch5 `scr_makenpc` (`visible = true`), `obj_plat_sequence_marker` | always / V | shows bullet areas, act hitboxes, climb tiles and solids at partial alpha | **yes** (hitbox viewer) | low; cosmetic (Ch5 `scr_makenpc` reveals intentionally hidden NPCs) |
| 2–5 | **OWHOTKEY ×11**: `obj_overworldc:Step_0` (Ch3@261, Ch4@284, Ch5@339), `obj_darkcontroller:Step_0` (Ch2@1723, Ch3@1733, Ch4@1748, Ch5@1767), `obj_rhythmgame_editor:Step_0` | S / L / R / Backspace+R / F (`sunkus_kb_*`, real keys) | **S** opens `obj_savemenu` anywhere, **L** `scr_load()`, **R** `room_restart()` + `interact = 0`, **Backspace+R** `game_restart_true()`, **F** `room_speed = 58` (overworld only) | **yes**: save-anywhere and quick-reload | save-anywhere can record a mid-cutscene/unreachable state (**softlock on load**); R in a cutscene can desync flags; the hotkeys are single letters with no modifier |
| 1, 3–5 | **TIME ×22**: `obj_time:Step_1` | **`` ` ``** (vk 192) | `room_speed` 30 ↔ 150 (Ctrl+`` ` `` → 10). Ch1@159: `` ` `` → 200, Numpad3 → 5 | **yes** (speed toggle) | low; physics/timers scale with room_speed |
| 3–5 | `obj_time:Step_1@21` | F6 / F7 | quicksave / quickload of every instance; gated on `quicksaved == 22` / `== 99`, which nothing sets (`obj_time:Create_0` → 0) | — | dead even with debug |
| 3–5 | `obj_time:Draw_77@1` | G | starts GIF recording (`gif_open`/`gif_save` to a dated file) | maybe | writes files to the save dir |
| 3–5 | `obj_time:Draw_0@1`, `Draw_64@1`, `Step_1@258` (middle mouse) | — | FPS text needs `global.debug_fps_display` (never created); the Draw_64 dim needs `global.chemg_menu_depth` (never created); middle mouse creates gutted `obj_debug_xy` | no | none |
| 3–5 | `obj_initializer2:Step_0@41`, `@74`; Ch5 `obj_initializer:Create_0@140`; Ch4/5 `console` (script) | boot | `menu_go = 3` → boots to `room_title_placeholder` instead of the normal menu; creates gutted `obj_debugProfiler` | no | **boot change** if debug is set before obj_initializer2 runs |
| 1–5 | **PLAYER ×35**: `obj_mainchara:Step_0` (Ch3@1, Ch4@20, Ch5@20) | **Ctrl+Shift+N** or pad LB+RB+Y | **noclip** toggle (`checkcol = false` at Ch5@369) | **yes** | low–medium: can walk out of bounds or into triggers out of order |
| 3–5 | `obj_mainchara:Step_0` (same block) | **Ctrl+Shift+0** or pad LB+RB+Select | `global.interact = 0`: frees a stuck player | **yes** ("unstuck") | medium if used mid-cutscene |
| 2–5 | `obj_mainchara:Step_0` (Ch2@733, Ch3@992, Ch4@870, Ch5@895) | **Insert / Delete**; **Home+2/3/7/8/9** | `room_goto_next/previous`; Home+2 → `room_krisroom` **with `global.plot = 0`**, Home+3 → castle with `plot = 7`, Home+7 legend, Home+8 `room_battletest`, Home+9 `room_bullettest` (Ch2 also Home+1 → cyber intro) | Insert/Delete maybe (with `scr_dogcheck` bypass) | **save corruption** (Home warps reset the plot); next/prev lands in rooms out of story order → softlock |
| 5 | `obj_mainchara:Step_0@895` | Shift+C / Shift+H | toggles `global.flag[24]` (platforming ability) / `flag[1311]` (Ralsei hat) | maybe (flag editor) | low |
| 1–5 | `obj_mainchara:Draw_0` (`button1_p() && scr_debug()`), Ch3–5 `Draw_0` Ctrl+Alt+I, Ch4/5 `obj_mainchara:Create_0` | Z / Ctrl+Alt+I / room entry | draws the interaction box (red rectangle); floorheight text; forces layer `OBJECTS_MAIN` visible | maybe (interaction box) | none |
| 3 | `obj_mainchara_board:Step_0@138,481,2006,2015,2626,2909,2989,3042`, `Draw_0@151,222`, `KeyPress_32`, `obj_mainchara_board_OLD:Draw_0` | 1/2/3, H, O, C+M, Shift+V, Ctrl+Space | board: switch controlled character, `myhealth = 1`, sword level-up, free move/leave, camera, input recording | no | Ch3 board crashes first anyway (BOARDNAME) |
| 2, 4 | `obj_caterpillarchara_free:Step_0@1` (Ch2, B), `obj_caterpillarchara:Step_2@1` (Ch4, Shift+N) | B / Shift+N | spawn a Susie actor / change follower target index | no | medium: follower desync |
| 3–5 | **SAVELOAD ×4**: `scr_load@358/370/371`, Ch5 `scr_save@3` | load / save | after load `room_goto(loaded room)` with an existence check; Ch5 save from `rm_blank` redirects to `live_live_room` | no | low |
| 2–5 | **MUSIC ×18**: `scr_music_mute` (M), `obj_credits_ch4/ch5_audio:Step_0` (R/Q/Space scrub), Ch5 `obj_flowery_lyrics:Draw_0@33,51` (D/E jump the song to 20 s / 94 s), Ch5 `obj_room_garden_video:Step_0` (Space pause video), Ch3 `obj_rhythmgame:Step_0@424` P pause, Create-time `audio_stop_sound(snd_deep_noise)` / resume | M / Space / D / E / P | music mute/pause, track scrub | maybe (mute) | low; D/E in the Flowery fight desync the lyric/attack timeline |
| 3–5 | **RHYTHM ×25**: `obj_rhythmgame:Step_0`, `Draw_0`, `Create_0`; `scr_rhythmgame_notechart@12,2841,4666`, `scr_rhythmgame_song_load` | F5 (win: points/fame max), F6 (lose), V (autoplay, Tenna boss), 4 (skip intro), U/I, Z/X (marker lines), hold 1/2/…@Create (song select), hold **Numpad+** | win/lose the rhythm game; Numpad+ loads the chart *from a file on disk* (`scr_rhythmgame_load_chart_from_file`) | **yes**: F5 "win rhythm game" | Numpad+ file load likely errors when the dev chart file is missing; Ch3 `Draw_0@568` reads unassigned `global.chemg_show_val` → **crash** |
| 2–5 | **RESTART ×26**: `DEVICE_FAILURE/DEVICE_MENU/DEVICE_NAMER:Step/Draw`, `obj_legend_neo:Draw_0` (Backspace), `obj_dogcheck2`, Ch5 `obj_dogcheck_ch5`, `obj_intro_ch5`, `obj_circlebg`, `obj_torigateAnimation`, Ch4 `obj_make_fountain:Step_0@121` (**hold Space**), Ch4 `obj_climbingarea:KeyPress_68` (D: toggles flag 23 + restart), Ch2 `obj_controller_dininghall:Step_0@172` (pad L3) | R / Backspace / Space / D | `room_restart()` in intro/test rooms | no | Ch4 fountain: holding Space restarts the room, an easy accidental loop |
| 3–5 | **ROOMKEY ×13**: Ch3/4/5 `DEVICE_CONTACT_old:Step_0@904` (Backspace → krisroom, flag 6 = 0); W+0 room swaps (Ch3 `obj_dw_b3bs_extrapuzzle/mysterypuzzle`, Ch4 `obj_dw_church_moneyfountain/churchb_moneyfountain/gerson_study`); Ch4 `obj_dw_church_worshiproom:Create_0@98` (**hold 1/2/3: `plot = 0` + room_goto**); Ch4 `obj_dw_churchb_gersonstudy:Step_0@394` (T/1/2…: `global.gold` = 4/100/99…); Ch4 `obj_dw_churchb_ripple1:Step_0@121` (W); Ch4 `obj_microphone_test:Step_0` | keys as listed | room jumps, gold set | gold: use the existing money cheat instead | **plot reset** (worship room); W+0 swaps break puzzle state |
| 1–5 | **ENEMYKEY ×27 + ENEMYDBG ×77**: boss/enemy objects. Highlights: Ch2 Berdly `obj_berdlyb{,2}_enemy:KeyPress_89/79` and Sneo `obj_spamton_neo_enemy:KeyPress_89` (−100 HP); Ch3 `obj_knight_enemy:Step_0@14` (Backspace −3000 HP); Ch2 `obj_queen_enemy:KeyPress_79` (break shield); Ch4 `obj_elnina_lanino_rematch_controller:KeyPress_17` (Ctrl: mercy 90), `obj_jackenstein_enemy:KeyPress_32` (mercy 70); Ch5 `obj_pink_enemy:Step_0@14,23,31` (F1 max doki, F7 max TP limit, I next phase), `obj_date_controller:Step_0@2151` (V win minigame), `obj_flowery_enemy:Step_0@78` (P mercy +10 / phase), `obj_trashy_trio:Step_0@2` (Ctrl mercy +98), `obj_trashy_beam` (Ctrl), `obj_bullet_green_food`/Ch4 `obj_ghosthouse_dot` (Tab); mouse wheel attack/difficulty selectors (Queen, Sneo, Jackenstein, Hammer/Sound of Justice); `overrideAttack` (Tasque Manager B/Q/W, Knight); Ch3/4 `obj_dbulletcontroller:Step_0` **hold Shift/Ctrl/Alt** → `special += 1/2/4`; Ch3 `obj_battlecontroller:Step_0@1565` (F11 lose); Ch3/4/5 `obj_battlecontroller:Draw_0` (hold A: show act info); Ch1 `obj_king_body:Step_0@1` (Enter cycles King attacks) | keys/events as listed | damage, mercy, phase skip, attack override | **yes** as menu actions (set `global.monsterhp[i]` / `global.mercymod[i]` directly instead) | medium: skipping boss phases can leave scripted fights in a bad state; held Shift/Ctrl/Alt silently alter bullet patterns |
| 5 | `obj_dbulletcontroller:Step_0@1415,1460` | entering that attack | `global.turntimer = 9999999` | no | **softlock**: with debug on, that bullet wave never ends |
| 5 | `obj_orange_green_controller:Step_0@99`, Ch3 `obj_knight/rouxls_ch3/shutta/tenna*_enemy:Step_0` `debugvar` | — | only prints "You need Susie and Ralsei" | no | none |
| 1–5 | **SKIPKEY ×49**: cutscene `con`/state jumps, e.g. Ch5 `obj_ch5_DWCL03:Step_0@43` (P: builds a whole cutscene), `obj_ch5_DWCR02:Step_0@1` (1), `obj_dw_garden_enemyrush:Step_0@29` (1 / tempflag 90), Ch4 `obj_dw_churchb_nongerson:Step_0@23,29` (hold B/N: skip to mid/end battle cutscene), `obj_round_evaluation/minigame_evaluation:Draw_0` (P: restart grading), `obj_gameover_minigame:Draw_0` (Numpad0), Ch3 `obj_quizsequence:Step_0@1` (F5 random answers), Ch4 `obj_titan_star_dissolve*` (Numpad5), Ch4 `obj_dw_church_watercooler:Step_0@25` (P), Ch1 `obj_starwalker_overworld:Step_0@32` (dead: `beatcon == 89898`) | keys as listed | jump cutscene state machines | no | **softlock**: con jumps skip setup steps |
| 2–5 | **FLAGKEY ×14**: `obj_dialoguer:Draw_0@3` (D toggles `tempflag[19]`, dialogue box top/bottom); Ch5 `obj_ch5_DWCL00:Step_0@20,25` (hold P/O: flag 1455 = 100/0, "right wing done"); Ch3 `obj_b2pantheonentrance:Step_0@145` (W+1: flags 1041–1043, 1227); Ch3 `obj_chefs_game:Step_0@20` (F7: win, flag 1118 = 999); Ch3 `obj_ch3_gameshow:Step_0@348` (Shift+Up); Ch4 W+V / W+2 warps setting flags (`obj_dw_church_rippleworship`, `pianopiece_left`, `knightclimb`, `sideclimb`), `obj_dw_churchb_nongerson:Step_0@19` (V) | keys as listed | flag writes | flag editor covers this | medium: progression flags |
| 1–5 | **OVERLAY ×61**: Ch5 example enemies `obj_enemy_{aqua,blue,green,orange}_example:Draw_0` (`showval` con/mode), `obj_round_evaluation:Draw_0@1808` (grade bar), `obj_date_ui:Draw_0@221` ("[PRESS F1 FOR MAX DOKI]"…), `obj_treasure_room{,_altsolid}:Draw_0` ("needs flag/item"), `obj_plat_follower:Draw_0@137` (AI target lines), `obj_flowery_enemy:Draw_0@146,156` (End / Ctrl/Alt pattern picker), Ch4 bbox/collision drawers (`distance_to_object_bbox`, `obj_blue_cat`, `obj_gold_cat`, `obj_dw_bluebook`, `obj_traintrack_parent`), `obj_darkness_overlay:Draw_0` (V/B toggle darkness), Ch3 TV funometer (1/2/3) | always / keys | debug text, rectangles, darkness toggle | maybe: grade bar, darkness toggle | Ch3 `showval` (script) reads unassigned `global.chemg_show_val` → **crash** wherever Ch3 calls `showval` (board `obj_b1rocks2`/`obj_b2fashionshop2`/`obj_b2loverscliff:Draw_0`, and `obj_dialoguer:Draw_0` once D has set `tempflag[19]`); Ch5 `obj_dw_fcastle_yellow_miniboss:Draw_0@1` calls `scr_roommenu_debug()`, which has **no definition** in the Ch5 decompile → likely **crash** |
| 2–5 | **KEYMISC ×71**: room-specific dev keys (puzzle autosolve F5 in Ch3 `obj_b2bridgepuzzle1`/`obj_dw_b3bs_mysterypuzzle`/Ch4 `obj_church_secretpiano_playable`; Ch3 sword-board hold H@Create `global.swordboardeath`; Ch3 `obj_ch3_GSA04/GSB03/GSC05/obj_room_stage` hold F: `saveload = true`; Ch3 `obj_ch3_gameshow:Step_0@107` I invulnerable; Ch3 `obj_gameshow_swordroute:Step_0@15` P god mode; Ch3 `obj_susiezilla_statue` Backspace god mode; Ch4 W+1/W+2 teleports; Ch4 `obj_dw_church_organ` B solved / O clipboard; Ch5 `obj_lw_rain_effect` K/P/O; Ch5 `obj_dw_garden_susiechase:Create_0@175` hold O; `obj_sprite_hunter` F11) | keys as listed | puzzle solve, god modes, teleports | puzzle autosolve and god modes are attractive, but each is room-local | medium: teleports and autosolve skip flags the room expects |
| 2–5 | **MISC ×68**: unconditional tweaks. Ch2 `obj_ch2_sceneex2_special:Step_0@3` **`global.interact = 1` on init**; Ch2 `obj_fountainkris_ch2_sideb:Step_0@229`; Ch3 `obj_readable_room1:Other_10@2025` **removes weapon 26**; Ch3 `obj_ch3_GSB03:Create_0@7` creates `obj_round_evaluation` and `exit`s; Ch3 `obj_couchwriter:Step_0@48` failsafe break; Ch3 `scr_quizsetup@4,44` `scr_quizdebug`; Ch4 `scr_plotforce@3` (**plot jumps forward**: release no-op); Ch4 `obj_border_controller` show/hide in about 10 room controllers; Ch4 `obj_treasure_room:Create_0@12`/`Step_1@11` dev defaults; Ch4 `obj_event_ralsei_runahead:Step_0@33` `doevent = true`; Ch4 `obj_ch4_DCA09:Step_0@179` flag 844 = 0; Ch5 `button4_h/p` (pad face button 4); Ch5 `obj_ch5_LW21_arrive` Kris reposition | always | assorted | no | Ch2 sceneex2 **soft-freezes the player**; `scr_plotforce` and the treasure defaults alter progression |

## Most useful branches for a cheat menu

Implement these as **direct calls** from `obj_drcheat` instead of enabling debug:

1. **Battle**: `scr_wincombat()` (all chapters; already ROADMAP 30), `scr_weaken_enemies()`
   (Ch3–5), `scr_raise_party()` / `scr_weaken_party()` (Ch2–5), and TP via `global.tension`
   (`124.9999999` / `250` are the dev values). These are the targets of the stub-gated F-keys in
   `obj_battlecontroller:Step_0`.
2. **Noclip**: the `noclip` instance variable on `obj_mainchara`. Setting it is not enough on its
   own, because the collision skip at `Step_0@369` (Ch5) is itself `scr_debug() && noclip`. A
   cheat needs either a debug-on window or its own collision bypass.
3. **Unstuck**: `global.interact = 0` (the Ctrl+Shift+0 dev key).
4. **Speed toggle**: `room_speed` 30 ↔ 150 (`` ` `` in `obj_time:Step_1`).
5. **Hold-C text automash**: set `global.flag[10] = 1`. No debug needed (see the `obj_writer:Draw_0`
   guard).
6. **Hitbox view**: set `showArea = 1` / `image_alpha` on `obj_overworld_bulletarea*`, and
   `draw_self` for `obj_bulletdeleter`.
7. **Save anywhere / reload**: `instance_create(0, 0, obj_savemenu)` / `scr_load()`. Show a
   warning and refuse during cutscenes (`global.interact != 0`).
8. **Room warp support**: `scr_dogcheck` returns 0 under debug. Any warp feature must bypass the
   dog check the same way.
9. **Rhythm-game win**: the F5 branch in `obj_rhythmgame:Step_0` (`points`/`fame` → max,
   `_skip = true`).

## Dangerous branches

- **Crash**: Ch3 BOARDNAME ×137 (`scr_board_objname` → `global.chemg_show_room`); Ch3 `showval`
  and `obj_rhythmgame:Draw_0@568` (`global.chemg_show_val`); Ch5
  `obj_dw_fcastle_yellow_miniboss:Draw_0@1` (`scr_roommenu_debug` undefined); Ch3–5
  `scr_rhythmgame_notechart` Numpad+ chart-from-file.
- **Save corruption / plot reset**: Home+2/Home+3 in `obj_mainchara:Step_0` (`plot = 0/7`); Ch4
  `obj_dw_church_worshiproom:Create_0@98`, `obj_dw_churchb_rotatingtower:Create_0@12`,
  `obj_dw_churchb_staircaseintro:Create_0@40` (held key on entry → `plot = 0`); Ch4
  `scr_plotforce`; the unconditional STARTSTATE writers (Ch5 susiechase/LWF01/LW11A, Ch4
  DCA08C/angelprophecy/prophecies/climbstartertrig/castle_tv_zone); SETPARTY ×84; Ch3
  `obj_readable_room1:Other_10@2025` (weapon 26 removed); save-anywhere (S) mid-cutscene.
- **Softlock**: Ch5 `obj_dbulletcontroller:Step_0@1415,1460` (infinite turn timer); Ch2
  `obj_ch2_sceneex2_special:Step_0@3` (`interact = 1`); every SKIPKEY `con` jump; Insert/Delete
  room stepping; Ch4 `obj_make_fountain` (hold Space restarts the room).
- **Boot change**: `obj_initializer2:Step_0@41` `menu_go = 3` (dev title placeholder) when debug is
  already on at boot.

## Is `global.debug = 1` viable?

No, not as a toggle offered to players:

- **Ch2**: no effect. `scr_debug()` is `return 0;`. Enabling Ch2 branches requires replacing
  `gml_GlobalScript_scr_debug` (LooseMods can override the code entry). Ch2's battle helpers are
  partly stubs anyway (`scr_weaken_enemies`, `scr_turn_skip`).
- **Ch3**: likely fatal in board rooms (see Crash above).
- **Ch3–5**: about 140 room-entry branches (SETPARTY + STARTSTATE) mutate party, plot and flags
  without any input. Dozens of bare-letter hotkeys go live (S save, L load, R restart, M mute,
  D dialogue box, V/B toggles, P/W/1/2 at room entry), and some reset the plot.
- **The stub keycheck blocks only part of it.** It disables exactly the battle F-keys, V
  turn-skip and the two Justice V skips (`scr_debug_keycheck`: 41 call sites in Ch2–5). The rest of the
  dev hotkeys use `keyboard_check*` or the real `sunkus_kb_*` wrappers and **would** fire.
  Stubbed helpers (`scr_84_debug`, `scr_debug_fullheal`, `obj_debugProfiler`, `obj_debug_xy`,
  the quicksave `quicksaved == 22/99` gates) mean that the dev menu, full-heal, profiler and
  quicksave do not come back.

A narrow, deliberate use is workable: set `global.debug = 1` for a single frame or event around
one known-safe branch, then restore it. Calling the target scripts directly is simpler and safer.

## Open questions

1. Confirm in-game that reading `global.chemg_show_room` in Ch3 board rooms with debug on is a
   fatal error (expected: "global variable not set before reading it"). An existing error handler
   could turn it into a logged error instead.
2. Is `scr_roommenu_debug` present as a function asset in Ch5 `data.win` (with a stripped or empty
   code entry), or genuinely missing? Check the FUNC/SCPT chunks.
3. Asset index of the `scr_debug` script per chapter. The bare-`scr_debug` sites (Ch3
   `obj_npc_PAspeaker`, `obj_b1store`, `obj_b2bridgepuzzle2`) are release-reachable only if it is
   non-zero. Verify with W+A next to the Ch3 PA speaker.
4. Do `room_battletest` / `room_bullettest` (Home+8/9) still exist in release `data.win`? The code
   still references them (Ch5: 6 and 2 files).
5. What is trophy `UnknownEnum.Value_0` queued by `obj_event_manager:Create_0@290` (Ch1–3) /
   `@301` (Ch4)? Would debug mode unlock a Steam achievement?
6. Does `scr_rhythmgame_load_chart_from_file` fail gracefully when the dev chart file is absent?
7. Remaining roadmap 32 bullets (not covered here): `global.encounterno` as an encounter toggle,
   and whether to offer `room_goto` warps. The DOGCHECK and Home/Insert findings above feed into
   the warp decision.
8. Follow-up: correct the `global.debug` row in `mods_examples/CheatMenu/RESEARCH.md` (Ch2 is
   hard-coded 0; the 136 figure is Ch5-only; 1091 sites total).

## Method (reproducible)

All counts come from a script over `RE/code/chapter{1..5}/*.gml`:
- Every `scr_debug()` occurrence is located. The enclosing `if` condition and its brace-matched
  body are extracted, and the 5 definitions are excluded. Result: 1091 sites.
- Stub helpers are detected as `gml_GlobalScript_*` functions whose body is empty or a constant
  `return`. A body is "stripped" when nothing remains after removing stub/print calls, `var`
  declarations and empty `if {}` blocks.
- Sites are grouped by ordered regex rules on file name, condition and body. The groups below are
  exhaustive: every site lands in exactly one, and the totals match the per-chapter count.
- Crash candidates come from cross-checking each branch's `global.X` reads against every
  `global.X =` assignment in the chapter, and its calls against the builtin manifest
  (`RE/gml_builtins_manifest.json`) plus all function definitions in the chapter.

## Group × chapter matrix

| group | ch1 | ch2 | ch3 | ch4 | ch5 | total |
|---|---|---|---|---|---|---|
| INV: Inverse guard (runs in release) | 2 | 1 | 3 | 18 | 4 | 28 |
| NOOP: Stripped body (no effect) | 5 | 31 | 47 | 72 | 20 | 175 |
| BOARDNAME: Ch3 board object-name label | 0 | 0 | 137 | 0 | 0 | 137 |
| STUBKEY: Battle F-key cheats (stub-gated) | 0 | 1 | 1 | 1 | 1 | 4 |
| NOTEXT: NPC missing-text placeholder | 0 | 5 | 5 | 6 | 5 | 21 |
| SETPARTY: Force party on room Create | 0 | 0 | 15 | 48 | 21 | 84 |
| STARTSTATE: Room-entry story setup | 0 | 0 | 11 | 37 | 5 | 53 |
| TROPHY: Achievement (trophy) bookkeeping | 1 | 1 | 1 | 2 | 0 | 5 |
| CUTCMD: Cutscene debug commands | 0 | 2 | 3 | 3 | 3 | 11 |
| FASTTEXT: Hold-C text automash | 1 | 1 | 2 | 1 | 1 | 6 |
| DOGCHECK: Disable dog check | 0 | 1 | 1 | 1 | 1 | 4 |
| HITBOX: Hitbox / trigger visibility | 0 | 5 | 7 | 23 | 7 | 42 |
| OWHOTKEY: Overworld save/load/restart keys | 0 | 1 | 3 | 3 | 4 | 11 |
| TIME: obj_time / boot debug | 1 | 0 | 6 | 7 | 8 | 22 |
| PLAYER: Player debug (noclip, warps) | 1 | 3 | 18 | 7 | 6 | 35 |
| SAVELOAD: scr_save / scr_load tweaks | 0 | 0 | 1 | 1 | 2 | 4 |
| MUSIC: Music / video debug | 0 | 2 | 3 | 6 | 7 | 18 |
| RHYTHM: Rhythm-game debug | 0 | 0 | 11 | 9 | 5 | 25 |
| RESTART: Per-room restart key | 0 | 1 | 11 | 7 | 7 | 26 |
| ROOMKEY: Room jump / gold keys | 0 | 0 | 4 | 8 | 1 | 13 |
| ENEMYKEY: Boss debug keys | 1 | 2 | 8 | 8 | 8 | 27 |
| ENEMYDBG: Enemy/minigame debug | 1 | 20 | 25 | 20 | 11 | 77 |
| SKIPKEY: Cutscene/state skip keys | 1 | 1 | 14 | 28 | 5 | 49 |
| FLAGKEY: Flag-toggle keys | 0 | 1 | 4 | 6 | 3 | 14 |
| OVERLAY: Debug draw overlay | 1 | 2 | 19 | 30 | 9 | 61 |
| KEYMISC: Room-specific dev keys | 0 | 6 | 31 | 28 | 6 | 71 |
| MISC: Other unconditional tweaks | 0 | 9 | 18 | 35 | 6 | 68 |
| **total** | **15** | **96** | **409** | **415** | **156** | **1091** |

## Appendix: every call site by group

Format: `object:event@line` or `script (script)@line`. The group IDs are the ones used in the catalog table.

### Chapter 1

- **INV** (2): `obj_darkcastle_event:Step_0@39`, `obj_darkcastle_event:Step_0@67`
- **NOOP** (5): `obj_event_manager:Create_0@32`, `obj_event_manager:Create_0@162`, `obj_event_manager:Create_0@208`, `obj_event_manager:Create_0@233`, `obj_mainchara:Draw_0@95`
- **TROPHY** (1): `obj_event_manager:Create_0@290`
- **FASTTEXT** (1): `obj_writer:Draw_0@12`
- **TIME** (1): `obj_time:Step_1@159`
- **PLAYER** (1): `obj_mainchara:Draw_0@2`
- **ENEMYKEY** (1): `obj_king_body:Step_0@1`
- **ENEMYDBG** (1): `obj_elevatorcontroller:Step_0@1`
- **SKIPKEY** (1): `obj_starwalker_overworld:Step_0@32`
- **OVERLAY** (1): `obj_kingcutscene:Draw_0@4`

### Chapter 2

- **INV** (1): `obj_rouxls_enemy:Create_0@71`
- **NOOP** (31): `DEVICE_CONTACT_old:Step_0@904`, `obj_battlecontroller:Draw_64@1`, `obj_beatbulletroomexample:Draw_64@1`, `obj_ch2_sceneex2:Draw_0@31`, `obj_controller_mansion_traffic:Step_0@185`, `obj_event_manager:Create_0@32`, `obj_event_manager:Create_0@162`, `obj_event_manager:Create_0@208`, `obj_event_manager:Create_0@233`, `obj_initializer2:Step_0@96`, `obj_kris_fountain:Step_0@121`, `obj_legend_neo:Draw_0@1`, `obj_mainchara:Draw_0@101`, `obj_overworldc:Step_0@261`, `obj_pipis_enemy:Step_0@1`, `obj_queen_enemy:Step_0@1629`, `obj_rouxls_enemy:Draw_0@78`, `obj_shop_ch2_spamton:Draw_0@1`, `obj_sneo_wireheart:Collision_obj_yheart_shot@52`, `obj_sneo_wireheart_edit:Collision_obj_yheart_shot@52`, `obj_sneo_wireheart_old:Collision_obj_yheart_shot@52`, `obj_spamton_neo_enemy:Draw_0@1633`, `obj_spamton_neo_enemy:Step_0@2327`, `obj_tasque_manager_enemy:Step_0@380`, `obj_time:Draw_64@1`, `obj_time:Draw_77@1`, `obj_time:Step_0@1`, `obj_time:Step_1@20`, `obj_time:Step_1@135`, `obj_traffic_car:Step_0@152`, `obj_weirdroute_manipulator:Step_0@64`
- **STUBKEY** (1): `obj_battlecontroller:Step_0@1187`
- **NOTEXT** (5): `obj_npc_facing:Other_10@16`, `obj_npc_room:Other_10@16`, `obj_npc_room_animated:Other_10@11`, `obj_npc_sign:Other_10@11`, `obj_readable_room1:Other_10@16`
- **TROPHY** (1): `obj_event_manager:Create_0@290`
- **CUTCMD** (2): `c_instant (script)@3`, `c_saveload (script)@3`
- **FASTTEXT** (1): `obj_writer:Draw_0@13`
- **DOGCHECK** (1): `scr_dogcheck (script)@3`
- **HITBOX** (5): `obj_act_hitbox:Create_0@2`, `obj_bulletdeleter:Draw_0@1`, `obj_overworld_bulletarea:Create_0@1`, `obj_overworld_bulletarea:Create_0@26`, `obj_overworld_bulletarea:Step_0@30`
- **OWHOTKEY** (1): `obj_darkcontroller:Step_0@1723`
- **PLAYER** (3): `obj_caterpillarchara_free:Step_0@1`, `obj_mainchara:Draw_0@5`, `obj_mainchara:Step_0@733`
- **MUSIC** (2): `obj_rouxls_enemy_old_copy:Step_0@234`, `obj_sweet_enemy:Step_0@750`
- **RESTART** (1): `obj_controller_dininghall:Step_0@172`
- **ENEMYKEY** (2): `obj_rouxls_enemy:Draw_0@336`, `obj_tasque_manager_enemy:Other_24@330`
- **ENEMYDBG** (20): `obj_beatbullet_2:KeyPress_32@1`, `obj_berdlyb2_enemy:KeyPress_79@1`, `obj_berdlyb2_enemy:Step_0@681`, `obj_berdlyb_enemy:KeyPress_89@1`, `obj_berdlyb_enemy:Step_0@532`, `obj_controller_dininghall:Create_0@1`, `obj_controller_dw_mansion_potBalance:Step_0@48`, `obj_queen_enemy:KeyPress_79@1`, `obj_queen_enemy:Mouse_60@1`, `obj_queen_enemy:Mouse_61@1`, `obj_rouxls_enemy:Step_0@424`, `obj_rouxls_enemy_old_copy:Draw_0@56`, `obj_rouxls_enemy_old_copy:Draw_0@249`, `obj_spamton_enemy:Draw_0@149`, `obj_spamton_neo_enemy:Draw_0@511`, `obj_spamton_neo_enemy:Draw_0@521`, `obj_spamton_neo_enemy:Draw_0@1258`, `obj_spamton_neo_enemy:KeyPress_89@1`, `obj_spamton_neo_enemy:Mouse_60@1`, `obj_spamton_neo_enemy:Mouse_61@1`
- **SKIPKEY** (1): `obj_berdly_wire_release_example_2:Step_0@100`
- **FLAGKEY** (1): `obj_dialoguer:Draw_0@3`
- **OVERLAY** (2): `obj_musical_battle_end:Draw_0@280`, `obj_queenscreen_dance:Draw_0@9`
- **KEYMISC** (6): `obj_baseenemy:Step_0@1`, `obj_room_dw_city_postbaseball:Step_0@790`, `obj_sneo_faceattack:Step_0@249`, `obj_sprite_hunter:Step_0@1`, `obj_thrashmachine:Step_0@12`, `obj_thrashmachine_backup:Step_0@16`
- **MISC** (9): `o_boxingqueen:KeyPress_79@1`, `obj_ch2_sceneex2_special:Step_0@3`, `obj_fountainkris_ch2_sideb:Step_0@229`, `obj_queen_winedrop:Create_0@9`, `obj_queen_wineglass:Create_0@18`, `obj_room_dw_city_postbaseball:Step_0@176`, `obj_room_dw_city_postbaseball:Step_0@191`, `obj_room_dw_city_postbaseball:Step_0@428`, `obj_room_dw_city_postbaseball:Step_0@443`

### Chapter 3

- **INV** (3): `obj_ch3_GSB02:Step_0@7`, `obj_snd_maker:Step_0@1`, `obj_spritecomparer:Draw_0@1`
- **NOOP** (47): `showval (script)@15`, `obj_b2_bino_v2:Draw_0@1`, `obj_b2atlantis:Draw_0@1`, `obj_b2d_dungeonend:Step_0@206`, `obj_b3bfinale:Step_0@7`, `obj_b3bintro:Step_0@7`, `obj_b3broof1:Step_0@7`, `obj_b3broof2:Step_0@7`, `obj_b3broof3:Step_0@7`, `obj_b3broof4:Step_0@7`, `obj_b3bs_console:Draw_0@94`, `obj_b3bs_stealth:Draw_0@3`, `obj_b3undergroundleft:Draw_0@1`, `obj_b3undergroundright:Draw_0@1`, `obj_battlecontroller:Draw_64@1`, `obj_board_b3s_repeatintro:Create_0@9`, `obj_board_controller:KeyPress_79@1`, `obj_board_lawnmower:Step_0@4`, `obj_board_preshadowmantle:Step_0@127`, `obj_board_screenColorChanger:Step_0@11`, `obj_board_screen:Draw_0@3`, `obj_caterpillarchara:Step_2@1`, `obj_ch3_GSA02:Step_0@354`, `obj_ch3_gachapon:Draw_0@18`, `obj_ch3_gachaunknown:Step_0@155`, `obj_credits_ch3:Step_0@1`, `obj_dw_b3bs_zapper_a:Draw_0@3`, `obj_dw_puzzlecloset_1_consolesequence:Create_0@2`, `obj_dw_teevie_shuttahmaze:Draw_0@2`, `obj_dw_teevie_shuttahmaze:Step_0@183`, `obj_dw_teevie_watercooler:Draw_0@2`, `obj_dw_tv_starbgtile_old:Draw_0@3`, `obj_event_manager:Create_0@32`, `obj_event_manager:Create_0@163`, `obj_event_manager:Create_0@209`, `obj_event_manager:Create_0@234`, `obj_gameover_minigame:Draw_0@73`, `obj_knight_enemy:Step_0@37`, `obj_mainchara_board:Destroy_0@1`, `obj_mainchara_board:Step_0@3046`, `obj_rhythmgame:Step_0@940`, `obj_shadow_mantle_enemy:KeyPress_100@1`, `obj_shadow_mantle_enemy:KeyPress_101@1`, `obj_shadow_mantle_enemy:KeyPress_97@1`, `obj_shootout_controller:Draw_64@2`, `obj_tenna_snap:Step_0@1`, `obj_time:Step_0@1`
- **BOARDNAME** (137): `obj_b0entrance:Draw_0@1`, `obj_b1_finale:Draw_0@1`, `obj_b1_finalehalls:Draw_0@1`, `obj_b1boardend:Draw_0@1`, `obj_b1bonuszone:Draw_0@1`, `obj_b1cactusfield1:Draw_0@1`, `obj_b1cactusfield2:Draw_0@1`, `obj_b1controls:Draw_0@1`, `obj_b1enemy:Draw_0@1`, `obj_b1intro:Draw_0@1`, `obj_b1lancer:Draw_0@1`, `obj_b1mailroom:Draw_0@11`, `obj_b1mailroom_intro:Draw_0@1`, `obj_b1oasis:Draw_0@1`, `obj_b1power:Draw_0@1`, `obj_b1push2:Draw_0@1`, `obj_b1pushpyramid:Draw_0@1`, `obj_b1pyramid1:Draw_0@1`, `obj_b1pyramid2:Draw_0@1`, `obj_b1pyramid_rouxlsjoke:Draw_0@1`, `obj_b1quiz:Draw_0@1`, `obj_b1rocks1:Draw_0@1`, `obj_b1spring:Draw_0@1`, `obj_b1susiedig:Draw_0@1`, `obj_b1swordconnector:Draw_0@1`, `obj_b2_antlionroom:Draw_0@1`, `obj_b2_board_event_template:Draw_0@1`, `obj_b2_crabroom:Draw_0@1`, `obj_b2_ice_a1:Draw_0@1`, `obj_b2_ice_a2:Draw_0@1`, `obj_b2_ice_b1:Draw_0@1`, `obj_b2_ice_b2:Draw_0@1`, `obj_b2_ice_c1:Draw_0@1`, `obj_b2_ice_c2:Draw_0@1`, `obj_b2_ice_d1:Draw_0@1`, `obj_b2_ice_d2:Draw_0@1`, `obj_b2_ice_e1:Draw_0@1`, `obj_b2_ice_e2:Draw_0@1`, `obj_b2_icedoor:Draw_0@1`, `obj_b2_moneyhole:Draw_0@1`, `obj_b2_southwaterarea:Draw_0@1`, `obj_b2atlantisentrance:Draw_0@1`, `obj_b2atlantispreview:Draw_0@1`, `obj_b2bombfun2:Draw_0@1`, `obj_b2bombfun:Draw_0@1`, `obj_b2bossencounterroom:Draw_0@1`, `obj_b2bridgeintro:Draw_0@1`, `obj_b2bridgepuzzle1:Draw_0@1`, `obj_b2bridgepuzzle2_alt:Draw_0@1`, `obj_b2camera:Draw_0@1`, `obj_b2d_dungeoncorner:Draw_0@1`, `obj_b2d_firstencounter:Draw_0@1`, `obj_b2d_firstswitches:Draw_0@1`, `obj_b2d_intro:Draw_0@1`, `obj_b2d_mazeescape:Draw_0@1`, `obj_b2d_mazemanager:Draw_0@1`, `obj_b2d_threefreeze:Draw_0@1`, `obj_b2d_treefreeze:Draw_0@1`, `obj_b2danger1:Draw_0@1`, `obj_b2enemy1:Draw_0@1`, `obj_b2enemyninfriendo:Draw_0@1`, `obj_b2enemysquabble:Draw_0@1`, `obj_b2enrichmentenclosure:Draw_0@1`, `obj_b2fashionshop1:Draw_0@1`, `obj_b2fashionshop2:Draw_0@1`, `obj_b2ferntalk:Draw_0@1`, `obj_b2greenx:Draw_0@1`, `obj_b2heartisland:Draw_0@1`, `obj_b2iceregion:Draw_0@1`, `obj_b2intro:Draw_0@1`, `obj_b2lancersalsa:Draw_0@1`, `obj_b2loverbridge:Draw_0@1`, `obj_b2loverscliff:Draw_0@1`, `obj_b2lovershallway:Draw_0@1`, `obj_b2mowdock:Draw_0@1`, `obj_b2ninfriendotop:Draw_0@1`, `obj_b2northcross:Draw_0@1`, `obj_b2pantheonentrance:Draw_0@1`, `obj_b2photohint:Draw_0@1`, `obj_b2pippinsisland:Draw_0@1`, `obj_b2pyramidrouxlsstore:Draw_0@1`, `obj_b2quiz2:Draw_0@1`, `obj_b2raftget:Draw_0@1`, `obj_b2raftintro:Draw_0@1`, `obj_b2ralseichoose:Draw_0@1`, `obj_b2riversouth:Draw_0@1`, `obj_b2s_heartisland:Draw_0@1`, `obj_b2s_northernlightsroom:Draw_0@1`, `obj_b2s_swordroom:Draw_0@1`, `obj_b2s_tennaentrance:Draw_0@1`, `obj_b2s_tennamonologue:Draw_0@1`, `obj_b2sadislandprev:Draw_0@1`, `obj_b2savelancer:Draw_0@1`, `obj_b2southwest:Draw_0@1`, `obj_b2sphinx2:Draw_0@1`, `obj_b2sphinxdock:Draw_0@1`, `obj_b2sphinxvoucher:Draw_0@1`, `obj_b2tennaleave1:Draw_0@1`, `obj_b2waterfall:Draw_0@1`, `obj_b2westpip:Draw_0@1`, `obj_b2westshop:Draw_0@1`, `obj_b3_board_event_template:Draw_0@1`, `obj_b3alley:Draw_0@1`, `obj_b3b_board_event_template:Draw_0@1`, `obj_b3belevator:Draw_0@1`, `obj_b3belevatorexit:Draw_0@1`, `obj_b3belevatorroom:Draw_0@1`, `obj_b3bfinale:Draw_0@1`, `obj_b3bintro:Draw_0@1`, `obj_b3bridge:Draw_0@1`, `obj_b3broof1:Draw_0@1`, `obj_b3broof2:Draw_0@1`, `obj_b3broof3:Draw_0@1`, `obj_b3broof4:Draw_0@1`, `obj_b3cactusfield:Draw_0@1`, `obj_b3camerashop:Draw_0@1`, `obj_b3center:Draw_0@1`, `obj_b3danger1:Draw_0@1`, `obj_b3danger2:Draw_0@1`, `obj_b3desert:Draw_0@1`, `obj_b3dock1:Draw_0@1`, `obj_b3dock2:Draw_0@1`, `obj_b3elevator:Draw_0@1`, `obj_b3encountera:Draw_0@1`, `obj_b3entertainmentalley:Draw_0@1`, `obj_b3entertainmentdistrict:Draw_0@1`, `obj_b3gameshow:Draw_0@1`, `obj_b3grassjoke:Draw_0@1`, `obj_b3intro:Draw_0@1`, `obj_b3lobby:Draw_0@1`, `obj_b3quiza:Draw_0@1`, `obj_b3quizb:Draw_0@1`, `obj_b3stairs:Draw_0@1`, `obj_b3underground:Draw_0@1`, `obj_board_event_sphinx_tenna:Draw_0@7`, `obj_bpush1:Draw_0@1`, `obj_dungeon3_tennataps:Draw_0@1`
- **STUBKEY** (1): `obj_battlecontroller:Step_0@1530`
- **NOTEXT** (5): `obj_npc_facing:Other_10@16`, `obj_npc_room:Other_10@17`, `obj_npc_room_animated:Other_10@11`, `obj_npc_sign:Other_10@11`, `obj_readable_room1:Other_10@17`
- **SETPARTY** (15): `obj_b3bs_zapper_b:Create_0@1`, `obj_b3bs_zapper_c:Create_0@8`, `obj_ch3_GSB02:Create_0@22`, `obj_ch3_gameshow:Create_0@1`, `obj_ch3_man_controller:Create_0@2`, `obj_dw_b3bs_extrapuzzle:Create_0@1`, `obj_dw_b3bs_mysterypuzzle:Create_0@1`, `obj_dw_b3bs_zapper_a:Create_0@1`, `obj_dw_teevie_maze:Create_0@1`, `obj_dw_teevie_shuttahmaze:Create_0@2`, `obj_dw_teevie_stealth_d:Create_0@1`, `obj_dw_teevie_susiezilla:Create_0@1`, `obj_gameshow_swordroute:Create_0@11`, `obj_puzzlecloset_manager:Create_0@2`, `obj_room_teevie_large_01:Create_0@1`
- **STARTSTATE** (11): `obj_ch3_BTB02:Create_0@1`, `obj_ch3_BTB03:Create_0@2`, `obj_ch3_GSA06:Create_0@6`, `obj_ch3_GSB05:Create_0@1`, `obj_ch3_GSD03:Create_0@9`, `obj_dw_b3bs_idcardpuzzle_pond:Create_0@1`, `obj_dw_b3bs_idcardpuzzle_pond:Create_0@46`, `obj_dw_ch3_puzzlecloset0_introsequence:Create_0@22`, `obj_puzzlecloset_manager:Create_0@111`, `obj_room_teevie_large_02:Create_0@1`, `obj_swordroute_event_leavescreen:Create_0@43`
- **TROPHY** (1): `obj_event_manager:Create_0@291`
- **CUTCMD** (3): `c_instant (script)@3`, `c_saveload (script)@3`, `scr_cutscene_saveload_extra (script)@3`
- **FASTTEXT** (2): `obj_writer:Draw_0@15`, `obj_writer_quiz:Other_10@13`
- **DOGCHECK** (1): `scr_dogcheck (script)@3`
- **HITBOX** (7): `obj_board_dungeon_3_shelter:Step_0@1`, `obj_bulletdeleter:Draw_0@1`, `obj_ch3_GSB02:Step_0@18`, `obj_ch3_gameshow:Step_0@236`, `obj_overworld_bulletarea:Create_0@1`, `obj_overworld_bulletarea:Create_0@26`, `obj_overworld_bulletarea:Step_0@30`
- **OWHOTKEY** (3): `obj_darkcontroller:Step_0@1733`, `obj_overworldc:Step_0@261`, `obj_rhythmgame_editor:Step_0@112`
- **TIME** (6): `obj_initializer2:Step_0@41`, `obj_initializer2:Step_0@74`, `obj_time:Draw_64@1`, `obj_time:Draw_77@1`, `obj_time:Step_1@21`, `obj_time:Step_1@258`
- **PLAYER** (18): `obj_mainchara:Draw_0@5`, `obj_mainchara:Draw_0@119`, `obj_mainchara:Step_0@1`, `obj_mainchara:Step_0@319`, `obj_mainchara:Step_0@328`, `obj_mainchara:Step_0@992`, `obj_mainchara_board:Draw_0@151`, `obj_mainchara_board:Draw_0@222`, `obj_mainchara_board:KeyPress_32@1`, `obj_mainchara_board_OLD:Draw_0@7`, `obj_mainchara_board:Step_0@138`, `obj_mainchara_board:Step_0@481`, `obj_mainchara_board:Step_0@2006`, `obj_mainchara_board:Step_0@2015`, `obj_mainchara_board:Step_0@2626`, `obj_mainchara_board:Step_0@2909`, `obj_mainchara_board:Step_0@2989`, `obj_mainchara_board:Step_0@3042`
- **SAVELOAD** (1): `scr_load (script)@370`
- **MUSIC** (3): `scr_music_mute (script)@3`, `obj_rhythmgame:Step_0@424`, `obj_rhythmgame_tenna_tester:Step_0@23`
- **RHYTHM** (11): `scr_rhythmgame_song_load (script)@23`, `obj_rhythmgame:Create_0@145`, `obj_rhythmgame:Draw_0@539`, `obj_rhythmgame:Draw_0@550`, `obj_rhythmgame:Draw_0@568`, `obj_rhythmgame:Step_0@61`, `obj_rhythmgame:Step_0@145`, `obj_rhythmgame:Step_0@218`, `obj_rhythmgame:Step_0@606`, `obj_rhythmgame:Step_0@1015`, `obj_rhythmgame:Step_0@1196`
- **RESTART** (11): `DEVICE_FAILURE:Step_0@464`, `DEVICE_MENU:Step_0@1`, `DEVICE_NAMER:Draw_0@174`, `obj_ch3_BTB05_screenbloom:Draw_0@3`, `obj_ch3_PGS01D:Draw_0@49`, `obj_ch3_PGS01E_hallway:Step_0@1`, `obj_chefs_game:Step_0@241`, `obj_dogcheck2:Draw_0@1`, `obj_legend_neo:Draw_0@1`, `obj_shadowmantleExample:Step_0@11`, `obj_susiezilla_gamecontroller:Step_0@1`
- **ROOMKEY** (4): `DEVICE_CONTACT_old:Step_0@904`, `obj_chefs_game:Step_0@2`, `obj_dw_b3bs_extrapuzzle:Step_0@5`, `obj_dw_b3bs_mysterypuzzle:Step_0@1`
- **ENEMYKEY** (8): `obj_dbulletcontroller:Step_0@2354`, `obj_dbulletcontroller:Step_0@2358`, `obj_dbulletcontroller:Step_0@2362`, `obj_gameshow_swordroute:Step_0@15`, `obj_knight_enemy:Step_0@14`, `obj_tasque_manager_enemy:Other_24@330`, `obj_tasque_manager_enemy:Step_0@380`, `obj_tenna_zoom:Step_0@1`
- **ENEMYDBG** (25): `obj_battlecontroller:Draw_0@1330`, `obj_battlecontroller:Step_0@1565`, `obj_board_controller:Create_0@2`, `obj_board_controller:Draw_0@5`, `obj_board_controller:Draw_0@199`, `obj_knight_enemy:Other_10@6`, `obj_knight_enemy:Step_0@3`, `obj_knight_enemy:Step_0@44`, `obj_lightemup_controller:Step_0@39`, `obj_rouxls_annyoing_dog_controller:Create_0@29`, `obj_rouxls_ch3_enemy:Step_0@4`, `obj_rouxls_laser_pointer_controller:KeyPress_32@1`, `obj_shootout_controller:Other_10@1`, `obj_shootout_controller:Step_0@1`, `obj_shutta_enemy:Step_0@18`, `obj_shutta_enemy:Step_0@917`, `obj_susiezilla_controller:Draw_64@65`, `obj_susiezilla_controller:KeyPress_72@1`, `obj_susiezilla_gamecontroller:Step_0@5`, `obj_tenna_board4_enemy:Step_0@3`, `obj_tenna_enemy:KeyPress_49@1`, `obj_tenna_enemy:Step_0@3`, `obj_tenna_enemy:Step_0@17`, `obj_tenna_enemy_minigametext:Draw_0@30`, `obj_tennabattleconvo_controller:Step_0@79`
- **SKIPKEY** (14): `obj_bpush1:Step_0@77`, `obj_ch3_GSB02:Step_0@47`, `obj_ch3_PTB02_roaringknight:Step_0@9`, `obj_ch3_gameshow:Step_0@320`, `obj_chefs_game:Draw_0@1`, `obj_gameover_minigame:Draw_0@121`, `obj_quizsequence:Other_10@2`, `obj_quizsequence:Other_11@2`, `obj_quizsequence:Step_0@1`, `obj_quizsequence:Step_0@1469`, `obj_round_evaluation:Draw_0@1858`, `obj_screen_channel_change_backup_2:Draw_0@1`, `obj_screen_channel_change_backup:Draw_0@17`, `obj_shutta_nobyact_shutta:Step_0@45`
- **FLAGKEY** (4): `obj_b2pantheonentrance:Step_0@145`, `obj_ch3_gameshow:Step_0@348`, `obj_chefs_game:Step_0@20`, `obj_dialoguer:Draw_0@3`
- **OVERLAY** (19): `scr_board_objname (script)@3`, `showval (script)@3`, `obj_board_shadowspotlight:Draw_0@1`, `obj_ch3_GSB02:Draw_0@1`, `obj_ch3_couch_video:Draw_0@38`, `obj_chaseenemy:Draw_0@75`, `obj_circlezoom:Draw_0@5`, `obj_dw_teevie_cameras_cheer:Draw_0@8`, `obj_dw_teevie_shuttahmaze:Draw_0@7`, `obj_dw_tv_funometer:Draw_0@4`, `obj_dw_tv_funometer:Draw_0@146`, `obj_dw_tv_funometer_old:Draw_0@32`, `obj_dw_tv_starbgtile:Draw_0@3`, `obj_gameshow_swordroute:Draw_0@39`, `obj_room_ranking_b:Draw_0@14`, `obj_round_evaluation:Draw_0@1899`, `obj_screen_channel_change_tester:Draw_0@1`, `obj_shadowman_sharpshoot_heart:Draw_0@1`, `obj_treasure_room:Draw_0@2`
- **KEYMISC** (31): `scr_quizsetup (script)@76`, `obj_b1push2:Step_0@323`, `obj_b1susiedig:Step_0@56`, `obj_b2bridgepuzzle1:Step_0@493`, `obj_b2d_intro:Create_0@9`, `obj_b2s_swordmanager:Create_0@8`, `obj_b3s_swordmanager:Create_0@8`, `obj_board_oasis:Step_0@1`, `obj_board_pickup:Step_0@921`, `obj_ch3_GSA01G:Step_0@939`, `obj_ch3_GSA04:Step_0@23`, `obj_ch3_GSB03:Step_0@25`, `obj_ch3_GSC05:Step_0@41`, `obj_ch3_dw_man_scenetransition_persistent:Step_0@1`, `obj_ch3_gameshow:Step_0@107`, `obj_ch3_gameshow:Step_0@341`, `obj_chaseenemy_board:Step_0@279`, `obj_chefs_game:Step_0@8`, `obj_chefs_game:Step_0@14`, `obj_dw_b3bs_mysterypuzzle:Step_0@25`, `obj_npc_PAspeaker:Step_0@67`, `obj_puzzlecloset_manager:Step_0@130`, `obj_quiz_thrashmachine:Step_0@9`, `obj_quizchaser:Step_0@200`, `obj_room_stage:Step_0@446`, `obj_sprite_hunter:Step_0@1`, `obj_sprite_zoom_example:Step_0@4`, `obj_susiezilla:Step_0@6`, `obj_susiezilla_mansion:Step_0@70`, `obj_susiezilla_statue:Step_0@88`, `obj_title_placeholder:Step_0@84`
- **MISC** (18): `scr_quizsetup (script)@4`, `scr_quizsetup (script)@44`, `obj___:KeyPress_105@1`, `obj_b1intro:Step_0@35`, `obj_b2_crabroom:Step_0@7`, `obj_b2bridgepuzzle1:Step_0@6`, `obj_b2d_dungeonend:Step_0@1`, `obj_b2d_mazemanager:Step_0@177`, `obj_b2fashionshop2:Create_0@44`, `obj_b2intro:Step_0@6`, `obj_ch3_GSB03:Create_0@7`, `obj_ch3_PTB02:Create_0@8`, `obj_ch3_PTB02:Step_0@1`, `obj_couchwriter:Step_0@48`, `obj_dw_ch3_b3bs_trashcan:Step_1@18`, `obj_dw_teevie_stealth_d:Create_0@100`, `obj_dw_teevie_watercooler:Create_0@1`, `obj_readable_room1:Other_10@2025`

### Chapter 4

- **INV** (18): `obj_andydebug:Create_0@1`, `obj_rotating_tower_controller:KeyPress_49@1`, `obj_rotating_tower_controller:KeyPress_50@1`, `obj_rotating_tower_controller:KeyPress_51@1`, `obj_rotating_tower_controller:KeyPress_52@1`, `obj_rotating_tower_controller:KeyPress_53@1`, `obj_rotating_tower_controller:KeyPress_54@1`, `obj_rotating_tower_controller:KeyPress_55@1`, `obj_rotating_tower_controller_new:KeyPress_53@1`, `obj_rotating_tower_controller_old:KeyPress_49@1`, `obj_rotating_tower_controller_old:KeyPress_50@1`, `obj_rotating_tower_controller_old:KeyPress_51@1`, `obj_rotating_tower_controller_old:KeyPress_52@1`, `obj_rotating_tower_controller_old:KeyPress_53@1`, `obj_rotating_tower_controller_old:KeyPress_54@1`, `obj_rotating_tower_controller_old:KeyPress_55@1`, `obj_snd_maker:Step_0@1`, `obj_tower_dog_controller:KeyPress_32@1`
- **NOOP** (72): `scr_flag_set (script)@4`, `showval (script)@12`, `obj_battlecontroller:Draw_64@1`, `obj_bookshelf_moveable2x2:Step_0@50`, `obj_ch4_DCC02:Create_0@11`, `obj_church_piano_playable:Draw_0@69`, `obj_church_secretpiano_playable:Draw_0@61`, `obj_climb_kris:Draw_0@102`, `obj_cloud_render:Draw_0@147`, `obj_cloud_render_new:Draw_0@156`, `obj_credits_2:Step_0@6`, `obj_dw_church_moneyfountain:Draw_0@8`, `obj_dw_church_nwconnect:Create_0@7`, `obj_dw_church_nwconnect:Draw_0@1`, `obj_dw_church_organ_notes:Draw_0@104`, `obj_dw_church_organ_notes:Draw_0@107`, `obj_dw_church_quicktest:Create_0@6`, `obj_dw_church_quicktest:Draw_0@1`, `obj_dw_church_ripseq1:Draw_0@1`, `obj_dw_church_ripseq2:Draw_0@1`, `obj_dw_church_waterfallchaser:Draw_0@2`, `obj_dw_church_waterfallchaser_churchc:Draw_0@1`, `obj_dw_churchb_bellroom:Draw_0@8`, `obj_dw_churchb_bookshelf:Create_0@6`, `obj_dw_churchb_darkclimb:Draw_0@1`, `obj_dw_churchb_escherstaircase:Create_0@2`, `obj_dw_churchb_fireplace:Draw_0@1`, `obj_dw_churchb_gallery:Draw_0@1`, `obj_dw_churchb_gersonchase:Draw_0@1`, `obj_dw_churchb_man:Create_0@13`, `obj_dw_churchb_prophecyencounter:Step_0@1`, `obj_dw_churchb_ripple1:Draw_0@1`, `obj_dw_churchb_rotatingtower:Draw_0@1`, `obj_dw_churchb_savepoint:Create_0@11`, `obj_dw_churchb_staircaseintro:Create_0@94`, `obj_dw_churchb_staircaseintro:Draw_0@1`, `obj_dw_churchb_windows:Draw_0@1`, `obj_dw_churchc_:Create_0@6`, `obj_dw_churchc_:Draw_0@1`, `obj_dw_churchc_darkswords:Create_0@141`, `obj_dw_churchc_dodge:Create_0@6`, `obj_dw_churchc_encounter1:Create_0@20`, `obj_dw_churchc_prepretitan:Create_0@11`, `obj_dw_churchc_pretitan:Create_0@6`, `obj_dw_churchc_prophecies:Draw_0@1`, `obj_dw_churchc_ripplesneak_poc:Create_0@6`, `obj_dw_churchc_savepoint:Create_0@10`, `obj_dw_churchc_savepoint:Draw_0@1`, `obj_dw_churchc_titanclimb1:Create_0@14`, `obj_dw_churchc_titanclimb2:Create_0@6`, `obj_dw_churchc_titandefeated:Create_0@6`, `obj_event_manager:Create_0@34`, `obj_event_manager:Create_0@168`, `obj_event_manager:Create_0@244`, `obj_floorSwitcher:Draw_64@1`, `obj_gameover_minigame:Draw_0@58`, `obj_gameover_minigame_ch4:Draw_0@27`, `obj_homealone_heart:Step_0@730`, `obj_legend_cutscene_base:Alarm_2@2`, `obj_legend_cutscene_base:Alarm_3@2`, `obj_mainchara:Draw_0@192`, `obj_microphone_test:Create_0@6`, `obj_mike_controller:Create_0@166`, `obj_rhythmgame:Step_0@1002`, `obj_roomManager_Template:Create_0@6`, `obj_roomManager_Template:Draw_0@1`, `obj_room_castle_tv_zone_battle:Step_0@250`, `obj_roomglow:Step_0@121`, `obj_text_spikeballoon:Draw_0@4`, `obj_time:Step_0@1`, `obj_titan_star_dissolve:Draw_0@73`, `obj_titan_star_dissolve_simple:Draw_0@73`
- **STUBKEY** (1): `obj_battlecontroller:Step_0@1371`
- **NOTEXT** (6): `obj_npc_facing:Other_10@16`, `obj_npc_jackenstein:Other_10@16`, `obj_npc_room:Other_10@16`, `obj_npc_room_animated:Other_10@11`, `obj_npc_sign:Other_10@11`, `obj_readable_room1:Other_10@17`
- **SETPARTY** (48): `obj_ch4_DCA04:Create_0@34`, `obj_ch4_DCA05:Create_0@4`, `obj_ch4_DCA09:Create_0@28`, `obj_ch4_DCB03:Create_0@12`, `obj_dw_church_biblioxencounter:Create_0@1`, `obj_dw_church_darkclimb:Create_0@4`, `obj_dw_church_darkmaze:Create_0@37`, `obj_dw_church_holywatercooler:Create_0@3`, `obj_dw_church_intro_gerson:Create_0@6`, `obj_dw_church_knightclimb:Create_0@22`, `obj_dw_church_lantern_hallway:Create_0@4`, `obj_dw_church_minorlegend:Create_0@5`, `obj_dw_church_mizzleencounter:Create_0@2`, `obj_dw_church_moneyfountain:Create_0@3`, `obj_dw_church_northprophecies:Create_0@2`, `obj_dw_church_pianopiece_left:Create_0@1`, `obj_dw_church_pianopiece_left_b:Create_0@62`, `obj_dw_church_pianopiece_rightprophecy:Create_0@2`, `obj_dw_church_pianopuzzle:Create_0@1`, `obj_dw_church_ripplepuzzle_postgers:Create_0@49`, `obj_dw_church_ripseq1:Create_0@7`, `obj_dw_church_ripseq2:Create_0@7`, `obj_dw_church_savepoint:Create_0@1`, `obj_dw_church_secretpiano:Create_0@1`, `obj_dw_church_shadowgerson:Create_0@3`, `obj_dw_church_stairspreview:Create_0@2`, `obj_dw_church_tallbookcases:Create_0@3`, `obj_dw_church_trueclimbadventure:Create_0@7`, `obj_dw_church_waterfallroom:Create_0@1`, `obj_dw_churchb_bellroom:Create_0@24`, `obj_dw_churchb_darkclimb:Create_0@15`, `obj_dw_churchb_darkclimb_scene:Create_0@1`, `obj_dw_churchb_escherstaircase:Create_0@24`, `obj_dw_churchb_extinguisher:Create_0@91`, `obj_dw_churchb_fireplace:Create_0@8`, `obj_dw_churchb_fountain:Create_0@11`, `obj_dw_churchb_gallery:Create_0@13`, `obj_dw_churchb_library:Create_0@9`, `obj_dw_churchb_libraryconnector:Create_0@1`, `obj_dw_churchb_prophecyencounter:Create_0@18`, `obj_dw_churchb_prophecymaze:Create_0@7`, `obj_dw_churchb_rotatingtower:Create_0@35`, `obj_dw_churchb_savepoint:Create_0@6`, `obj_dw_churchc_angelprophecy:Create_0@7`, `obj_dw_churchc_encounter2:Create_0@26`, `obj_dw_churchc_finalclimb:Create_0@9`, `obj_dw_churchc_slidingpiano:Create_0@6`, `obj_dw_churchc_superprophecies:Create_0@26`
- **STARTSTATE** (37): `obj_ch4_DCA04:Create_0@48`, `obj_ch4_DCA04:Create_0@95`, `obj_ch4_DCA04:Create_0@110`, `obj_ch4_DCA05:Create_0@80`, `obj_ch4_DCA08C:Create_0@1`, `obj_ch4_DCC01:Create_0@51`, `obj_ch4_PDC03:Create_0@79`, `obj_ch4_PDC10:Create_0@15`, `obj_climbstartertrig:Create_0@10`, `obj_dw_church_arena:Create_0@71`, `obj_dw_church_bookshelfpuzzle:Create_0@89`, `obj_dw_church_intro_guei:Create_0@8`, `obj_dw_church_intropiano:Create_0@5`, `obj_dw_church_knightclimbpost:Create_0@2`, `obj_dw_church_knightclimbpost:Create_0@25`, `obj_dw_church_organ:Create_0@4`, `obj_dw_church_organpuzzle:Create_0@2`, `obj_dw_church_pianopiece_left:Create_0@49`, `obj_dw_church_pianopiece_right:Create_0@1`, `obj_dw_church_ripplepuzzle_postgers:Create_0@2`, `obj_dw_church_ripplepuzzle_postgers:Create_0@54`, `obj_dw_church_sideclimb:Create_0@3`, `obj_dw_church_waterfalltearoom:Create_0@43`, `obj_dw_churchb_gersonstudy:Create_0@22`, `obj_dw_churchb_libraryconnector:Create_0@12`, `obj_dw_churchb_libraryconnector:Create_0@68`, `obj_dw_churchb_moneyfountain:Create_0@19`, `obj_dw_churchb_ripplepost:Create_0@4`, `obj_dw_churchb_rotatingtower2:Create_0@8`, `obj_dw_churchb_rotatingtower:Create_0@12`, `obj_dw_churchb_staircaseintro:Create_0@40`, `obj_dw_churchb_windows:Create_0@6`, `obj_dw_churchc_angelprophecy_encounter:Create_0@6`, `obj_dw_churchc_prophecies:Create_0@6`, `obj_dw_churchc_titanclimb2_post:Create_0@14`, `obj_dw_churchc_treasurechest:Create_0@7`, `obj_room_castle_tv_zone_battle:Create_0@37`
- **TROPHY** (2): `obj_event_manager:Create_0@214`, `obj_event_manager:Create_0@301`
- **CUTCMD** (3): `c_instant (script)@3`, `c_saveload (script)@3`, `scr_cutscene_saveload_extra (script)@3`
- **FASTTEXT** (1): `obj_writer:Draw_0@13`
- **DOGCHECK** (1): `scr_dogcheck (script)@3`
- **HITBOX** (23): `scr_makemarker (script)@36`, `obj_bulletdeleter:Draw_0@1`, `obj_church_piano_playable:Create_0@3`, `obj_church_secretpiano_playable:Create_0@9`, `obj_climb_climbable:Draw_0@1`, `obj_climb_evilgrowth:Draw_0@1`, `obj_climb_immobileeye:Draw_0@1`, `obj_climb_kris:Draw_0@6`, `obj_climb_spiketile:Draw_0@1`, `obj_climb_watergenerator:Draw_0@1`, `obj_climbloc:Draw_0@1`, `obj_dw_church_arena:Step_0@323`, `obj_dw_church_pushableshelf_3x3:Step_2@9`, `obj_dw_church_pushableshelf:Step_2@9`, `obj_dw_church_pushableshelf_darklight:Step_2@9`, `obj_overworld_bulletarea:Create_0@1`, `obj_overworld_bulletarea:Create_0@26`, `obj_overworld_bulletarea:Step_0@30`, `obj_overworld_bulletarea_no_dark:Create_0@1`, `obj_overworld_bulletarea_no_dark:Create_0@18`, `obj_overworld_bulletarea_no_dark:Step_0@30`, `obj_overworld_darkness_bulletarea:Step_0@34`, `obj_rotating_tower_controller_new:Step_0@64`
- **OWHOTKEY** (3): `obj_darkcontroller:Step_0@1748`, `obj_overworldc:Step_0@284`, `obj_rhythmgame_editor:Step_0@112`
- **TIME** (7): `console (script)@4`, `obj_initializer2:Step_0@41`, `obj_initializer2:Step_0@74`, `obj_time:Draw_64@1`, `obj_time:Draw_77@1`, `obj_time:Step_1@21`, `obj_time:Step_1@258`
- **PLAYER** (7): `obj_caterpillarchara:Step_2@1`, `obj_mainchara:Create_0@409`, `obj_mainchara:Draw_0@15`, `obj_mainchara:Draw_0@179`, `obj_mainchara:Step_0@20`, `obj_mainchara:Step_0@326`, `obj_mainchara:Step_0@870`
- **SAVELOAD** (1): `scr_load (script)@371`
- **MUSIC** (6): `scr_music_mute (script)@3`, `obj_ch4_DCA15:Create_0@3`, `obj_credits_ch4:Step_0@5`, `obj_dw_churchb_darkclimb:Step_0@46`, `obj_rhythmgame:Step_0@462`, `obj_rhythmgame_tenna_tester:Step_0@23`
- **RHYTHM** (9): `scr_rhythmgame_song_load (script)@23`, `obj_rhythmgame:Create_0@166`, `obj_rhythmgame:Draw_0@541`, `obj_rhythmgame:Draw_0@570`, `obj_rhythmgame:Step_0@99`, `obj_rhythmgame:Step_0@183`, `obj_rhythmgame:Step_0@646`, `obj_rhythmgame:Step_0@1077`, `obj_rhythmgame:Step_0@1259`
- **RESTART** (7): `DEVICE_NAMER:Draw_0@170`, `obj_ch4_PDC01A:Step_0@1`, `obj_church_statue_shatter:Step_0@1`, `obj_climbingarea:KeyPress_68@1`, `obj_dogcheck2:Draw_0@1`, `obj_legend_neo:Draw_0@1`, `obj_make_fountain:Step_0@121`
- **ROOMKEY** (8): `DEVICE_CONTACT_old:Step_0@904`, `obj_dw_church_moneyfountain:Step_0@2`, `obj_dw_church_worshiproom:Create_0@98`, `obj_dw_churchb_gersonstudy:Step_0@394`, `obj_dw_churchb_moneyfountain:Step_0@6`, `obj_dw_churchb_ripple1:Step_0@121`, `obj_dw_gerson_study:Step_0@1`, `obj_microphone_test:Step_0@1`
- **ENEMYKEY** (8): `obj_dbulletcontroller:Step_0@1514`, `obj_dbulletcontroller:Step_0@1518`, `obj_dbulletcontroller:Step_0@1522`, `obj_jackenstein_enemy:Step_0@9`, `obj_mike_attack_controller:Step_0@1`, `obj_tasque_manager_enemy:Other_24@330`, `obj_tasque_manager_enemy:Step_0@380`, `obj_titan_enemy:KeyPress_8@1`
- **ENEMYDBG** (20): `obj_battlecontroller:Draw_0@712`, `obj_dw_church_dark_maze_controller:Create_0@7`, `obj_elnina_lanino_rematch_controller:KeyPress_17@1`, `obj_elnina_lanino_rematch_controller:KeyPress_17@5`, `obj_hammer_of_justice_enemy:KeyPress_17@1`, `obj_hammer_of_justice_enemy:Mouse_60@1`, `obj_hammer_of_justice_enemy:Mouse_61@1`, `obj_jackenstein_enemy:KeyPress_32@1`, `obj_jackenstein_enemy:Mouse_60@1`, `obj_jackenstein_enemy:Mouse_61@1`, `obj_mike_attack_controller:Draw_0@1`, `obj_mike_controller:Step_1@14`, `obj_mike_minigame_controller:Draw_0@5`, `obj_pippins_enemy:Step_0@1`, `obj_rotating_tower_controller_new:Draw_0@15`, `obj_sound_of_justice_enemy:Mouse_60@1`, `obj_sound_of_justice_enemy:Mouse_61@1`, `obj_titan_enemy:Step_0@9`, `obj_titan_enemy:Step_0@16`, `obj_train_controller:Step_0@1`
- **SKIPKEY** (28): `obj_burgerpants_runaway:Step_0@29`, `obj_ch4_DCC00_cupstack:Draw_0@1`, `obj_church_secretpiano_playable:Step_0@169`, `obj_dw_church_arena_bg:Step_0@37`, `obj_dw_church_cupstack:Draw_0@6`, `obj_dw_church_darkmaze:Step_0@2`, `obj_dw_church_lantern_hallway:Step_0@1`, `obj_dw_church_pianopiece_left_b:Create_0@5`, `obj_dw_church_ripseq1:Step_0@1`, `obj_dw_church_watercooler:Step_0@25`, `obj_dw_church_waterfallroom:Step_0@17`, `obj_dw_churchb_darkclimb_scene:Step_0@307`, `obj_dw_churchb_extinguisher:Step_0@21`, `obj_dw_churchb_gersonchase:Create_0@21`, `obj_dw_churchb_nongerson:Step_0@23`, `obj_dw_churchb_nongerson:Step_0@29`, `obj_dw_churchb_ripple1:Step_0@105`, `obj_dw_churchb_rotatingtower:Step_0@41`, `obj_gameover_minigame:Draw_0@106`, `obj_gerson_windowbg:Draw_0@3`, `obj_gerson_windowbg:Other_24@3`, `obj_minigame_evaluation:Draw_0@1795`, `obj_roomglow:Step_0@21`, `obj_round_evaluation:Draw_0@1774`, `obj_titan_star_dissolve:Draw_0@56`, `obj_titan_star_dissolve:Draw_0@79`, `obj_titan_star_dissolve_simple:Draw_0@56`, `obj_titan_star_dissolve_simple:Draw_0@79`
- **FLAGKEY** (6): `obj_dialoguer:Draw_0@3`, `obj_dw_church_knightclimb:Step_0@6`, `obj_dw_church_pianopiece_left:Step_0@16`, `obj_dw_church_rippleworship:Step_0@5`, `obj_dw_church_sideclimb:Step_0@2`, `obj_dw_churchb_nongerson:Step_0@19`
- **OVERLAY** (30): `distance_to_object_bbox (script)@9`, `distance_to_object_bbox (script)@33`, `showval (script)@3`, `obj_blue_cat:Draw_0@2`, `obj_church_old_man_ripple:Draw_0@298`, `obj_darkness_overlay:Draw_0@1`, `obj_dw_bluebook:Draw_0@30`, `obj_dw_church_organikchaser:Draw_0@82`, `obj_dw_church_pushableshelf_3x3:Draw_0@2`, `obj_dw_church_staticlight_overworld:Draw_0@2`, `obj_dw_church_watercooler:Draw_0@18`, `obj_dw_churchb_darkclimb_scene:Draw_0@1`, `obj_dw_churchb_escherstaircase:Draw_0@1`, `obj_dw_churchb_extinguisher:Draw_0@1`, `obj_dw_churchb_gersonstudy:Draw_0@1`, `obj_dw_churchb_libraryconnector:Draw_0@1`, `obj_dw_churchb_man:Draw_0@1`, `obj_dw_churchb_prophecymaze:Draw_0@1`, `obj_encounter_incenseturtle:Draw_0@3`, `obj_ghosthouse_jackolantern:Draw_0@29`, `obj_ghosthouse_jackolantern_merciful:Draw_73@1`, `obj_gold_cat:Draw_0@7`, `obj_light_following:Draw_0@2`, `obj_microphone_test:Draw_0@3`, `obj_microphone_test:Draw_0@66`, `obj_minigame_evaluation:Draw_0@1836`, `obj_round_evaluation:Draw_0@1815`, `obj_screen_channel_change_tester:Draw_0@1`, `obj_traintrack_parent:Draw_0@2`, `obj_treasure_room:Draw_0@2`
- **KEYMISC** (28): `scr_raise_party (script)@5`, `obj_church_ripple_area_manager:Step_0@191`, `obj_church_statue_shatter:Step_0@8`, `obj_dw_church_holywatercooler:Step_0@5`, `obj_dw_church_intro_gerson:Step_0@96`, `obj_dw_church_mizzleencounter:Step_0@20`, `obj_dw_church_organ:Step_0@9`, `obj_dw_church_organ:Step_0@771`, `obj_dw_church_remotepianomove:Step_0@289`, `obj_dw_church_trueclimbadventure:Step_0@2`, `obj_dw_church_waterfallroom:Step_0@314`, `obj_dw_churchb_darkclimb_scene:Create_0@36`, `obj_dw_churchb_gallery:Create_0@42`, `obj_dw_churchb_gersonchase:Create_0@63`, `obj_dw_churchb_prophecymaze:Step_0@40`, `obj_dw_churchb_ripple1:Create_0@6`, `obj_dw_churchc_darkswords:Step_0@1`, `obj_dw_churchc_darkswords:Step_0@970`, `obj_dw_churchc_encounter2:Step_0@2`, `obj_dw_churchc_titanclimb1_post:Create_0@6`, `obj_ghosthouse_dot:Step_0@1`, `obj_ghosthouse_jackolantern_merciful:Step_0@20`, `obj_homealone_dess_closet_kris:Step_0@9`, `obj_lw_rain_effect:Step_0@1`, `obj_music_wobbler:Step_0@21`, `obj_pumpkin_yell_proof:Step_0@36`, `obj_roomglow:Step_0@62`, `obj_sprite_hunter:Step_0@1`
- **MISC** (35): `scr_monstersetup (script)@1910`, `scr_plotforce (script)@3`, `obj_ch4_DCA02:Create_0@42`, `obj_ch4_DCA04:Create_0@82`, `obj_ch4_DCA04:Step_0@202`, `obj_ch4_DCA08C:Create_0@74`, `obj_ch4_DCA09:Step_0@179`, `obj_ch4_PDC10A:Create_0@25`, `obj_ch4_PDC14B:Create_0@27`, `obj_ch4_PDC14C:Step_0@29`, `obj_ch4_PDC15:Create_0@23`, `obj_ch4_PDC16:Create_0@30`, `obj_church_statue_shatter:Create_0@10`, `obj_climb_climbable:Create_0@4`, `obj_dw_church_arena:Step_0@1199`, `obj_dw_church_arena:Step_0@1413`, `obj_dw_church_gerson_follow:Create_0@14`, `obj_dw_church_intro_guei:Create_0@60`, `obj_dw_church_intro_guei:Step_0@1`, `obj_dw_church_knightclimbpost:Step_0@23`, `obj_dw_church_prophecy:Create_0@35`, `obj_dw_church_ripseq2:Step_0@2`, `obj_dw_church_staircase:Create_0@69`, `obj_dw_churchb_fireplace:Create_0@27`, `obj_dw_churchb_fountain:Create_0@34`, `obj_dw_churchb_gersonstudy:Step_0@95`, `obj_dw_churchc_final_prophecy:Create_0@24`, `obj_event_manager:Create_0@38`, `obj_event_ralsei_runahead:Step_0@33`, `obj_noellehouse_basement:Step_0@104`, `obj_noellehouse_dess:Step_0@148`, `obj_readable_nubert:Other_10@16`, `obj_spearblocker_old:KeyPress_32@1`, `obj_treasure_room:Create_0@12`, `obj_treasure_room:Step_1@11`

### Chapter 5

- **INV** (4): `scr_debug_print (script)@3`, `obj_ch5_LW20:Create_0@53`, `obj_platswap_statue:Create_0@4`, `obj_snd_maker:Step_0@1`
- **NOOP** (20): `scr_cutscene_commands (script)@1279`, `showval (script)@12`, `obj_b3bs_stealth:Draw_0@3`, `obj_battlecontroller:Draw_64@1`, `obj_ch5_DWCL03:Step_0@527`, `obj_ch5_DWCR03:Step_0@1049`, `obj_ch5_LW20W_blur2:Draw_0@59`, `obj_dw_cliff_sethaqua_battle:Step_0@326`, `obj_dw_fcastle_seth_encounter:Step_0@105`, `obj_dw_pink_encounter:Draw_0@1`, `obj_dw_pink_encounter:Step_0@597`, `obj_flowery_enemy:Create_0@164`, `obj_gameover_minigame:Draw_0@58`, `obj_heart:Draw_0@101`, `obj_ow_shinobeetle:Draw_0@72`, `obj_roomManager_Template:Create_0@8`, `obj_roomManager_Template:Draw_0@1`, `obj_time:Step_0@1`, `obj_torigateAnimation:Create_0@8`, `obj_torigateAnimation:Draw_0@1`
- **STUBKEY** (1): `obj_battlecontroller:Step_0@1420`
- **NOTEXT** (5): `obj_npc_facing:Other_10@16`, `obj_npc_room:Other_10@16`, `obj_npc_room_animated:Other_10@11`, `obj_npc_sign:Other_10@11`, `obj_readable_room1:Other_10@16`
- **SETPARTY** (21): `obj_ch5_DW19B:Create_0@8`, `obj_ch5_DWCL00:Create_0@8`, `obj_ch5_DWCR01:Create_0@5`, `obj_ch5_DWCR03:Create_0@7`, `obj_ch5_DWCR04:Create_0@5`, `obj_ch5_DWCR_documents:Create_0@10`, `obj_ch5_LW02W:Create_0@3`, `obj_dw_cliff_gardentransition_new:Create_0@2`, `obj_dw_cliff_sethaqua_battle:Create_0@3`, `obj_dw_fcastle_entrance:Create_0@2`, `obj_dw_fcastle_onsen:Create_0@3`, `obj_dw_fcastle_post_party_jail:Create_0@13`, `obj_dw_fcastle_right_endingscene:Create_0@12`, `obj_dw_fcastle_yellowjail:Create_0@3`, `obj_dw_flowery_tree:Create_0@4`, `obj_dw_garden_cliffexit:Create_0@3`, `obj_dw_garden_exit:Create_0@2`, `obj_dw_garden_hardpressureplates:Create_0@3`, `obj_dw_garden_hospital:Create_0@2`, `obj_dw_pink_encounter:Create_0@16`, `obj_dw_post_fountain_close:Create_0@4`
- **STARTSTATE** (5): `obj_ch5_LW11A:Create_0@7`, `obj_ch5_LWF01:Create_0@92`, `obj_dw_fcastle_top_ascent:Create_0@28`, `obj_dw_garden_intro:Create_0@2`, `obj_dw_garden_susiechase:Create_0@20`
- **CUTCMD** (3): `c_instant (script)@3`, `c_saveload (script)@3`, `scr_cutscene_saveload_extra (script)@3`
- **FASTTEXT** (1): `obj_writer:Draw_0@17`
- **DOGCHECK** (1): `scr_dogcheck (script)@3`
- **HITBOX** (7): `scr_makenpc (script)@75`, `scr_makenpc (script)@112`, `obj_bulletdeleter:Draw_0@1`, `obj_overworld_bulletarea:Create_0@1`, `obj_overworld_bulletarea:Create_0@26`, `obj_overworld_bulletarea:Step_0@30`, `obj_plat_sequence_marker:Create_0@8`
- **OWHOTKEY** (4): `obj_darkcontroller:Step_0@1767`, `obj_overworldc:Step_0@339`, `obj_rhythmgame_editor:Step_0@1`, `obj_rhythmgame_editor:Step_0@143`
- **TIME** (8): `console (script)@4`, `obj_initializer2:Step_0@41`, `obj_initializer:Create_0@140`, `obj_time:Draw_0@1`, `obj_time:Draw_64@1`, `obj_time:Draw_77@1`, `obj_time:Step_1@21`, `obj_time:Step_1@258`
- **PLAYER** (6): `obj_mainchara:Create_0@475`, `obj_mainchara:Draw_0@54`, `obj_mainchara:Draw_0@165`, `obj_mainchara:Step_0@20`, `obj_mainchara:Step_0@369`, `obj_mainchara:Step_0@895`
- **SAVELOAD** (2): `scr_load (script)@358`, `scr_save (script)@3`
- **MUSIC** (7): `scr_music_mute (script)@3`, `obj_ch5_LW21_window:Create_0@3`, `obj_credits_ch5_audio:Step_0@5`, `obj_flowery_lyrics:Draw_0@33`, `obj_flowery_lyrics:Draw_0@51`, `obj_room_garden_video:Step_0@1`, `obj_town_south_strength:Create_0@1`
- **RHYTHM** (5): `scr_rhythmgame_notechart (script)@12`, `scr_rhythmgame_notechart (script)@2841`, `scr_rhythmgame_notechart (script)@4666`, `scr_rhythmgame_song_load (script)@31`, `obj_rhythmgame:Step_0@1079`
- **RESTART** (7): `DEVICE_NAMER:Draw_0@170`, `obj_circlebg:Draw_0@2`, `obj_cuttableflower_example:Step_0@1`, `obj_dogcheck_ch5:Step_0@1`, `obj_intro_ch5:Step_0@5`, `obj_legend_neo:Draw_0@1`, `obj_torigateAnimation:Step_0@1`
- **ROOMKEY** (1): `DEVICE_CONTACT_old:Step_0@904`
- **ENEMYKEY** (8): `obj_date_controller:Step_0@2151`, `obj_flowery_enemy:Step_0@78`, `obj_pink_enemy:Step_0@14`, `obj_pink_enemy:Step_0@23`, `obj_pink_enemy:Step_0@31`, `obj_tasque_manager_enemy:Other_24@330`, `obj_tasque_manager_enemy:Step_0@380`, `obj_trashy_trio:Step_0@2`
- **ENEMYDBG** (11): `obj_battlecontroller:Draw_0@764`, `obj_bullet_green_food:Step_0@1`, `obj_dbulletcontroller:Step_0@1415`, `obj_dbulletcontroller:Step_0@1460`, `obj_enemy_aqua_example:Draw_0@207`, `obj_enemy_blue_example:Draw_0@132`, `obj_enemy_green_example:Draw_0@9`, `obj_enemy_orange_example:Draw_0@317`, `obj_flowery_enemy:Draw_0@146`, `obj_flowery_enemy:Draw_0@156`, `obj_orange_green_controller:Step_0@99`
- **SKIPKEY** (5): `obj_ch5_DWCL03:Step_0@43`, `obj_ch5_DWCR02:Step_0@1`, `obj_dw_garden_enemyrush:Step_0@29`, `obj_gameover_minigame:Draw_0@106`, `obj_round_evaluation:Draw_0@1767`
- **FLAGKEY** (3): `obj_ch5_DWCL00:Step_0@20`, `obj_ch5_DWCL00:Step_0@25`, `obj_dialoguer:Draw_0@3`
- **OVERLAY** (9): `showval (script)@3`, `obj_darkness_overlay:Draw_0@1`, `obj_date_ui:Draw_0@221`, `obj_dw_fcastle_yellow_miniboss:Draw_0@1`, `obj_orange_puppet:Other_21@25`, `obj_plat_follower:Draw_0@137`, `obj_round_evaluation:Draw_0@1808`, `obj_towery_orbiter:Draw_0@3`, `obj_treasure_room_altsolid:Draw_0@2`
- **KEYMISC** (6): `scr_raise_party (script)@5`, `obj_dw_garden_susiechase:Create_0@175`, `obj_flowery_towery_old:Step_0@32`, `obj_lw_rain_effect:Step_0@1`, `obj_torigateAnimation:Step_0@8`, `obj_trashy_beam:Step_0@1`
- **MISC** (6): `button4_h (script)@8`, `button4_p (script)@8`, `obj_ch5_LW20W_end:Step_0@41`, `obj_ch5_LW21_arrive:Create_0@27`, `obj_ch5_LW21_arrive:Step_0@52`, `obj_treasure_room_altsolid:Create_0@12`
