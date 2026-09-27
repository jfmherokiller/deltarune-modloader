# DELTARUNE / GameMaker VC_Runner — Data Structures (ds_*) Subsystem

Reverse-engineering notes for the GameMaker `ds_*` subsystem in `DELTARUNE.exe`.
All offsets, sizes and values below are taken directly from the Hex-Rays decompilation
of the named functions; nothing here is guessed. Where something could not be confirmed
it is called out explicitly.

Source files referenced by the runtime (from embedded path strings):
- `Function/Function_Data_Structures.cpp` (the `gml_ds_*` builtin wrappers / bank management)
- `Support/Support_Data_Structures.cpp` (the `CDS_*` container implementations)

---

## 1. The ds bank / handle system

Every ds container type has its **own global "bank"** = a dynamically grown array of
pointers (one slot per live container). A `ds_*_create` builtin:

1. Locks the global `DsMutex` (a single `CRITICAL_SECTION`) — see `GM_Mutex_Lock`.
2. Linearly scans the bank for the **first NULL slot** (a free slot). The scan walks
   indices `0 .. count-1`; the first slot whose pointer is `0` is reused.
3. If no free slot is found and `count >= capacity`, the bank pointer array is grown by
   `GM_ReallocArray(&bank, 8 * (count + 16))` (grow-by-16-slots) and `capacity = count + 16`.
   Then `count` is incremented.
4. Allocates the concrete container object, calls its C++ constructor, stores the object
   pointer into the chosen slot.
5. Returns the **slot index** as a `double` in the result RValue (`*(double*)result = (double)index`).
6. Unlocks the mutex.

So a GML `ds_map`/`ds_list`/... value is simply the **integer index** into the per-type bank.

### Per-type bank globals

Each bank is described by three globals: a pointer to the slot array, a live `count`
(next-index / high-water mark), and an allocated `capacity`. The mapping was confirmed via
the `switch` in `gml_ds_exists` (0x140152B50), which keys on the GML ds-type id (1..6).

| ds type (id)   | bank array ptr            | count                       | capacity                    | element ctor          | object size |
|----------------|---------------------------|-----------------------------|-----------------------------|-----------------------|-------------|
| map (1)        | `g_DsMapBank` 0x1408C8E78 | `g_DsMapBankCount` 0x..E80  | `g_DsMapBankCap` 0x..E70    | `DS_Map_Construct`    | 24 bytes    |
| list (2)       | `g_DsListBank` 0x1408C8E90| `g_DsListBankCount` 0x..E84 | `g_DsListBankCap` 0x..E88   | `DS_List_Construct`   | 48 bytes    |
| stack (3)      | `g_DsStackBank` 0x..EA8   | `g_DsStackBankCount` 0x..EB0| `g_DsStackBankCap` 0x..EA0  | `DS_Stack_Construct`  | 40 bytes    |
| queue (4)      | `g_DsQueueBank` 0x..EC0   | `g_DsQueueBankCount` 0x..EC8| `g_DsQueueBankCap` 0x..EB8  | `DS_Queue_Construct`  | 40 bytes    |
| grid (5)       | `g_DsGridBank` 0x..EE8    | `g_DsGridBankCount` 0x..EF0 | `g_DsGridBankCap` 0x..EE0   | `DS_Grid_Construct`   | 24 bytes    |
| priority (6)   | `g_DsPriorityBank` 0x..ED8| `g_DsPriorityBankCount` 0x..ECC| `g_DsPriorityBankCap` 0x..ED0| `DS_Priority_Construct`| 56 bytes  |

Other globals:
- `g_DsMutex` (0x1408C8E98) — pointer to the single `CRITICAL_SECTION` guarding *all* banks.
  Lazily created on first use: `malloc(8)` holds the pointer, `GM_Mutex_Init` allocates a
  0x28-byte critical section with `InitializeCriticalSectionAndSpinCount(cs, 0x80000400)`.
  The literal name "DsMutex" (0x1404F65B8) is passed to the init wrapper.
- `g_CRC32Table` (0x1408D9E30) — 256-entry CRC32 lookup table used by the hash functions.

The slot layout in memory is interleaved (cap/ptr/count are not strictly contiguous per
type); the addresses above are the authoritative mapping.

### Index validation & errors

Accessor builtins validate `0 <= index < count` **and** `bank[index] != 0`; on failure they
raise `YYError("Data structure with index does not exist.")` (string at 0x1404F6588).
`ds_*_destroy` calls the container destructor, frees the object, and **NULLs the slot**
(so the slot becomes free for reuse by the next create).

