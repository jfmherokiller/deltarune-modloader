# CheatMenu data: known `global.flag` / `global.plot` meanings

The game keeps its story state in `global.flag[0..9998]` and its progress counter in
`global.plot`. The game's own flag-name table (`scr_flag_names`, `global.flagname`) is empty in
release builds, so CheatMenu ships this list for the flag viewer (ROADMAP task 31).

| File | What |
|---|---|
| `flags.json` | Source of truth: 1499 flags, plus `global.plot` milestones for Chapters 1–5 |
| `../chapterN_windows/code/gml_GlobalScript_scr_drcheat_flagnames.gml` | Generated from `flags.json`, one per chapter. Do not edit |
| `../tools/gen_flag_names.py` | `gml` regenerates the GML; `import` refreshes the community entries |
| `../tools/scan_flag_usage.py` | Scans the decompiled code for literal flag and plot uses (fills `ch`) |
| `THIRD_PARTY.md` | Licenses of the community lists the entries come from |

## `flags.json`

```json
{
 "_about": "...", "sources": {...}, "revisions": {"tenna": "<commit> <date>", ...},
 "flags": {
  "1411": {"name": "flowery_dollars", "label": "F$ (Flowery Dollars)", "desc": "...",
           "type": "number", "ch": [5], "conf": "verified", "src": "ours"}
 },
 "plot": {
  "1": {"0": {"name": "new_game", "label": "New Game", "src": "tenna"}, "1": {...}},
  "2": {...}
 }
}
```

Each flag (key = index) or plot value (key = `global.plot` value, per chapter) is one line.

| Field | Meaning |
|---|---|
| `name` | short snake_case id, unique |
| `label` | display name. Many community labels are short within their `group` ("Defeated"), so the GML shows `group: label` |
| `group` | optional story section / topic ("Hammer of Justice", "Game show boards") |
| `desc` | optional description |
| `type` | `bool`, `number`, `enum` (see `values`) or `color` |
| `values` | optional `{"value": "meaning"}`. Booleans and enums only list the values the game uses |
| `min` / `max` | optional range |
| `ch` | chapters whose code uses the flag by a literal index (from `scan_flag_usage.py`; recruit flags from `scr_recruit_info`). Missing = only reached through computed indices |
| `conf` | `verified` (checked against the game code), `community` (from a community list, consistent with the code), `uncertain` (the source says it is a guess) |
| `src` | `ours`, `tenna`, `utdrwiki`, `recruit_info` (see `sources`) |
| `alias` | the code-viewer's short name, when it differs (handy for searching) |
| `volatile` | value only matters during a scene or battle; the game resets it |
| `unused` | set but never read, or never set, in the current game |

Flag indices are unique across chapters: a chapter never reuses another chapter's number, and
flags carry over from one chapter to the next through the completion save.

Recruit progress, `flag[600 + enemy id]`, is a fraction, not a boolean: `scr_recruit` adds
`1 / recruitcount` per recruit (capped at 1), and `-1` marks a lost recruit. The import applies
this to every recruit entry.

## Generated GML

For chapter N it holds every flag first used in chapter N or earlier, plus every flag with no
`ch`. Ch1: 222 flags (40 KB), Ch5: 1499 flags (300 KB). It adds about 0.1 s to the Chapter 5
loose-GML compile.

```gml
// [short name, label, description, value notes, confidence 0/1/2], or undefined
var names = scr_drcheat_flagnames();   // builds the array: call once and cache it
if (n < array_length(names) && is_array(names[n])) label = names[n][1];

// [[plot value, label, description], ...] ascending; the current milestone is the
// last entry whose value <= global.plot
var plot = scr_drcheat_plotnames();
```

Strings are folded to ASCII, which the game's fonts can draw.

## Editing

1. Edit the entry in `flags.json` and set `"src": "ours"` (and `"conf": "verified"` if you checked
   it in the code). `import` never overwrites `ours` entries, so the change survives a refresh.
2. `python3 tools/gen_flag_names.py gml`

## Refreshing from the community lists

```sh
git clone --depth 1 https://github.com/tennaproject/tenna-editor /tmp/tenna-editor
git clone --depth 1 https://github.com/utdrwiki/code-viewer /tmp/code-viewer
python3 tools/scan_flag_usage.py -o /tmp/usage.json          # needs RE/code (see RESEARCH.md)
python3 tools/gen_flag_names.py import --tenna /tmp/tenna-editor --utdrwiki /tmp/code-viewer --usage /tmp/usage.json
python3 tools/gen_flag_names.py gml
```

`import` reads Tenna Editor's TypeScript tables through `node` (Node 23+ runs `.ts` directly).
Re-run `scan_flag_usage.py` after a game update; a new chapter needs `CHAPTERS` in
`gen_flag_names.py` extended.

## `global.plot`

The milestone tables come from Tenna Editor. Checked against the code with
`scan_flag_usage.py`: every `global.plot = N` the code sets has an entry, apart from:

- code shared between chapters that assigns earlier chapters' values (`obj_classscene`,
  `obj_darkdoor`, `obj_krisroom`, `obj_unusedclassevent`; values 1–16, 49, 250, 251),
- placeholders and debug code (`scr_monstersetup` 40, `obj_npc_puzzlemaster1` 42, Ch4 999 in
  `obj_legend_cutscene_base`, Ch5 332 in the prototype `obj_proto_fcastle_ultrafox`),
- Ch4 68 and 80 (`obj_ch4_PDC14C`, `obj_ch4_PDC13B`), Ch4 130 and 249, Ch5 199.

Some milestones (e.g. Ch4 238.1–239.99) are set through computed or fractional values the scan
does not see. `plot` values are ordered, and most story checks are `global.plot >= N`, so
lowering it replays content and raising it skips it.
