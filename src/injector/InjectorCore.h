#pragma once
// Logic of the Cairn Injector with no UI: configuration, release download, validation,
// cache handling, target detection, loading and initialization checks. Every function
// reports failures as readable messages; nothing here claims success it has not verified.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "../shared/Status.h"

namespace inj
{
    // ── Configuration (config\injector.ini) ─────────────────
    struct Config
    {
        // [release] -- where the client DLL comes from and what it must hash to.
        std::string releaseUrl;             // empty until the real release exists
        std::string expectedSha256;         // trusted pin, 64 hex chars; REQUIRED to load
        std::string dllFileName = "cairn.dll";
        bool strictGithubUrl = true;        // URL must be https://github.com/<owner>/<repo>/releases/...

        // [target]
        std::string processName = "javaw.exe";
        std::string windowTitleContains = "Minecraft";

        // [developer] -- not part of the normal workflow. A local build (e.g. bin\cairn.dll)
        // is copied into the cache and loaded instead of downloading. A configured sha256 is
        // still enforced; without one the load is allowed but flagged "not integrity-checked".
        std::string devLocalDll;

        std::filesystem::path rootDir;      // folder that contains bin\, config\, ...
        std::filesystem::path configDir;
        std::filesystem::path cacheDir;     // %LOCALAPPDATA%\Cairn\cache
        std::filesystem::path iniPath;
    };

    // Locates (and creates, if missing) the config next to the executable. A config from
    // before the rename is migrated (the old file is kept as injector.ini.pre-cairn).
    // `note` receives a human-readable warning when something could not be read/created.
    Config loadConfig(std::string* note);

    // Lower-cases and strips spaces/colons. Returns "" unless the result is 64 hex chars.
    std::string normalizeSha256(const std::string& text);

    enum class ConfigState
    {
        Ready,          // release workflow can run
        DeveloperMode,  // local DLL override is active
        NoUrl,          // [release] url is empty
        BadUrl,         // not HTTPS / not a GitHub release URL
        NoHash,         // [release] sha256 is empty
        BadHash,        // [release] sha256 is not 64 hex chars
    };

    struct ConfigCheck
    {
        ConfigState state = ConfigState::NoUrl;
        std::string message;                // what is wrong and how to fix it, or a short OK note
        bool canRun() const { return state == ConfigState::Ready || state == ConfigState::DeveloperMode; }
    };
    ConfigCheck checkConfig(const Config& cfg);

    // ── Validation ──────────────────────────────────────────
    enum class HashState { NotChecked, Verified, Mismatch };

    struct Validation
    {
        bool ok = false;
        std::string message;                // success summary or the reason it failed
        std::uint64_t sizeBytes = 0;
        std::string sha256;                 // computed, lower-case hex (when readable)
        HashState hash = HashState::NotChecked;
        bool wrongArch = false;             // valid PE, but not an x64 image
    };

    // Checks: file exists, plausible size, valid PE, x64, is a DLL, and the SHA-256 against
    // `expectedSha256`. With `requireHash`, an empty expected hash is a failure. Never reports
    // Verified without comparing.
    Validation validateDll(const std::filesystem::path& file, const std::string& expectedSha256,
                           bool requireHash);

    // ── Download ────────────────────────────────────────────
    struct Progress
    {
        std::atomic<std::uint64_t> received{0};
        std::atomic<std::uint64_t> total{0};    // 0 = unknown
        std::atomic<bool> cancel{false};
    };

    struct DownloadResult
    {
        bool ok = false;
        std::string error;
        std::filesystem::path file;             // "<name>.download" -- NOT yet validated
        std::uint64_t bytes = 0;
    };

    // HTTPS only (TLS 1.2+, certificate checks on, no HTTPS->HTTP redirects). Writes to
    // "<destDir>\<fileName>.download"; the caller validates it and then calls stageDll().
    // `fileName` must be a bare *.dll name.
    DownloadResult downloadFile(const std::string& url, const std::filesystem::path& destDir,
                                const std::string& fileName, Progress& progress);

    // ── Cache ───────────────────────────────────────────────
    struct StagedDll
    {
        bool ok = false;
        std::filesystem::path path;
        std::string error;
        bool usedFallbackName = false;          // the primary name was locked by a running game
    };

