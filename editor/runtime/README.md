# RunnerEditor runtime bridge (read-only)

This is a generic x64 Aurie/YYToolkit **v5** plugin for Runner Studio. It is not
specific to DELTARUNE. Building it does **not** install, inject, launch, close, or
change any game. No code execution, arbitrary commands, global writes, asset
replacement, or screenshot endpoint exists.

## Build and verify

From the repository root on Windows with the Visual Studio x64 C++ tools:

```bat
editor\runtime\build.bat
editor\runtime\build.bat test
node --test editor/runtime/tests/native-interop.cjs
node --test editor/tests/runtime.test.cjs
```

Output: `editor/runtime/build/RunnerEditor.dll`. Build scripts only write under
`editor/runtime/build`; headers are copied from the current `external/YYToolkit`
fork, with its HEAD recorded in `build/YYTK_SOURCE_COMMIT.txt`. Runtime uses the
MSVC dynamic CRT, matching the existing YYTK plugin build conventions.

The Node tests create explicitly labelled SYNTHETIC local named-pipe fixtures.
When `build/native-tests.exe` exists, they also launch that isolated test executable
and read the **production C++ transport** via Node. This is not live-game proof.
Native tests compile/run four executables: telemetry, inspector protocol/value
formatting, inspector transport, and the inspector sampler against an explicitly
synthetic minimal YYTK interface. They cover strict parsing, UTF-8/JSON and response
bounds, exact int64 display, opaque containers, pagination, enumeration status,
builtin routing, dead/duplicate instance IDs, budgets and injected SEH faults.
Transport tests use real Win32 pipes, inspect the actual current-user-only DACL,
exercise malformed/oversized/fragmented frames, queued expiry, one outstanding
request, disconnect/recovery, and bounded idle/nonreading/parked-callback shutdown.
The Node interoperability test issues 40 immediate sequential requests through the
production client and C++ pipe. This proves transport compatibility, not game reads.
Each new behavior group was observed failing before its implementation (RED).

## Integration contract

```js
const runtime = require('./lib/runtime.cjs');
await runtime.discover(); // [{ pid: number, pipe: String }]
await runtime.snapshot(pid); // validated JSON; rejects invalid/stale/offline peers
```

* Pipe: `\\.\pipe\RunnerEditor-<decimal PID>`; local Windows pipes only, no TCP.
* `discover()` enumerates exact pipe names without connecting or sending data.
  It returns up to 256 unique entries sorted by PID. A name is a candidate, not
  proof that a game or compatible plugin is running; call `snapshot()` to verify.
* A connection receives one UTF-8 newline-terminated JSON object immediately,
  with no request or command sent by the client. Reconnect for each snapshot.
* The client deadline covers connect and the complete frame: **1200ms**. Frame
  maximum: **4096 bytes**. Invalid UTF-8/JSON/schema, truncated frames, PID mismatch,
  unsafe capability flags, and sample ages above **3000ms** are rejected.
* `sampledAt`, when supplied, is Unix epoch milliseconds and also checked for
  staleness/future skew. `ageMs` is required and is calculated using
  `GetTickCount64` on the server; it is not derived from the wall clock.
* No implicit game-specific values: **chapter and gold are absent**. Future
  adapters may add optional game fields without changing the generic transport.

```json
{
  "protocol": 1,
  "pid": 1234,
  "room": "room_name_or_null",
  "events": 123,
  "sampledAt": 1790000000000,
  "ageMs": 20,
  "runnerType": "unknown",
  "capabilities": { "inspect": true, "liveCode": false, "liveAssets": false }
}
```

The JSON above is a schema illustration, **not observed game data**. `room` is a
string of at most 512 UTF-8 bytes or JSON null. `events` counts observed object-call
callbacks outside telemetry reentrancy and is captured at sample time.
Before the first successful sample, the bridge reports an invalid/old timestamp
and age, which the client intentionally rejects. Paused or stalled games age out.

