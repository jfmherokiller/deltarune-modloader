# Runtime editor architecture

## Product constraints

- Separate desktop application; familiar modern GameMaker / Studio 2 workflow.
- Game-independent core, including games beyond DELTARUNE.
- Mod source and assets remain loose files. Do not write a rebuilt `data.win`.
- Reuse UTMT where compilation is applicable; do not build a replacement GML compiler.
- Runtime inspection and file authoring are independent of live mutation capabilities.
- UnityExplorer-style live variable browsing/editing is a core fallback, independent
  of live asset/code replacement. See [LIVE_INSPECTOR_PLAN.md](LIVE_INSPECTOR_PLAN.md).
  Read-only browsing is implemented; variable editing remains planned.
- Treat the game as an external process, never as a renderer privilege extension.

## Layers

```
Desktop workspace (Electron + Ace)
    | sandboxed preload IPC
Workspace service                 Runtime client
    | bounded local files             | local per-PID named pipe
Loose mod directory               RunnerEditor / YYToolkit bridge
                                      | safe runner-thread sampling
                                  GameMaker process
                                      |
                                  Game-specific hooks/adapters (future)
```

The native bridge does not decide mod load order, compile GML, or use a named game
controller object. The UI must work without a running game and without an installed
copy of either initial target. Installation locations belong in local settings,
not committed manifests or source constants.

## Capabilities and compatibility

A versioned bridge handshake/snapshot is authoritative for the active connection.
Capabilities are independently supported and must default to unavailable. This
first protocol exposes inspection only; both liveCode and liveAssets are false.
Do not infer live-write support from a matching executable name or successful
YYToolkit initialization.

Protocol v1 remains passive room/event telemetry. When `inspectVariables` is true,
protocol v2 on the separate `RunnerInspector-<pid>` pipe accepts only read requests
for paged globals, instances, or selected-instance fields. All runner work occurs
on the game-thread callback; transport threads only marshal copied display values.
The schema/deadlines/bounds are documented in `runtime/README.md`.

Future commands require a separate implementation and capability for:

- Generation-safe handles for mutation (read-only requests currently re-resolve IDs).
- Editing values on the runner thread, bounded types/ranges, readback verification.
- Reloading sprites/sounds without invalidating resources still in use.
- Replacing VM code while correctly registering symbols, strings, and references.
- Replacing YYC behavior through verified native detours or another supported path.
- Scheduling reload at a safe point, with rollback and structured diagnostics.

Every future mutation request must include the process/session generation. Current
read-only pages are independent samples, not stable cursors or handles: ID reuse
cannot imply object identity across requests. UI request tickets invalidate on
reconnect, target or room change. Pipe disconnect/stale samples clear the connected
state rather than leaving old values displayed as live. The client never executes
commands from game strings or project files.

## Initial targets and evidence

| Target | Existing evidence | Consequence |
|---|---|---|
| DELTARUNE | Existing loader documentation and tested UTMT loose-import path; VM runner | Chapter scope is an adapter concern. Current import happens at data read, not per-script live replacement. |
| Dwarf Eats Mountain 1.1.0 | Existing install `aurie.log` says `m_IsYYCRunner => true` and successful YYToolkit initialization; saved `yytk_probe.log` reports 19 passes | YYC/native game code. Shared inspection is plausible and previously probed, but VM bytecode replacement is not transferable. |

The Dwarf logs are prior-run evidence, not proof that the new RunnerEditor bridge
has been loaded there. Initial discovery was read-only; no injection, binaries,
configuration, saves, or game data were changed. No offsets were imported from
DELTARUNE into Dwarf Eats Mountain.

## Game adapter boundary (planned)

Adapters should supply verified runner/build identity, supported load/reload paths,
resource scope mapping, optional watched globals, and hook capabilities. Prefer
data-only settings when possible. A native hook belongs with its build verification
and tests, not in the desktop UI. Generic fallback has no game-specific globals.

Keep adapters out of the renderer. Never load executable adapter code automatically
from an untrusted mod directory. Hook injection and game restart must be explicit
operations, separate from Open Mod and Save.

## Roadmap

1. Verify the desktop authoring slice and read-only pipe transport.
2. Install/test the generic bridge in each target separately, preserving existing
   loader/framework files and obtaining permission before stopping a running game.
3. Implement the [Live Inspector](LIVE_INSPECTOR_PLAN.md): Globals/Instances browse,
   then explicit scalar editing, readback, and conditional undo. Keep usable with
   liveAssets/liveCode disabled; verify DELTARUNE and Dwarf Eats Mountain separately.
4. Extend the inspector to bounded containers, watches, and opt-in freezes.
5. Integrate GMEdit-specific code intelligence or an embedded GMEdit session through
   a narrow loose-project adapter. Do not confuse Ace syntax mode with GMEdit parity.
6. Implement asset mutation and VM/YYC code replacement as separate tested backends.
7. Add room editing, visual asset tools, and dockable documents.
