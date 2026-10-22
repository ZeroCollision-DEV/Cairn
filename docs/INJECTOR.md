# Cairn Injector

Downloads the Cairn client DLL from an official GitHub release, verifies it, loads it into your own Minecraft
process, and reports whether the client actually started. No stealth, persistence, anti-virus evasion or unrelated
process manipulation is involved: it is a plain `LoadLibraryW` remote-thread load that you start by pressing a button.

## Configuration: `config\injector.ini`

```ini
[release]
url=        ; https://github.com/<owner>/<repo>/releases/download/<tag>/cairn.dll
sha256=     ; 64 hex characters, the SHA-256 of that exact file
filename=cairn.dll
strict_github_url=true
```

**Nothing is configured by default.** No repository, release, URL or hash is assumed; both fields ship empty.

- `url` must be an `https://github.com/<owner>/<repo>/releases/download/<tag>/<file>.dll` (or
  `.../releases/latest/download/<file>.dll`) URL. `strict_github_url=false` allows another HTTPS host. Plain `http` is
  never used.
- `sha256` is **required**. The injector refuses to load a DLL whose hash does not match, and refuses to load at all when
  no hash is configured. Update it whenever you publish a new build. Compute it with
  `Get-FileHash -Algorithm SHA256 cairn.dll`.
- No tokens or credentials are stored or sent. Only public release assets are supported.

Where the hash comes from matters. It is your pin: take it from your own build, not from the download itself. A hash
fetched from the same release would only protect against corruption, not against a tampered release, so the injector
does not do that.

An `injector.ini` from before the rename (`[download]` / `[local]` sections) is migrated automatically: its URL and hash
are carried over, the old local-DLL path is dropped, and the old file is kept as `injector.ini.pre-cairn`.

### Developer override (not the normal workflow)

```ini
[developer]
local_dll=bin\cairn.dll
```

Loads a locally built DLL (relative paths resolve from the Cairn folder) instead of downloading. It is copied into the
cache first so rebuilding is not blocked by a loaded copy. A configured `sha256` is still enforced; without one the load
is allowed but marked "integrity not pinned". The UI shows a warning and the button reads "Inject local build". There is no
local-file field in the window.

## What happens when you press "Download & Inject"

1. **Select**: you choose the Minecraft instance (see below). Right at the click the injector looks that exact process up
   again; if it is gone, replaced, or no longer usable, nothing starts.
2. **Download**: HTTPS (TLS 1.2+, certificate checks on, no HTTPS-to-HTTP redirects), 15 s connect / 30 s receive
   timeouts, 256 MB cap, written to `cairn.dll.download`. HTTP 200 is not treated as success: the transfer must complete
   (Content-Length match when sent) and the file must pass the next step.
3. **Verify**: size sanity, valid PE, x64, is a DLL, SHA-256 equals the configured pin. A rejected file is deleted and is
   never loaded.
4. **Stage**: the verified file is moved to `cairn.dll` in the cache and verified **again at its final path** immediately
   before loading.
5. **Inject**: the selected process is re-checked a second time (same PID **and** same creation time, still a 64-bit game
   window), then `LoadLibraryW` runs in that process via a remote thread. The creation time is checked once more on the
   process handle itself. The result is accepted only after the module shows up in the target's module list.
6. **Confirm**: the injector polls the client's status block (below) for up to 30 s.

Success is shown only when the client reports **Ready**. Other outcomes are reported as they are: *limited* (client running
but could not reach the JVM/Minecraft), *failed*, *no report* (DLL loaded but never reported, e.g. an old build), *game
closed*, or *cancelled*.

## Choosing the Minecraft instance

The injector never picks a process silently when there is a choice, and it never trusts a process name or a window title
alone. The decision logic is `src/injector/TargetMatch.h`, which has no Windows dependency and is unit-tested.

**Which processes are looked at.** `javaw.exe` and `java.exe` (plus `[target] process=` if you changed it). The official
Minecraft Launcher and the Modrinth App are not Java processes and are never candidates; they only start the game. Which
Java executable a launcher uses does not matter: official launcher, Modrinth App and other launchers all end up as a Java
process running the game.

**What identifies the game.** For each Java process the injector collects:
- its visible top-level windows with their window **class** (Minecraft 1.13+ creates its window through GLFW, class `GLFW30`) and title;
- its command line, read with `NtQueryInformationProcess` (needs only the same access the injector already uses, no administrator rights).

| Result | Condition |
|---|---|
| **Game** (selectable) | a visible `GLFW30` window **and** either a Minecraft-style command line (`net.minecraft.client.main.Main`, Fabric/Quilt `KnotClient`, Forge/NeoForge client launch target, or `--assetIndex` with `--gameDir`) or a window title containing "Minecraft" |
| **Starting** (greyed) | Minecraft-style command line, no game window yet |
| **Launcher** (greyed) | launcher main class, or a "Launcher" titled non-game window |
| not listed | any other Java process: IDEs, servers (a server GUI titled "Minecraft server" has no GLFW window), tools, a Java game that is not Minecraft |

