#include "Client.h"
#include "JVM.h"
#include "Hooks.h"
#include "../utils/Logger.h"
#include "../menu/Menu.h"
#include "../menu/Theme.h"
#include "../modules/ModuleManager.h"
#include "../packet/TickSync.h"
#include "../packet/PacketManager.h"
#include "../shared/Brand.h"
#include "../shared/UiTheme.h"
#include "../shared/Status.h"
#include "Settings.h"
#include "Sound.h"
#include "Profile.h"

#include "../modules/Speed.h"
#include "../modules/Fly.h"
#include "../modules/NoFall.h"
#include "../modules/Sprint.h"
#include "../modules/KillAura.h"
#include "../modules/Reach.h"
#include "../modules/FullBright.h"

Client& Client::instance()
{
    static Client inst;
    return inst;
}

// ════════════════════════════════════════════════════════════
//  Entry — called from DllMain (DLL_PROCESS_ATTACH)
//  Creates the background init thread.
// ════════════════════════════════════════════════════════════
void Client::startThread()
{
    m_thread = std::thread([this]() { run(); });
}

// ════════════════════════════════════════════════════════════
//  Startup — runs on the background init thread
// ════════════════════════════════════════════════════════════
void Client::run()
{
    Logger::init();
    m_status.open(Brand::kVersion);
    m_status.set(Status::State::Loaded);

    // UI services that do not depend on the game: settings, sound, optional profile.
    Settings::load();
    Sound::init();
    UI::g_sfxHandler = &Sound::play;
    Profile::refresh();

    // The diagnostic console is opt-in (Settings -> Diagnostics, or client.ini).
    if (Settings::debugConsole() && AllocConsole())
    {
        FILE* out = nullptr;
        FILE* err = nullptr;
        freopen_s(&out, "CONOUT$", "w", stdout);
        freopen_s(&err, "CONOUT$", "w", stderr);
        SetConsoleTitleW(L"Cairn");
    }

    printf("Cairn injected into process\n");
    printf("Log file: %s\n", Logger::getLogPath().c_str());
    printf("Waiting for first frame to init ImGui...\n");
    OutputDebugStringA("cairn: DLL loaded\n");

    // Hook MUST be set up as fast as possible so the first frame gets intercepted.
    // This must happen BEFORE any blocking or JVM work.
    if (!Hooks::instance().init())
    {
        printf("FATAL: wglSwapBuffers hook failed\n");
        OutputDebugStringA("cairn: hook init failed\n");
        m_status.set(Status::State::Failed, 0, "Could not hook the renderer (wglSwapBuffers)");
        return;
    }
    printf("wglSwapBuffers hook installed. Waiting for JVM...\n");
    m_status.set(Status::State::HookInstalled);

    // ── JVM setup (best-effort, non-blocking for the hook) ──────
    if (initJVM())
    {
        // Register modules BEFORE the tick hook goes live: the tick callback walks
        // the module list on the game thread and must never see it half-built.
        initModules();
        if (initTickHook()) m_status.addFlag(Status::FlagTickHook);
        const uint32_t n = static_cast<uint32_t>(ModuleManager::instance().size());
        printf("Cairn fully loaded - %u modules registered\n", n);
        m_status.set(Status::State::Ready, n);
    }
    else
    {
        printf("WARNING: JVM init failed. Modules will be unavailable.\n");
        m_status.set(Status::State::Limited, 0, "JVM attach or Minecraft lookup failed (Fabric Loader 1.21.11 required)");
    }

    m_hudSince = GetTickCount();
    m_ready = true;     // publishes the finished module registry to the render thread
    m_running = true;

    // Idle loop — keeps the init thread alive
    while (m_running.load())
    {
        m_status.beat();
        Sleep(500);
    }

    JVM::instance().detach();   // this thread attached itself; the JVM must not keep a dead thread
}

