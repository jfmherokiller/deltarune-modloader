# DELTARUNE — Cheat Engine stat tooling

Everything here is derived from the RE in `..\07_globals_and_variable_storage.md`
(plus the GML code dump). It gets you live, editable/freezable pointers for **gold,
tension (TP), HP and Max HP**, and documents the rest of the stat block.

## Why a script (and not a plain pointer path)

In this GameMaker runtime, `global.*` variables live in a **hashmap** at
`g_pGlobalObject + 72`, not at fixed offsets. A scalar's static pointer path can break
when the hashmap rehashes, which is why Cheat Engine's pointer scanner produced those
long, fragile multi-level chains. The script sidesteps that: it walks the hashmap, locks
onto each variable's stable **`var_id`**, and re-resolves the real address every tick.

## Quick start

1. Launch DELTARUNE, open Cheat Engine, attach to `DELTARUNE.exe`.
2. Open **Memory View → Tools → Lua Engine** (or Table → Lua Script). Paste
   `deltarune_stats.lua` and execute it. It runs `dr_find()` once automatically.
3. By-name mode needs **no config**. `CFG` near the top is only used by the value
   fallback if the variable-name table can't be read.
4. `dr_find()` (re-)resolves everything and adds entries prefixed **`[DR]`** to your
   address list (auto-kept-fresh every second). Tick a box to **freeze**; double-click
   the value to **edit**.
5. `dr_stop()` removes the entries, the refresh timer, and disables the damage guard.

Synced with the Python/PINCE trainer on 2026-09-27, so the CE script now also has:

- **Noelle** (character id 4) in every per-character stat row.
- Ch2+ inventory capacities: 48 weapon/armor slots and 72 `pocketitem` slots. Slots
  missing from the live array (e.g. Ch1) or holding non-REAL values are skipped.
- A **`[DR] No damage (global.inv guard)`** checkbox row (or `dr_nodamage(true/false)`)
  that holds `global.inv = 120` every 50 ms. Same coverage limits as the PINCE toggle
  described below: scripted hazards that reset `inv` and direct HP writes bypass it.
- `dr_instances([kind], [limit])`: read-only list of live instances with x/y
  (`dr_instances(false)` for every kind). Same limits as `list_instances()` below.
- Safety checks: writes only go into REAL RValues, array elements are bounds- and
  type-checked, and ambiguous value-fallback matches are refused instead of guessed.

Re-run `dr_find()` after switching chapters (each chapter is a new process).
The CE script was checked against simulated memory (Lua 5.4 with stubbed CE APIs),
not yet inside a live Cheat Engine session.

## Linux (no Cheat Engine) — `deltarune_stats_pince.py`

`deltarune_stats_pince.py` is a straight Python port of this script for Linux, where
DELTARUNE runs under Wine/Proton. Same idea: walk the global hashmap, lock each
variable by **name**, re-resolve every tick, freeze/set the double. It reads and writes
`/proc/<pid>/mem` directly — no ptrace/GDB, and on most kernels **no root** is needed
even with `kernel.yama.ptrace_scope=1` (if a write is refused it prints `(read-only!)`
— then re-run with `sudo`).

**Standalone:**

```
python3 cheatengine/deltarune_stats_pince.py        # auto-detects the DELTARUNE.exe pid
```

It prints the resolved table, starts the 1 s refresh/freeze thread, and drops into a
REPL with `dr` bound:

```
>>> dr.dump()
>>> dr.freeze("Gold", 999999)
>>> dr.freeze("HP Kris")            # freeze at the current value
>>> dr.unfreeze("HP")
>>> dr.set("TP (tension)", 250)     # one-shot write
>>> dr.stop()
```

**Inside PINCE** (attach PINCE to `DELTARUNE.exe` first, then Tools → Libpince Engine):

```python
import sys, importlib
sys.path.insert(0, "<workspace>/RE/cheatengine")
import deltarune_stats_pince as dr
dr.stop()             # clean up the old worker/controls before reloading
importlib.reload(dr)  # picks up edits to the .py file if you re-run this block

dr.start()            # uses PINCE's attached pid AND auto-adds the results to
                      # PINCE's address table as "[DR] ..." rows
dr.dump()
dr.from_pince()       # remove the [DR] rows again  (dr.stop() also removes them)
dr.to_pince()         # (re-)push them
```