## Sampling and runner compatibility

`EVENT_OBJECT_CALL` drives sampling without matching any game object/event name.
The first callback thread is recorded; other threads are ignored. A thread-local
reentrancy guard excludes nested telemetry calls. Monotonic throttling limits
telemetry reads to once per 250ms. Telemetry uses `GetBuiltin("room")` and
`CallBuiltinEx("room_get_name")`, as in the existing generic YYTK probe. Inspector
requests are sampled separately on this same established thread, at most one at a
time; neither pipe worker constructs or destroys RValues.
All `RValue` work is inside the game-thread callback. The telemetry worker receives
only a copied fixed-size POD snapshot; the inspector worker receives owned strings
and scalar fields, never engine pointers. Publishing/request pickup uses try-locks
and never waits for the pipe worker. Aurie interface lookup/version check/callback registration are
framework setup calls, performed in Aurie's initialization context.

The inspected public v5 interface does not expose a supported runner-type query.
Therefore `runnerType` is **unknown**, not inferred from an old log, process name,
or undocumented struct. VM and YYC availability depends on compatible YYTK hooks.
In particular, YYC does **not** imply support for loose GML or bytecode replacement.
`liveCode` and `liveAssets` are always false.

## Security and lifecycle limits

* The pipe DACL grants read/write only to the process user. Windows
  `PIPE_REJECT_REMOTE_CLIENTS` is mandatory. `FILE_FLAG_FIRST_PIPE_INSTANCE` avoids
  silently joining an existing server name. Node needs duplex pipe access, but the
  telemetry v1 worker never reads clients. The independent inspector v2 pipe reads
  only the three strictly validated read-only requests documented below.
* The worker uses overlapped connect/write; writes have a 250ms deadline, followed
  by a bounded consumption grace period. No unbounded `FlushFileBuffers` occurs.
* Stop signals a manual-reset event. The worker owns cancellation and waits for
  cancelled I/O completion before reusing its `OVERLAPPED`/buffer. Unload joins for
  at most 1500ms; if cancellation does not complete, resources are retained rather
  than freed under the thread. There is no forced thread termination.
* **The plugin is pinned until process exit. Hot reload / physical DLL unloading
  is not supported; restart the game through its normal UI to update it.**
  ModuleUnload stops future sampling and the pipe worker. Pinned code/POD cache
  remain resident so parked/in-flight callbacks cannot execute freed instructions.
* The current YYTK fork automatically erases a plugin's callbacks before its
  ModuleUnload, but its callback vector has no dispatch/removal synchronization.
  Consequently **arbitrary live Aurie/YYTK unload cannot be certified safe** without
  a framework change outside this component's ownership. Runner Studio should
  disconnect its client only, not request runtime framework/plugin unload.
* This is same-user local telemetry, **not peer authentication**. Another process
  running as the same user can impersonate an endpoint; never treat a snapshot as
  authority to perform writes or execute code. Discovery is not game identification.
* Live sampling has been verified in DELTARUNE's chapter-select launcher after a
  Steam launch: fresh `PLACE_CHAPTER_SELECT_2x` snapshots and an advancing event
  counter from the confirmed DELTARUNE process. The connector was installed alone;
  existing loader/framework binaries were not replaced. Chapter-transition
  reconnection, Dwarf Eats Mountain, and live Aurie unload remain unverified.

## Read-only inspector v2

The independent `\\.\pipe\RunnerInspector-<pid>` service uses the same current-user
DACL, remote-client rejection and first-instance protection as telemetry. It is
optional: if startup fails, telemetry remains available and does **not** advertise
`capabilities.inspectVariables`. Only a successfully started inspector adds
`inspectVariables:true`; the original telemetry fixture remains unchanged.
`liveCode`/`liveAssets` remain false; no write capability exists.

One connection accepts one ASCII request (LF, not CRLF), up to **128 bytes**:

