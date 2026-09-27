# GML Built-in Functions

## The registration system

GameMaker exposes its native API to GML code through a runtime-built **function table**.
At startup the runner calls `Function_Add` once per built-in:

```c
void *Function_Add(const char *name, void *func, int argc);   // 0x1401BAA10
```

It appends a 24-byte record `{ const char* name; void* func; int argc; /*pad*/ }` to a
growable global table at `qword_1408C9E40` (count `dword_1408C9E48`, capacity
`dword_1408C9E4C`, grown by 500 entries at a time). Source file:
`Code_Function.cpp`. `argc == -1` denotes a **variadic** builtin (46 of them).

Built-ins are grouped into per-subsystem registration functions, e.g.
`Register_TimeSource_Functions` (`0x140003640`) registers `time_source_create`,
`time_source_destroy`, … `call_cancel`. There are 2,514 registration call sites across
the binary.

### The registrar functions (named 2026-09-04)

`Function_Add` has exactly **27 distinct callers** — the per-subsystem registrars. A
master dispatcher **`Register_AllBuiltinFunctions`** (`0x1401FFD90`, ex-`sub_1401FFD90`)
calls them in sequence, emitting trace category markers as it goes (`"HighScore.."`,
`"Game.."`, `"Math.."`, `"Graphic.."`, `"Action.."`, `"File.."`, `"Resource.."`,
`"Interaction.."`, `"3D.."`, `"Particle.."`, `"Misc.."`, `"Time.."`, `"DS.."`,
`"Sound.."`, `"Physics.."`, `"Gamepad.."`, `"Buffers.."`, `"Networking.."`,
`"Shaders.."`, `"YoYo.."`, `"Multiplayer.."`, `"Fini"`). Names below are anchored to the
builtin string literals each function passes to `Function_Add` (evidence, not inference).

| Address | Name | Registers |
|---|---|---|
| `0x140003640` | `Register_TimeSource_Functions` | `time_source_*`, `call_later`/`call_cancel` |
| `0x14000A7F0` | `Register_VMIntrinsic_Functions` | `@@...@@` operator intrinsics, `$PRINT`, `$FAIL` |
| `0x14002E690` | `Register_Layer_Functions` | `layer_*` |
| `0x14006D260` | `Register_Camera_Functions` | `camera_*` |
| `0x140137410` | `Register_Gesture_Functions` | `gesture_*` |
| `0x1401519F0` | `Register_Matrix_Functions` | `matrix_*` |
| `0x14015A430` | `Register_DataStructure_Functions` | `ds_*` |
| `0x140161270` | `Register_File_Functions` | `file_bin_*`, `file_text_*`, `directory_*` |
| `0x140168D90` | `Register_MoveCollision_Functions` | `move_*`, `place_*`, `distance_to_*` |
| `0x14016BDB0` | `Register_Gamepad_Functions` | `gamepad_*` |
| `0x140177890` | `Register_Display_Functions` | `display_*`, `window_*`, `draw_enable_drawevent` |
| `0x140179A70` | `Register_IAP_Functions` | `iap_*` (also called from `Register_Platform_Functions`) |
| `0x14017AF70` | `Register_Interaction_Functions` | `show_message`/`show_question`[`_async`], `highscore_*` (Function_Interaction.cpp) |
| `0x1401846D0` | `Register_TypeQuery_Functions` | `is_bool`/`is_real`/`is_array`/… , `typeof` |
| `0x140188380` | `Register_Win8_Functions` | `win8_*` (UWP/WinRT: livetile, appbar, share) |
| `0x14018DE60` | `Register_Event_Functions` | `event_*`, `external_*`, `window_handle`/`window_device`, `show_debug_*` |
| `0x140191590` | `Register_Particle_Functions` | `part_type_*`, `part_system_*`, `part_emitter_*` |
| `0x140196AF0` | `Register_Physics_Functions` | `physics_*` (Box2D) |
| `0x1401A2990` | `Register_Sprite_Functions` | `sprite_*` |
| `0x1401A6A60` | `Register_Audio_Functions` | `audio_*`, `MCI_command` |
| `0x1401A9EB0` | `Register_Platform_Functions` | large catch-all: `virtual_key_*`, `push_*`, `achievement_*`, `cloud_*`, `url_open`/`YoYo_OpenURL`, `clickable_*`, `shop_leave_rating`, `YoYo_Get*`, `get_timer`, `os_get_config`; dispatches `Register_IAP_Functions` + `Register_VMIntrinsic_Functions` |
| `0x1401DDE20` | `Register_Buffer_Functions` | `buffer_*` (uses `g_pBufferMutex`, `Buffer_Alloc`) |
| `0x1401E13D0` | `Register_Vertex_Functions` | `vertex_*` buffer-build ops |
| `0x1401E7820` | `Register_Network_Functions` | `network_*` (uses `SocketMutex`) |
| `0x1401F2960` | `Register_Shader_Functions` | `shader_*` |
| `0x1401F7110` | `Register_VertexFormat_Functions` | `vertex_format_*` |
| `0x14022D830` | `Register_Rollback_Functions` | `rollback_*` (GGPO) |