---

## 2. ds_map internals

### Map object (24 bytes) — `DS_Map_Construct` (0x1400E5B30)

| offset | type        | meaning                                                              |
|--------|-------------|---------------------------------------------------------------------|
| +0     | `Node**`    | bucket array; `nBuckets * 16` bytes, zero-initialized                |
| +8     | `int`       | bucket **mask** = `nBuckets - 1` (bucket count is a power of two)    |
| +16    | `void*`     | "observer"/change-notification object pointer (0 until a value of a tracked kind is stored) |

Default constructor allocates **256 buckets** (mask `255`, table `256*16 = 0x1000` bytes,
memset to 0). The N-bucket variant `DS_Map_Construct_N` (0x1400E5AA0) takes a bucket count
`a2` (used when `ds_map_create(size)` is called with an argument), sets mask `a2-1` and
allocates `16 * a2` bytes — i.e. a bucket entry is **16 bytes**.

A **bucket** is a 16-byte `{ Node* head; Node* tail; }`.

### Map node (32 bytes) — `DS_Map_InsertNode` (0x1400E6500)

| offset | type      | meaning                                |
|--------|-----------|----------------------------------------|
| +0     | `Node*`   | next (within bucket chain)             |
| +8     | `Node*`   | prev                                   |
| +16    | `int`     | cached key hash                        |
| +24    | `Pair*`   | pointer to the 32-byte key/value pair  |

Insertion prepends to the bucket's chain (maintaining head/tail), then `++map.count`.
Note: the running map element count lives at **map+12** (incremented in InsertNode, and the
field overlaps the high dword referenced as `*(a1+12)` — counted alongside the mask dword
region; confirmed by `++*(_DWORD *)(a1 + 12)`).

### Key/value pair (32 bytes)

Allocated separately (32 bytes) in `DS_Map_Set`. Layout:

| offset | type     | meaning            |
|--------|----------|--------------------|
| +0     | `RValue` | key  (16 bytes)    |
| +16    | `RValue` | value (16 bytes)   |

`DS_Pair_SetKey` (0x1400EE630) writes the key into pair+0; `DS_Pair_SetValue`
(0x1400EE840) writes the value into pair+16. Both free any previous RValue and deep-copy
when the kind is string/array (kind mask `0x46` = bits for kinds 1=STRING, 2=ARRAY, 6=OBJECT).

### Hash function — `DS_HashKey` (0x1400EA490)

The hash is **CRC32** (initial value `0xFFFFFFFF`, table `g_CRC32Table`). Dispatch on the
RValue kind (`*(a1+12) & 0xFFFFFF`):

```
hash(RValue* k):
    kind = k->kind & 0xFFFFFF
    if kind == 0xFFFFFF:           return 0        # sentinel/unset
    switch kind:
      case 1 (STRING):             return CRC32_String( k->ptr ? *(char**)k->ptr : NULL )
      case 2,3,4,6,8,9,11:         return CRC32_Buffer( k, 8 )   # hash 8 raw bytes (ptr/handle/etc.)
      case 5 (UNDEFINED):          return 0
      default (0 REAL, 7 INT32, 10 INT64, 13 BOOL, ...):
                                   d = (kind==0) ? k->val : ConvertToReal(k)   # sub_1401BCFD0
                                   return CRC32_Buffer( &d, 8 )                 # hash the 8-byte double
```

`GM_CRC32_Buffer` (0x140393B80) and `GM_CRC32_String` (0x140393D20) are standard
table-driven CRC32 (`crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8)`), init `0xFFFFFFFF`,
**not** finalized/inverted. These are general GM utilities (also usable elsewhere), hence the
`GM_` prefix rather than `DS_`.

### Lookup — `DS_Map_FindNode` (0x1400E9B40)

```
node = map->buckets[ hash & map->mask ]      # buckets[i] = *(map[0] + 16*i)  -> head
while node:
    if node->hash == hash AND DS_CompareRValues(node->pair->key, searchKey) == 0:
        return node->pair                    # *(node+24)
    node = node->next                        # *(node+8)
return 0
```

Collision handling is **separate chaining**: each bucket holds a doubly-linked list of nodes;
a match requires both the cached hash to match and `DS_CompareRValues` to report equality (0).

### Key comparison — `DS_CompareRValues` (0x1400EFF70)

