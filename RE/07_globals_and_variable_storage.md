# Globals, Variable Storage & the Cheat Engine Pointer Scan

This document maps a Cheat Engine pointer-scan result (132 paths, all resolving to a
`double = 999`) back to the runtime structures in `DELTARUNE.exe`, and explains how GML
variable values (e.g. player health) are stored and reached.

## The scan's static base addresses (imagebase 0x140000000)

| CE base | Absolute | Identity (this analysis) | Evidence |
|---|---|---|---|
| `+006A9DC0` | `0x1406A9DC0` | **`g_pGlobalObject`** — the GML `global` instance | read by `gml_@@Global@@`, `gml_@@NewGMLObject@@` |
| `+006A9DC8` | `0x1406A9DC8` | **`g_pInstanceManager`** — master active-instance manager (`+136` = list head) | used by `sub_14007DBB0` with `Instance_UpdateObjectTypeLinks` |
| `+006A9CA8` | `0x1406A9CA8` | **`g_pGlobalScope`** — global scope object | read by `gml_@@GlobalScope@@` |
| `+00690850` | `0x140690850` | **`g_InstancePool`** — instance slot-id allocator | `InstancePool_AllocSlot(&g_InstancePool,obj)`; 20+ instance funcs |
| `+006AAF48` | `0x1406AAF48` | runtime-written pointer, **no static xref** | field inside a live YYObjectBase (variable storage) |
| `+006AAFA8` | `0x1406AAFA8` | runtime-written pointer, **no static xref** | field inside a live YYObjectBase (variable storage) |

All six read `0x0` in the static IDB — they are populated when the runner boots and
loads `data.win`, which is why Cheat Engine sees real pointers there at runtime.

The first four are the **canonical roots** of the GML object graph. The last two
(`+006AAF48`, `+006AAFA8`) have no code reference at all: they are not engine entry
points but raw pointer fields that live inside a heap-allocated object — i.e. they are
*inside* the variable storage that holds the 999 value. They make poor, fragile CE bases.

## How a GML object/instance stores variables (YYObjectBase)

**Full struct declared in the IDB 2026-09-17 (Track A pass P2)**, reconstructed from
the actual base constructor `YYObjectBase_Construct` (`0x14007C4A0` — identified by its
own RTTI vtable-symbol assignment, `*(_QWORD*)this = &YYObjectBase::`vftable'`, not
just inferred from usage), cross-checked against `Variable_GetValue` (`0x1400426B0`),
`Object_FindVariableSlot` (`0x140084990`), `Variable_HashFind` (`0x1400832C0`), and the
`CScriptRef` constructor (`0x14007C030`). Applying the struct to those four function
signatures visibly improved the Hex-Rays output (raw `object+124`/`*(object+32)` casts
became `object->kind`/`object->parent`, and a previously-dropped second parameter on
`Object_FindVariableSlot` was recovered) — the standard "did this help" check for a
struct like this.