bool Client::initJVM()
{
    if (!JVM::instance().attach())
    {
        printf("JVM attach failed (is the game running?)\n");
        OutputDebugStringA("cairn: JVM attach failed\n");
        return false;
    }
    printf("JVM attached successfully\n");
    m_status.set(Status::State::JvmAttached);

    // Retry MinecraftClient resolution a few times (game may still be loading)
    for (int i = 0; i < 15; i++)
    {
        if (JVM::instance().getMinecraft())
        {
            printf("MinecraftClient instance resolved\n");
            strcpy_s(m_hudMessage, "Cairn loaded");
            return true;
        }
        printf("Waiting for MinecraftClient... (%d/15)\n", i + 1);
        Sleep(500);
    }

    printf("Could not resolve MinecraftClient instance\n");
    return false;
}

bool Client::initTickHook()
{
    JVM::instance().setTickCallback([this]() { onTick(); });
    if (JVM::instance().hookTick())
    {
        printf("Tick hook active (JVMTI breakpoint)\n");
        return true;
    }
    printf("JVMTI tick hook unavailable; using frame fallback\n");
    return false;
}

void Client::initModules()
{
    auto& mgr = ModuleManager::instance();
    mgr.add<Speed>();
    mgr.add<Fly>();
    mgr.add<NoFall>();
    mgr.add<Sprint>();
    mgr.add<KillAura>();
    mgr.add<Reach>();
    mgr.add<FullBright>();
    mgr.finalize();   // build category lists once, before any other thread reads them
    mgr.restoreSettings();   // saved slider/toggle values; enabled-state is restored later (needs a world)
}

// ════════════════════════════════════════════════════════════
//  First frame — called from hkSwapBuffers (RENDER THREAD)
//  This wires the render callback so the HUD/menu draw.
// ════════════════════════════════════════════════════════════
void Client::onFirstFrame()
{
    OutputDebugStringA("cairn: first frame, wiring callbacks\n");

    Hooks::instance().onRender = [this]() { onRender(); };
    m_status.addFlag(Status::FlagFirstFrame);

    // Callback for the frame-counting tick fallback
    if (!JVM::instance().jvmti())
    {
        Hooks::instance().onTickFallback = [this]() { onTickFallback(); };
        m_lastTickTime = GetTickCount();
        OutputDebugStringA("cairn: frame-counting tick fallback active\n");
    }
}

// ════════════════════════════════════════════════════════════
//  Per-frame render callback (RENDER THREAD)
// ════════════════════════════════════════════════════════════
void Client::onRender()
{
    // RightShift toggle — always operational, even before m_ready
    static bool rshiftPrev = false;
    bool rshiftNow = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    if (rshiftNow && !rshiftPrev)
        toggleMenu();
    rshiftPrev = rshiftNow;

    // Menu rendering (keeps drawing while it fades out)
    if (Hooks::instance().menuVisible() || Menu::instance().isVisibleOrFading())
        Menu::instance().render();

    // ── HUD overlay ────────────────────────────────────────────
    // Always draw a small watermark + status so the user knows it's alive.
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    char fpsText[64];
    snprintf(fpsText, sizeof(fpsText), "%s | %.0f FPS", Brand::kClientName, io.Framerate);
    ImVec2 fpsSize = ImGui::CalcTextSize(fpsText);
    float x = io.DisplaySize.x - fpsSize.x - 14.0f;
    float y = 8.0f;

    // Subtle dark background pill
    dl->AddRectFilled(
        ImVec2(x - 14, y - 4),
        ImVec2(x + fpsSize.x + 8, y + fpsSize.y + 4),
        UI::C(UI::Col::Bg, 0.72f), 6.0f);

    // Status dot: green once startup finished, amber while still loading
    const ImVec4& dotCol = m_ready.load() ? UI::Col::Success : UI::Col::Accent2;
    dl->AddCircleFilled(ImVec2(x - 5, y + fpsSize.y * 0.5f), 3.5f, UI::C(dotCol), 12);

    dl->AddText(ImVec2(x, y), UI::C(UI::Col::Text, 0.9f), fpsText);

    // One-off startup message (e.g. "Cairn loaded"), fades out after a few seconds
    if (m_hudMessage[0] != '\0' && m_hudSince != 0)
    {
        const DWORD age = GetTickCount() - m_hudSince;
        if (age < 6000)
        {
            const float a = age > 4500 ? 1.0f - static_cast<float>(age - 4500) / 1500.0f : 1.0f;
            ImVec2 msgSize = ImGui::CalcTextSize(m_hudMessage);
            dl->AddText(
                ImVec2(12, io.DisplaySize.y - msgSize.y - 12),
                UI::C(UI::Col::Accent, 0.85f * a), m_hudMessage);
        }
    }

    // Module overlays (ESP, tracers, etc.) and toggle keys
    if (m_ready.load())
    {
        // Toggle keys are ignored while the menu is open, while a menu text field has focus,
        // and while any game screen (chat, inventory, pause) is open.
        const bool keysBlocked = Hooks::instance().menuVisible() || io.WantTextInput ||
                                 JVM::instance().isScreenOpen();
        ModuleManager::instance().pollKeybinds(keysBlocked);
        ModuleManager::instance().renderAll();
    }

    // While the menu is open, show a software cursor and free the mouse from any clip
    // region the game set. UNVERIFIED in-game: the game may re-capture the cursor.
    const bool menuOpen = Hooks::instance().menuVisible();
    if (menuOpen && !m_menuWasOpen)
        m_clipSaved = GetClipCursor(&m_savedClip) != FALSE;   // remember the game's clip region
    if (menuOpen)
    {
        io.MouseDrawCursor = true;
        ClipCursor(nullptr);
    }
    else
    {
        io.MouseDrawCursor = false;
        if (m_menuWasOpen && m_clipSaved)
        {
            ClipCursor(&m_savedClip);   // hand the cursor back the way the game had it
            m_clipSaved = false;
        }
    }
    m_menuWasOpen = menuOpen;
}

