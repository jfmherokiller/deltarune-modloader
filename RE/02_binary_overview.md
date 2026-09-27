# Binary Overview

## Identity

`DELTARUNE.exe` is the **GameMaker Studio 2 VC_Runner** — the C++ runtime/VM that the
GameMaker IDE bundles with an exported game. It is engine code, identical in structure
to other GameMaker games of the same runtime version; the DELTARUNE-specific content is
in `data.win`. Evidence: the embedded build paths
`D:\a\GameMaker\GameMaker\GameMaker\Runner\VC_Runner\...` on assert/error strings, and
the full set of GameMaker runtime subsystems registered as builtins (see
[04_builtin_functions.md](04_builtin_functions.md)).

## Segments

| Name | Start | End | Size | Perm | Notes |
|---|---|---|---|---|---|
| `.text` | 0x140001000 | 0x14046F000 | 0x46E000 | r-x | Code (20,798 functions) |
| `.idata` | 0x14046F000 | 0x14046FC30 | 0xC30 | r-- | Import directory |
| `.rdata` | 0x14046FC30 | 0x1405F3000 | 0x1833D0 | r-- | Read-only data, strings, vtables |
| `.data` | 0x1405F3000 | 0x1408DD000 | 0x2EA000 | rw- | Globals (builtin table, ds banks, VM state) |
| `.pdata` | 0x1408DD000 | 0x14091F000 | 0x42000 | r-- | x64 unwind info |
| `minATL` | 0x14091F000 | 0x140920000 | 0x1000 | r-- | ATL |
| `.mydata` | 0x140920000 | 0x140921000 | 0x1000 | rw- | |
| `_RDATA` | 0x140921000 | 0x140922000 | 0x1000 | r-- | |
| `.fptable` | 0x140922000 | 0x140923000 | 0x1000 | rw- | Function-pointer table |
| `_guard_c`/`_guard_d` | 0x140923000 | 0x140925000 | 0x2000 | rw- | Control Flow Guard |

Entry point: `start` @ `0x1403C86E4` (MSVC CRT startup).

## Statically-linked libraries and capabilities (from imports)

The import table reveals the full capability surface of the engine:

- **Graphics**: `d3d11` (`D3D11CreateDevice`), `gdiplus` (`GdiplusStartup`), GDI32.
- **Video playback**: Media Foundation — `MF`/`MFPlat` (`MFCreateMediaSession`,
  `MFCreateSourceResolver`, `MFCreateTopology`, …). Backs the `video_*` GML builtins
  (14 of them). Also `WINMM` `mciSendStringA` for legacy audio/video.
- **Networking**: `WININET` (full HTTP client: `InternetOpenA`, `HttpOpenRequestA`,
  `HttpSendRequestA`, `InternetReadFile`, …) and `WS2_32` (raw BSD sockets:
  `socket`/`bind`/`connect`/`send`/`recv`/`select`/`WSAStartup`). Backs `http_*` and
  `network_*` builtins.
- **Rollback netcode (GGPO)**: statically linked. 18 `rollback_*` GML builtins plus an
  internal `GGPO_Log` logger (`0x14020C4E0`) gated by `ggpo_log` / `ggpo_log_file`
  config, writing `log-<pid>.log`. This is GameMaker's built-in rollback multiplayer.
- **Input**: `WINMM` joystick (`joyGetPosEx`), raw input (`GetRawInputDeviceList`),
  `GetAsyncKeyState`, XInput-style gamepad (28 `gamepad_*` builtins).
- **Crash reporting**: `dbghelp` (`MiniDumpWriteDump`, `SymInitialize`, `SymFromAddr`).
- **System integration**: `ShellExecuteW`, `CreateProcessW`, registry reads
  (`RegOpenKeyExW`), `SHGetFolderPathW` (save-data path), clipboard, DWM, DPI awareness,
  `UuidCreate` (GUIDs).

## High-xref core functions (entry points for analysis)

Ranked by cross-reference count (from `survey_binary`). These are the runtime's hot
primitives — now named (see other docs):

| Address | Xrefs | Name (assigned) | Role |
|---|---|---|---|
| 0x1401BAA10 | 2538 | `Function_Add` | Register a GML builtin |
| 0x1401BEF50 | 2237 | `YYGetInt32` | Read arg as int32 |
| 0x1401D7B10 | 1701 | `YYError` | Variadic error reporter |
| 0x1401BEDE0 | 1069 | `YYGetFloat` | Read arg as float |
| 0x140082340 | 568 | *(unnamed)* | Instance depth/grid manager — needs analysis |
| 0x140002C40 | 531 | `RValue_Free` | Release RValue heap payload |
| 0x1401A7C30 | 439 | `gml_ps4_share_screenshot_enable` | Unsupported-platform stub (returns -1; ~hundreds of aliases) |
| 0x140012060 | 417 | *(libc)* | `std::string` destructor (SSO) |
| 0x1401BF490 | 407 | `YYGetString` | Read arg as string |
| 0x14020C4E0 | 205 | `GGPO_Log` | GGPO netcode logging |

> Note on `gml_ps4_share_screenshot_enable`: this single 4-instruction stub returning
> `-1.0` is the registered implementation for **hundreds** of PS4/Xbox One/UWP/Switch
> platform builtins on PC (they alias to the same address). It is the engine's "feature
> not available on this platform" sentinel. See its aliases in the manifest.
