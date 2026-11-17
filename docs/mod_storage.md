# Mod storage: per-mod config and data folders (ROADMAP 47)

Research and decided design for BepInEx-style mod storage. Work is planned in two cards: the
research card t_36d7f648 (this file) and the implementation card t_bd9dfca1.

```
<game>\mods\config\<ModId>.ini        settings (like BepInEx\config\<GUID>.cfg)
<game>\mods\data\<ModId>\             anything else a mod saves
<game>\mods\data\<ModId>\logs\        logs, transcripts
```

This layout matches BepInEx: settings live in one central folder, not inside the mod's own
folder, so updating or deleting a mod never wipes its settings.

The findings below come from static analysis (IDA 9.1, scratch copies of the IDBs) of two
runners:
- DELTARUNE, the VM runner (v2022.0 era, `VC_Runner`);
- Dwarf Eats Mountain v1.1.0, the YYC runner (`GMS2-Runner-Main`, 2024.x).

Addresses are for those builds only. The implementation must find every routine by its unique
string (see the table), never by address. Nothing was run live. The live write/read check on
both games is part of t_bd9dfca1.

## Summary of decisions

| Question | Answer |
|---|---|
| Can we add GML built-ins? | Yes, on both runners, by calling the runner's `Function_Add` |
| When? | Right after `Register_AllBuiltinFunctions` returns. A detour installed in `ModulePreinitialize` calls `Function_Add` at that point. This is before the `FUNC` chunk is resolved (VM) and before any code runs |
| Do GML file functions reach `<game>\mods\...`? | No, on either runner (see "File sandbox") |
| Sandbox fix | (b): one detour on the runner's save-path mapper. It passes paths under `mods\config\` and `mods\data\` through unchanged. No native write helpers |
| GML API | `modloader_config_path(mod_id)`, `modloader_data_dir(mod_id)`, `modloader_log_dir(mod_id)`. Each returns an absolute path, or `""` |
| Compiler | Declare the three names as built-in functions in the UTMT `BuiltinList` (one shared C# list) |
| Owner module | ModHost. It already owns `PluginInfo.configPath`, ships in every install and runs on both runners |
| YYToolkit | Has no public way to add built-ins. The feature is generic, but it goes upstream only after it is verified here (see "Upstream") |

## 1. Adding built-ins: `Function_Add`

### The routine

```c
// DELTARUNE 0x1401BAA10 (named Function_Add), DEM 0x14100BC80
void Function_Add(const char* name, TRoutine func, int argc);   // argc -1 = variadic
// TRoutine = void (*)(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
```

The routine appends a 24-byte record `{name, func, argc}` to the global `the_functions` array.
The array grows by 500 entries at a time.
- DELTARUNE: array at `0x1408C9E40`, count at `+8`, capacity at `+0xC`.
- DEM: array at `0x141E8AC80`, with the same layout.

The two runners differ in two ways:

| | DELTARUNE (VM) | DEM (YYC, newer runner) |
|---|---|---|
| Name storage | The `name` pointer is stored, not copied | Same in the array. The name is also inserted into a `std::unordered_map<std::string,int>` (FNV-1a hash) next to the array |
| Duplicate name | Appended silently | `YYError("Function_Add being called twice for the same function name %s")` |
| Lookup (`Code_Function_Find`, DR `0x1401BA8B0`, DEM `0x14100BA20`) | Scripts (`100000+`), then extension functions (`500000+`), then a linear scan of the array | Same order, but the built-in step is a hash-map lookup |

Two consequences for our code:
- Pass names with static storage duration.
- Register each name exactly once per process, guarded by a flag.

A script with the same name would shadow a built-in, because the lookup checks scripts first.
The `modloader_` prefix avoids that.

### Finding it on both exes (no fixed addresses)

Each entry below was checked in both IDBs, and each string is referenced by exactly one
function.

| Routine | Unique anchor | DELTARUNE | DEM |
|---|---|---|---|
| `Function_Add` | The only function that references a string ending in `\Code\Code_Function.cpp` (its realloc call passes the source file). DEM also has `"Function_Add being called twice…"` | `0x1401BAA10` | `0x14100BC80` |
| `Register_AllBuiltinFunctions` | The only function that references `"Fini\n"`. It is also the first direct `call` after the `lea` of `"InitGMLFunctions\n"` | `0x1401FFD90` | `0x140F93DB0` |
| Data.win header pass (calls the above) | References `"InitGMLFunctions\n"` | `0x1400B7520` | `0x140F1D800` |
| `FUNC` chunk loader | References `"Error on load\nUnable to find function %s\n"` | `0x1400B72E0` | `0x140F1D600` |
| Save-path mapper (`LoadSave::_GetSaveFileName`, our name) | References `"Error! not allowing file operation with filename '%s'.\n"` | `0x140092580` | `0x14100B260` |

Use `yyc::vm::detail::FunctionReferencing(text)` from `yyc_interop/include/vm_hooks.hpp`. It
finds the one function whose code takes a RIP-relative `lea` of a string, and it is how
`VM_PushFrame` is found. Move it to a shared header so ModHost can include it without the VM hook
code. Then give it a suffix-match variant, because the `Code_Function.cpp` path differs between
runner builds.

### When to call it

These are the startup steps on DELTARUNE (the `WinMain`-era bootstrap, `0x1401FF5E0`):

1. `RunnerLoadGame`: data.win is read (LooseMods serves the patched copy here).
2. The data.win header pass. On `GEN8` it prints `InitGMLFunctions` and calls
   `Register_AllBuiltinFunctions`. Every built-in is registered here, not at process start.
3. `VM Init`, `Do The Work`.
4. `LoadGameData` → `LoadGameData_ProcessChunks` → the `FUNC` chunk loader. Every bytecode call
   name is resolved here with `Code_Function_Find`. A name that is not found gives
   `"Unable to find function %s"`, followed by `exit(1)`.

The timing options compare as follows:
- **`ModulePreinitialize` directly (entry point parked): no.** On DEM, the `unordered_map` that
  `Function_Add` inserts into is a static C++ object. It is only constructed when the CRT starts,
  after the entry point resumes. On DELTARUNE a direct call would work, but it would put our
  functions in front of the real ones.
- **After the game is up (`ModuleInitialize`, first object event): too late for VM bytecode.**
  The `FUNC` chunk has already been resolved, and a data.win that calls our functions would have
  exited at load. This timing is enough for runtime GML only (see 2c).
- **Decided:** in `ModulePreinitialize`, install an Aurie `MmCreateHook` detour on
  `Register_AllBuiltinFunctions`. The detour calls the original, then `Function_Add` for each of
  our built-ins. This works on both runners:
  - it runs on the game's main thread;
  - the CRT and the map exist by then;
  - our functions come after every real one;
  - it is still before `FUNC` resolution.

  DEM's data.win still has `GEN8`, so the same path runs on YYC.

## 2. Calling them from GML

### 2a. VM bytecode in data.win (DELTARUNE)

A `call.i` operand in the file is a chain. Each word holds the distance to the next use of the
same function, and a `FUNC` entry gives `{name, occurrences, first address}`. At load, the
`FUNC` loader resolves each name with `Code_Function_Find` and writes the runtime id into every
link of the chain. A bytecode 17+ file uses the counted variant. A built-in's id is its index in
`the_functions`, and the VM later calls `the_functions[id].func` (RE/10).

For a call to `modloader_data_dir` to load, two things are needed:
- the name is registered before the `FUNC` loader runs (section 1);
- the compiler emitted a `FUNC` entry and a `call.i`, not a variable call (2b).

If ModHost is missing, a data.win that uses these functions exits at load with "Unable to find
function". ModHost ships in every install (`tools/package_release.py`), so this is acceptable.
LooseMods should log a clear warning if it compiles such a call while `mods\aurie\ModHost.dll`
is absent.

### 2b. The compiler (LooseModCompiler / UTMT 0.9.2 + Underanalyzer)

`SimpleFunctionCallNode.GenerateCode` decides how a call compiles:
- If the name is a known global function, it compiles a direct call.
- Otherwise, on GMLv2, it compiles a variable call (`push self.modloader_data_dir` + `callv`).
  That fails at run time.

"Known global function" includes the entries in `IBuiltins.LookupBuiltinFunction`. That is
UTMT's `BuiltinList.Functions`, a public `Dictionary<string, FunctionInfo>`, reached through
`data.BuiltinList`. When a call to a built-in is compiled, `CodeBuilder` adds the `FUNC` entry
with `data.Functions.EnsureDefined(name)`.

The fix is one shared helper in `compiler/GmlObjectCompiler/GmlObject.cs`. LooseModCompiler
already compiles that file in, next to `ImportLooseMod.csx`:

```csharp
public static void DefineModloaderBuiltins(UndertaleData data)
{
    data.BuiltinList ??= new BuiltinList(data);
    foreach (var (name, argc) in ModloaderBuiltins)       // the one C# list
        data.BuiltinList.Functions.TryAdd(name, new FunctionInfo(name, argc));
}
```

Call it in three places:
- `ImportLooseMod.csx`, before building the `CodeImportGroup`;
- `ObjectCompiler.PrepareContext`, for VM data too. Today that method returns early for VM;
- the runtime path, `RuntimeExports.CompileObject`, through `PrepareContext`.

The C++ table in ModHost and this C# list are the only two copies. Add a unit test that
compares them; the test can read the C++ source.

### 2c. Runtime GML (`gmlo_link.hpp`, both runners)

`gmlo::Linker` resolves each `func` relocation with YYToolkit's `GetNamedRoutineIndex`.
That is a direct call to the runner's `Code_Function_Find`, with no cache:
- On DELTARUNE, that function re-scans `the_functions` up to the current count on every call.
- On DEM, it looks the name up in the hash map that `Function_Add` updates.

So a built-in added at any time before the link is found, on both runners. The VM then calls it
through the array by index, re-reading the array pointer, so a later reallocation is safe. The
runtime path compiles through the same `PrepareContext`, so the declaration in 2b covers it.

## 3. File sandbox

Every GML file function first maps its path through the runner's save-path mapper:
- `file_text_open_write/append/read`, `ini_open` → `IniFile` load and save, `file_exists`,
  `directory_create`, `file_delete`, `file_bin_open`, `buffer_save`, …;
- in DELTARUNE, 46 call sites go through `0x140092580`;
- `ReadEntireFile_Plain` goes through it too.

The mapper runs `GetFullPathNameW` on the argument, then checks the prefixes in this order:

| Full path starts with | DELTARUNE (older runner) | DEM (2024.x runner) |
|---|---|---|
| the current directory (`cwd` = game folder) | **Rewritten** to `<SavePrePend>\<rest>`, i.e. `%LOCALAPPDATA%\<game>\…` | Same, but only when the sandbox is on |
| `working_directory` (`0x1408BA818` / `0x141E89D68`) | Rewritten into the save area | Same, sandbox on only |
| `SavePrePend` | Kept | Kept, sandbox on only |
| the parent-app working dir (`0x1408BA828` / `0x141E89D78`) | Kept, with a warning | Same |
| anything else | Allowed if the sandbox is off or the path was picked in a `get_save_filename` dialog (allow list at `0x140693950`); otherwise `"Error! not allowing file operation…"` and an empty path | Same |

The "sandbox off" flag is `OPTN` flags bit 28 (`DisableSandbox`). It is read in
`LoadGameData_ProcessChunk_OPTN` (DR `0x14009A13B`, `shr eax,1Ch; and al,1` → `0x1406A7AD0`) and
in DEM's OPTN handler (`0x1410937EB` → `0x141E9760C`).

The `OPTN` flags of the shipped data.win files:
- DELTARUNE (launcher and chapters): `0x38400204`, bit 28 **set**, so the sandbox is off;
- DEM: `0xC00400230`, bit 28 **clear**, so the sandbox is on.

What this means:
- **DELTARUNE:** the sandbox is off, but DELTARUNE's runner rewrites every path inside the game
  folder before it checks the flag. `file_text_open_write("<game>\mods\data\X\a.txt")` writes to
  `%LOCALAPPDATA%\DELTARUNE\mods\data\X\a.txt`. This matches what the owner saw with
  DialogueBacklog, whose transcript went to the save folder (inside the Wine prefix under Proton).
- **DEM:** the sandbox is on, so the same call is redirected into the save area. A path outside
  both the game folder and the save area is refused.
- **Either way, GML cannot write into `<game>\mods\` today.**

### Decision: (b) lift the sandbox for `mods\config\` and `mods\data\` only

Install one Aurie detour on the save-path mapper. It is found by its unique string on both exes,
and its signature is `int (char* out, int outSize, const char* path)`:

```
full = GetFullPathNameW(utf8→wide(path))
if full starts (case-insensitively) with <exe dir>\mods\config\ or <exe dir>\mods\data\:
    out = utf8(full); return 0          // same as the runner's own pass-through branch
else:
    return original(out, outSize, path)
```

Why (b) and not (a), native write helpers:
- With (b), GML mods use the normal GML file API: `file_text_*`, `ini_*`, `buffer_save/load`,
  `file_exists`, `directory_create`, `file_delete`. With (a) we would re-implement a subset of it
  in C++, which is duplicated code and a second API to learn.
- `ini_open(modloader_config_path(id))` writes a normal INI file, which native plugins read with
  `GetPrivateProfileStringW`. That is the shared convention.
- The detour is narrow. Vanilla code never uses those two prefixes, and every other path (saves,
  other mods' folders, the loader's DLLs under `mods\aurie`) behaves exactly as before.
- The cost is one runner hook, found by a string present in both runner generations.

Reads need no extra hook. `file_text_open_read` tries the bundle (game folder) first, then
`ReadEntireFile_Plain`, which goes through the hooked mapper. `ini_open` and `file_exists` use
the hooked mapper as well. The read-side mapper (`0x1400921B0` on DR, which references the same
allow list at `0x140693948`) is left alone. The live test in t_bd9dfca1 must still check each
function on both runners: write, append, read back, `ini_open`/`ini_close` round trip,
`file_exists`, `directory_create`.

**Fallback.** If the mapper is not found, or the hook fails, all three GML functions return `""`
and ModHost logs once. A mod then writes relative names (`"" + "x.txt"`), which land in the save
area as before. Nothing breaks.

INI interop. GameMaker writes `key="value"`. `GetPrivateProfileStringW` strips the quotes, but
`GetPrivateProfileIntW` does not parse `"1.000000"` as 1. Native code should read strings and
convert them itself. GameMaker loads the whole INI at `ini_open` and rewrites it at `ini_close`,
so the GML and native sides of one mod must not both write the same file.

## 4. API

### GML built-ins (registered by ModHost)

| Function | argc | Returns |
|---|---|---|
| `modloader_config_path(mod_id)` | 1 | `"<game>\mods\config\<mod_id>.ini"`. Creates `mods\config\`; the file is created by the mod |
| `modloader_data_dir(mod_id)` | 1 | `"<game>\mods\data\<mod_id>\"` (trailing `\`). Creates the folder |
| `modloader_log_dir(mod_id)` | 1 | `"<game>\mods\data\<mod_id>\logs\"`. Creates the folder |

All three return `""` in these cases:
- `mod_id` is invalid;
- the folder cannot be created;
- the sandbox detour is not active (see Fallback).

Paths are absolute, use backslashes, and are UTF-8. GameMaker accepts `/` appended after them.

A valid `mod_id` is 1–64 characters from `[A-Za-z0-9._ -]`. These are rejected:
- `.` and `..`;
- a leading or trailing dot or space;
- any `/`, `\`, `:` or other character outside the set;
- Windows device names (`CON`, `NUL`, `COM1`, …).

For a loose GML mod, `mod_id` is its folder name under `mods\` by convention. For a native
plugin it is `plugin.ini`'s `id`. Both share one namespace on purpose: a mod with a GML half and
a native half uses one id.

```gml
var _cfg = modloader_config_path("DialogueBacklog");
if (_cfg == "") _cfg = "dialogue_backlog.ini";               // old location (save area)
ini_open(_cfg); ini_write_real("ui", "lines", 40); ini_close();

var _log = modloader_log_dir("DialogueBacklog");
var _f = file_text_open_append(_log + "transcript.txt");      // "" + name = save area fallback
file_text_write_string(_f, "hello"); file_text_writeln(_f); file_text_close(_f);
```

### Native: `modhost.hpp` (append only, ABI-safe for built plugins)

```cpp
struct PluginInfo {
    const char* id; const char* name; const char* version;
    const wchar_t* folder;       // mods\plugins\<folder>, absolute
    const wchar_t* configPath;   // CHANGED: mods\config\<id>.ini; may not exist yet
    const wchar_t* dataDir;      // NEW (appended): mods\data\<id>\, created on first GetDataDir/plugin load
};
struct ModHostInterface : Aurie::AurieInterfaceBase {
    /* ... existing methods unchanged, in the same order ... */
    // NEW (appended). Same rules and same code as the GML built-ins. Returned pointers stay
    // valid for the process lifetime. nullptr for an invalid id.
    virtual const wchar_t* GetConfigPath(const char* modId) = 0;
    virtual const wchar_t* GetDataDir(const char* modId) = 0;
};
```

`QueryVersion` reports 1.1. A plugin that needs `dataDir` checks the version first.

**`configPath` migration.** At plugin load, ModHost checks for the old file. If
`<plugin folder>\config.ini` exists and `mods\config\<id>.ini` does not, it moves the file with
`MoveFileExW` (no overwrite) and logs the move. If both exist, it keeps the new file and logs that
the old one is ignored. Update HelloPlugin, `modhost/README.md` and the `PluginInfo` comment.

### Ownership and layout of the code

ModHost owns it all, and the logic lives in one place:
- `modhost/source/Storage.{hpp,cpp}`: id validation, the mods root (from the exe path), path
  building and folder creation. It has no runner dependencies, so `modhost/tests` can unit-test it.
- `modhost/source/GmlBuiltins.cpp`: finds `Function_Add` and `Register_AllBuiltinFunctions` and
  installs the post-registration detour (from `ModulePreinitialize`; ModHost does all its work in
  `ModuleInitialize` today). It also holds the three `TRoutine`s, thin wrappers over `Storage`,
  which return strings with `YYCreateString` from the runner interface.
- `modhost/source/SandboxHook.cpp`: the save-path mapper detour. Its allowed roots come from
  `Storage`.
- The string-xref finder moves out of `vm_hooks.hpp` into a shared header that `vm_hooks.hpp`
  includes, so there is no second copy.
- LooseMods, RunnerCompiler and other modules do not repeat any of this. If they need a path, they
  call `modhost::Get()->GetDataDir(id)`.

Why not the alternatives:
- **LooseMods:** its file hooks are fixed-RVA and DELTARUNE-only, it is not the module native
  plugins talk to, and it would need ModHost's paths anyway.
- **A new module:** it would split one convention across two DLLs.

## Upstream (YYToolkit)

YYToolkit has no public way to add a built-in. `YYTKInterface` offers lookup only:
`GetNamedRoutineIndex/Pointer`, `CallBuiltin`. YYToolkit locates `the_functions` from
`Code_Function_Find` / `Script_Perform`, but never `Function_Add`.

An `AddBuiltinFunction(name, routine, argc)` would be generic and small. It needs two parts:
- a queue;
- a flush after `Register_AllBuiltinFunctions`.

A plain immediate call is not enough, for the timing reasons in section 1, unless the caller only
needs runtime GML.

Under the upstream policy in ROADMAP.md (generic, small, verified in a game), it goes to the fork's
`deltarune` branch only after the ModHost version passes the live test on both runners. Then:
1. Add the method at the end of `YYTKInterface`, as `ExecuteCodeObject` was.
2. Switch ModHost to call it and delete ModHost's own finder and detour.
3. Offer it upstream as a PR.

Building it in the fork first would mean RE work on YYToolkit's staged init order, with no game
verification behind it yet.