| Offset | Field | Meaning |
|---:|---|---|
| +0x00 | `void *vftable` | `YYObjectBase::vftable` — confirmed via the RTTI symbol name itself, not inferred |
| +0x08 | `RValue *members` | Flat array of variable values, indexed by `var_id` (16 B each). Fast path. |
| +0x10 | `unk_10` | Zeroed by the base ctor; not yet analyzed |
| +0x18 | `unk_18` | Zeroed by the base ctor; not yet analyzed |
| +0x20 | `YYObjectBase *parent` | Prototype/inheritance parent; walked on a lookup miss |
| +0x28 – +0x40 | `unk_28`/`unk_30`/`unk_38`/`unk_40` | Four more zeroed qwords; not yet analyzed |
| +0x48 | `VarHashMap *varHashMap` | Member hashmap for dynamically-named variables (already-declared `VarHashMap` type — see below) |
| +0x50 | `unk_50` | Zeroed; not yet analyzed |
| +0x58 | `unk_58` | Zeroed; not yet analyzed |
| +0x5C | `int count` | Ctor's count param; also touched by the pooled allocator `sub_14007DED0` |
| +0x60 | `int inUse` | Always set to `1` by the base ctor; possibly a liveness/refcount flag |
| +0x64 | `int capacity` | Same value as `count` at construction; the two presumably diverge as the object grows |
| +0x68 | `unk_68` | Zeroed; not yet analyzed |
| +0x70 | `int typeIndex` | Debug-only (gated by `byte_1405FCF10`); indexes `g_ObjectTypeTable` (96 B stride) |
| +0x74 | `unk_74` | Copied from `dword_1405FC074` at construction time; not yet analyzed |
| +0x78 | `int slotId` | This object's id in `g_InstancePool`, assigned by `InstancePool_AllocSlot`; `-1` until then |
| +0x7C | `int kind` | Object type discriminator: `0`=base ctor default (uninitialized), `1`=instance, `3`=`CScriptRef`/method ref — overwritten by the derived ctor right after the base ctor returns |
| +0x80 | `int secondaryKind` | Ctor's third param; `0xFFFFFF` observed for the global object. Purpose TBC |
| +0x84 | `unk_84` | Zeroed; not yet analyzed |

Declared in the IDB as `struct YYObjectBase` (flat — see the class-hierarchy note below
for why a flat struct is the right call here despite real C++ inheritance existing).

### Class hierarchy — confirmed via RTTI, not inferred

The binary retains full MSVC RTTI (`??_7`/`??_R0`-`??_R4` symbols — **287** vtable
symbols total, spanning the whole engine: STL, Box2D (`b2*`), OpenAL (`ALCdevice*`),
GGPO netcode backends (`Peer2PeerBackend`, `SpectatorBackend`, `SyncTestBackend`,
`UdpBackend` — directly relevant to Track A's unstarted P7), and the GML value/object
family clustered together in `.rdata` at `0x1404ea620`-`0x1404ea7f8`). Walked the actual
`RTTIClassHierarchyDescriptor`/`RTTIBaseClassArray`/`type_info` chain by hand (reading
raw bytes and decoding the RVAs with a script — MSVC x64 RTTI pointers are 4-byte RVAs
off the image base, not real pointers) rather than trusting IDA's own demangling alone,
and confirmed:

```
CInstanceBase              (root — numBaseClasses=1, i.e. no base of its own)
  └─ YYObjectBase          (numBaseClasses=2: itself + CInstanceBase)
       ├─ CInstance        (numBaseClasses=3: itself + YYObjectBase + CInstanceBase)
       ├─ CScriptRef       (same base chain — the kind=3 method/script ref)
       ├─ CWeakRef
       └─ GCObjectContainer
```

**`CInstanceBase` confirmed 2026-09-17 to have zero data members of its own** — found
and decompiled its actual constructor/destructor pair (`CInstanceBase_Construct`,
`0x14007C760`, and `CInstanceBase_Destruct`, `0x14007CB50`, identified via xrefs to the
`CInstanceBase::vftable` RTTI symbol): the constructor is a single line,
`*this = &vftable`, nothing else — it's a pure vtable-defining abstract base, exactly
as hypothesized. This is why `YYObjectBase_Construct` never calls a separate
`CInstanceBase` sub-constructor: there's nothing for one to initialize. Because of
that, the `YYObjectBase` struct above is declared **flat** (covering the whole concrete
object from offset 0) rather than as a formal `struct YYObjectBase : CInstanceBase` in
IDA's type system — it matches everything `YYObjectBase_Construct` actually initializes
and is safe to apply to casts/signatures regardless of how the C++ inheritance is
eventually resolved.

**`CScriptRef`'s own extension fields are now declared** too (`struct CScriptRef` in
the IDB, applied to its renamed constructor `CScriptRef_Construct`, `0x14007C030`):
12 more fields from `+0x88` to `+0xD8` beyond the shared `YYObjectBase` base, including
two 16-byte regions at `+0xA0`/`+0xB0` shaped exactly like an `RValue` (zeroed value,
`kind` set to the "undefined" sentinel `0xFFFFFF`) — likely cached bound-argument or
return-value slots for a method reference, not confirmed further. Most of the other
fields are only known to be "zeroed by the constructor," not semantically identified.