Not renamed: `sub_1401FFD10` (called from `WinMain`) is a broader runtime-init sequence
that also calls `Register_Camera_Functions`; its individual steps still need analysis.

### Calling convention (verified)

Every built-in is a C function with the uniform signature:

```c
void builtin(RValue *result,   // [out] return value written here
             void   *self,     // current instance context
             void   *other,    // 'other' context
             int     argc,     // actual argument count
             RValue *args);     // argument array (use YYGet* to read)
```

Verified on `gml_abs` (`0x14017C0B0`):
```c
void gml_abs(RValue *result, void *self, void *other, int argc, RValue *args) {
    result->kind = 0;                       // VALUE_REAL
    result->val  = fabs(YYGetReal(args, 0));
}
```
This signature has been applied to all 1,861 recovered builtin functions in the IDB,
which propagates correct `RValue*` typing into their decompilation.

### String results — the RefString

Built-ins that return strings allocate a **RefString** (`{char* data; int refcount;
int length}`, 16 bytes; defined in IDB) and store it in `result->v64` with `kind=1`.
Confirmed in `gml_string_copy` (`0x140180660`), which also shows GameMaker strings are
**UTF-8** — `string_copy`/`string_length` advance by UTF-8 lead-byte width
(1/2/3/4 bytes) rather than by byte.

## Recovery results

- **2,514** `Function_Add` call sites, **all** resolved.
- **1,877** unique target functions; **1,876** renamed `gml_<name>` (one left as a CRT
  false-positive). 91 functions are registered under multiple names (aliases).
- Full machine-readable mapping in `gml_builtins_manifest.json`:
  `{addr, name, all_names, argc, renamed_to, was}`.

## Catalog by family

189 name-families. The distribution maps directly onto GameMaker's documented API
surface. Top families:

| Count | Family | Domain |
|---:|---|---|
| 145 | `layer_*` | Room layer management (the GMS2 layer system) |
| 135 | `ds_*` | Data structures — see [05_data_structures.md](05_data_structures.md) |
| 97 | `physics_*` | Box2D physics integration |
| 95 | `draw_*` | Immediate-mode drawing |
| 94 | `audio_*` | Audio (incl. legacy + buses) |
| 64 | `gpu_*` | GPU render-state |
| 51 | `part_*` | Particle systems |
| 46 | `window_*` | OS window |
| 43 | `date_*` | Date/time |
| 43 | `sprite_*` | Sprite assets |
| 42 | `string_*` | String ops (UTF-8) |
| 40 | `buffer_*` | Binary buffers |
| 39 | `skeleton_*` | Spine 2D skeletal animation |
| 34 | `path_*` | Motion paths |
| 33 | `camera_*` | Cameras/views |
| 29 | `file_*` | File I/O (text + binary) |
| 29 | `vertex_*` | Vertex buffers |
| 28 | `gamepad_*` | Gamepad input |
| 25 | `YoYo_*` | Misc engine/internal |
| 24 | `sequence_*` | GMS2 sequences |
| 24 | `tilemap_*` | Tilemaps |
| 23 | `instance_*` | Instance lifecycle |
| 23 | `room_*` | Rooms |
| 21 | `font_*`, `gesture_*`, `mp_*`, `surface_*` | Fonts / touch gestures / motion planning / surfaces |
| 20 | `array_*`, `display_*`, `object_*` | Arrays / display / object metadata |
| 19 | `time_*` | Time sources |
| **18** | **`rollback_*`** | **GGPO rollback multiplayer** |
| 16 | `keyboard_*`, `matrix_*`, `network_*`, `is_*` | Input / matrices / sockets / type tests |
| 14 | `shader_*`, `video_*` | Shaders / Media-Foundation video |

(See `family_stats.txt` / the manifest for all 189 families and the long tail of
math/trig/utility singletons like `abs`, `sin`, `lerp`, `clamp`, `choose`, `md5`,
`sha1`, `base64_*`, `json_*`.)

## GGPO rollback (Track A P7, 2026-09-17)

**The real online-multiplayer path is disabled in this build.** Decompiled
`gml_rollback_create_game` (`0x14022E270`): the session backend to construct is chosen
by `g_RollbackSessionType` (`0x140675EF8`, renamed from `dword_140675EF8`):

| `g_RollbackSessionType` | Meaning | What happens |
|---:|---|---|
| `1` | Local synctest | Constructs a `SyncTestBackend` (`sub_1402244C0`); "Starting in local synctest mode, all events will fire twice." — a debug mode that re-runs every frame to verify determinism, not real netplay. |
| `2` | Online rollback | Hits `YYError("Multiplayer rollback is only supported in the operagx target.")` and aborts — **this Steam/Windows build does not implement it.** `"operagx"` is Opera GX's browser game distribution; presumably only *that* build target links a working `Peer2PeerBackend`/`UdpBackend`. |
| else (default) | Single player | Constructs a `SinglePlayerBackend` (`sub_140224250`); "Starting in single player mode." |