A large dispatch on `(aKind*16 + bKind)` returning `0` = equal, `1` = a>b, `-1` = a<b,
`-2` = incomparable. Reals compared with an epsilon (`fabs(diff) > eps`), strings compared
via `strcmp`-style byte loop, ints by subtraction. Used as the map-key equality predicate
(treats `0` as "match"). Raises `YYError("Cannot compare unset variables")` /
`"Illegal array use"` for invalid combinations.

### Add / Set / Find-value (builtins)

- `gml_ds_map_add` (0x140156080): `DS_Map_Set(map, &args[1], &args[2])`; returns `1.0` (true)
  if inserted, `0.0` if the key already existed (Set returns 0 when the key is present).
- `gml_ds_map_find_value` (0x1401574C0): `DS_Map_FindNode(map, &args[1])`; on hit copies the
  **value** (pair+16) into the result RValue; on miss returns `undefined` (kind 5).

### Destroy — `DS_Map_Destroy` (0x1400E5E60)

`DS_Map_FreeBuckets(map, mode=1)` walks every bucket chain, frees each node's 32-byte pair
(releasing key & value RValues for string/array kinds) and the node itself; then frees the
bucket array (16-byte-aligned free) and the observer object at map+16.

---

## 3. ds_list internals

### List object (48 bytes) — `DS_List_Construct` (0x1400E5A70)

The list is a C++ object with a vtable (`CDS_List::vftable`, 0x1404EEE88 — confirms the
container is `CDS_List`).

| offset | type       | meaning                                                |
|--------|------------|--------------------------------------------------------|
| +0     | `void*`    | vtable pointer (`CDS_List::vftable`)                   |
| +8     | `int`      | **count** (number of elements in use)                 |
| +16    | `int`      | **capacity** (allocated element slots)                 |
| +24    | `RValue*`  | element array (each element is a 16-byte RValue)        |
| +32    | `int`      | observer/dirty field (init 0)                          |
| +40    | `void*`    | observer/change-notification object pointer (init 0)  |

Constructor zeroes count(+8), capacity(+16), array(+24), +32 and +40 — i.e. the element
array starts NULL and is allocated lazily on first add.

### Append — `DS_List_Add` (0x1400E65A0)

```
if count(+8) >= capacity(+16):
    grow = capacity >> 3            # 1/8 of current capacity
    if grow < 16: grow = 16         # minimum growth of 16 elements
    GM_ReallocArray(&array(+24), 16 * (grow + count))   # element = 16 bytes
    capacity = grow + count
# write element at array + 16*count
slot->kind  = value->kind
slot->flags = value->flags
if kind is string/array/object (mask 0x46): deep-copy via DS_RValueCopy (sub_1400E7850)
else:                                        slot->v64 = value->v64
++count
```

So the grow strategy is **geometric-ish**: enlarge by `max(capacity/8, 16)` elements each
time capacity is exhausted. Element storage is a flat contiguous `RValue[]`.

`gml_ds_list_add` (0x140155090) is variadic: it loops over `args[1 .. argc-1]`, calling
`DS_List_Add` once per value.

---

## 4. Renamed internal helpers

Functions renamed (old `sub_*` → new name). `DS_*` = ds-subsystem-specific; `GM_*` = shared
GameMaker runtime helpers used by (but not exclusive to) the ds subsystem.