`import` only runs once per PINCE session - if you edit `deltarune_stats_pince.py`
(new stats, a fix, `CFG` tweaks) and re-run the block above, plain `import` is a no-op
and you'll keep executing the old code. `importlib.reload(dr)` forces PINCE to re-read
the file. Restarting `dr` this way loses any running resolver/freeze state, so it's safe
to precede with `dr.stop()` if one is already active.

`dr.start()` inside PINCE drops one **`[DR] <stat>`** row per resolved entry into the
address-table pane:

* **scalars** (gold, TP, LV, ...) go in as a plain absolute address rooted in the
  currently resolved `RValue`;
* **array elements** (HP, items, weapon IDs, ...) go in as a PINCE **pointer chain**
  `[rv] -> +0x90 -> +16*i`, which PINCE re-walks on every refresh, so the row stays
  correct even if the array buffer is reallocated.

Freeze / edit the rows with PINCE's own **Freeze** checkbox and **Value** column, or
keep using `dr.freeze(...)` / `dr.set_value(...)` (which write via `/proc/<pid>/mem`
directly and also work standalone). `dr.to_pince(clear_existing=False)` appends without
clearing; `dr.start(push_to_pince=False)` skips the auto-push.

Imported as a module its state persists across Engine runs, so `dr.freeze(...)` /
`dr.dump()` / `dr.stop()` work from later snippets. Module-level write helper is
`dr.set_value(name, v)` (aliased `dr.poke`) - `set` is avoided there because it would
shadow the builtin. `dr.dr_find()` / `dr.dr_stop()` exist as Lua-name aliases.

Verified live against a running Proton copy driven through a real PINCE `MainForm`
(gdb-attached to the game): by-name mode, 84 `[DR]` rows added to the address table, all
resolving to correct values (HP/MaxHP/AT/DF/MAG/GUTS, weapon/armor/item IDs, gold, TP,
LV, ...) — cross-checked against an independent `/proc/<pid>/mem` read. PINCE's own
**Freeze** checkbox on a pointer-chain row (`[DR] HP Kris`) held the value after the
address was perturbed behind PINCE's back, and `dr.stop()` removed every row.

**2026-09-04 fix:** a user report ("Noelle's HP doesn't come up") led to reading the
live process on a Chapter 2 save: `global.char = [1, 4, 0]` and `global.hp[4] = 2`,
`global.maxhp[4] = 90` — exactly the in-game "2/90" HP bar. Noelle is character id **4**;
the script previously only enumerated ids 1-3 (`PARTY`), so her rows were silently
skipped rather than unresolved. Fixed by adding id 4 to `PARTY`; all her per-character
stats (`at`/`df`/`mag`/`guts`/`charweapon`/`chararmor1`/`chararmor2`) confirmed sane and
live in the same save.

## How the values are reached (for manual CE use)

### Linux trainer checks (2026-09-06)

The Python trainer now refuses ambiguous scalar value matches instead of labeling
the first match Gold/TP. A unique value match is still a heuristic, not proof of a
variable's identity. Prefer by-name mode. Manual set/freeze operations resolve again
before writing; direct double writes require a readable REAL tag (low 24 bits = 0)
and a complete eight-byte write. Failed initial writes do not arm a freeze.
Array resolution checks bounds and the element's REAL tag. PINCE export skips
unresolved/non-REAL entries and refuses to export to a different attached PID.

These checks do not suspend the game: memory can change between resolving, checking,
and writing. PINCE's own edits/freezes bypass the Python write checks, and exported
rows still depend on their original RValue root. Stop and restart the trainer after
changing chapters/processes; rebuild PINCE rows when their roots change.

Offline regression checks (no game required):

```sh
python3 -B -m unittest discover -s cheatengine -p 'test_*.py' -v
```

Run the trainer on the machine hosting the game. An SSHFS mount exposes files, not
that machine's live process-memory interface; `/proc/<pid>/mem` access must run there.

If you prefer to build pointers by hand, the structures are:

```
g_pGlobalObject = [DELTARUNE.exe+006A9DC0]
varHashMap      = [g_pGlobalObject + 72]
   numSlots = [varHashMap + 0]        slots = [varHashMap + 16]
   slot(i)  = slots + 16*i  ->  { RValue* value@0, nameHash@8, var_id+1 @12 }

scalar value (gold/tension):   [slot.value + 0]        (kind 0 = REAL double)
array  value (hp/maxhp):
   arrRValue = slot.value                              (kind 2 = ARRAY)
   arrObj    = [arrRValue + 0]
   elemBuf   = [arrObj + 0x90]                          (+144)
   length    = [arrObj + 0xA4]                          (+164)
   hp[id]    = [elemBuf + 16*id] + 0                     (id: 1=Kris 2=Susie 3=Ralsei)
```

