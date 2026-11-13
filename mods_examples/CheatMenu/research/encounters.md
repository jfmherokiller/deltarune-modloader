# CheatMenu research: encounter toggle (`global.encounterno`)

ROADMAP task 32, second bullet. This is read-only research against the decompiled code in
`RE/code/chapterN/` (gitignored, not shipped). Paths below are relative to `RE/code/chapterN/`,
and line numbers come from that dump. Recheck them after a game update.

## Verdict: no on `encounterno`; partial on an encounter toggle

- **`global.encounterno` cannot work as a toggle.** It does not gate whether a battle starts. It
  only says which battle `scr_encountersetup()` builds. Every trigger path writes it just before the
  battle starts, so a value the cheat sets would be overwritten. Chapter 3 even resets it to 0
  whenever a chaser is created.
- **No global "encounters off" switch exists.** The only shared gate is `global.interact`: contact
  counts only while it is 0 or 4. Holding it at another value freezes the player too, so it is not
  usable.
- **One chokepoint does exist, and a cheat can hook it safely.** In every chapter, a contact
  encounter on the overworld goes through the transition object `obj_testoverworldenemy`. It waits
  about 20 frames before it creates `obj_encounterbasic`. A Begin Step hook can cancel it in that
  window. This covers the normal roaming chasers (`obj_chaseenemy` and children). It does **not**
  cover the room-specific chasers that call `scr_battle()` directly, and those are left alone on
  purpose (see below).
- **Instant win is supported.** The developers' own debug key does it: F5 calls `scr_wincombat()`
  in `obj_battlecontroller_Step_0` (ch2:1198, ch3:1541, ch4:1382, ch5:1435). It is gated by
  `scr_debug()`, so it is dead in release builds.
- **There are no step-counter random encounters.** Every encounter comes from a placed or spawned
  object, or from a cutscene.

## 1. Where `encounterno` is written and read

Counts of `encounterno` matches: ch1 33, ch2 65, ch3 120, ch4 121, ch5 90. Every one falls into
one of these groups.

### Writers (all of them select a battle just before it starts)

| Writer | Where |
|---|---|
| New game default `= 1` | `gml_GlobalScript_scr_gamestart.gml:26` (ch1), `:27` (ch2–5) |
| Chaser contact (ch1) | `gml_Object_obj_chaseenemy_Other_10.gml:15` `global.encounterno = myencounter` |
| Chaser contact (ch2–5) | `gml_Object_obj_overworldenemy_parent_Other_10.gml:52` (same line in all four) |
| `scr_battle(id, …)` | `gml_GlobalScript_scr_battle.gml:4` `global.encounterno = arg0` (ch2–5). It is the entry point for scripted and room-specific battles: 15/22/20/14 caller objects in ch2/3/4/5 |
| Ch5 platforming contact | `gml_GlobalScript_scr_plat_encounter.gml:90,125`, `gml_Object_obj_encounterable_projectile_Other_10.gml:54` |
| Multi-wave battles | `gml_GlobalScript_scr_wincombat.gml:34` (ch2–5): `encounterno = flag[60]`, which loads the next wave |
| Scripted cutscenes | ch1 `obj_kingcutscene_Step_0:905`, `obj_jokerbattleevent_Step_0:302`, `obj_susieandlancer_event_Step_0:135`, `obj_lancerbattle2_event_Step_0:31`, `obj_hathyfightevent_Step_0:31`, `obj_tutorialbattleevent_Step_0:17`, …; ch2 `obj_ch2_scene9_Step_0:419`, `obj_berdly_encounter_setup_*`, `obj_ch2_scene21_loop_Step_0:469`, `obj_ch2_cyber01_Step_0:620`; ch5 `obj_dw_post_fountain_close_Step_0:450` (230) |
| Dojo / fusion menu | `gml_Object_obj_fusionmenu_Step_0.gml:628` (ch2/3), `:631` (ch4), `:616` (ch5) |
| Debug-only rooms | `obj_battletester_*` (`+/-` keys), `obj_bullettester*_Create_0` (`= 777`) |
| **Ch3 reset** | `gml_Object_obj_chaseenemy_Create_0.gml:101` `global.encounterno = 0` on every chaser create |

There are also many **instance-local** `encounterno` variables, for example ch4
`obj_dw_church_*_Create_0` (`encounterno = 150..191`) and the ch3 board `RoomCC_room_board_*`. Room
controllers keep these and pass them to `scr_battle()`. They are not the global.

### Readers (all of them branch on which battle is running)

