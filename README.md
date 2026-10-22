# Cairn

A personal Minecraft Java **1.21.11** client written in C++ (ImGui menu, JNI/JVMTI), plus the standalone
**Cairn Injector** that downloads, verifies and loads it. The injector contains no stealth, persistence or evasion code.

> **Build status.** The rename to Cairn, the release-download injector and the first redesign compiled natively on the
> author's Windows machine (MSVC 19.51, Windows SDK 10.0.26100.0, CMake 4.5.0-rc1, JDK 21). The **latest injector
> redesign and the instance-selection code have not been compiled yet**; run the build below. Nothing has been run inside Minecraft.

## What has been verified, by level

| Level | What |
|---|---|
| Implemented in source | Client DLL, menu, shared widgets, sound, settings, module system, injector, status reporting, CMake, build script |
| Compiled with MSVC | Both targets (x64) up to the first Cairn injector. **Not yet** the minimal injector redesign and instance selection |
| Passed portable tests (Linux g++) | INI parser, module/settings API, unfinished-module restriction (`tests/portable_tests.cpp`) |
| Checked against mapping data | Every Minecraft name/descriptor in `src/core/Mappings.h` vs FabricMC/yarn 1.21.11 (`tools/verify_mappings.py`, `docs/MAPPINGS.md`) |
| Visually inspected | **No.** The client menu and the injector window were written without being run or screenshotted. The new injector layout is checked only by reading the source |
| Tested in the actual game | **None** (injection, init reporting, hooks and modules are all untested) |
| Experimental / unverified | Speed, Fly, NoFall, Sprint, FullBright, KillAura |
| Unfinished | Reach (placeholder, cannot be enabled), Discord profile provider |

## Build on Windows (MSVC x64)

Requirements: CMake 3.20+, Visual Studio with "Desktop development with C++" (x64 + Windows SDK), a **64-bit JDK**
(only its headers are used), and internet on the first configure (MinHook and Dear ImGui are fetched at pinned versions).

From `C:\Users\cbenn\Downloads\Cairn`:

```powershell
powershell -ExecutionPolicy Bypass -File ".\scripts\build.ps1" -JavaHome "C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot"
```

Options: `-OutDir <folder>` (default **`Desktop\Cairn`**; an older `Desktop\YTClient` package is left exactly as it is),
`-Generator "Visual Studio 17 2022"`, `-Tests`, `-BuildDir <folder>` (default `build-release\`). `JAVA_HOME` is used if
`-JavaHome` is omitted. The script builds `cairn` and `cairn_injector`, installs, **moves** pre-rename binaries
(`ytclient.dll`, `YTInjector.exe`, old icons) into `_previous_ytclient_build\` (never deletes), and fails unless the new
files exist and are x64. An existing `config\injector.ini` is never overwritten.

Only the `Cairn` install component is installed. Earlier builds also dropped MinHook's `include\`, `lib\` and `share\` into the
output folder; those old folders are left untouched and can be deleted by hand.

## Output layout

| Path | Purpose |
|---|---|
| `bin\cairn.dll`, `bin\CairnInjector.exe` | produced by the build |
| `assets\icons\` | `cairn.ico`, `cairn_injector.ico`, `logo.png`, `logo_injector.png`, `logo.svg`, `wordmark.png` |
| `assets\sounds\` | UI sounds (original, CC0) |
| `config\injector.ini` | injector settings (release URL + trusted SHA-256, both **blank** until you supply them) |
| `docs\` | `INJECTOR.md`, `DESIGN.md`, module development guide, mapping verification |
| `THIRD_PARTY_NOTICES.md` | licences |

## Use

1. Start Minecraft 1.21.11 through **Fabric Loader** and load a world.
2. Run `bin\CairnInjector.exe`. It lists the running Minecraft instances (PID and process name). With exactly one it is
   pre-selected; with several you pick one yourself. **Refresh** rescans on demand.
3. Press **Download & Inject**. The injector re-checks the selected process, downloads the release DLL, verifies it,
   injects it into **that** process and then waits for the client to report that it actually started. Success is shown
   only after that report.
4. In game press **Right Shift** for the menu.

The release URL and SHA-256 are **not built in** and are still **unconfigured**: see [docs/INJECTOR.md](docs/INJECTOR.md).
Until both are set in `config\injector.ini` the button stays disabled and says why. No repository, release or hash has
been invented. Closing the injector does not unload the client.

## Runtime requirements and assumptions

- Windows 10/11 x64, 64-bit Java 21+.
- Minecraft names in `src/core/Mappings.h` are **Fabric intermediary** names: they resolve only under Fabric Loader.
  Verified against yarn 1.21.11 data, which does **not** prove the modules work (`docs/MAPPINGS.md`).
- The DLL is built with the static MSVC runtime (no VC++ redistributable needed) and finds `jvm.dll` at runtime inside the
  game process. Assets are not read from disk at runtime: icons are build-time resources, sounds are embedded.
- Hooks: `wglSwapBuffers` (MinHook), the game window's WndProc, and a JVMTI breakpoint on `MinecraftClient.tick()`.
  There is no eject feature. A full unload (`FreeLibrary`) runs a best-effort ordered shutdown; process exit only saves settings.

## Modules (all unverified in-game)

| Module | Status |
|---|---|
| Speed, Sprint, NoFall, Fly, FullBright | Implemented, **Experimental**. Fly's `AntiKick` and NoFall send extra movement packets. FullBright may be rejected by the game's option validation. |
| KillAura | Implemented, **Experimental**. Attacks the nearest living entity in range through `ClientPlayerInteractionManager.attackEntity` and snaps the player's rotation to it. No world/server restriction. |
| Reach | **Unfinished placeholder**; cannot be enabled. |

Adding modules: [docs/MODULE_DEVELOPMENT.md](docs/MODULE_DEVELOPMENT.md).

## Files written at runtime

- `%APPDATA%\Cairn\client.ini`, `profile.ini` - client settings. On first run, `client.ini`/`profile.ini` from
  `%APPDATA%\YTClient\` are **copied** across (the old folder is left alone).
- `%TEMP%\cairn.log` - client log
- `%LOCALAPPDATA%\Cairn\cache\` - injector download cache (see docs/INJECTOR.md)
- `<root>\config\injector.ini` - injector settings (an old-layout file is migrated; the old copy is kept as `injector.ini.pre-cairn`)
- `Local\CairnStatus.<pid>` - an in-memory status block the client publishes inside the game process; the injector reads it

## Known limitations

- Discord profile provider not implemented (local `profile.ini` only).
- Module state is saved when the menu closes and on exit; a crash loses changes since then.
- The injector window is fixed-size (not resizable). Its layout is built for 540x720 logical pixels and scales with DPI.
- The init report proves the client started its own initialization (hook installed, JVM attached, MinecraftClient found,
  modules registered). It does not prove any module works in game.

## Source layout

`src/shared` (theme, widgets, brand, INI, status block) - `src/core` - `src/menu` - `src/modules` - `src/packet` -
`src/injector` - `src/resources` - `tests` - `tools` - `scripts` - `docs`. `_original_backup\` holds the untouched original source.