> The `slot` index isn't stable across rehashes. Do not assume an RValue root or array
> buffer stays valid for the entire session: the resolver re-walks the structures,
> while exported PINCE chains follow buffer moves only while their RValue root remains
> valid. Whole-session allocation lifetimes have not been established here.

### Manual fallback (no script): "find out what accesses"
Because globals are name-resolved at runtime, the cleanest manual route is:
1. Value scan the number (Double), narrow by changing it in-game.
2. Right-click the result → **Find out what accesses this address**.
3. Trigger it in-game; inspect the instruction — the base register holds the RValue/array
   slot, and you can confirm it chains from `[DELTARUNE.exe+006A9DC0]`.

## Stat reference (from `scr_gamestart`)

Character id: **1 = Kris, 2 = Susie, 3 = Ralsei, 4 = Noelle** (0 = empty). Party slot →
id is `global.char[slot]`. `scr_gamestart` (Ch1 start) only initializes ids 0-3.
CONFIRMED: Chapter 2's extracted `scr_gamestart` initializes stat arrays for ids 0-19
and explicitly assigns Noelle's id 4 HP/MaxHP to 90 at startup; allocation does not
wait for her to join. The earlier live observation on 2026-09-04 was against a
Ch2 save: `global.char = [1, 4, 0]`, `global.hp[4] = 2`, `global.maxhp[4] = 90`, matching
the in-game HP bar exactly.

### Per-character arrays (same access pattern as `hp[]`)
| Variable | Meaning | Kris / Susie / Ralsei defaults |
|---|---|---|
| `global.hp[id]` | Current HP | 90 / 110 / 70 |
| `global.maxhp[id]` | Max HP | 90 / 110 / 70 |
| `global.at[id]` | Attack | 10 / 14 / 8 |
| `global.df[id]` | Defense | 2 |
| `global.mag[id]` | Magic | 0 / 1 / 7 |
| `global.guts[id]` | Guts | 0 |
| `global.charweapon[id]` | Equipped weapon id | 1 / 2 / 3 |
| `global.chararmor1[id]` / `chararmor2[id]` | Equipped armor ids | 0 |
| `global.spell[id][j]` | Spell ids (2-D array) | varies |

Defaults above are Ch1's `scr_gamestart` values for ids 1-3 only; id 4 (Noelle) has no
`scr_gamestart` defaults in the original Ch1 dump but resolves fine live -
`deltarune_stats_pince.py` covers all four (`PARTY = {1..4}`).

### Global scalars (single RValue — shortest pointer paths)
| Variable | Meaning | Default |
|---|---|---|
| `global.gold` | Money | 0 |
| `global.tension` | TP (cap `global.maxtension` = 250) | 0 |
| `global.xp` | Experience | 0 |
| `global.lv` | Level | 1 |

### Inventory / storage arrays (treat like `hp[]`, index = slot)
| Variable | Meaning | Size |
|---|---|---|
| `global.item[i]` | Consumable inventory | 13 |
| `global.keyitem[i]` | Key items | 13 |
| `global.weapon[i]` / `global.armor[i]` | Owned weapons / armor | 13 |

CONFIRMED: these sizes describe Chapter 1 initialization. Chapter 2's
[`scr_gamestart`](../code/chapter2/gml_GlobalScript_scr_gamestart.gml) initializes
48 weapon slots, 48 armor slots, and 72 `global.pocketitem` slots; item/keyitem remain
13. The Python trainer exposes these larger inventories, omitting slots outside the
live array bounds or with non-REAL values. Restart discovery if an array grows after
startup. These are initialized array capacities, not a claim about visible UI slots.
The extracted Chapter 3-5 initialization scripts confirm the same inventory
capacities; see [cross-chapter evidence](../07_globals_and_variable_storage.md).

Direct HP edits do not run the game's revival routine. The extracted Chapter 3-5
`scr_heal` scripts call `scr_revive` separately when recovering from downed HP;
do not assume that setting positive HP also performs those side effects.

### Light World (overworld) mirrors
`global.lhp`, `global.lmaxhp`, `global.lat`, `global.ldf`, `global.lgold`, `global.lxp`,
`global.llv` — single scalars used while in the Light World.

## To extend the script to any of the above
- **Scalars**: add the current value to `CFG`, add a candidate collector in `discover()`
  exactly like `gold`/`tension`.