`scr_monstersetup` (`== 7` and similar), `obj_battlecontroller_Create_0`/`Draw_0`/`Step_0`
(per-battle tweaks such as `!= 31` for the Ch1 Susie+Lancer fight, `== 123` Pippins, `== 222`
yellow trial), `obj_heroparent_Create_0`, `obj_testoverworldenemy_Other_10` (which battle music to
use), `obj_encounterbasic_Step_0:21` (`!= 84`, which skips the slash sound), plus per-enemy checks
(`obj_tasque_manager_enemy` `== 89`, `obj_netskie_enemy` `== 229`, and so on). **No reader ever
uses it to decide whether a battle happens.**

## 2. How encounters trigger

Every battle ends at `obj_encounterbasic` → `obj_battlecontroller`
(`gml_Object_obj_encounterbasic_Step_0.gml`: ch1:65, ch2:71, ch3:71, ch4:87, ch5:89). Only two
paths create `obj_encounterbasic` in ch2–5:

1. **`obj_testoverworldenemy_Step_0` when `con == 5`** (ch1:67, ch2:82, ch3:82, ch4:82, ch5:88).
   This is the shared contact path.
2. **`scr_battle()`** (`gml_GlobalScript_scr_battle.gml:18`), used by scripted and room-specific
   encounters.

Ch1 has no `scr_battle`, so its cutscenes create `obj_encounterbasic` themselves
(`obj_kingcutscene`, `obj_jokerbattleevent`, `obj_hathyfightevent`, …). Ch1 to Ch4 also have a
leftover debug object, `obj_checker_animtest`.

### The contact path (all chapters)

1. **Detection.**
   - Ch1: collision event `gml_Object_obj_chaseenemy_Collision_obj_mainchara.gml:1-4`
     (`if (global.interact == 0) event_user(0)`).
   - Ch2–5: the collision event is just `exit;`. Detection moved to End Step
     `gml_Object_obj_chaseenemy_Step_2.gml` (ch2:54-62, ch3:57-83, ch4:65-74, ch5:54-62). It uses
     `place_meeting`, a custom hitbox, or `largetouchregion` (ch4). It requires
     `global.interact == 0 || == 4`, and it sets `global.flag[54] = encounterflag` before calling
     `event_user(0)`.
2. **The chaser hands off.**
   - Ch1 `obj_chaseenemy_Other_10.gml` sets `flag[touchflag] = 1` (lines 1-4), creates an
     `obj_testoverworldenemy` and runs its `event_user(0)`, then sets `global.encounterno`,
     `instance_destroy()` and `global.interact = 2`. Because the transition object has already set
     `flag[9] = 1`, it then fades the field music (`snd_volume(currentsong[1], 0, 20)`, lines 15-21).
   - Ch2–5 `obj_overworldenemy_parent_Other_10.gml:1-54` does the same work: touchflag, ambush
     shake, `flag[55/56] = x/y`, a new `obj_testoverworldenemy`, `encounterno`, and
     `instance_destroy()`.
3. **The transition object** `obj_testoverworldenemy_Other_10` (ch1:1-10, ch4:1-55) sets
   `specialbattle = 0`, `flag[9] = 1` (battle music), `batmusic[0] = snd_init(...)`,
   `global.interact = 1` and `con = 1`.
4. **The delay** in `obj_testoverworldenemy_Step_0`: at `con 1` the tension horn plays and
   `alarm[4] = 8`. `Alarm_4` does `con++`. At `con 3` the horn plays again and `alarm[4] = 12`.
   At `con 5` it creates `obj_battleback` and `obj_encounterbasic`. That gives **about 20 frames
   between contact and battle creation**.

Other objects also create `obj_testoverworldenemy` and go down the same path: ch1
`obj_ponman_appear_Step_0` (Ponman ambush), ch5 `scr_plat_encounter`,
`obj_encounterable_projectile`, `obj_dw_garden_enemyrush`, `obj_dw_garden_floradinnencounter`
(through chasers) and `obj_terracotta_stationary_overworld`. Ch2–4 `obj_encounter_bullet_Step_0:30`
only destroys itself while a transition exists.

### Encounters that bypass the shared path (they call `scr_battle` directly)

- **Ch3:** the board chasers `obj_chaseenemy_board_Step_0:227-245` → `obj_gameshow_battlemanager`
  → `scr_battle` (`obj_gameshow_battlemanager_Step_0:35`). These award board points. Also
  `obj_dw_teevie_shuttahmaze`, `obj_dw_b3bs_rabbick_b`, `obj_room_teevie_chef`,
  `obj_room_green_room` and `obj_room_ranking_c`.