**`CInstance`'s builtin `x`/`y` accessors were found to read `self+232`/`self+236`**
(`BuiltinVar_Get_x`/`BuiltinVar_Get_y`, both 4-byte `float`s, `y` confirmed
2026-09-17 while building the instance-enumeration cheat-tooling feature) — concrete,
confirmed `CInstance`-specific fields far beyond the shared `YYObjectBase` base, but
`CInstance`/`CWeakRef`/`GCObjectContainer`'s full field layouts remain **not
reconstructed** — the natural next targets if this work continues. `x`/`y` are used
directly by `cheatengine/deltarune_stats_pince.py`'s live-instance enumerator (see
[08_instances.md](08_instances.md) for the `g_pInstanceManager` hashmap it walks).

**Builtin-var accessor signature confirmed** (closes that type-backlog item): every
getter/setter is `char (__fastcall*)(YYObjectBase *self, unsigned int flags, RValue *value)`
— getters write through `value`, setters read from it. Declared as
`PFN_BuiltinVarAccessor` in the IDB and applied to `BuiltinVar_Get_x`/`BuiltinVar_Set_x`
as worked examples; the other 349 accessors share the same signature but weren't
individually retyped (mechanical, low-value to do exhaustively).

**`data.win` chunk header struct declared** (closes that type-backlog item too):
`struct DataWinChunkHeader { char tag[4]; unsigned int length; }` — the 8-byte header
every chunk in [09_datawin_loader.md](09_datawin_loader.md)'s FORM format starts with.

Still open: `CInstance`/`CWeakRef`/`GCObjectContainer`'s full field layouts, and
`YYRoom`/room-asset structs (no live-room runtime object has been examined yet — recall
`LoadGameData_ProcessChunk_ROOM`'s 0x218-byte allocation is the on-disk asset record,
almost certainly not the same as whatever live/runtime room object `layer_*`'s
`owner+376` layer list actually lives on, per [04_builtin_functions.md](04_builtin_functions.md)'s
P5 section).

**A variable's value is an `RValue`**, located at:

```
value_rvalue = *(uint64*)(object + 8) + 16 * var_id      // fast array, or
value_rvalue = <hashmap lookup at object+72 by var_id>   // dynamic vars
```

and the playable number is `RValue.val` at **offset +0** of that 16-byte slot
(`+8`=flags, `+12`=kind; kind `0` = REAL — see
[03_rvalue_type_system.md](03_rvalue_type_system.md)).

## How a lookup flows (the path CE's offsets trace)

`global.foo` → `variable_global_get("foo")`:
1. `Variable_GetBuiltinId("foo")` (`0x1400423C0`) tries to resolve a builtin/known id;
   else the name is hashed to a member id.
2. `Variable_GetValue(g_pGlobalObject, var_id, …, out)`:
   - If `var_id < g_BuiltinVarCount` and `obj[124]==1`, it dispatches through the builtin
     variable accessor table `g_BuiltinVarAccessors` (`0x14067F188`) — this is how
     `x`, `y`, `image_index`, etc. are read.
   - Otherwise it indexes the member array / hashmap as above, then walks the `+32`
     parent chain if not found locally.

A multi-level CE chain like
`[g_pGlobalObject]+0x148 → +0x10 → +0xFB0 → +0x20 → +0x190 → +0x50`
is simply this graph in pointer form: the global object → a member that is itself a
struct/array/instance → its member array → … → the final `RValue`, whose `+0x50`-style
terminal offset lands on `RValue.val` (the `double`).

## Practical guidance: locating player health

