# YYTKProbe

Game-independent self-test for YYToolkit v5. Drop `YYTKProbe.dll` into `mods\aurie\`, start the
game, and read `yytk_probe.log` next to the exe. About 10 s after the first frame it calls, once,
from inside a GML event, every YYTK API a runtime editor relies on (frame events, globals, rooms,
instance lookup, member enumeration and reads, built-ins, routine lookup). Each call runs under
SEH, so a crash is logged as `FAIL ... exception 0xC0000005` instead of killing the game.
Set `YYTK_PROBE_GLOBAL=<name>` to also read a specific global.

Build: `build.bat` (compiles against the `external/YYToolkit` submodule headers).

DELTARUNE Ch2, YYToolkit fork `deltarune` branch: 18 passed, 0 failed (2026-09-28).
