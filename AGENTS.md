# cairn — Minecraft 1.21.11 Utility Mod (C++)

## Build

Requires: CMake 3.20+, MSVC (Visual Studio, C++ workload), 64-bit JDK (JAVA_HOME or -JavaHome).

```
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -JavaHome <jdk>
```

Builds `cairn.dll` and `CairnInjector.exe` in a fresh `build-release/` and installs to `Desktop\Cairn` (see README.md).
Shared UI code lives in `src/shared`; the injector in `src/injector`.

## Keybind

- **Right Shift**: Toggle menu

## Architecture

- **Injection**: DLL injected into Minecraft Java process; MinHook hooks `wglSwapBuffers`
- **JVM access**: `JNI_GetCreatedJavaVMs` from `jvm.dll`; JVMTI via `GetEnv`
- **Tick sync**: JVMTI `SetBreakpoint` on `MinecraftClient.tick()` at location 0; all module logic + packet flush runs at tick start
- **Packets**: Only existing game packets via `ClientPlayNetworkHandler.sendPacket()`; queued in `PacketManager`, flushed at tick start
- **Menu**: ImGui (OpenGL3 + Win32 backend), graphite dark theme with a teal accent and amber secondary (docs/DESIGN.md)

## Mappings

All Minecraft class/method/field names are in `src/core/Mappings.h` using Fabric intermediary names for 1.21.11. If a mapping is wrong, update the constant in that file. No runtime remapping.

## Adding a module

1. Create `src/modules/MyModule.h` and `MyModule.cpp`
2. Inherit from `Module`, call base ctor with name/desc/category
3. Add settings via `addFloat/addBool/addInt` in constructor
4. Override `onEnable/onDisable/onTick/onRender` as needed
5. Register in `src/core/Client.cpp` → `initModules()`: `mgr.add<MyModule>()` (full guide: docs/MODULE_DEVELOPMENT.md)
6. Add `.cpp` to the `cairn` target in `CMakeLists.txt`