1. **Prefer the `g_pGlobalObject` root** (`DELTARUNE.exe+006A9DC0`). Of the scan bases it
   is the most semantically stable across runs/saves — it is the documented root of the
   GML object graph. Avoid `+006AAF48`/`+006AAFA8` (raw interior pointers; they will move
   between sessions).
2. **Confirm the slot is REAL, not coincidence.** The terminal address is an `RValue`.
   In CE, also inspect `value_address + 0xC` (the `kind` field). It should read `0`
   (`VALUE_REAL`). `value_address + 0x8` is `flags`. If `+0xC` is not 0, you found the
   wrong field.
3. **The number is editable in place.** `RValue.val` at `+0` is a plain IEEE double;
   writing it changes the variable. For HP you typically also want to write the *max*
   HP variable (a separate RValue slot) so the value is not clamped back down.
4. **Names live in `data.win`, not the exe.** This binary is the engine; it does not
   contain the GML variable name → `var_id` mapping for DELTARUNE's own variables (HP,
   gold, etc.). To label a slot "hp" you must either (a) read it at runtime via
   `variable_instance_get`/`variable_global_get` semantics, or (b) decode `data.win`
   (e.g. with UndertaleModTool). The engine analysis here tells you the *mechanism and
   memory layout*; the *name* is content.
5. **Value 999 caveat.** `999` is a common cap/sentinel in DELTARUNE (gold, certain
   stats, menu counters). Because several globals legitimately hold 999, scan-narrow by
   changing the in-game value and re-filtering before trusting any one path as "health".

## CONFIRMED: player health = `global.hp[1]` (from the GML code dump)

