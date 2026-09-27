# Error Handling & Exceptions

GameMaker's runtime reports errors through a two-layer system: a variadic formatter
(`YYError`) feeding a central handler (`GML_HandleError`) that builds a GML-level stack
trace and either throws a `YYGMLException` or halts.

## `YYError` (`0x1401D7B10`)

```c
void YYError(const char *fmt, ...);
```

Formats the message into a 1 KB stack buffer via `__stdio_common_vsprintf` and forwards
it to `GML_HandleError`. Two gates:
- `byte_1408CA00F` — when set, `YYError` suppresses output and instead sets the
  pending-error flag `byte_1408C9E21` (used during controlled shutdown / re-entrancy).
- This is the sink for every `YYGet*` type-mismatch message
  (`"%s argument %d incorrect type (%s) expecting a <T>"`).

## `GML_HandleError` (`0x1401B9CE0`)

The core handler. Behavior reconstructed from the decompilation:

1. **Walks the GML call stack** (`qword_1408D02A0` chain) frame by frame, resolving each
   frame's script name and line number via `sub_1401C09C0` / `sub_1401C0AD0`, formatting
   `"%s (line %d)"` and `"%s (line %d) - %s"` entries. Object event scripts named
   `gml_Object_*` have their trailing numeric id stripped to recover the event number.
2. **Formats a context header** depending on `dword_1408BA7F0`:
   - `-2`: `"FATAL ERROR in Room Creation Code for room %s"`
   - `-1`: object/event name header (`"...Name: ... "`)
   - `100000`: `"ERROR in action number %d at time step %d of time line %s:"`
   - else: `"ERROR in action number %d of %s for object %s:"`
3. **Throws or halts**: when not suppressed and an exception context exists
   (`qword_1408C9EE8`), it constructs a `YYGMLException` object (`sub_1401CDC10`) and
   `throw`s it — this is what GML `try/catch` catches. Otherwise it falls through to the
   console path.
4. **Console + abort path**: prints `"ERROR!!! :: %s\n"` through the error console
   (`off_1405F4438` vtable, a `TErrStreamConsole`), records the message into a global
   error slot (`qword_1408C9E38`), sets `byte_1408C9E22`, and — if fatal — sets
   `dword_1405FC4E8 = -400` and calls `sub_1402BCC30` (the abort/quit path).

## `YYGMLException`

The thrown C++ object whose vtables (`TErrStreamConsole`, `tagIConsole`) appear in
`GML_HandleError`. Carries the message, source location, and the captured stack-trace
array assembled in step 1. Caught by GML-level `try`/`catch` (see the `@@try_hook@@` /
`@@throw@@` / `@@finish_*@@` VM intrinsics in
[04_builtin_functions.md](04_builtin_functions.md)).

## Sentinels & globals

| Global | Meaning |
|---|---|
| `byte_1408CA00F` | Suppress error output (shutdown/re-entrancy guard) |
| `byte_1408C9E21` | Pending-error flag set while suppressed |
| `byte_1408C9E22` | An error has been reported |
| `byte_1408C9E23` | Error latch (prevents re-entry of the abort path) |
| `qword_1408D02A0` | Current GML call-stack frame (walked for traces) |
| `qword_1408C9EE8` | Exception context (non-null ⇒ throw instead of halt) |
| `dword_1408BA7F0` | Error-context selector (room/object/timeline) |
| `dword_1405FC4E8` | Set to -400 to signal fatal quit |

## Related debug strings

The MemoryManager (`MemoryManager.h`, 200 xrefs), `Sprite_Class.cpp`, `Hash.h`,
`Room_Layers.h`, `cArray.h` and the data-structure source files all emit asserts through
this same path, which is why their file-path strings dominate the high-xref string list.
