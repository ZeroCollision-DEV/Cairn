# Module development

Everything here matches the current source. Files: `src/modules/Module.h` (base class, settings),
`src/modules/ModuleManager.{h,cpp}` (registry, lifecycle, persistence), `src/core/Client.cpp` (registration),
`src/menu/Menu.cpp` (GUI, which discovers modules and settings automatically).

## 1. Write the module

`src/modules/MyModule.h`
```cpp
#pragma once
#include "Module.h"

class MyModule : public Module
{
public:
    MyModule() : Module("MyModule", "One-line description", Category::Misc)
    {
        addBool("Enabled Thing", true);
        addFloat("Range", 4.5f, 1.0f, 8.0f);          // name, default, min, max
        addInt("Delay", 2, 0, 20);                     // name, default, min, max
        addChoice("Mode", {"Soft", "Hard"}, 0);        // name, options, default index
        addKey("Keybind", 0);                          // virtual-key code; 0 = unbound; toggles the module
        // setMaturity(Maturity::Unfinished, "why");   // optional, see below
    }
    void onEnable() override;    // module switched on
    void onDisable() override;   // switched off: undo anything you changed in the game
    void onTick() override;      // every game tick, game thread
    void onRender() override;    // every frame, render thread (ImGui background draw list is available)
};
```
`src/modules/MyModule.cpp` implements the callbacks. Read settings with `getBool("name")`, `getFloat("name")`,
`getInt("name")` (a `Choice` setting is read with `getInt`, giving the selected index).

Categories: `Movement, Combat, Player, Render, World, Misc` (enum `Category` in `Module.h`).

## 2. Register it

1. In `src/core/Client.cpp` add `#include "../modules/MyModule.h"` and, in `Client::initModules()`,
   `mgr.add<MyModule>();` (before the existing `mgr.finalize();`).
2. Add `src/modules/MyModule.cpp` to the `cairn` target in `CMakeLists.txt`.

Nothing else. The menu builds its sidebar counts, cards, search, favourites and the settings panel from
`ModuleManager::all()` and each module's `settings()`; there is no per-module UI code.

## 3. Lifecycle and threads

| Callback | Thread | Notes |
|---|---|---|
| constructor | init thread | register settings here; no JNI |
| `onEnable` / `onDisable` | whichever thread toggled it (usually render thread; game thread for keybinds/auto-disable) | always call through `ModuleManager::setEnabled()` (the menu does) so a JNI local frame and a try/catch wrap it |
| `onTick` | game thread (JVMTI breakpoint on `MinecraftClient.tick()`), or a 20 Hz frame-counting fallback on the render thread when JVMTI is unavailable | already inside a JNI local frame |
| `onRender` | render thread | wrapped in a JNI local frame |

- `JVM::instance().env()` returns the **calling thread's** `JNIEnv`. Never cache a `JNIEnv*` across calls or threads.
- Local refs returned by `getPlayer()`/`getWorld()`/`findClass()` are freed by the surrounding frame, but delete
  large loops' refs yourself as the existing modules do.
- The enabled flag is atomic. Setting values are plain fields read on the game thread and written by the menu
  (single float/int/bool writes; no locking). Do not hold pointers to `Setting` across frames.
- `onDisable` runs on shutdown/unload so modules can restore game state.
- Throwing out of a callback is caught and logged, but do not rely on it.

## 4. Maturity and safety flags

- `setMaturity(Maturity::Experimental, note)` (default): shows an "Experimental" badge; the note is a tooltip.
- `setMaturity(Maturity::Unfinished, note)`: the module is listed but **cannot be enabled** (see `Reach`).

## 5. Persistence

`client.ini` (`%APPDATA%\Cairn\`) keeps, under `[modules]`, one `Module.enabled` entry and one
`Module.Setting` entry per setting. `ModuleManager::saveState()` runs when the menu closes and on shutdown;
`restoreSettings()` runs right after registration; `restoreEnabled()` runs on the first tick that has a player,
and only if "Re-enable modules on startup" is on (off by default). Unknown/malformed/out-of-range values are
ignored or clamped. **Setting names are the keys: renaming a setting resets it to its default.**

## 6. Minecraft access and mapping verification (required before using a name)

All game names live in `src/core/Mappings.h` (Fabric intermediary, Minecraft 1.21.11). Every entry there has been
checked against the FabricMC/yarn 1.21.11 mapping data, including its JNI descriptor; see
[MAPPINGS.md](MAPPINGS.md). To add one:

1. Find the member in a yarn checkout (`mappings/net/minecraft/...`, e.g. `METHOD method_2918 attackEntity (Lnet/minecraft/class_1657;Lnet/minecraft/class_1297;)V`).
   Note which class **declares** it; yarn lists an overridden method only on the class/interface at the top of the chain.
2. Add the constant to `Mappings.h` with the descriptor in a comment.
3. Add a matching row to `EXPECTED` in `tools/verify_mappings.py` and run it:
   `python tools/verify_mappings.py --yarn <yarn checkout> --src src`. It fails if a member/descriptor is wrong or if
   any `field_`/`method_`/`class_` literal under `src/` is not in the table.
4. A verified name is not a tested module: test the module in the game and record the result (README "Verification").

Wrong or missing names make JNI lookups return null (exceptions are cleared) and the module silently does nothing.

## 7. Testing expectations

- `tests/portable_tests.cpp` covers INI parsing and the module/settings API (`-DCAIRN_BUILD_TESTS=ON`).
- Everything that touches JNI, ImGui or Windows needs a Windows build and an in-game check. Do not mark a module
  as working until you have seen it work; keep `Maturity::Experimental` until then.
