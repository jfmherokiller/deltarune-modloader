# DialogueBacklog (example mod)

An Ace Attorney-style **backlog** for DELTARUNE. Every page of dialogue the game shows is recorded with
its speaker, and **T** opens the history in any chapter, in the overworld or in battle. The game
is paused while the backlog is open.

| Key | Effect |
|---|---|
| T | open / close the backlog (configurable, see below) |
| ↑ / ↓, mouse wheel | scroll one entry (hold to repeat) |
| Page Up / Page Down | scroll five entries |
| Home / End | oldest / newest |
| X | close |

The newest line is at the bottom, as in Ace Attorney. Each line has a coloured name plate for its
speaker. Narration has no plate. The options you pick in choice menus are listed with a heart, and
a divider marks the start of each play session.

## Changing the key

T is the default. DELTARUNE reads T only in developer test objects that normal play never creates
(the Chapter 4 piano-recording tool, the title placeholder and similar), checked in chapters 1-5.
F6 and F8 aren't used because Steam Remote Play takes both. If you bind T to a game control in the
Config menu, the backlog leaves T to the game; pick another key then. The first launch writes
`dialogue_backlog.ini` to the save folder (same place as the transcript below):

```ini
[keys]
open="84"
```

Set `open` to a Windows virtual-key code in decimal: T 84, L 76, Tab 9, F9 120. The change applies
at the next launch.

## Saved transcript

The lines are also saved in a plain-text file per chapter, in the game's save folder:

- Windows: `%LOCALAPPDATA%\DELTARUNE\dialogue_log_ch<N>.txt`
- Linux/Proton: `<steam>/steamapps/compatdata/1671210/pfx/drive_c/users/steamuser/AppData/Local/DELTARUNE/`

```
# 11/06/2026 14:02:11
Susie	Hey, Kris.\nYou ready?
	* (You couldn't think of anything to say.)
> Yes
```

Each line is either `# <date>` (a session start), `Speaker<TAB>text` (the speaker is empty for
narration; line breaks are written as `\n`), or `> text` (a chosen option). When a chapter starts,
the end of its transcript (the newest 500 lines) is loaded back into the backlog, so the history
survives quitting and chapter switches. A transcript over 20,000 lines is trimmed to the newest
10,000 when the chapter starts. Delete the file to clear the history.

## How it works

Everything is in `all_chapters\`:

- `code\gml_Object_obj_writer_Draw_0.prepend.gml` makes every `obj_writer` (dialogue boxes, battle
  text, enemy speech balloons) call `scr_drbacklog_writer()` once per frame. That records a page
  when it first appears. It compares each page without its whitespace, so the writer's own
  line-wrapping isn't recorded as a new page.
- `scr_drbacklog_clean` strips the writer's control codes (`&` line breaks, `^` pauses, `/` `%`
  page ends, `\XY` face/voice/colour commands). Button icons (`\*Z`) become `[Z]`.
- `scr_drbacklog_speaker` names the speaker from what the game already uses:
  1. a `\F` face code in the page,
  2. a `\T` voice code,
  3. the current face (`global.fc`),
  4. the writer's text sound (`snd_txtsus`, `snd_tv_voice_short`, and so on).

  Characters without a face or voice of their own are named in battle after the enemy nearest to
  their speech balloon. The name tables are in `scr_drbacklog_name`.
- `code\gml_Object_obj_choicer_neo_Other_10.append.gml` records the chosen option.
- `obj_drbacklog` is the viewer. A 3-line `obj_time_Step_1.append.gml` keeps it alive in every
  room. It freezes the game the same way CheatMenu does.

## Limits

- Text that a cutscene draws itself, without an `obj_writer` (credits, some minigame UI), isn't
  recorded.
- Characters who have no face portrait and use the generic text sound show without a name plate
  outside battle.
- Chapter 1's old two-option choicer (`obj_choicer_old`) isn't recorded.

Install: copy `DialogueBacklog\` into `DELTARUNE\mods\`. It needs the loader plus `mods\tools\`
(the UTMT CLI and `ImportLooseMod.csx`). Works alongside CheatMenu (F7) and SpriteInspector (F9).