- **Arrays**: match a distinctive element triple in `discover()` like `hp`/`maxhp`, then
  emit `elemAddr(i)` for the indices you care about in `resolveAll()`.


## Damage guard toggle (Linux/PINCE, 2026-09-06)

After the usual one-time `dr.start()` setup inside PINCE, use the **[DR] No damage**
checkbox in the **DELTARUNE trainer** toolbar. Tick to enable; untick to disable.
No Python calls are needed to toggle it. Reload an already-loaded trainer once to
pick up this update (use the setup block above).

The checkbox reflects automatic disarming, rejects failed enables, and is disabled
when PINCE is attached to a different process or memory is read-only. Removing the
trainer rows or stopping the trainer also removes the toolbar and disables the guard.
Re-pushing rows with the default clearing behavior starts with the guard off.

The standalone REPL and optional Python API still support:

```python
dr.no_damage(True)    # enable normal damage protection
dr.no_damage(False)   # disable and clear the invulnerability timer
```

The checkbox and Python API control the same guard. It writes process memory
only and does not modify the executable or any chapter's `data.win`.

The guard maintains `global.inv = 120` and refreshes every 50 ms while active;
normal stat refresh/freezes also run at that cadence while enabled. The first write
is immediate. It requires by-name resolution and a writable REAL RValue. Each
refresh resolves the current hashmap slot, and failed writes or a replaced global
object disarm the guard. After switching chapters/processes, call `dr.start()` again
and re-enable it. `dr.stop()` disables the guard before closing process memory.
Disabling clears the current timer to zero rather than restoring an old countdown.
If cleanup fails or the trainer is killed, the finite timer can expire naturally
as game frames resume. The timer may affect blinking and other invulnerability UI.

**Coverage limit:** this is invulnerability-timer protection, not universal immunity.
The available `scr_damage` dumps in chapters 1-5 check `global.inv < 0` before damage.
Chapter 1/2 party and overworld damage routines also check it, as do Chapter 2's
proportional and SNEO-final-attack damage scripts. But some hazards force `inv = -1`
immediately before a damage call (notably Chapter 2 teacups), and direct HP writes
can bypass this protection. Polling faster does not eliminate those cases. A game
reset can also leave a window before the next refresh. The trainer does not restore
HP after damage and does not revive already-down party members.

Fourteen offline regression tests passed, including real Qt checkbox clicks, failed
enables, process changes, state synchronization, and toolbar cleanup with simulated
memory. No running game/PINCE session was available for live integration validation. Further coverage needs an in-process damage/HP-write hook;
Frida remains the planned tool for measuring these paths across chapter transitions.
See [damage findings](../07_globals_and_variable_storage.md#damage-gates-and-trainer-protection-2026-09-06).

## Live-instance listing (`list_instances()`, added 2026-09-17)

New capability beyond the global-variable stat walk above: it reads
`g_pInstanceManager`'s own pointer-keyed hashmap (reverse-engineered this session —
see [08_instances.md](../08_instances.md)) to enumerate **every live GML object
instance currently in the room**, not just the fixed set of party/inventory globals.
For each one it reports the raw instance pointer, its `YYObjectBase.kind`
(confirmed: `1` = a real game-object `CInstance`; other values share the same table
because script/method references are registered there too), and — for real
instances — their `x`/`y` position (also reverse-engineered this session,
`CInstance+232`/`+236`).

```python
>>> dr.list_instances()                    # real CInstances only (kind=1), up to 200
   0x1830000  kind=1  x=0.0 y=0.0
   0x182e800  kind=1  x=128.0 y=32.0
   ...
[*] 214 instance(s) (kind=1)

>>> dr.list_instances(kind=None, limit=500)  # everything in the table, any kind
```

**Coverage limit, by design, not a bug**: this does *not* tell you *which* GML
object each instance is (e.g. "that's `obj_teacup_bullet`"), only that it exists and
where it is. The natural field for that (`YYObjectBase.typeIndex`, `self+0x70`) is
gated behind a debug-build flag that's off in this shipped build, so it isn't
reliably populated here — confirmed a dead end, not just unexplored. A real
"object name" lookup would need a different approach (e.g. reading each instance's
own object-type field some other way, or correlating positions/behavior instead of
trusting a name field) — flagged as open in `08_instances.md`, not attempted.

Read-only — this only inspects memory, it doesn't write anything, so it's safe to
call anytime `dr` is started.
