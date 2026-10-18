# Live Inspector: read-only browsing implemented; mutation roadmap

**Decision:** live variable inspection/editing is a first-class Runner Studio work
stream. It does not depend on live sprite, sound, or script replacement succeeding.
Do not declare asset replacement impossible: each feature is separately negotiated
and verified per runner/build.

The installed connector now supports **read-only globals/instances inspection**,
using a separate v2 request endpoint alongside unchanged v1 telemetry. Browsing is
verified live in DELTARUNE launcher and Chapter 3. Mutation, nested expansion,
watches and undo below remain the implementation plan, not shipped editing support.

## Reference and interaction model

Reference: [UnityExplorer](https://github.com/sinai-dev/UnityExplorer), especially
its README sections Object Explorer, Inspector, and Clipboard. Use its browse,
filter, expand, explicitly apply, and inspect-result workflow. Keep Runner Studio's
modern GameMaker-style desktop layout rather than porting UnityExplorer's overlay.

UnityExplorer's Unity/C# reflection backend is not a GameMaker backend. Implement
GameMaker runtime objects and RValues through YYToolkit and verified hooks; do not
promise reflection into every native field or compiled local variable. No source
from UnityExplorer is incorporated by this plan.

## User-facing workspace

A Runtime Explorer beside the existing file Asset Browser:

- **Globals**: searchable runtime-exposed global variable names and values.
- **Current room**: live instances grouped by object asset, with instance IDs;
  persistent instances remain identifiable but are not assumed immortal.
- **Inspector**: Name | Type | Current value | Pending value | Writable/reason.
- **Watch list**: pinned variables with opt-in bounded refresh, visible sample age,
  and a clear disconnected/stale state. Refresh must not overwrite pending edits.
- **Edit controls**: Apply explicitly commits one field; Escape/Cancel discards the
  pending edit. Display the actual readback, not merely the submitted text.
- **History**: old value, requested value, resulting value, target, and outcome.
  Undo is conditional restoration of a value, not reversal of gameplay side effects.

This makes HP, currency, flags, movement values, and other *exposed* game state
editable without recompiling a script or rebuilding a data.win. These are examples,
not universal variable names or guarantees that every field is safely writable.
An optional DELTARUNE adapter can add friendly names/presets from the existing cheat
trainer; the generic inspector must not depend on that adapter.

## Feature tiers

| Tier | Goal | Must work without |
|---|---|---|
| A | Browse/read runtime globals and instance fields | Asset/code replacement or source files |
| B | Explicit typed scalar edits and conditional undo | GML compilation, VM bytecode swapping |
| C | Bounded array/struct inspection and verified element edits | Replacing entire scripts |
| D | Optional watch/freeze controls with explicit opt-in | Any asset reload support |
| E | Live asset and code replacement, each its own capability | Nothing inferred from tiers A-D |

VM and YYC are separate test targets. YYC does not inherently prevent editing
runtime-exposed variables, but discovery/writes must be proven on that build.
Native-only fields, optimized-away locals, pointers, functions, and opaque handles
remain read-only/unsupported unless a specific adapter safely exposes them.

## Available foundations and evidence

- The current connector has a generic object-call callback, reentrancy guard,
  bounded local transport, and fresh room/event snapshots verified in DELTARUNE's
  chapter-select launcher and Chapter 3. A read-only v2 endpoint now enumerates
  globals/instances and reads typed selected-instance fields. There is no setter.
- `modloader/yytk_probe/source/ModuleMain.cpp` exercises `GetGlobalInstance`,
  `variable_instance_get_names`, `EnumInstanceMembers`, `instance_find`,
  `GetInstanceObject`, and `GetBuiltin`. Existing Dwarf probe logs report successful
  reads on a YYC runner. Those tests did **not** establish safe write behavior.
- `external/YYToolkit/.../Shared/YYTK_Shared_Interface.hpp` declares
  `GetBuiltin`, `SetBuiltin`, `GetArraySize`, and `GetArrayEntry`.
- Built-in instance properties such as x/y are not ordinary dynamic members:
  the current fork rejects them through `GetInstanceMember`. Resolve their metadata
  and setter through the built-in interface instead.
- Returning `AURIE_OBJECT_NOT_FOUND` after a full enumeration can mean no callback
  requested early termination; distinguish this from an empty enumeration by
  following the documented callback contract, not by flattening all statuses.

## Implementation boundaries

### 1. Read-only discovery first

Enumerate/paginate a bounded set of instance IDs and variable names on the runner
thread. Prefer supported APIs and verified builtins over native linked-list offsets.
Resolve selected instances again for every operation. Copy typed values into plain
transport data; never send native pointers or retain RValue pointers in the client.

Initially support finite numbers, exact integers, booleans, strings, null/undefined
(read-only), and summaries for containers. Represent int64 as tagged decimal text,
not a JavaScript Number that may lose precision. Bound string length, page size,
container depth, and per-frame work; detect cycles and shared references.

### 2. Versioned command channel, separate from telemetry v1

Do not silently add writes to the existing connect-and-read protocol. Introduce a
new protocol/capability set with operations such as listInstances, listGlobals,
readMembers, readValue, and writeValue. These are proposed operations, not current
APIs. Keep inspectGlobals, inspectInstances, writeScalars, writeBuiltins,
writeArrayElements, liveAssets, and liveCode independent and default-off.

For mutation require explicit editor authorization, a verified local peer process,
a process/session nonce, request ID, selected target token, expected type/value
revision, and a typed replacement value. A pipe name/PID from an untrusted JSON
message is not authentication. Verify server process identity using an OS-backed
mechanism before enabling writes. Do not load arbitrary commands/adapters from mods.

A pipe worker validates and queues bounded messages; only the verified runner-thread
callback touches engine values. Deduplicate requests. Check expiry/session again
when dequeuing. A transport timeout after dispatch is an **unknown outcome**, not
proof no write occurred: reconcile via request status/readback rather than replaying
blindly. Reconnect never auto-reapplies mutations.

### 3. Safe scalar application

Before each write:

1. Verify connected build, capabilities, mutation authorization, and session.
2. Re-resolve the global/member/instance token and validate lifetime. Reject stale
   room/instance generations and reused IDs; if liveness cannot be proved, refuse.
3. Read the present value. Require an expected-value/type match or make the user
   refresh; never overwrite a value that changed unnoticed.
4. Validate parsing/range/writable metadata. Do not coerce a reference into a number.
5. Use the runner/YYToolkit setter with proper RValue ownership. Candidate dynamic
   routes are `variable_global_set` / `variable_instance_set`; confirm semantics
   with the pinned API and live tests before enabling them. Builtins use their
   verified setter; a missing setter stays read-only.
6. Read back immediately on the same runner-thread turn and report the actual value
   and any clamp/failure. A later refresh may show the game overwrote it again.

Do not expose structural builtin changes such as room transitions as ordinary
scalar writes. Begin with a narrow tested set, expanding only after verification.
No arbitrary expression evaluator, C++ call endpoint, raw memory editor, or blanket
'make writable' fallback in the first release.

### 4. Arrays, watches, and undo

Array/struct editors must resolve the path again, check type and bounds at each
step, and use the engine's ownership/copy-on-write semantics. No UI address arithmetic
or cached element pointer. Add container writes only after scalar tests pass.

Undo submits another guarded operation and only succeeds if the target still exists
and its current value matches the earlier readback. It cannot undo a sound, spawned
object, quest transition, or save triggered by the game. Multi-field atomic undo is
not promised.

Watch is read-only by default. A later Freeze feature requires separate opt-in,
per-frame budgets and clear indication that values are being reapplied. Disconnect,
room/session changes, or lost ownership must stop freezes; no auto-resume.

Runtime editing does not directly patch disk files. The game may autosave edited
state, so warn before enabling writes and recommend a save backup. Restarting the
game is not a guaranteed rollback of persistent consequences.

## Acceptance gates

- [ ] Read-only instance/global search and typed values work in DELTARUNE and in
  Dwarf Eats Mountain; report results separately, not inferred from injection.
- [ ] A dedicated disposable test variable can be read, edited, read back, and
  restored in each game without touching the user's save progress.
- [ ] Numeric/boolean/string behavior is verified separately; preserve int64 exactly.
- [ ] Known builtins are routed through their builtin setter, not dynamic members.
- [ ] Invalid type, read-only field, stale revision, destroyed/reused instance,
  room transition, process restart, and unsupported runner all refuse writes.
- [ ] Malformed/oversized requests, unauthorized connection, timeout, duplicate
  request, and cancellation paths are covered by automated transport tests.
- [ ] Array bounds/cycles/ownership tests pass before container editing is enabled.
- [ ] Undo conflicts are visible; pending UI edits survive background refresh.
- [ ] End-to-end UI tests and live-game readback prove application. Synthetic
  transport tests alone do not satisfy live-edit acceptance.
- [ ] With liveAssets and liveCode both false, tiers A/B remain usable.

**Next implementation milestone:** one explicit scalar edit with verified readback,
mutation authorization, lifetime guards and conditional undo. Read-only browsing is
implemented; Dwarf live acceptance and nested expansion remain open. Do not block this
on full live asset replacement, GMEdit integration, or a room designer.
