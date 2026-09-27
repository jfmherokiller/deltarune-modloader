# Instance manager

Track A pass **P3**. Cracks `g_pInstanceManager` (`0x1406A9DC8`) and
`Instance_UpdateObjectTypeLinks` (`0x140082340`, previously the highest-xref unnamed
function in the binary at 568 call sites).

## `g_pInstanceManager` (`+136`)

`+136` holds a **pointer to** a generic open-addressing (Robin Hood) hashmap — **not
the header inline** (a mistake this doc first made, live-corrected 2026-09-17 while
building the `cheatengine/` instance-enumeration feature: reading `+136` as the
header directly gives a bogus multi-million "capacity" and a `NULL` slots pointer
against a real running game; dereferencing it once first gives sane values —
`capacity=1024`/`count=399`/`mask=1023`/a valid slots pointer). Confirmed from
`Instance_RegisterWithManager`'s own decompile:
`InstancePool_HashMap_Insert(*(QWORD*)(mgr+136), instance, instance)` — note the
dereference. Same algorithm shape as `VarHashMap`/`g_BuiltinVarNameMap`, just a
separate compiled instantiation — keyed by **pointer value**
(`hash = (7 * (instance_ptr >> 6) + 1) & 0x7FFFFFFF`). This is the live-instance
existence/lookup set: every `YYObjectBase`-derived object that should be tracked as
"alive" gets inserted here.

```
Instance_RegisterWithManager(instance)      [0x14007DBB0, ~40 call sites: gml_method,
                                              the unhandled-exception handler, and
                                              assorted object/instance constructors]
  ├─ InstancePool_HashMap_Insert(*(QWORD*)(g_pInstanceManager+136), instance, instance)  [0x140084130]
  └─ Instance_UpdateObjectTypeLinks(g_pInstanceManager, instance)            [0x140082340]
```

`InstancePool_HashMap_Insert` is a **separate compiled instantiation** of the same
generic Robin-Hood insert as `HashMap_Insert` (`0x140042070`, used for
`g_BuiltinVarNameMap`) and `ObjectTypeTable_HashMap_Insert` (`0x140084350`, see below)
— almost certainly a C++ template (`HashMap<K,V>::Insert` or similar) the compiler
instantiated three times for three different key/value types, not the same function
reused. All three share the same grow-at-threshold-then-rehash structure and
Robin-Hood linear-probe displacement logic.

## `Instance_UpdateObjectTypeLinks` (`0x140082340`)

**Debug-only** — the entire function body is gated on `byte_1405FCF10` (the same
debugger/IDE-connected flag seen gating other debug-tracking code in this project's
other docs). Maintains a **per-object-type linked list of live instances** inside
`g_ObjectTypeTable` (96-byte stride entries, matching the stride already documented in
[07_globals_and_variable_storage.md](07_globals_and_variable_storage.md)'s `typeIndex`
field note): when an instance's type index (`+28` on the *instance*, not on the type
table) increases (object got reparented/retyped, or this is walking a type hierarchy),
it unlinks the instance from its old type's bucket and relinks it into the new one via
`ObjectTypeTable_HashMap_Insert`, using `sub_1401CE1E0` for the actual link/unlink
splice. Also does a live-window sanity check against four counters
(`dword_1408CE26C/78/7C/70`) whose exact roles aren't pinned down — flagged for further
analysis, but clearly some kind of "is this id/generation still in the valid range"
guard, not central to the list-maintenance logic itself.

**Why 568 xrefs**: this is called any time an instance's active object-type
association needs to be re-indexed — not just at creation, but whenever GameMaker's
object-type-swap or instance-reparenting machinery runs. The call-site density reflects
how many different code paths can change an instance's effective type, not that this
function does 568 different things.

## `g_pInstanceManager+136`'s exact layout — CONFIRMED 2026-09-17

Decompiled `InstancePool_HashMap_Insert` fully to close this out (needed for the
cheat-tooling work in `cheatengine/` — enumerating live instances safely from an
external process requires knowing this exactly, not approximately). Declared in the
IDB as `struct InstancePoolHashMap` / `struct InstancePoolSlot`. **Reminder (this
tripped up the first draft of this doc): `g_pInstanceManager+136` holds a *pointer
to* this header, not the header inline — dereference once first.**

```c
struct InstancePoolHashMap {       // at *(g_pInstanceManager + 136)
    unsigned int capacity;          // +0,  slot array length; doubles on rehash
    unsigned int count;             // +4,  live entries
    unsigned int mask;              // +8,  capacity - 1 (bucket = hash & mask)
    unsigned int growThreshold;     // +12, capacity * 0.6; count > this triggers rehash
    struct InstancePoolSlot* slots; // +16
    void* onEvictCallback;          // +24, nullable; called when overwriting a slot's key
};
struct InstancePoolSlot {           // 24 bytes each
    void* value;                    // +0
    void* key;                      // +8,  the instance pointer; hash = (7*(key>>6)+1) & 0x7FFFFFFF
    unsigned int hash;              // +16, 0 = empty slot (sentinel; real hashes are always >=1)
    unsigned int _pad;              // +20
};
```

`Instance_RegisterWithManager` always calls `InstancePool_HashMap_Insert(*(QWORD*)
(g_pInstanceManager+136), instance, instance)` — value and key are the same pointer
for this map's actual usage — so enumerating every live instance is a straight
linear scan, no hashing needed: dereference `g_pInstanceManager+136` once to get the
header, then for `i` in `0..capacity`, read `slots[i]`; if `hash != 0`, `key` (or
`value`) is a live instance pointer (`YYObjectBase`-derived — cast per its
RTTI-confirmed subclass, see `07_globals_and_variable_storage.md`). Live-verified
(against a real running chapter, 399 live instances, plausible on-screen `x`/`y`
positions) and used directly in `cheatengine/deltarune_stats_pince.py`'s new
`list_instances()` feature.

## Open ends / needs further analysis
- `sub_1401CE1E0` (the actual list link/unlink splice `Instance_UpdateObjectTypeLinks`
  calls) — not decompiled.
- The four `dword_1408CE26C/78/7C/70` counters used for the live-window check — purpose
  inferred (id/generation validity range) but not confirmed against a clear write site.
- `g_ObjectTypeTable`'s own full 96-byte entry layout — only the fields touched by this
  pass (`+28` count/threshold-ish per the P2-era debug tracking, and whatever
  `ObjectTypeTable_HashMap_Insert`'s target pointer resolves to per entry) are known;
  not formalized as a struct. Type backlog item already tracks this.