- **Ch4:** almost every church encounter has its own chaser, for example `obj_dw_church_organikchaser`
  (`Step_0:156,160`), `obj_dw_churchb_bellchaser_Step_2:129`, `obj_encounter_incenseturtle`,
  `obj_dw_bluebook*`, `obj_dw_church_tallbookcases`, `obj_dw_church_darkmaze:1523`,
  `obj_dw_churchb_gallery:865` and `obj_dw_churchb_prophecyencounter:314`.
- **Ch5:** `obj_overworld_scarecrow_Step_0:16`, `obj_overworld_kawkaw_Step_0:126` and
  `obj_ow_shinobeetle_Step_0:31`.

Hooking these needs one patch per object. They also share `scr_battle` and `obj_encounterbasic`
with the story bosses, so a hook at either of those two points cannot tell an optional fight from a
mandatory one. `global.flag[54]` (the encounter flag) does not help either: it is also set for
mandatory fights such as the ch4 `obj_dw_churchc_titanclimb*_post` battles.

## 3. Other gating state

| State | Effect |
|---|---|
| `global.interact` | Contact counts only at 0 or 4 (ch2–5 `obj_chaseenemy_Step_2`), or at 0 in ch1. Chasers freeze otherwise (`obj_chaseenemy_Step_0:27-41`, ch4). Not usable as a toggle, because it also freezes Kris |
| `global.flag[encounterflag]` (ch2–5) | The result is written back by `scr_monsterdefeat.gml:92-98` (ch4) as `flag[flag[54]] = flag[50]` (1 = violence, 2 = spare, 3 = pacify, 6 = frozen). `obj_chaseenemy_Step_0:1-23` (ch4) destroys the chaser when the flag is 1, or replaces it with a frozen NPC when it is 6 |
| `touchflag` (ch1 505-508, forest rooms) | Set **on contact**, before the battle (`obj_chaseenemy_Other_10:1-4`). It changes chaser behaviour, for example `chasetype = 3` in `room_forest_maze1` (`obj_chaseenemy_Create_0:112`) |
| `global.ambush` / `washit` | Decides who strikes first. `washit == 1` also lets the transition run while `interact != 0` (`obj_testoverworldenemy_Other_10:3`, ch4/5), and cutscenes use that to force a battle |
| `global.flag[60]` | Multi-wave battles. `scr_wincombat` loads the next wave instead of ending the battle |

## 4. Recommended implementation

### (a) "No encounters": cancel the shared contact transition

Hook the **Begin Step**. CheatMenu already appends to `gml_Object_obj_time_Step_1`, which is
`obj_time`'s Begin Step and is persistent. Begin Step runs before any normal Step, so the cancel
lands before `obj_testoverworldenemy_Step_0` reaches `con == 1`. The horn never plays, and it fires
well before `con 5`. The game's code is not patched.

```gml
// Begin Step (obj_time Step_1 append), every frame:
if (drc_noenc) {
    var _exempt = (drc_prev_interact != 0 && drc_prev_interact != 4)   // cutscene-forced
               || (global.chapter == 5 && (instance_exists(obj_dw_garden_enemyrush)
                                        || instance_exists(obj_dw_garden_floradinnencounter)));
    with (obj_testoverworldenemy) {
        if (con >= 1 && con < 5 && washit == 0 && !_exempt) {
            global.flag[54] = 0;            // else the next battle writes its result to this chaser's flag
            global.flag[9]  = other.drc_flag9;
            snd_free(global.batmusic[0]);   // stream loaded by Other_10; endbattle normally frees it
            if (global.chapter == 1) snd_volume(global.currentsong[1], 1, 10); // undo the ch1 fade
            global.interact = 0;
            instance_destroy();
        }
    }
}
// sample "last frame" state only while no encounter is in flight
if (!instance_exists(obj_testoverworldenemy) && !instance_exists(obj_battlecontroller)) {
    drc_prev_interact = global.interact;
    drc_flag9 = global.flag[9];
}
```

What the player sees: the touched enemy disappears and play goes on. The chaser has already
destroyed itself in its `Other_10`, so this can't be undone. Because `encounterflag` is never
written, the enemy returns the next time the room loads. Nothing permanent changes, which makes the
toggle safe to turn on and off.

**Why the exemptions are needed.** Cutscenes start story battles through this same path:

- ch1 `obj_cloverpuzzle_event_Step_0:108-119` creates a chaser, sets `global.interact = 0`, then
  calls `event_user(0)`. This is the Clover fight.
- ch5 `obj_dw_post_fountain_close_Step_0:446-461` uses `washit = 1` and encounter 230.
- ch5 `obj_dw_garden_enemyrush_Step_0:94-103` starts the enemy rush. Its cutscene polls
  `obj_testoverworldenemy` (`:629`), and the `flag[1412]` Flowery Dollar state hangs off that rush.