| old address   | new name                | purpose (one line)                                                       |
|---------------|-------------------------|--------------------------------------------------------------------------|
| 0x1400E5B30   | `DS_Map_Construct`      | CDS_Map ctor, 256 buckets default (16B bucket, mask=255, observer=0)      |
| 0x1400E5AA0   | `DS_Map_Construct_N`    | CDS_Map ctor with N buckets (mask=N-1)                                    |
| 0x1400E66A0   | `DS_Map_Set`            | Insert key→value (alloc 32B pair); returns 0 if key already present       |
| 0x1400E6500   | `DS_Map_InsertNode`     | Alloc 32B node, prepend to bucket chain, ++count                         |
| 0x1400E9B40   | `DS_Map_FindNode`       | Hash→bucket, chain walk, return pair ptr (or 0)                          |
| 0x1400E5E60   | `DS_Map_Destroy`        | Free bucket chains + pairs + bucket array + observer                      |
| 0x1400E7B90   | `DS_Map_FreeBuckets`    | Walk all buckets/chains and free nodes+pairs (modes 1/2/3)               |
| 0x1400EA490   | `DS_HashKey`            | CRC32 of RValue key, dispatched by kind                                   |
| 0x1400EE630   | `DS_Pair_SetKey`        | Store key RValue into pair+0 (free/deep-copy)                            |
| 0x1400EE840   | `DS_Pair_SetValue`      | Store value RValue into pair+16 (free/deep-copy)                         |
| 0x1400EFF70   | `DS_CompareRValues`     | RValue comparator (0=eq,1=gt,-1=lt,-2=incomparable); map-key equality     |
| 0x1400E5A70   | `DS_List_Construct`     | CDS_List ctor (vtable, count/cap/array=0)                                 |
| 0x1400E65A0   | `DS_List_Add`           | Append RValue, grow array by max(cap/8,16) elements                       |
| 0x1400E5A20   | `DS_Grid_Construct`     | CDS_Grid ctor (w,h)                                                       |
| 0x1400E5C40   | `DS_Stack_Construct`    | CDS_Stack ctor (40B object)                                               |
| 0x1400E5C00   | `DS_Queue_Construct`    | CDS_Queue ctor (40B object)                                               |
| 0x1400E5BC0   | `DS_Priority_Construct` | CDS_PriorityQueue ctor (56B object)                                       |
| 0x1403905D0   | `GM_ReallocArray`       | realloc(*ptr, size); grows bank arrays & list element arrays             |
| 0x140392420   | `GM_Mutex_Init`         | Alloc + InitializeCriticalSectionAndSpinCount (DsMutex)                  |
| 0x140392450   | `GM_Mutex_Lock`         | EnterCriticalSection                                                      |
| 0x140392470   | `GM_Mutex_Unlock`       | LeaveCriticalSection                                                      |
| 0x140393B80   | `GM_CRC32_Buffer`       | CRC32 over N bytes (table g_CRC32Table)                                   |
| 0x140393D20   | `GM_CRC32_String`       | CRC32 over null-terminated string                                        |

### Renamed globals

| address     | new name                  | description                                  |
|-------------|---------------------------|----------------------------------------------|
| 0x1408C8E78 | `g_DsMapBank`             | map slot array                               |
| 0x1408C8E80 | `g_DsMapBankCount`        | map live count / next index                  |
| 0x1408C8E70 | `g_DsMapBankCap`          | map slot capacity                            |
| 0x1408C8E90 | `g_DsListBank`            | list slot array                              |
| 0x1408C8E84 | `g_DsListBankCount`       | list live count                              |
| 0x1408C8E88 | `g_DsListBankCap`         | list slot capacity                           |
| 0x1408C8EA8 | `g_DsStackBank` + Count(0x..EB0)/Cap(0x..EA0) | stack bank                |
| 0x1408C8EC0 | `g_DsQueueBank` + Count(0x..EC8)/Cap(0x..EB8) | queue bank                |
| 0x1408C8EE8 | `g_DsGridBank` + Count(0x..EF0)/Cap(0x..EE0)  | grid bank                 |
| 0x1408C8ED8 | `g_DsPriorityBank` + Count(0x..ECC)/Cap(0x..ED0) | priority bank          |
| 0x1408C8E98 | `g_DsMutex`               | pointer to the shared DsMutex CRITICAL_SECTION |
| 0x1408D9E30 | `g_CRC32Table`            | 256-entry CRC32 table                        |

---

## 5. Things not fully determined

- **Map count field exact offset.** `DS_Map_InsertNode` does `++*(int*)(map+12)`, i.e. there is
  an element-count int at map+12 overlapping the dword region around the mask (map+8). The map
  object is 24 bytes total; +12 is a distinct count separate from the mask at +8. Not
  independently cross-checked against a `ds_map_size` builtin (out of task scope).
- **Stack/queue/grid/priority object internals** were not reversed beyond object size and the
  bank wiring (out of scope; only their constructors were identified/renamed).
- **The "observer" objects** (map+16, list+40, the 152-byte object from `sub_1400E5CA0`) are a
  change-notification/weak-ref mechanism tied to RValue kinds 1/2/6 (string/array/object) for
  GC/struct tracking. Their full semantics were not reversed here; only that they are created
  lazily when a tracked-kind value is stored and torn down on destroy.
- `DS_CompareRValues` epsilon is the caller-supplied `a3` (passed 0 from `DS_Map_FindNode`),
  so map key matching for reals uses an exact `fabs(diff) > 0` test (i.e. exact equality).