```text
globals <offset>\n
instances <offset>\n
instance <id> <offset>\n
```

Offsets are decimal integers 0..1000000; IDs are 0..2147483647. No signs, whitespace
variations, extra arguments, mutation/evaluation operations or multiple commands
are accepted. Invalid frames close without a response or engine dispatch. Reconnect
for another page. The next offset is the returned offset plus `items.length`.
Pages are fresh independent samples, not stable database cursors: room/instance
changes can reorder results between pages, so refresh rather than infer continuity.

Each reply is one UTF-8 NDJSON object of at most **65536 bytes**:

```js
{
  protocol: 2, pid, kind: 'globals' | 'instances' | 'instance',
  target: null | id, offset, items: [], hasMore: false,
  sampledAt: epochMilliseconds, ageMs: monotonicMilliseconds,
  room: null | 'room name', error: null | 'explanation'
}
```

`globals`/`instance` items: `{name,type,value,source:'dynamic'|'builtin',readOnly:true}`.
`instances` items: `{id,object}`. At most 64 items; names, values, object/room/error
strings are at most 512 UTF-8 bytes and type labels at most 64. Values are display
strings, with exact decimal int64 (never JavaScript floating-point coercion).
Arrays/structs and unknown pointer-bearing kinds are opaque summaries. There is no
array expansion/editing, raw pointer output or variable-write path. JSON escaping
can shorten a page further to fit the wire bound; `hasMore` then remains true.
Duplicate names/IDs fail closed. Error pages contain no items and `hasMore:false`;
budget exhaustion before any progress is an explicit error, not an empty-more loop.

### Sampling evidence and limits

* Globals use `GetGlobalInstance` and `EnumInstanceMembers` and read the callback's
  RValue fields directly. `OBJECT_NOT_FOUND` after callbacks is the documented full
  traversal result; a callback returns true to stop. An empty/no-callback result
  without any builtin rows is ambiguous in the pinned fork (empty versus failed
  first lookup), so it reports an explicit unavailable/empty-enumeration error.
* Instances use `instance_number(-3)` and `instance_find(-3,index)` (`all`), then
  re-resolve each ID through `GetInstanceObject`, verify its builtin `id`, and obtain
  its object name through `GetBuiltin("object_index")` plus `object_get_name`.
  Builtin signatures are present in the RE manifest. These reads were live-verified
  in DELTARUNE launcher and Chapter 3 on 2026-10-09 (details below).
* Selected instances are re-resolved for every request. Their property order is
  `id`, `object_index`, `x`, `y`, `depth`, `visible`, `persistent`, `sprite_index`,
  `image_index`, `image_speed`, `image_xscale`, `image_yscale`, `image_angle`,
  `image_alpha`, `image_blend`, `speed`, `direction`, `hspeed`, `vspeed`, followed by
  exposed dynamic members (excluding those builtin names). All builtin reads use
  `GetBuiltin`, never the dynamic-member fallback. Unavailable builtin reads return
  an error; compatibility is not inferred from the synthetic tests.
* General `ToString`/`GetKindName` is avoided. Only tagged strings use the bounded
  `ToCString` path; numeric/boolean fields are copied and formatted without runner
  conversion functions. No object/array pointer traversal is performed.
* The adapter converts at most 64 values and checks a QPC-based **4ms discretionary
  budget** between calls and enumeration callbacks. **This is not a hard frame-time
  guarantee:** the supported YYTK `EnumInstanceMembers` implementation gathers all
  names before callbacks and resolves skipped members internally. Its pre-work,
  memory allocation, individual engine calls and offset scans cannot be preempted
  or hard-capped at 64 by this plugin. Large/slow pages fail explicitly if the
  requested offset cannot be reached within the callback budget. Fixing the API's
  internal bound requires a separate YYTK change, outside this component.
