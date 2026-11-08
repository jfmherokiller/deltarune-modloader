# CheatMenu: cheat research

This file lists where the game stores things worth cheating, and the cheats that could be added next. The evidence comes from the decompiled code of each chapter (`utmtcli dump chapterN_windows/data.win -c UMT_DUMP_ALL`), kept locally under `RE/code/chapterN/`, which is gitignored. Code entry names are given so each finding can be rechecked after a game update.

The tasks themselves are in `docs/ROADMAP.md` under "CheatMenu".

## Currencies

The menu has one **Gold** row today, which only edits D$.

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
| Walk/run speed | `obj_mainchara.bwspeed` (3, or 4 in some rooms); `wspeed` is derived from it each step (`obj_mainchara_Step_0`: bwspeed + 1..4 while running) | 1–5 | Multiply `bwspeed`; restore it on close. Platformer and climbing sections use other objects |
| Light World stats | `global.lhp`, `lmaxhp`, `lat`, `ldf`, `llv`, `lxp`, `lweapon` | 1–5 | Light World is short in every chapter; probably a small page |
| Instant battle win | `scr_wincombat` exists in every chapter (two entries in Ch4) | 1–5 | Has to respect the scripted battles that `scr_drcheat_nospare` already lists; check what the win screen expects (gold, XP-equivalents, flags) |
| Encounter control | `global.encounterno` (77 uses in Ch5) | 1–5 | Research needed: Dark World encounters are mostly placed enemies, not random |
| Flag viewer/editor | `global.flag[0..]`, `global.plot` | 1–5 | Power-user page: read any flag, edit with a confirm step. The flag-name table (`scr_flag_names`) is stripped from release builds, so the page needs its own list of known flags (the numbers in this file are a start) |
| Room warp | `room_goto` | 1–5 | High risk for story state (`global.plot`). Only worth offering with a warning and limited to rooms already visited |
| `global.debug` | `scr_debug()` returns `global.debug == 1`; 136 code entries in Ch5 test it | 1–5 | `scr_debug_keycheck` is an empty stub in release builds, so the developer hotkeys are gone. What the remaining `scr_debug()` branches do is unknown; research before exposing |

## Already covered

These are in the menu now (`README.md`): gold, heal, TP, lock HP / no damage / infinite TP, puzzle timer, party stats, inventory, enemies (HP / mercy / tired / auto mercy), recruits, and the Chapter 3 TV show.
