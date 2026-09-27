# CheatMenu (example mod)

An in-game version of the Cheat Engine table. Press **F7** anywhere, in the overworld or in battle. The game
freezes and a cheat overlay opens.

| Row | Keys |
|---|---|
| Gold | ←/→ ±10 (Shift ±1000) |
| Heal party / Fill TP | Z |
| Lock HP / No damage / Infinite TP | Z or ←/→ toggles; stays active after closing |
| Character | ←/→ picks the party member for the stat rows |
| AT / DF / MAG / Max HP | ←/→ ±1 (Max HP ±10; Shift ×10) |
| Close | Z, X or F7 |

Install: copy `CheatMenu\` into `DELTARUNE\mods\`. It needs the loader plus `mods\tools\` (the UTMT CLI and
`ImportLooseMod.csx`).

## What it shows off

Everything is in `all_chapters\`, which is applied to every `chapterN_windows\data.win`:

- `code\gml_Object_obj_drcheat_*.gml`: a **new object**, created from nothing by naming its event code.
- `code\gml_Object_obj_time_Step_1.append.gml`: an **append patch**. It adds code to the end of the
  persistent `obj_time` step event without replacing it, so it stacks with other mods (tested
  together with LocalMultiplayer, which replaces that same script).
- `code\gml_Object_obj_time_Draw_64.patch`: a **find/replace patch** that shows "press F7" for about 5 s.
- `sprites\spr_drcheat_icon_0.png` + `.origin.txt`: a **new sprite**.
- `sounds\snd_drcheat_open.ogg`: a **new sound**, played through `snd_play(snd_drcheat_open)`.

"No damage" holds `global.inv` (the invulnerability timer), the same trick as the CE table, so
scripted hazards that bypass it still hit.