So a custom Modrinth window title ("My Pack | Survival") is fine as long as the command line identifies Minecraft; the title
does not have to say "Minecraft". If the command line cannot be read and the title does not say Minecraft either, the window
cannot be confirmed and is not offered. Details lists every skipped Java process and why. The command line contains the
session token, so it is only reduced to facts (is it Minecraft, version, launcher brand) and is never stored, shown or logged.

**Selection.**
- One usable instance: it is selected automatically (the radio button shows it).
- Several usable instances: nothing is selected until you click one; the button says *Multiple instances found. Select one.*
- None: the button is disabled and the status line says why (nothing found / only a launcher / still starting).

**Refresh, exits and stale selections.** *Refresh* rescans immediately; the list also rescans every two seconds while idle (it is
frozen while an operation runs). A selection belongs to one exact process (PID **and** creation time, so a reused PID is never
mistaken for the original) and is kept while that process exists, even if its window is briefly missing; the row then says why
it is unavailable and the button is disabled. If the process is gone, the selection is cleared and a note
("... has closed.") is shown for eight seconds, then disappears by itself. The injector does **not** jump to another instance
during that time; once the note has expired the usual rule applies again (a single usable instance is pre-selected).

**The selected process is the injected process.** The PID and creation time that were selected are passed unchanged to the
worker and to the loader. They are re-validated at the click, again just before the remote thread is created, and the
creation time is compared on the opened process handle. A mismatch aborts with "Not injected".

**Version.** Shown when the command line states it (`--version fabric-loader-0.16.10-1.21.11` gives `1.21.11`), otherwise
when the window title has it right after "Minecraft" (`Minecraft* 1.21.11 - Singleplayer`). Both are launcher-supplied text,
not read from the game's files, so they are hints; Details says which source was used. A version other than 1.21.11 is shown in
amber. The injector cannot confirm Fabric Loader or the installed mods before injection; the client reports its own result afterwards.

## How initialization is verified

The client creates a small named file mapping `Local\CairnStatus.<pid>` inside the game process (`src/shared/Status.h`)
and updates it as it starts: Loaded, HookInstalled, JvmAttached, then **Ready** (MinecraftClient resolved, modules
registered) or **Limited** / **Failed** with a short reason. It also records whether the render hook has drawn its first
frame and whether the JVMTI tick breakpoint is active, plus a heartbeat. The injector only reads it.

Limits: this shows the client's own initialization completed. It does not prove the modules behave correctly in game.
The mapping lives in the user session namespace (`Local\`), so an injector and a game started in different Windows
sessions will not see each other's block.

## Cache and cleanup

Location: `%LOCALAPPDATA%\Cairn\cache\` (falls back to `%TEMP%\Cairn\cache\`).

| File | Meaning |
|---|---|
| `cairn.dll.download` | download or developer copy in progress / interrupted. Always overwritten; removed on start and after runs |
| `cairn.dll` | the verified DLL that gets loaded |
| `cairn.<8 hex>.dll` | fallback name (first 8 hex of the SHA-256) used only when `cairn.dll` is locked by another running game with a different build |

Windows will not delete a DLL that is mapped into a running process, so the loaded file stays until the game exits.
The injector therefore cleans up at three points: when it starts, after each run, and when it closes. A file Windows refuses
to delete is left alone (and counted in the log as "in use") and is removed by a later run. A failed or rejected attempt
removes its own files immediately. Only files matching the names above are ever touched, nothing else in the folder. If
the primary file is locked but byte-identical to the new download, it is reused.

Closing the injector never unloads the client from the game. There is no eject feature.

## Window

Borderless window, 500 logical px wide, with its own dark title bar: brand mark and name, minimise and close buttons, drag by
the title bar (`WM_NCHITTEST`). Its height follows the layout and grows when *Details* is opened. Per-monitor DPI aware (fonts and layout rebuild on `WM_DPICHANGED`).
A drop shadow and, on Windows 11, rounded corners come from DWM. The user cannot resize it. It allows one injector instance at a time.

## Where it installs

`scripts\build.ps1` installs to `C:\Users\<you>\Desktop\Cairn` by default (`-OutDir` overrides it). The install step passes
the folder explicitly (`cmake --install ... --prefix`), so a stale CMake cache cannot send files elsewhere. An older
`Desktop\YTClient` package is never modified or deleted by the build.

## Still to configure

`[release] url` (the official HTTPS release asset) and `[release] sha256` (your trusted pin) are empty. Nothing else is
needed for the download workflow; no live download has been made.

## Not verified

None of this has been run against a real game yet. In particular untested: the TLS download against a real release,
injection into a live game, process selection with two or more real Minecraft instances, identification of games started by the
official launcher and the Modrinth App (the matcher is unit-tested with sample command lines, not with live processes), the status-block handshake, DWM shadow/rounded corners, DPI changes between monitors, window growth when
*Details* is opened, and cache cleanup against a game that holds the DLL. The injector layout has been reviewed in source
only; it has not been viewed on screen.