`g_RollbackBackend` (renamed from `qword_1408D7C68`) holds the active session as a
polymorphic `Backend*` — the class hierarchy is confirmed by RTTI (found during the P2
sweep, [07_globals_and_variable_storage.md](07_globals_and_variable_storage.md)):
`Backend` (base) ← `Peer2PeerBackend` / `SinglePlayerBackend` / `SpectatorBackend` /
`SyncTestBackend` / `UdpBackend` (+ `UdpProtocol`/`UdpRelayProtocol`/`UdpSocket`
plumbing for the networked backends). Calls like `gml_rollback_start_game` just
virtual-dispatch through this object's vtable (e.g. slot `+112`); the 18 `rollback_*`
builtins are a thin GML-facing shell over this backend object, not GGPO's own algorithm
implementation (that lives in the vendored GGPO library itself — third-party code, not
DELTARUNE-specific, and not re-derived here).

**Conclusion**: since this repo's binary is the Steam build, `rollback_*` in practice
only supports single-player and local-synctest-debug session types; the actual GGPO
rollback/prediction machinery for cross-network play is present in the binary (the
`Peer2PeerBackend`/`UdpBackend` classes and their vtables exist) but is unreachable
from this build's own `rollback_create_game` entry point. Deeper GGPO internals
(save-state snapshotting, input prediction, `GGPO_Log`) were intentionally not pursued
further — TODO.md's own P7 note already flagged this as "only matters if netplay is in
scope," and it now demonstrably isn't for this shipped build.

## Subsystem internals: `layer_*` (Track A P5, 2026-09-17)

Picked the largest family (145 builtins) to establish the pattern. Decompiled
`gml_layer_create` and `gml_layer_x`; both funnel through the same "resolve current
layer-list owner" logic:

```
dword_1405F414C == -1?  -> owner = qword_1408BA790            (no explicit layer context set)
else                     -> owner = qword_1405FC528[dword_1405F414C]   (id -> owner lookup table)
                            if that's stale/missing:
                              owner = Layer_ResolveCurrentOwner()      (renamed from sub_1400B6330)
                              fall back to qword_1408BA790 if still null
```

`owner` (very likely the live *room instance* — not the `0x218`-byte on-disk `ROOM`
chunk record from `LoadGameData_ProcessChunk_ROOM`, but a separate larger runtime
object; not confirmed) holds a **linked list of layer nodes at `owner+376`**, each node
roughly `{ +4 depth/order?, +8 x (float), +32 name pointer, +36 flags byte,
+136 next-pointer, +144 nested-sublist }` (fields inferred positionally from these two
functions' field accesses, not exhaustively mapped). Two ways builtins reach a specific
layer node from there:

- **By numeric id**: `Layer_FindById(owner, id)` (renamed from `sub_14002D610`).
- **By name**: linear-scan the `+376` list comparing each node's `+32` name pointer
  against the requested string (`sub_14042B5A0`, likely a `strcmp`-family compare, not
  renamed — not confirmed).

`gml_layer_create` additionally auto-generates a `"_layer_<hex id>"` name when none is
given, and does its own linked-list insertion sorted by the new layer's depth (walking
`+136`/`+144`, splicing via `sub_140030000`/`sub_140016290`/`sub_140016550` — the
depth-ordered insert variants for "before/after/head" cases, not individually named).

**Established pattern for future P5 sweeps of other families**: a GML builtin resolves
a "current context" (room/layer/instance/etc, usually via a similar
`-1`-sentinel-plus-lookup-table-plus-fallback idiom), then operates on a linked list or
array of internal engine records hanging off that context at a fixed offset. `physics_*`
(97), `draw_*` (95), and `audio_*` (94) are the next-largest families and almost
certainly follow the same shape against `b2World`/`CBitmap32`/`cAudio_Sound`-family
objects respectively (see the RTTI class list in
[07_globals_and_variable_storage.md](07_globals_and_variable_storage.md)) — not pursued
further this pass; picking one and repeating this same trace-two-builtins approach is
the natural way to continue P5.

## VM intrinsics (`@@...@@`)

A distinct group of internal compiler/VM operations is registered with sentinel
`@@name@@` identifiers — these implement GML language semantics rather than library
functions:

| Name | Purpose |
|---|---|
| `@@NewGMLObject@@` / `@@NewObject@@` | Construct a struct/object instance |
| `@@NewGMLArray@@` / `@@array_*@@` | Construct an array literal |
| `@@This@@` / `@@Other@@` / `@@Global@@` / `@@GlobalScope@@` | Scope/context resolution |
| `@@GetInstance@@` | Resolve an instance reference |
| `@@NewProperty@@` | Define a struct property |
| `@@typeof@@` / `@@instanceof@@` | `typeof` / `instanceof` operators |
| `@@new@@` / `@@delete@@` / `@@throw@@` | `new` / `delete` / `throw` statements |
| `@@try_hook@@` / `@@finish_*@@` | try/catch/finally machinery |
| `@@Null@@` / `@@NullObject@@` | null sentinels |

These are the entry points if one wants to study how the GML VM implements language
constructs (object creation, exceptions, scoping).