    // Moves a validated temp file into the cache as "<name>.dll". If that name is locked
    // (mapped in a running game), falls back to "<stem>.<sha8>.dll". If the existing file
    // already has the same hash it is reused. Never touches anything outside cacheDir.
    StagedDll stageDll(const Config& cfg, const std::filesystem::path& validatedTemp, const std::string& sha256);

    // Copies a local build into the cache (developer mode); returns the temp file to validate.
    bool copyToTemp(const Config& cfg, const std::filesystem::path& source, std::filesystem::path* temp, std::string* error);

    struct CleanupReport
    {
        int removed = 0;
        int kept = 0;                           // locked (mapped by a running game) or otherwise undeletable
    };

    // Deletes interrupted downloads ("*.download") and cached client DLLs ("<stem>.dll",
    // "<stem>.<8 hex>.dll") that are not in use. A file that Windows will not delete (still
    // mapped in a running game) is left alone and retried on the next run. Anything in the
    // folder that does not match those names is never touched. `keep` is spared.
    CleanupReport cleanCache(const Config& cfg, const std::filesystem::path& keep = {});

    // ── Target detection ────────────────────────────────────
    // What we can know about a candidate process without touching the game:
    //   Game      - a process of the configured name with a visible window whose title contains
    //               "Minecraft" (and is not a launcher window)
    //   Launcher  - such a window, but the title says "launcher"
    //   NoWindow  - the process exists but has no Minecraft window (yet): still loading, a server,
    //               or some other Java program
    enum class TargetKind { Game, Launcher, NoWindow };

    struct Target
    {
        unsigned long pid = 0;
        std::string exeName;                // e.g. "javaw.exe"
        std::string title;                  // matched window title, empty when none
        bool titleMatched = false;
        TargetKind kind = TargetKind::NoWindow;
        std::string version;                // parsed from the window title only ("1.21.11"); empty if absent
        bool accessible = true;             // false if the process could not be opened
        bool is64 = true;
        std::string note;                   // why it cannot be used, when it cannot
        std::string exePath;
        unsigned long long startTime = 0;   // process creation time: identifies this exact process (PIDs get reused)
        bool clientPresent = false;         // a Cairn status block exists in this process
        Status::State clientState = Status::State::None;
        bool usable() const { return accessible && is64 && kind == TargetKind::Game; }
    };

    std::vector<Target> findTargets(const Config& cfg);

    // Looks the process up again right now and checks it is still the same process (same PID *and*
    // creation time), is reachable, is 64-bit and is a game window. `startTime` 0 skips the identity check.
    bool revalidateTarget(const Config& cfg, unsigned long pid, unsigned long long startTime,
                          Target* out, std::string* why);

    // ── Loading ─────────────────────────────────────────────
    struct LoadResult
    {
        bool ok = false;
        bool alreadyLoaded = false;
        std::string message;
    };

    // Standard LoadLibraryW-in-remote-thread load of `dll` into `pid`. Reports ok only after
    // the module is actually seen in the target's module list. `moduleStem` ("cairn") is used
    // to recognise an earlier copy ("cairn.dll", "cairn.1a2b3c4d.dll") already in the process.
    LoadResult loadDllIntoProcess(unsigned long pid, const std::filesystem::path& dll, const std::string& moduleStem,
                                  unsigned long long expectedStartTime = 0);

    // ── Initialization check ────────────────────────────────
    enum class InitOutcome
    {
        Initialized,        // client reported Ready (JVM attached, Minecraft resolved, modules registered)
        Limited,            // client is running but reported it could not reach the JVM / Minecraft
        ClientFailed,       // client reported a fatal init error
        NoReport,           // module is loaded but never published a status block in time
        ProcessExited,      // the game closed while we waited
        Cancelled,
    };

    struct InitResult
    {
        InitOutcome outcome = InitOutcome::NoReport;
        Status::Block block{};
        bool haveBlock = false;
        std::string message;
    };

    // Polls the client's status block until it reports a final state, the process exits, the
    // timeout elapses or `cancel` is set. `onState` (optional) is called when the state changes.
    InitResult waitForClientInit(unsigned long pid, unsigned timeoutMs, const std::atomic<bool>& cancel,
                                 const std::function<void(const Status::Block&)>& onState = {});

    std::string formatBytes(std::uint64_t bytes);
}