// ════════════════════════════════════════════════════════════
//  Tick callbacks
// ════════════════════════════════════════════════════════════
void Client::onTick()
{
    TickSync::instance().onTickStart();
}

void Client::onTickFallback()
{
    DWORD now = GetTickCount();
    m_tickAccumulator += (now - m_lastTickTime);
    m_lastTickTime = now;

    const DWORD tickInterval = 50;
    while (m_tickAccumulator >= tickInterval)
    {
        m_tickAccumulator -= tickInterval;
        TickSync::instance().onTickStart();
    }
}

// ════════════════════════════════════════════════════════════
//  Shutdown — called from DllMain (DLL_PROCESS_DETACH)
//  Waits for the background thread, then cleans up.
// ════════════════════════════════════════════════════════════
void Client::shutdown()
{
    m_status.set(Status::State::Unloading);

    // Signal the background thread to exit
    m_running = false;

    // Wait for it to wake up and finish
    if (m_thread.joinable())
        m_thread.join();

    m_ready = false;

    // Stop the game thread calling into us, then undo module changes (Fly, FullBright, ...).
    Hooks::instance().setMenuVisible(false);
    if (m_menuWasOpen && m_clipSaved) { ClipCursor(&m_savedClip); m_clipSaved = false; }
    JVM::instance().unhookTick();
    ModuleManager::instance().saveState();
    ModuleManager::instance().disableAll();

    Settings::flush();
    Sound::shutdown();
    UI::g_sfxHandler = nullptr;

    Hooks::instance().shutdown();
    PacketManager::instance().clearAll();
    JVM::instance().detach();

    if (GetConsoleWindow())
        FreeConsole();

    m_status.close();
    OutputDebugStringA("cairn: unloaded\n");
}

// Process-exit path: no JNI, no joins -- just make sure preferences hit the disk.
void Client::saveOnExit()
{
    if (m_ready.load()) ModuleManager::instance().saveState();
    Settings::flush();
}

void Client::toggleMenu()
{
    const bool wasVisible = Hooks::instance().menuVisible();
    Hooks::instance().setMenuVisible(!wasVisible);
    UI::playSfx(wasVisible ? UI::Sfx::Close : UI::Sfx::Open);
    if (wasVisible)
    {
        // Persist favourites, sound prefs and module state when the menu closes.
        if (m_ready.load()) ModuleManager::instance().saveState();
        Settings::flush();
    }
}