The decompiled GML (`E:\DeltaRune\RE\code\`) resolves this exactly.
`scr_gamestart` (`gml_GlobalScript_scr_gamestart.gml`) sets the defaults:

```gml
global.hp[0] = 0;    global.maxhp[0] = 0;     // index 0 = empty slot
global.hp[1] = 90;   global.maxhp[1] = 90;    // index 1 = KRIS   <-- your "90"
global.hp[2] = 110;  global.maxhp[2] = 110;   // index 2 = SUSIE
global.hp[3] = 70;   global.maxhp[3] = 70;    // index 3 = RALSEI
```

So health is **`global.hp[]`, a global array indexed by character id**:

| char id | character | default hp / maxhp |
|---:|---|---|
| 0 | (none/empty) | 0 |
| 1 | **Kris** | **90** |
| 2 | Susie | 110 |
| 3 | Ralsei | 70 |

Party slot → character id is `global.char[slot]` (`global.char[0]=1` ⇒ Kris leads).

CONFIRMED (chapter extraction, 2026-09-06): Chapter 2's
[`scr_initialize_charnames`](code/chapter2/gml_GlobalScript_scr_initialize_charnames.gml)
explicitly maps ids 1/2/3/4 to Kris/Susie/Ralsei/Noelle. Its
[`scr_gamestart`](code/chapter2/gml_GlobalScript_scr_gamestart.gml) initializes stat
arrays for ids 0-19, then assigns starting HP/MaxHP 120/140/100/90 for ids 1/2/3/4.
Noelle's storage therefore exists at chapter startup, not only when she joins.
The same script initializes 48 weapon slots, 48 armor slots, and 72 `pocketitem`
slots, compared with 13 weapon/armor slots in Chapter 1. These are GML initialization
findings; they do not prove allocation lifetimes or the native layout of another
chapter executable. The Linux trainer now exposes those inventory capacities while
checking live array bounds and REAL element tags.

CONFIRMED: the extracted Chapter
[3](code/chapter3/gml_GlobalScript_scr_gamestart.gml),
[4](code/chapter4/gml_GlobalScript_scr_gamestart.gml), and
[5](code/chapter5/gml_GlobalScript_scr_gamestart.gml) initialization scripts also
initialize 48 weapon/armor slots and 72 pocket-item slots. Their chapter-specific
branches assign Kris/Susie/Ralsei HP and MaxHP as follows (initialization values,
not predictions for an imported save):

| Chapter | Kris | Susie | Ralsei |
|---|---:|---:|---:|
| 3 | 160 | 190 | 140 |
| 4 | 200 | 230 | 180 |
| 5 | 240 | 290 | 210 |

CONFIRMED: [`scr_heal` in Chapter 3](code/chapter3/gml_GlobalScript_scr_heal.gml)
maps a party slot through `global.char`, clamps normal healing to MaxHP, and calls
`scr_revive` when healing from nonpositive HP to nonnegative HP. Chapter 4 retains
this structure; [Chapter 5](code/chapter5/gml_GlobalScript_scr_heal.gml) adds an
optional third argument (default `true`) gating that revival branch. A trainer's
direct HP write does not execute `scr_revive`. Its other side effects have not been
analyzed in this pass, so an HP edit is not established as a complete revival.

HP is read/written all over combat: `scr_damage`, `scr_heal`, `scr_charbox` (the HP bar
UI), `scr_charcan`, etc., always as `global.hp[global.char[target]]`.

### The full memory chain to the value

`global.hp` is a member variable on `g_pGlobalObject` whose RValue is a **`GMArray`**
(kind 2). Element access (from `Array_GetElement` / `gml_array_length`):

```
g_pGlobalObject (DELTARUNE.exe+006A9DC0)
  └─ members:  *(uint64*)(global + 8)
       └─ + 16 * var_id("hp")          → RValue, kind = 2 (ARRAY)
            └─ .ptr (+0)               → GMArray object
                 └─ +144 elements buf  → RValue[]
                      └─ + 16 * 1       → RValue for Kris (kind 0 = REAL)
                           └─ +0        → double = 90   ← HP (edit here)
```

`GMArray` (declared in IDB): elements pointer at **+144**, length at **+164**.
So once a pointer path lands on Kris's HP, the *other party members are trivial*:
- Susie  `global.hp[2]` = same path, final offset **+16**
- Ralsei `global.hp[3]` = same path, final offset **+32**

### "Next targets" — the rest of the stat block

All character-indexed (same `[1]`=Kris,`[2]`=Susie,`[3]`=Ralsei pattern, each a separate
global array = a separate `var_id`/member offset, same `GMArray` internals):

| Variable | Meaning | Kris default |
|---|---|---|
| `global.maxhp[c]` | Max HP (freeze this too, or HP re-clamps) | 90 |
| `global.at[c]` | Attack | 10 |
| `global.df[c]` | Defense | 2 |
| `global.mag[c]` | Magic | 0 |
| `global.guts[c]` | Guts | 0 |
| `global.charweapon[c]` / `global.chararmor1[c]` / `global.chararmor2[c]` | Equipment ids | — |

Global **scalars** (single RValue member, no array indirection — shorter pointer paths):

| Variable | Meaning | Default |
|---|---|---|
| `global.gold` | Money (dark dollars) | 0 |
| `global.tension` | TP / tension (max `global.maxtension` = 250) | 0 |
| `global.xp` | Experience | 0 |
| `global.lv` | Level | 1 |

> Tip: the scalars (`gold`, `tension`, `xp`, `lv`) are *single* RValues directly in the
> global object's member array, so their CE pointer paths are one indirection shorter
> than the `hp[]` array path — usually the easiest "next targets" to lock.

## The builtin instance-variable table (`x`, `y`, `image_index`, …)

Named 2026-09-04 from `Variable_AddBuiltin` (`0x1400422D0`, ex-`sub_1400422D0`, ~224
call sites — one per builtin variable):

```
g_BuiltinVarTable       0x14067F180   array of 32-byte entries
g_BuiltinVarTableCount  0x140683000   live entry count (hard cap 500 —
                                      "INTERNAL ERROR: Adding too many variables")
g_BuiltinVarNameMap     0x140683008   name -> entry index (HashMap_Insert)

entry (32 B):  +0  int   nameId      (= sub_1401D8EC0(name))
               +8  void* getter      (RValue accessor: read x, y, …)
               +16 void* setter      (0 if read-only)
               +24 byte  writable    (= setter != 0)
```

`g_BuiltinVarTable + 8` is exactly the `g_BuiltinVarAccessors` address (`0x14067F188`)
that `Variable_GetValue` dispatches through — i.e. the "accessor table" is the `getter`
column of this table.

**P4 done 2026-09-17.** `g_BuiltinVarTable` itself can't be read statically (it's
runtime-populated, reads `0x0` in the IDB like the other globals in the table above)
— but `Variable_AddBuiltin` turned out to have only **4 callers**, not ~224 individual
call sites, because registration is a flat list of calls inside 4 registrar functions
rather than data-table-driven. Read every `(name, getter, setter)` literal triple
straight from those calls and renamed all 351 getter/setter functions
(`BuiltinVar_Get_<name>` / `BuiltinVar_Set_<name>`, setter omitted when read-only) plus
the 4 registrars themselves:

| Registrar | Count | Covers |
|---|---:|---|
| `Register_BuiltinVars_Environment` (`0x14003D3F0`) | ~110 | `argument*`, `room`/`view_*`/`background_*`, `mouse_*`/`keyboard_*`, `score`/`lives`/`health`, date/time, `event_*`/`error_*`, `gamemaker_*` |
| `Register_BuiltinVars_Instance` (`0x14003E110`) | ~90 | `x`/`y`/`hspeed`/`vspeed`, `sprite_*`/`image_*`, `path_*`/`timeline_*`, `phy_*` (Box2D physics), `layer`, `sequence_*` |
| `Register_BuiltinVars_Platform` (`0x1401A9E00`) | 6 | `os_*`, `browser_*` |
| `Register_BuiltinVars_Rollback` (`0x14022DA30`) | 6 | `rollback_*` — GGPO netcode vars, a head start on Track A's unstarted P7 |

5 name↔function aliases found (same getter/setter fn registered under two names):
`background_color`/`background_colour`, `background_showcolor`/`background_showcolour`
(US/UK spelling pairs), `gamemaker_registered`/`gamemaker_pro` (synonyms).

## The instance-context stack (current `self`)

Named 2026-09-04:

```
g_InstanceContextStack           0x1408CE220   void*[]  (stack of instance pointers)
g_InstanceContextStackCount      0x1408CE228   int
g_InstanceContextStackCapacity   0x1408CA01C   int      (doubles on grow)

GetCurrentInstance()   0x1401D15F0   -> g_InstanceContextStack[count-1] or 0
PushCurrentInstance(i) 0x1401D52A0   -> push (gated by byte_1405FCF10)
```

`GetCurrentInstance` (~399 xrefs) supplies the owning instance to
`Instance_UpdateObjectTypeLinks` whenever an array/object RValue is assigned — i.e. it
is the "which instance does this value belong to" context, distinct from
`g_pInstanceManager` (the *set* of live instances). `sub_1401CE600` appears to
init/reset the stack; the matching pop was not isolated this pass.

## Named in the IDB during this analysis

Functions: `Variable_GetValue` (0x1400426B0), `Variable_GetBuiltinId` (0x1400423C0),
`Variable_AddBuiltin` (0x1400422D0), `InstancePool_AllocSlot` (0x1400326C0),
`Instance_UpdateObjectTypeLinks` (0x140082340, the long-unnamed 568-xref function),
`GetCurrentInstance` (0x1401D15F0), `PushCurrentInstance` (0x1401D52A0),
`HashMap_Insert` (0x140042070, generic Robin-Hood map insert),
`stdstring_Tidy_deallocate` (0x140012060, MSVC std::string cleanup — not GML-specific).

Functions (variable store): `Variable_HashFind` (0x1400832C0), `Object_FindVariableSlot`
(0x140084990), `Variable_GetName` (0x1401BFB50), `Array_GetElement` (0x1401BC080).

Globals: `g_pGlobalObject`, `g_pInstanceManager`, `g_pGlobalScope`, `g_InstancePool`,
`g_ObjectTypeTable`, `g_BuiltinVarCount`, `g_pVarLookupContext`, `g_BuiltinVarTable`,
`g_BuiltinVarTableCount`, `g_BuiltinVarNameMap`, `g_InstanceContextStack`
(+ `Count` / `Capacity`).

Types: `RValue`, `RValueKind`, `RefString`, `GMArray` (+0x90 elements / +0xA4 length),
`VarHashMap` (+0 numSlots / +8 mask / +16 slots), `VarHashSlot` (16 B: value/nameHash/var_id+1).

## The variable hashmap (how `global.*` is actually stored)

Confirmed from `Variable_HashFind` and the `variable_instance_get_names` enumerator. Each
object/instance/struct (including the global object) keeps its dynamic variables in an
open-addressing hashmap at **`obj+72`**:

```
VarHashMap:  +0 int numSlots   +8 int mask   +16 VarHashSlot* slots
VarHashSlot (16 B):  +0 RValue* value   +8 uint nameHash   +12 int (var_id+1; <=0 empty)
```

Lookup: `key=(var_id+1)&0x7FFFFFFF`, `bucket=key & mask`, linear/Robin-Hood probe; the
result is the `RValue*` at `slot+0`. The variable's editable value is the `RValue` at that
pointer (`+0`=double for REAL, `+12`=kind). This is what `cheatengine/deltarune_stats.lua`
walks to resolve gold/TP/HP without relying on fragile fixed offsets.


## Damage gates and trainer protection (2026-09-06)

CONFIRMED: the extracted `scr_damage` in each chapter gates its damage body on
`global.inv < 0`: [Chapter 1](code/chapter1/gml_GlobalScript_scr_damage.gml),
[Chapter 2](code/chapter2/gml_GlobalScript_scr_damage.gml),
[Chapter 3](code/chapter3/gml_GlobalScript_scr_damage.gml),
[Chapter 4](code/chapter4/gml_GlobalScript_scr_damage.gml), and
[Chapter 5](code/chapter5/gml_GlobalScript_scr_damage.gml).
This is a countdown, not a Boolean. In Chapter 2,
[obj_heart Step](code/chapter2/gml_Object_obj_heart_Step_0.gml) and
[obj_mainchara Step](code/chapter2/gml_Object_obj_mainchara_Step_0.gml) decrement it.

CONFIRMED: Chapter 1/2 `scr_damage_all` and `scr_damage_all_overworld`, plus Chapter 2
`scr_damage_proportional` and `scr_damage_sneo_final_attack`, also check that gate.
The party-damage wrapper resets `inv` within its loop after passing the outer check.

CONFIRMED: a positive timer is **not universal damage immunity**. The Chapter 2
[teacup bullet Step](code/chapter2/gml_Object_obj_teacup_bullet_Step_0.gml) forces
`global.inv = -1` in its hit path before damage; the
[base-enemy debug F12 path](code/chapter2/gml_Object_obj_baseenemy_Step_0.gml) also
forces the timer negative. Thus an external timer freeze cannot guarantee blocking
every hit even with a short polling interval. Coverage of all direct HP reductions
and special encounters remains unconfirmed, especially in partially dumped chapters.

The old flat [scr_damage](code/gml_GlobalScript_scr_damage.gml) additionally checks
`global.chemg_god_mode`; the freshly extracted Chapter 2 script has no such check.
Do not assume that optional variable provides a portable cross-chapter toggle.

[Linux trainer](cheatengine/deltarune_stats_pince.py) now exposes `no_damage(True/False)`
as a by-name timer guard, with finite lifetime and explicit cleanup. It does not
modify game files. Offline memory-safety/lifecycle checks pass; live protection has
not been validated. See [usage and limits](cheatengine/README.md#damage-guard-toggle-linuxpince-2026-09-06).
Frida follow-up: instrument the actual VM damage paths and direct HP writes during
chapter transitions, then establish an in-process guard for timer-bypassing damage.
