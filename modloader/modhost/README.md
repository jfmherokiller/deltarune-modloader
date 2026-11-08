# ModHost

ModHost is the plugin host of this loader, in the role BepInEx's chainloader plays for Unity
games. It works on both kinds of GameMaker game:
- **VM** games run GML bytecode, e.g. DELTARUNE;
- **YYC** games run natively compiled GML, e.g. Dwarf Eats Mountain.

It is an Aurie module. It reads a manifest per plugin, decides which plugins load and in which
order, maps them with Aurie, and gives them a small interface (`include/modhost.hpp`):
- their own folder and config path;
- a shared log;
- the runner type.

```
<game>\
  version.dll                       loader (modloader/)
  mods\aurie\AurieCore.dll, YYToolkit.dll, ModHost.dll
  mods\plugins\
    HelloPlugin\plugin.ini, HelloPlugin.dll, config.ini
    SomeOtherMod\plugin.ini, ...
  mods\modhost.log                  what loaded, what was skipped and why, plugin log lines
```

## plugin.ini

```ini
[plugin]
id=example.hello              ; required; letters, digits, . _ -
name=Hello Plugin
version=1.0.0
dll=HelloPlugin.dll           ; required; relative to the plugin folder
runner=any                    ; any (default), vm or yyc
game=DELTARUNE.exe            ; optional; exe file name, case-insensitive
depends=other.id, lib.id>=1.2 ; optional
enabled=1                     ; 0 keeps the plugin installed but not loaded
```

### Load rules

`source/Manifest.cpp` applies these rules; `tests/resolve.cpp` tests them.

- A plugin is skipped when any of these holds:
  - its `plugin.ini` is invalid;
  - it is disabled;
  - its id duplicates a plugin earlier in id order;
  - its DLL is missing;
  - its `runner` or `game` does not match the running game.
- A plugin is also skipped when a dependency is missing, skipped, too old, or failed to map.
  Skips cascade to dependents.
- Dependencies load before their dependents. Otherwise plugins load in id order, so the order
  does not depend on folder names or file-system enumeration.
- Plugins in a dependency cycle are skipped.

Every decision is written to `mods\modhost.log`.

## Plugin side

A ModHost plugin is an ordinary Aurie/YYToolkit plugin. ModHost maps it with `MdMapImage`, so its
`ModulePreinitialize` and `ModuleInitialize` run as usual. `modhost.hpp` is optional:

```cpp
#include <modhost.hpp>
auto* host = modhost::Get();                          // nullptr without ModHost
const modhost::PluginInfo* me = host->GetPluginInfo(Module);
GetPrivateProfileStringW(L"hello", L"greeting", L"", buf, 256, me->configPath);
modhost::Log(Module, "%s %s on a %s runner", me->name, me->version,
             host->IsYYCRunner() ? "YYC" : "VM");
```

To hook scripts by name on either runner type, use `modloader/yyc_interop/include/script_hooks.hpp`.

## Timing

ModHost loads plugins from its own `ModuleInitialize`. That runs after the game window exists and
YYToolkit has finished initializing, at the same point as plugins placed directly in
`mods\aurie`.

A plugin that must run before the game initializes still goes in `mods\aurie`. An example is
hooking something in the game's first frames. Aurie gives modules in `mods\aurie` an early-launch
`ModulePreinitialize`.

## Build

```
modloader/modhost/build.sh          # build/ModHost.dll, build/HelloPlugin.dll (clang-cl, see docs/linux.md)
modloader/modhost/build.sh test     # manifest/load-order tests, host C++ compiler
modloader\modhost\build.bat         # Windows, MSVC
```

`example/plugin.ini` is the manifest for HelloPlugin.

## Verified (2026-11-08)

Both games ran the same test layout: HelloPlugin, ScriptHookDemo depending on it, plus plugins
restricted to VM, to YYC, to DELTARUNE, disabled, and with a missing dependency. Each was loaded
or skipped exactly as the rules say, and `GetPluginInfo`, the config path and `Log` worked from
the plugins.

| Game | Runner | Environment | Loaded | Skipped |
|---|---|---|---|---|
| DELTARUNE launcher | VM | Proton | 4 | 3 |
| Dwarf Eats Mountain 1.1.0 | YYC | Wine 11.21 | 3 | 4 |