The previous-frame `global.interact` check catches the cutscene cases, because `interact` is 1 while
the cutscene runs. The `washit == 0` check catches forced and first-strike starts. The ch5 objects
are listed explicitly.

**Caveats:**

- Ch1 forest chasers already have `flag[touchflag] = 1` by the time we cancel. They behave as if
  they had been fought (rooms 505-508). That only affects the maze chase pattern.
- Ch3 board battles, the ch4 church chasers and the ch5 scarecrow/kawkaw/shinobeetle still start
  battles (section 2). The UI should say "roaming enemies only". Extending the toggle would need one
  `.patch` per object at its `scr_battle(...)` call, gated on the same flag, plus a per-object check
  that the room does not wait on the result. Example: ch4 `obj_dw_churchb_bellroom_Step_0:171`
  reads `flag[encounterflag]`.
- Recruits (ch2+) are never earned for skipped enemies, so recruit counts drop. Nothing is
  "LOST", because only `scr_defeatrun` sets `flag[600+type] = -1`. The ch4/ch5 recruit checks and
  dialogue will reflect the lower count.
- Skipped battles give no D$ and no violence or spare tallies (`flag[40..45]`). The Ch2 Snowgrave
  route condition relies on these, but skipping cannot advance it by accident.

### (b) Instant win: `scr_wincombat()` while a battle is running

Offer this as a battle-page action (task 30) rather than a passive toggle. Only call it while
`instance_exists(obj_battlecontroller) && global.myfight == 0` (the player's menu turn, with no
bullets in flight). It reproduces the dev F5 key (ch5 `obj_battlecontroller_Step_0:1435-1438`).

What it does (`gml_GlobalScript_scr_wincombat.gml`, ch4 lines 1-54):

- It sets `myfight = 7` and `victory = 1`, then runs `scr_monsterdefeat()` on each living enemy.
- Each enemy is recorded as **spared** (`flag[51+i] = 2` → `flag[41]++`), or as killed if its HP
  is already ≤ 0. The encounter flag gets that result, so chasers do not respawn. Normal gold goes
  to `monstergold[3]`.
- No recruit progress is made, because `scr_recruit` only runs from the spare path
  (`scr_spare` → `event_user(10)` → `obj_monsterparent_Other_20`). No recruit is lost either.
- When `flag[60] != 0` it loads the next wave (lines 22-52) instead of ending. The cheat needs one
  press per wave.
- Ch1's version (lines 1-19) has no wave logic.

**Do not offer it for story or scripted battles.** These run their own end-of-battle code and call
`scr_wincombat` themselves, for example ch2 `obj_berdlyb2_enemy_Step_0:659`, ch2
`obj_rouxls_enemy_Step_0:420`, ch3 `obj_tvturnoff_manager`, ch3 `obj_tenna_board4_enemy`, ch4
`obj_guei_enemy_Step_0:656`, ch4 `obj_elnina_lanino_rematch_controller`, ch5 `obj_aqua_enemy`
and ch5 `obj_ball_enemy`. Reuse the existing `scr_drcheat_nospare()` list: refuse the action if
any living enemy is on it, as `drc_en_nospare` already does in `gml_GlobalScript_scr_drcheat_page_enemies.gml:17-26`.
Also refuse when `global.specialbattle != 0`.

### Forcing an encounter

It is possible: call `scr_battle(id, 0, noone)` from the overworld while `global.interact == 0`
(ch2–5). It is not recommended. Many ids depend on the room, on flags, or on scripted endings, so
the result can soft-lock or set story state too early. If it is ever added, limit it to that
chapter's ordinary chaser ids, read from `obj_chaseenemy_Create_0` `myencounter` assignments.

## Per-chapter summary

| Ch | Contact detection | Shared transition | Bypasses (`scr_battle` / own spawn) | Notes |
|---|---|---|---|---|
| 1 | `obj_chaseenemy` Collision event | yes | Cutscenes create `obj_encounterbasic` directly | `touchflag` set on contact; music fade must be undone; Clover fight comes through a chaser |
| 2 | `obj_chaseenemy_Step_2` | yes | 15 objects (scripted, Berdly, dojo) | `encounterflag` result write-back starts here |
| 3 | `obj_chaseenemy_Step_2` | yes | 22 objects, including all TV-board chasers | Chaser Create resets `encounterno` to 0 |
| 4 | `obj_chaseenemy_Step_2` (+ `largetouchregion`) | yes | 20 objects, mostly the church's own chasers | The toggle covers little of Ch4 |
| 5 | `obj_chaseenemy_Step_2`, `scr_plat_encounter`, projectiles | yes | 14 objects | Exempt the garden enemy rush and Floradinn; encounter 230 is scripted |