* An inspector SEH/C++ fault clears its page and disables future inspector engine
  reads while keeping the pipe's fresh error replies and telemetry running.
  SEH cannot reverse damage or locks inside a faulty engine function; this is a
  fail-closed guard, not a guarantee that an incompatible runner is safe.
* Live DELTARUNE acceptance on 2026-10-09: upgraded only `RunnerEditor.dll` after
  confirming no game process, backed up its predecessor and verified installed
  bytes. Steam launcher returned 110 globals and 8 instances. After a chapter
  transition, Chapter 3 `room_dw_green_room` returned 1,555 unique globals and 76
  unique instance IDs; repeat reads and telemetry remained responsive. Actual
  Electron UI loaded all global pages, read first-page fields on 64 instances,
  and cleared values on disconnect with no renderer errors. Arrays/objects were
  summaries, not dereferenced. Dwarf and mutation compatibility remain unverified.

### Deadlines, queue and fixtures

Read deadline: **250ms** from connection. Sampling deadline: **650ms** after a valid
request. Write deadline: **250ms**, with at most 250ms interruptible read-consumption
grace after the reply. The existing Node 1200ms response deadline is compatible.
Only one queued/dispatched request is retained; the callback rechecks expiry before
sampling. Queued disconnects/expiry are cancelled. A dispatched request remains the
single outstanding job until its callback finishes, even after the client times
out; later requests get a fresh `Inspector busy` error rather than growing a queue.

The worker owns all overlapped buffers until cancellation completes. Stop joins for
at most 1500ms per bridge and retains state on a failed join; jobs own their completion
events independently, so an in-flight callback never points to a worker's stack or
closed event. The DLL remains pinned. No blocking flush or forced thread kill occurs.

For client interop, start `build/inspector-transport-tests.exe --serve-synthetic`.
Its stdout readiness line begins `SYNTHETIC_INSPECTOR_READY`; its own PID identifies
the fixture pipe. It returns labelled fake globals/instances/properties at offset
zero and an empty final page for later offsets. Write a line to stdin to stop it.
This executable never loads YYTK, injects a DLL or accesses game state.

## Cheat Engine pipe (`RunnerCE-<pid>`)

A second, independent pipe `\\.\pipe\RunnerCE-<pid>` serves the Cheat Engine dissector
(`RE/cheatengine/runner_dissector.lua`). Same request grammar, sampler, deadlines, ACL and
remote rejection as the inspector; the editor's `RunnerInspector-<pid>` stays address-free.

* Frame: `uint32 LE length` + the same NDJSON object (CE pipe objects read fixed sizes).
* Reply adds `"addresses":true`; members add `kind` (raw RValue kind) and `address`
  (`"0x…"` live RValue slot from `StructGetMember`/`InternalGetYYVarRef`, `null` for
  builtins, which are `GetBuiltin` copies); instances add `address` (`CInstance*`).
* Still read-only: the pipe never writes. Cheat Engine edits/freezes the RValue itself.
  Slots live until the variable or instance is destroyed; refresh after room changes.
* Live check (DELTARUNE Ch2, 2026-10-09): 1,055/1,056 global slots matched kind+value via
  `ReadProcessMemory` (the miss was the ticking `time`), 54/54 instance member kinds.

## Source basis

* `modloader/yytk_plugin/source/ModuleMain.cpp`: established v5 callback and room reads.
* `modloader/yytk_probe/source/ModuleMain.cpp`: generic `EVENT_OBJECT_CALL` sampling,
  room-name API, isolated SEH guard.
* `external/YYToolkit/YYToolkit/source/YYTK/Shared`: actual public v5 headers.
* `external/YYToolkit/YYToolkit/source/ModuleMain.cpp`: automatic callback removal.
* `external/YYToolkit/YYToolkit/source/YYTK/Module Interface/MI.hpp`: callback dispatch.

No Dwarf game directory was inspected or modified by this component's work.
