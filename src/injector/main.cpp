// Cairn Injector -- standalone loader for the Cairn client DLL (personal use).
//
// Win32 + Direct3D 11 + Dear ImGui in a borderless window with its own dark title bar.
// All logic (download, validation, cache, injection, init check) lives in InjectorCore.
//
// Window behaviour: fixed width, height follows the layout (it grows when "Details" is opened),
// not user-resizable, DPI-aware (per-monitor), dragged by the title bar via WM_NCHITTEST,
// closed/minimised with the custom caption buttons.

#include "InjectorCore.h"
#include "../core/Settings.h"
#include "../core/Sound.h"
#include "../shared/Brand.h"
#include "../shared/Ini.h"
#include "../shared/UiTheme.h"
#include "../shared/Widgets.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace fs = std::filesystem;

// ── Window geometry (logical pixels at 100 % scaling) ────────
static constexpr float kWinW = 500.0f;
static constexpr float kWinHGuess = 480.0f;   // first-frame height; the layout then sets the exact one (App::wantH)
static constexpr float kTitleH = 40.0f;
static constexpr float kCapBtnW = 46.0f;

static float g_dpi = 1.0f;
static std::atomic<bool> g_dpiChanged{false};
static float g_newDpi = 1.0f;

// ── D3D11 plumbing ───────────────────────────────────────────
static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static IDXGISwapChain*         g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

static void createRtv()
{
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back)
    {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}
static void releaseRtv()
{
    if (g_ctx) g_ctx->OMSetRenderTargets(0, nullptr, nullptr);   // ResizeBuffers fails while the back buffer is still bound
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool createDevice(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) return false;
    createRtv();
    return g_rtv != nullptr;
}

static void destroyDevice()
{
    releaseRtv();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return TRUE;
    switch (m)
    {
    case WM_NCHITTEST:
    {
        // Borderless window: the title bar (minus the caption buttons) drags the window;
        // everything else is client area so ImGui receives the mouse.
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &pt);
        RECT rc;
        GetClientRect(h, &rc);
        const int titleH = static_cast<int>(kTitleH * g_dpi);
        const int btnZone = static_cast<int>(kCapBtnW * g_dpi) * 2;
        if (pt.y >= 0 && pt.y < titleH && pt.x >= 0 && pt.x < rc.right - btnZone)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCCALCSIZE:
        if (w) return 0;   // no non-client area: the client rect is the whole window
        break;
    case WM_SIZE:
        if (g_dev && w != SIZE_MINIMIZED)
        {
            releaseRtv();
            g_swap->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
            createRtv();
        }
        return 0;
    case WM_DPICHANGED:
    {
        g_newDpi = static_cast<float>(HIWORD(w)) / 96.0f;
        g_dpiChanged = true;
        const RECT* r = reinterpret_cast<const RECT*>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ── Helpers ──────────────────────────────────────────────────
static fs::path widePath(const std::string& utf8)
{
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<size_t>(n) - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    return fs::path(w);
}

static std::string utf8Of(const fs::path& p)
{
    const std::wstring w = p.wstring();
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// ── Pipeline state shared with the worker thread ─────────────
enum class Step { Idle, Running, Done, Failed, Skipped, Warn };
enum Stage { kGet = 0, kVerify, kInject, kInit, kStageCount };

static const char* kStageBusyLabel[kStageCount] = {
    "Downloading...", "Verifying...", "Injecting...", "Starting Cairn..."
};
static const char* kStageNote[kStageCount] = {
    "Downloading the release", "Verifying the download", "Injecting into Minecraft", "Waiting for Cairn to start"
};

// The game version this client targets. Only used to flag a window title that names another version.
static constexpr const char* kExpectedVersion = "1.21.11";

struct Pipeline
{
    std::mutex mtx;
    std::deque<std::pair<int, std::string>> log;   // 0 info, 1 ok, 2 error, 3 warning
    Step step[kStageCount] = {};
    std::string detail[kStageCount];
    std::string summary;                           // final one-line outcome
    int summaryKind = 0;                           // 0 none, 1 success, 2 failure, 3 warning/info
    std::atomic<bool> busy{false};
    unsigned long pid = 0;                         // instance of the current/last run
    inj::Progress progress;

    void add(int kind, const std::string& s)
    {
        std::lock_guard<std::mutex> g(mtx);
        log.emplace_back(kind, s);
        while (log.size() > 200) log.pop_front();
    }
    void set(Stage s, Step v, const std::string& d = std::string())
    {
        std::lock_guard<std::mutex> g(mtx);
        step[s] = v;
        detail[s] = d;
    }
    void reset()
    {
        std::lock_guard<std::mutex> g(mtx);
        for (int i = 0; i < kStageCount; i++) { step[i] = Step::Idle; detail[i].clear(); }
        summary.clear();
        summaryKind = 0;
    }
};

struct App
{
    inj::Config cfg;
    inj::ConfigCheck check;
    std::string configNote;

    // Instance selection. The pair (pid, start time) identifies one exact process: PIDs get reused.
    std::vector<inj::Target> targets;
    unsigned long selPid = 0;
    unsigned long long selStart = 0;
    std::string lostNote;               // why the previous selection was dropped; expires by itself
    double lostAt = 0.0;
    double lastScan = -10.0;

    bool detailsOpen = false;
    float wantH = 0.0f;                 // window height (logical px) the current layout needs

    Pipeline pipe;
    std::thread worker;
    size_t logSeen = 0;
};

static std::string stepDetailFor(const Status::Block& b)
{
    switch (static_cast<Status::State>(b.state))
    {
    case Status::State::Loaded:        return "Client loaded, installing the renderer hook";
    case Status::State::HookInstalled: return "Renderer hook installed, attaching to the JVM";
    case Status::State::JvmAttached:   return "JVM attached, looking up Minecraft";
    case Status::State::Ready:         return "Client ready, waiting for the first frame";
    default:                           return "Waiting for the client to report";
    }
}

static const char* configBlockerText(const inj::Config& cfg, inj::ConfigState s)
{
    const bool noHash = inj::normalizeSha256(cfg.expectedSha256).empty();
    switch (s)
    {
    case inj::ConfigState::NoUrl:   return noHash ? "Download and injection are unavailable until the release URL and trusted SHA-256 are set in config."
                                                  : "Download and injection are unavailable until the release URL is set in config.";
    case inj::ConfigState::BadUrl:  return "The release URL is not valid.";
    case inj::ConfigState::NoHash:  return "Download and injection are unavailable until the trusted SHA-256 is set in config.";
    case inj::ConfigState::BadHash: return "The configured SHA-256 is invalid.";
    default:                        return nullptr;
    }
}

// ── Worker ───────────────────────────────────────────────────
// `pid` + `startTime` identify the instance the user selected. They are the only target this run
// ever touches: the same pair is re-checked right before the DLL is loaded.
static void runPipeline(App* app, inj::Config cfg, unsigned long pid, unsigned long long startTime)
{
    Pipeline& p = app->pipe;
    auto finish = [&](int kind, const std::string& msg)
    {
        {
            std::lock_guard<std::mutex> g(p.mtx);
            p.summary = msg;
            p.summaryKind = kind;
        }
        p.busy = false;
    };
    // `msg` goes to the log and the stage detail; `shortMsg` (when given) is the one-line summary.
    auto fail = [&](Stage s, const std::string& msg, const std::string& shortMsg = std::string())
    {
        p.set(s, Step::Failed, msg);
        p.add(2, msg);
        finish(2, shortMsg.empty() ? msg : shortMsg);
        UI::playSfx(UI::Sfx::Error);
    };
    auto cancelled = [&]()
    {
        if (!p.progress.cancel.load()) return false;
        p.add(3, "Cancelled.");
        finish(3, "Cancelled. Nothing was injected.");
        return true;
    };
    std::error_code ec;

    const bool dev = !ini::trim(cfg.devLocalDll).empty();
    const std::string expected = ini::trim(cfg.expectedSha256);

    const inj::ConfigCheck cc = inj::checkConfig(cfg);
    if (!cc.canRun())
    {
        const char* c = configBlockerText(cfg, cc.state);
        fail(kGet, cc.message, c ? c : cc.message);
        return;
    }

    {
        inj::Target t;
        std::string why;
        if (inj::revalidateTarget(cfg, pid, startTime, &t, &why))
            p.add(0, "Target: PID " + std::to_string(pid) + " (" + t.exeName + "), window \"" + t.title + "\".");
    }

    // Housekeeping first: interrupted downloads and unused cached DLLs from earlier runs.
    const inj::CleanupReport pre = inj::cleanCache(cfg);
    if (pre.removed || pre.kept)
        p.add(0, "Cache: removed " + std::to_string(pre.removed) + " stale file(s)" +
                 (pre.kept ? ", " + std::to_string(pre.kept) + " still in use by a running game." : "."));

    // 1) Obtain ------------------------------------------------
    p.set(kGet, Step::Running, dev ? "Copying the local build" : "Connecting...");
    fs::path temp;
    if (dev)
    {
        fs::path src = widePath(cfg.devLocalDll);
        if (src.is_relative()) src = cfg.rootDir / src;
        p.add(3, "Developer mode: using local build " + utf8Of(src));
        std::string err;
        if (!inj::copyToTemp(cfg, src, &temp, &err)) { fail(kGet, err, "Could not copy the local build. " + err); return; }
        p.set(kGet, Step::Done, "Local build copied to the cache");
    }
    else
    {
        p.add(0, "Downloading " + cfg.releaseUrl);
        p.progress.cancel = false;
        const auto dl = inj::downloadFile(cfg.releaseUrl, cfg.cacheDir, cfg.dllFileName, p.progress);
        if (!dl.ok)
        {
            if (p.progress.cancel.load()) { p.set(kGet, Step::Skipped, "Cancelled"); cancelled(); return; }
            fail(kGet, dl.error, "Download failed. " + dl.error);
            return;
        }
        temp = dl.file;
        p.set(kGet, Step::Done, inj::formatBytes(dl.bytes) + " received");
        p.add(0, "Downloaded " + inj::formatBytes(dl.bytes) + ".");
    }
    if (cancelled()) { fs::remove(temp, ec); return; }

    // 2) Verify ------------------------------------------------
    p.set(kVerify, Step::Running, "Checking the file (x64 DLL, SHA-256)");
    const inj::Validation v = inj::validateDll(temp, expected, /*requireHash=*/!dev);
    if (!v.ok)
    {
        fs::remove(temp, ec);   // never keep a rejected file
        std::string brief;
        if (v.wrongArch)                       brief = "Invalid architecture: the release file is not a 64-bit DLL. Nothing was loaded.";
        else if (v.hash == inj::HashState::Mismatch) brief = "Integrity check failed: the SHA-256 does not match the pinned value. The file was discarded.";
        else                                   brief = "Integrity check failed. " + v.message;
        fail(kVerify, v.message, brief);
        return;
    }
    const inj::StagedDll staged = inj::stageDll(cfg, temp, v.sha256);
    if (!staged.ok)
    {
        fs::remove(temp, ec);
        fail(kVerify, staged.error, "Could not stage the verified file. " + staged.error);
        return;
    }
    // Re-check the exact file that is about to be loaded.
    const inj::Validation v2 = inj::validateDll(staged.path, expected, !dev);
    if (!v2.ok || v2.sha256 != v.sha256)
    {
        fs::remove(staged.path, ec);
        fail(kVerify, "The cached DLL failed re-verification: " + v2.message,
             "Integrity check failed on the cached file. Nothing was loaded.");
        return;
    }
    if (staged.usedFallbackName)
        p.add(3, "The primary cache file is in use by a running game; using " + utf8Of(staged.path.filename()) + ".");
    const bool pinned = v.hash == inj::HashState::Verified;
    p.add(pinned ? 1 : 3, v.message);
    p.set(kVerify, pinned ? Step::Done : Step::Warn,
          pinned ? "SHA-256 matches the pinned value" : "Valid x64 DLL, integrity not pinned");
    if (cancelled()) { inj::cleanCache(cfg); return; }

    // 3) Inject ------------------------------------------------
    // Re-check the very process the user selected: same PID and same creation time, still a 64-bit
    // game window. If anything differs, nothing is injected.
    p.set(kInject, Step::Running, "Re-checking the selected process");
    {
        inj::Target cur;
        std::string why;
        if (!inj::revalidateTarget(cfg, pid, startTime, &cur, &why))
        {
            inj::cleanCache(cfg);
            fail(kInject, why, "Not injected. " + why);
            return;
        }
    }

    const std::string stem = utf8Of(fs::path(widePath(cfg.dllFileName)).stem());
    const inj::LoadResult r = inj::loadDllIntoProcess(pid, staged.path, stem, startTime);
    bool alreadyLoaded = false;
    if (!r.ok && !r.alreadyLoaded)
    {
        inj::cleanCache(cfg);   // nothing was loaded, so the cached copy can go
        fail(kInject, r.message, "Injection failed. " + r.message);
        return;
    }
    if (r.alreadyLoaded)
    {
        alreadyLoaded = true;
        p.set(kInject, Step::Skipped, "Already loaded in this game");
        p.add(3, r.message);
    }
    else
    {
        p.set(kInject, Step::Done, "Loaded into process " + std::to_string(pid));
        p.add(1, r.message);
    }

    // 4) Confirm the client actually started ----------------------
    p.set(kInit, Step::Running, "Waiting for the client to report");
    const inj::InitResult ir = inj::waitForClientInit(pid, alreadyLoaded ? 5000u : 30000u, p.progress.cancel,
        [&](const Status::Block& b) { p.set(kInit, Step::Running, stepDetailFor(b)); });

    switch (ir.outcome)
    {
    case inj::InitOutcome::Initialized:
        p.set(kInit, Step::Done, ir.message);
        p.add(1, ir.message);
        finish(1, "Cairn is running in Minecraft (PID " + std::to_string(pid) + "). Press Right Shift in game for the menu.");
        UI::playSfx(UI::Sfx::ToggleOn);
        break;
    case inj::InitOutcome::Limited:
        p.set(kInit, Step::Warn, ir.message);
        p.add(3, ir.message);
        finish(3, "Cairn loaded but could not reach Minecraft. Restart the game to try again.");
        UI::playSfx(UI::Sfx::Error);
        break;
    case inj::InitOutcome::NoReport:
        p.set(kInit, Step::Warn, ir.message);
        p.add(3, ir.message);
        finish(3, "The DLL is loaded but did not report that it started. " + ir.message);
        UI::playSfx(UI::Sfx::Error);
        break;
    case inj::InitOutcome::Cancelled:
        p.set(kInit, Step::Skipped, "Cancelled while waiting");
        p.add(3, "Stopped waiting. The DLL stays loaded in the game; closing this window does not unload it.");
        finish(3, "Stopped waiting. Cairn may still finish starting inside the game.");
        break;
    case inj::InitOutcome::ProcessExited:
        p.set(kInit, Step::Failed, ir.message);
        p.add(2, ir.message);
        finish(2, "Minecraft closed before Cairn finished starting.");
        UI::playSfx(UI::Sfx::Error);
        break;
    case inj::InitOutcome::ClientFailed:
    default:
        p.set(kInit, Step::Failed, ir.message);
        p.add(2, ir.message);
        finish(2, "Cairn failed to initialize. " + ir.message);
        UI::playSfx(UI::Sfx::Error);
        break;
    }

    // The loaded DLL is mapped by the game, so Windows refuses to delete it while the game
    // runs; anything else stale goes now, and the rest on the next start of the injector.
    const inj::CleanupReport post = inj::cleanCache(cfg);
    if (post.kept)
        p.add(0, "Cache: " + std::to_string(post.kept) + " file(s) still in use by the game; they are removed on a later run once the game closes.");
}

// ── Instance list ────────────────────────────────────────────
// Re-reads the running Java processes. The selection belongs to one exact process (PID + creation time) and
// is kept for as long as that process exists, even if its window is briefly missing (it then shows why it is
// unavailable and the run button stays disabled). It is dropped only when the process is gone or its PID was
// reused. The injector never moves the selection to another process on its own; with nothing selected and
// exactly one usable instance, that one is pre-selected.
static constexpr double kLostNoteSeconds = 8.0;

static void rescan(App& a, double now)
{
    a.targets = inj::findTargets(a.cfg);

    if (!a.lostNote.empty() && now - a.lostAt > kLostNoteSeconds) a.lostNote.clear();

    if (a.selPid)
    {
        const inj::Target* m = nullptr;
        for (const auto& t : a.targets)
            if (t.pid == a.selPid && (a.selStart == 0 || t.startTime == 0 || t.startTime == a.selStart)) m = &t;
        if (!m)
        {
            a.lostNote = "The selected Minecraft instance (PID " + std::to_string(a.selPid) + ") has closed.";
            a.lostAt = now;
            a.selPid = 0;
            a.selStart = 0;
        }
    }

    if (!a.selPid && a.lostNote.empty())
    {
        const inj::Target* only = nullptr;
        int n = 0;
        for (const auto& t : a.targets) if (t.usable()) { only = &t; n++; }
        if (n == 1) { a.selPid = only->pid; a.selStart = only->startTime; }
    }
}

static const inj::Target* selectedTarget(const App& a)
{
    if (!a.selPid) return nullptr;
    for (const auto& t : a.targets) if (t.pid == a.selPid) return &t;
    return nullptr;
}

// ── Small drawing helpers ────────────────────────────────────
static ImFont* capFont()  { return UI::g_fonts.caption  ? UI::g_fonts.caption  : ImGui::GetFont(); }
static ImFont* semiFont() { return UI::g_fonts.semibold ? UI::g_fonts.semibold : ImGui::GetFont(); }

static float textWidth(ImFont* f, const char* s)
{
    ImGui::PushFont(f);
    const float w = ImGui::CalcTextSize(s).x;
    ImGui::PopFont();
    return w;
}
static float lineHeight(ImFont* f)
{
    ImGui::PushFont(f);
    const float h = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    return h;
}
static void textAt(ImDrawList* dl, ImFont* f, ImVec2 p, const ImVec4& col, const char* s, const ImVec4* clip = nullptr)
{
    ImGui::PushFont(f);
    dl->AddText(f, ImGui::GetFontSize(), p, UI::C(col), s, nullptr, 0.0f, clip);
    ImGui::PopFont();
}

static void captionText(const ImVec4& c, const char* s, bool wrap)
{
    ImGui::PushFont(capFont());
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    if (wrap) ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(s);
    if (wrap) ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Small text-only button. Does not move the layout cursor, so it can be placed beside other content.
static bool linkButton(const char* id, const char* label, ImVec2 pos, bool enabled = true,
                       const ImVec4& base = UI::Col::TextDim)
{
    using namespace UI;
    ImFont* f = capFont();
    const ImVec2 size(textWidth(f, label) + S(12.0f), lineHeight(f) + S(8.0f));
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool clicked = false;
    ImVec4 col = Col::Disabled;
    if (enabled)
    {
        ImGui::SetCursorScreenPos(pos);
        clicked = ImGui::InvisibleButton(id, size);
        const bool hov = ImGui::IsItemHovered();
        const float t = animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 16.0f);
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        col = Mix(base, Col::Text, t);
    }
    textAt(dl, f, ImVec2(pos.x + S(6.0f), pos.y + S(4.0f)), col, label);
    ImGui::SetCursorScreenPos(saved);
    if (clicked) playSfx(Sfx::Click);
    return clicked;
}

// Caption button: kind 0 = minimise, 1 = close. Returns true when clicked.
static bool captionButton(const char* id, ImVec2 pos, ImVec2 size, int kind)
{
    using namespace UI;
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    const float t = animate(ImGui::GetID(id), hov ? 1.0f : 0.0f, 18.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(pos.x + size.x, pos.y + size.y);
    if (kind == 1)
        dl->AddRectFilled(pos, p1, C(Col::Danger, 0.90f * t));
    else
        dl->AddRectFilled(pos, p1, C(Col::SurfaceHover, t));

    const ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    const float r = S(5.0f);
    const ImU32 col = C(Mix(Col::TextDim, Col::Text, t));
    if (kind == 0)
        dl->AddLine(ImVec2(c.x - r, c.y + S(0.5f)), ImVec2(c.x + r, c.y + S(0.5f)), col, 1.3f);
    else
    {
        dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.3f);
        dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.3f);
    }
    return clicked;
}

static void drawTitleBar(HWND hwnd)
{
    using namespace UI;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float H = S(kTitleH);
    const ImVec2 p0 = vp->Pos;

    dl->AddRectFilled(p0, ImVec2(p0.x + vp->Size.x, p0.y + H), C(Col::Sidebar));

    Brand::drawLogo(dl, ImVec2(p0.x + S(24.0f), p0.y + H * 0.5f), S(20.0f));

    ImGui::PushFont(semiFont());
    dl->AddText(ImVec2(p0.x + S(42.0f), p0.y + (H - ImGui::GetTextLineHeight()) * 0.5f), C(Col::Text), Brand::kInjectorName);
    ImGui::PopFont();

    const ImVec2 bs(S(kCapBtnW), H);
    if (captionButton("##min", ImVec2(p0.x + vp->Size.x - bs.x * 2.0f, p0.y), bs, 0))
        ShowWindow(hwnd, SW_MINIMIZE);
    if (captionButton("##close", ImVec2(p0.x + vp->Size.x - bs.x, p0.y), bs, 1))
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

// One selectable Minecraft instance. Returns true when clicked.
static bool instanceRow(const inj::Target& t, bool selected, bool enabled, float w)
{
    using namespace UI;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = S(44.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::PushID(static_cast<int>(t.pid));
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h)) && enabled;
    const bool hov = enabled && ImGui::IsItemHovered();
    const float hv = animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 16.0f);
    ImGui::PopID();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const ImVec2 p1(p.x + w, p.y + h);
    if (selected) dl->AddRectFilled(p, p1, C(Col::Selected), S(8.0f));
    else if (hv > 0.01f) dl->AddRectFilled(p, p1, C(Col::SurfaceHover, hv), S(8.0f));

    // Radio
    const ImVec2 rc(p.x + S(18.0f), p.y + h * 0.5f);
    const float rr = S(7.0f);
    dl->AddCircle(rc, rr, C(selected ? Col::Accent : enabled ? Col::BorderStrong : Col::Border), 24, 1.5f);
    if (selected) dl->AddCircleFilled(rc, rr * 0.5f, C(Col::Accent), 16);

    ImFont* rf = ImGui::GetFont();
    ImFont* cf = capFont();
    const float x = p.x + S(38.0f);
    const float y1 = p.y + S(5.0f);
    const float y2 = y1 + lineHeight(rf) + S(1.0f);

    // Right-hand tag: the version read from the window title, or "Launcher".
    std::string tag;
    ImVec4 tagCol = Col::TextFaint;
    if (t.kind == inj::TargetKind::Launcher) tag = "Launcher";
    else if (t.kind == inj::TargetKind::Starting) tag = "Starting";
    else if (!t.version.empty())
    {
        tag = t.version;
        if (t.version != kExpectedVersion) tagCol = Col::Warning;
    }
    const float tagW = tag.empty() ? 0.0f : textWidth(cf, tag.c_str());
    if (!tag.empty())
        textAt(dl, cf, ImVec2(p1.x - S(14.0f) - tagW, y1 + S(2.0f)), tagCol, tag.c_str());

    const float rightLimit = p1.x - S(14.0f) - (tag.empty() ? 0.0f : tagW + S(10.0f));
    const ImVec4 clip(x, p.y, rightLimit, p1.y);
    const std::string title = !t.title.empty() ? t.title
                              : t.kind == inj::TargetKind::Starting ? std::string("Minecraft (starting)")
                                                                    : std::string("Java process");
    textAt(dl, rf, ImVec2(x, y1), enabled ? Col::Text : Col::TextDim, title.c_str(), &clip);

    // Second line: process name and PID, then a note or the client state.
    char ids[96];
    std::snprintf(ids, sizeof(ids), "%s  -  PID %lu", t.exeName.c_str(), t.pid);
    textAt(dl, cf, ImVec2(x, y2), Col::TextFaint, ids, &clip);
    const float idsW = textWidth(cf, ids);

    std::string extra;
    ImVec4 extraCol = Col::TextDim;
    if (!t.usable() && !t.note.empty()) { extra = t.note; extraCol = (t.kind == inj::TargetKind::Game) ? Col::Danger : Col::TextDim; }
    else if (t.clientPresent)
    {
        using St = Status::State;
        switch (t.clientState)
        {
        case St::Ready:     extra = "Cairn running";           extraCol = Col::Accent;  break;
        case St::Limited:   extra = "Cairn loaded (limited)";  extraCol = Col::Warning; break;
        case St::Failed:    extra = "Cairn failed to start";   extraCol = Col::Danger;  break;
        case St::Unloading: extra = "Cairn unloading";         extraCol = Col::Warning; break;
        default:            extra = "Cairn starting";          extraCol = Col::TextDim; break;
        }
    }
    if (!extra.empty())
        textAt(dl, cf, ImVec2(x + idsW + S(14.0f), y2), extraCol, extra.c_str(), &clip);

    return clicked;
}

enum class PB { Ready, Disabled, Working, Success, Error };

// The one primary action. Distinct looks for ready / hover / pressed / disabled / working / success / error.
static bool primaryButton(const char* label, PB state, float w)
{
    using namespace UI;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 size(w, S(44.0f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clickable = state == PB::Ready || state == PB::Error;

    const bool pressedRaw = ImGui::InvisibleButton("##primary", size);
    const bool hov = clickable && ImGui::IsItemHovered();
    const bool act = clickable && ImGui::IsItemActive();
    const float hv = animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 16.0f);
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const ImVec2 p1(p.x + size.x, p.y + size.y);
    const float rad = S(8.0f);
    ImVec4 bg, text, border(0, 0, 0, 0);
    switch (state)
    {
    case PB::Ready:
        bg = act ? Col::Active : Mix(Col::Accent, Col::AccentHover, hv);
        text = Col::AccentText;
        break;
    case PB::Disabled:
        bg = Col::Surface;
        border = Col::Border;
        text = Col::TextDim;     // readable, but clearly not the teal "ready" look
        break;
    case PB::Working:
        bg = Mix(Col::SurfaceRaised, Col::Accent, 0.22f);
        text = Col::Text;
        break;
    case PB::Success:
        bg = WithAlpha(Col::Accent, 0.14f);
        border = WithAlpha(Col::Accent, 0.55f);
        text = Col::Accent;
        break;
    case PB::Error:
    default:
        bg = WithAlpha(Col::Danger, act ? 0.28f : 0.12f + 0.10f * hv);
        border = WithAlpha(Col::Danger, 0.50f);
        text = Col::Danger;
        break;
    }
    dl->AddRectFilled(p, p1, C(bg), rad);
    if (border.w > 0.0f) dl->AddRect(p, p1, C(border), rad, 0, 1.0f);

    ImGui::PushFont(semiFont());
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float iconW = (state == PB::Working || state == PB::Success) ? S(24.0f) : 0.0f;
    const float x0 = p.x + (size.x - ts.x - iconW) * 0.5f;
    const float cy = p.y + size.y * 0.5f;
    if (state == PB::Working)
    {
        const float r = S(7.0f);
        const ImVec2 c(x0 + r, cy);
        const float a0 = static_cast<float>(ImGui::GetTime()) * 5.0f;
        dl->PathArcTo(c, r, a0, a0 + 4.2f, 20);
        dl->PathStroke(C(Col::Accent), 0, 2.0f);
    }
    else if (state == PB::Success)
    {
        const float r = S(7.0f);
        const ImVec2 c(x0 + r, cy);
        const ImVec2 tick[3] = { ImVec2(c.x - r * 0.55f, c.y + r * 0.05f),
                                 ImVec2(c.x - r * 0.12f, c.y + r * 0.48f),
                                 ImVec2(c.x + r * 0.62f, c.y - r * 0.42f) };
        dl->AddPolyline(tick, 3, C(Col::Accent), 0, 2.0f);
    }
    dl->AddText(ImVec2(x0 + iconW, cy - ts.y * 0.5f), C(text), label);
    ImGui::PopFont();

    if (pressedRaw && clickable) { playSfx(Sfx::Click); return true; }
    return false;
}

// ── Release summary ──────────────────────────────────────────
static void describeRelease(const inj::Config& c, std::string& line)
{
    std::string u = ini::trim(c.releaseUrl);
    line.clear();
    if (u.empty()) return;
    if (ini::lower(u.substr(0, 8)) == "https://") u = u.substr(8);
    std::vector<std::string> seg;
    std::string cur;
    for (char ch : u) { if (ch == '/') { seg.push_back(cur); cur.clear(); } else cur.push_back(ch); }
    seg.push_back(cur);
    if (seg.size() == 7 && seg[3] == "releases")
    {
        line = seg[1] + "/" + seg[2] + "  -  " + seg[6] + "  -  " + (seg[4] == "latest" ? std::string("latest") : seg[5]);
        return;
    }
    line = u.size() > 56 ? u.substr(0, 53) + "..." : u;
}

static void kvRow(const char* key, const std::string& value, const ImVec4& valueCol = UI::Col::Text)
{
    using namespace UI;
    const float x0 = ImGui::GetCursorPosX();
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextFaint);
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::SameLine(x0 + S(74.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, valueCol);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value.empty() ? "-" : value.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// ── Main window content ──────────────────────────────────────
static void drawUi(App& a, HWND hwnd, float timeSec)
{
    using namespace UI;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    const bool busy = a.pipe.busy.load();
    const bool dev = a.check.state == inj::ConfigState::DeveloperMode;

    // Rescan while idle so a closed game disappears and a new one shows up. The list is frozen
    // while an operation runs, so the instance being worked on cannot change under it.
    if (!busy && timeSec - a.lastScan > 2.0f) { a.lastScan = timeSec; rescan(a, timeSec); }
    const inj::Target* sel = selectedTarget(a);

    int usableN = 0, launcherN = 0, startingN = 0, gameN = 0, otherN = 0;
    for (const auto& t : a.targets)
    {
        if (t.usable()) usableN++;
        switch (t.kind)
        {
        case inj::TargetKind::Launcher: launcherN++; break;
        case inj::TargetKind::Starting: startingN++; break;
        case inj::TargetKind::Game:     gameN++; break;
        default:                        otherN++; break;
        }
    }

    drawTitleBar(hwnd);
    ImGui::GetWindowDrawList()->AddRect(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                        C(Col::BorderStrong), 0.0f, 0, 1.0f);

    const float padX = S(24.0f);
    const float padB = S(16.0f);
    ImGui::SetCursorPos(ImVec2(0, S(kTitleH)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, S(18.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.0f), S(6.0f)));
    ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    const float w = ImGui::GetContentRegionAvail().x;
    const float x0 = ImGui::GetCursorScreenPos().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Minecraft ───────────────────────────────────────────
    {
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        SectionLabel("MINECRAFT");
        const float lw = textWidth(capFont(), "Refresh") + S(12.0f);
        if (linkButton("##refresh", "Refresh", ImVec2(x0 + w - lw, hp.y - S(4.0f)), !busy))
        {
            a.lastScan = timeSec;
            rescan(a, timeSec);
        }
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##list", ImVec2(w, S(94.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGui::PopStyleVar(1);
    {
        const float rowW = ImGui::GetContentRegionAvail().x;
        int shown = 0;
        for (const auto& t : a.targets)
        {
            // Other Java processes are never offered, except one that was selected and has lost its window.
            if (t.kind == inj::TargetKind::Other && t.pid != a.selPid) continue;
            shown++;
            const bool isSel = a.selPid == t.pid;
            if (instanceRow(t, isSel, !busy && t.usable(), rowW) && !isSel)
            {
                a.selPid = t.pid;
                a.selStart = t.startTime;
                a.lostNote.clear();
                a.pipe.reset();
            }
        }
        if (shown == 0)
        {
            ImGui::Dummy(ImVec2(0, S(14.0f)));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(4.0f));
            ImGui::PushFont(semiFont());
            ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
            ImGui::TextUnformatted(startingN ? "Minecraft is starting" : "No Minecraft instance detected");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(4.0f));
            captionText(Col::TextFaint,
                        startingN ? "Minecraft was started but its game window is not open yet. The list refreshes by itself."
                                  : otherN ? "Java is running, but nothing there looks like a Minecraft game window."
                                           : "Start Minecraft 1.21.11 (official launcher or Modrinth App) and open the game window.",
                        true);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor();

    // ── Release ─────────────────────────────────────────────
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    {
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        SectionLabel("RELEASE");
        const float lw = textWidth(capFont(), "Open config folder") + S(12.0f);
        if (linkButton("##cfg", "Open config folder", ImVec2(x0 + w - lw, hp.y - S(4.0f))))
            ShellExecuteW(hwnd, L"open", a.cfg.configDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    {
        const bool haveSrc = !ini::trim(a.cfg.releaseUrl).empty();
        std::string src;
        if (dev) src = "Local build (developer mode)";
        else describeRelease(a.cfg, src);
        ImGui::PushStyleColor(ImGuiCol_Text, (dev || haveSrc) ? Col::Text : Col::Warning);
        ImGui::TextUnformatted((dev || haveSrc) ? src.c_str() : "Not configured");
        ImGui::PopStyleColor();

        // With no URL, "Not configured" above is the one warning; a missing hash is just a secondary detail.
        const std::string sh = inj::normalizeSha256(a.cfg.expectedSha256);
        const bool bad = a.check.state == inj::ConfigState::BadHash;
        const bool secondary = sh.empty() && !bad && !haveSrc && !dev;
        StatusDot(!sh.empty() ? Col::Accent : bad ? Col::Danger : secondary ? Col::TextFaint : Col::Warning);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, !sh.empty() ? Col::TextDim : bad ? Col::Danger : secondary ? Col::TextDim : Col::Warning);
        ImGui::TextUnformatted(!sh.empty() ? "SHA-256 pinned" : bad ? "SHA-256 invalid" : dev ? "Integrity not pinned" : "SHA-256 not configured");
        ImGui::PopStyleColor();
    }

    // ── State shared with the worker ────────────────────────
    Step st[kStageCount];
    std::string det[kStageCount];
    std::string summary;
    int summaryKind;
    {
        std::lock_guard<std::mutex> g(a.pipe.mtx);
        for (int i = 0; i < kStageCount; i++) { st[i] = a.pipe.step[i]; det[i] = a.pipe.detail[i]; }
        summary = a.pipe.summary;
        summaryKind = a.pipe.summaryKind;
    }
    int running = -1;
    for (int i = 0; i < kStageCount; i++) if (st[i] == Step::Running) running = i;

    // What, if anything, stops a run right now.
    std::string blocker;
    if (const char* c = configBlockerText(a.cfg, a.check.state)) blocker = c;
    else if (sel && !sel->usable())
        blocker = sel->note.empty() ? "The selected process cannot be used right now." : sel->note;
    else if (sel)
    {
        if (sel->clientPresent)
        {
            if (sel->clientState == Status::State::Ready) blocker = "Cairn is already running in this game.";
            else if (sel->clientState == Status::State::Limited || sel->clientState == Status::State::Failed ||
                     sel->clientState == Status::State::Unloading) blocker = "Restart Minecraft to load Cairn again.";
            else blocker = "Cairn is still starting in this game.";
        }
    }
    else if (usableN == 0)
    {
        if (gameN > 0)           blocker = "The detected Minecraft process cannot be used. See the note above.";
        else if (startingN > 0)  blocker = "Minecraft is still starting. No game window yet.";
        else if (launcherN > 0)  blocker = "Only a launcher was found. Start the game from it.";
        else                     blocker = "No Minecraft instance detected.";
    }
    else blocker = usableN > 1 ? "Multiple instances found. Select one." : "Select a Minecraft instance.";

    // ── Primary action ──────────────────────────────────────
    ImGui::Dummy(ImVec2(0, S(6.0f)));
    PB pb = PB::Ready;
    const char* label = dev ? "Inject local build" : "Download & Inject";
    if (busy) { pb = PB::Working; label = running >= 0 ? kStageBusyLabel[running] : "Working..."; }
    else if (sel && sel->clientPresent && sel->clientState == Status::State::Ready) { pb = PB::Success; label = "Cairn running"; }
    else if (!blocker.empty()) pb = PB::Disabled;
    else if (summaryKind == 2) { pb = PB::Error; label = "Try again"; }

    if (primaryButton(label, pb, w))
    {
        // Re-check the selected process right now, before anything is downloaded or touched.
        inj::Target cur;
        std::string why;
        if (a.selPid && inj::revalidateTarget(a.cfg, a.selPid, a.selStart, &cur, &why))
        {
            if (a.worker.joinable()) a.worker.join();
            a.pipe.reset();
            a.pipe.progress.cancel = false;
            a.pipe.progress.received = 0;
            a.pipe.progress.total = 0;
            a.pipe.pid = a.selPid;
            a.pipe.busy = true;
            a.worker = std::thread(runPipeline, &a, a.cfg, a.selPid, a.selStart);
        }
        else
        {
            a.lostNote = why.empty() ? "Select a Minecraft instance first." : why;
            a.lostAt = timeSec;
            a.selPid = 0;
            a.selStart = 0;
            a.pipe.reset();
            rescan(a, timeSec);
            a.lastScan = timeSec;
            playSfx(Sfx::Error);
        }
    }

    // Thin progress line, always reserving its height so nothing jumps.
    ImGui::Dummy(ImVec2(0, S(2.0f)));
    if (busy)
    {
        const auto got = a.pipe.progress.received.load();
        const auto tot = a.pipe.progress.total.load();
        const float frac = (running == kGet && !dev && tot) ? static_cast<float>(static_cast<double>(got) / static_cast<double>(tot)) : -1.0f;
        ProgressBar(frac, ImVec2(w, S(3.0f)));
    }
    else
        ImGui::Dummy(ImVec2(w, S(3.0f)));

    // ── Status ──────────────────────────────────────────────
    // kind: 0 idle, 1 ok, 2 error, 3 warning, 4 busy
    int nKind = 0;
    std::string nText, nSub;
    if (busy)
    {
        nKind = 4;
        nText = running >= 0 ? ((running == kGet && dev) ? "Copying the local build" : kStageNote[running]) : "Working";
        if (running >= 0 && !det[running].empty()) nSub = det[running];
        if (running == kGet && !dev)
        {
            const auto got = a.pipe.progress.received.load();
            const auto tot = a.pipe.progress.total.load();
            nSub = inj::formatBytes(got) + (tot ? " / " + inj::formatBytes(tot) : std::string());
        }
    }
    else if (!a.lostNote.empty()) { nKind = 3; nText = a.lostNote; }
    else if (!summary.empty() && sel) { nKind = summaryKind; nText = summary; }
    else if (!blocker.empty())
    {
        const bool cfgProblem = configBlockerText(a.cfg, a.check.state) != nullptr;
        nKind = cfgProblem ? 3 : 0;
        nText = blocker;
    }
    else if (!summary.empty()) { nKind = summaryKind; nText = summary; }
    else
    {
        nKind = dev ? 3 : 0;
        nText = dev ? "Developer mode: a local build will be loaded. " + a.check.message
                    : "Ready. Cairn will be downloaded, verified and loaded into PID " + std::to_string(a.selPid) + ".";
    }

    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const bool canCancel = busy && (running == kGet || running == kInit);
    if (canCancel)
    {
        const float lw = textWidth(capFont(), "Cancel") + S(12.0f);
        if (linkButton("##cancel", "Cancel", ImVec2(x0 + w - lw, sp.y - S(3.0f)), true, Col::TextDim))
            a.pipe.progress.cancel = true;
    }
    // Height follows the message (at least two lines), so long errors are never clipped.
    const float textWrapW = w - S(26.0f) - (canCancel ? S(54.0f) : 0.0f);
    float statusH = ImGui::CalcTextSize(nText.c_str(), nullptr, false, textWrapW).y;
    if (!nSub.empty()) statusH += lineHeight(capFont()) + S(2.0f);
    statusH = std::max(S(42.0f), statusH + S(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##status", ImVec2(w, statusH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoInputs);
    ImGui::PopStyleVar(1);
    {
        ImDrawList* cd = ImGui::GetWindowDrawList();
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float lh = ImGui::GetTextLineHeight();
        const ImVec2 c(cp.x + S(8.0f), cp.y + lh * 0.5f + S(1.0f));
        const float r = S(7.0f);
        switch (nKind)
        {
        case 4:
        {
            const float a0 = static_cast<float>(ImGui::GetTime()) * 5.0f;
            cd->PathArcTo(c, r, a0, a0 + 4.2f, 20);
            cd->PathStroke(C(Col::Accent), 0, 2.0f);
            break;
        }
        case 1:
        {
            cd->AddCircleFilled(c, r, C(Col::Accent), 20);
            const ImVec2 tick[3] = { ImVec2(c.x - r * 0.45f, c.y + r * 0.02f), ImVec2(c.x - r * 0.12f, c.y + r * 0.38f),
                                     ImVec2(c.x + r * 0.50f, c.y - r * 0.36f) };
            cd->AddPolyline(tick, 3, C(Col::AccentText), 0, 1.8f);
            break;
        }
        case 2:
            cd->AddCircleFilled(c, r, C(Col::Danger), 20);
            cd->AddLine(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), ImVec2(c.x + r * 0.35f, c.y + r * 0.35f), C(Col::Bg), 1.8f);
            cd->AddLine(ImVec2(c.x - r * 0.35f, c.y + r * 0.35f), ImVec2(c.x + r * 0.35f, c.y - r * 0.35f), C(Col::Bg), 1.8f);
            break;
        case 3:
            cd->AddCircleFilled(c, r, C(Col::Warning), 20);
            cd->AddLine(ImVec2(c.x, c.y - r * 0.45f), ImVec2(c.x, c.y + r * 0.10f), C(Col::Bg), 1.8f);
            cd->AddCircleFilled(ImVec2(c.x, c.y + r * 0.42f), 1.2f * g_scale, C(Col::Bg), 8);
            break;
        default:
            cd->AddCircleFilled(c, S(3.0f), C(Col::TextFaint), 12);
            break;
        }

        const float tx = ImGui::GetCursorPosX() + S(26.0f);
        ImGui::SetCursorPosX(tx);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWrapW);
        ImGui::PushStyleColor(ImGuiCol_Text, nKind == 0 ? Col::TextDim : Col::Text);
        ImGui::TextUnformatted(nText.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        if (!nSub.empty())
        {
            ImGui::SetCursorPosX(tx);
            captionText(Col::TextDim, nSub.c_str(), true);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ── Details (collapsed by default) ──────────────────────
    {
        const ImVec2 tp = ImGui::GetCursorScreenPos();
        const ImVec2 size(S(96.0f), S(24.0f));
        if (ImGui::InvisibleButton("##detailsToggle", size)) { a.detailsOpen = !a.detailsOpen; playSfx(Sfx::Click); }
        const bool hov = ImGui::IsItemHovered();
        const float t = animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 16.0f);
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec4 col = Mix(Col::TextDim, Col::Text, t);
        const float cx = tp.x + S(8.0f), cy = tp.y + size.y * 0.5f, q = S(3.5f);
        if (a.detailsOpen)
            dl->AddTriangleFilled(ImVec2(cx - q, cy - q * 0.6f), ImVec2(cx + q, cy - q * 0.6f), ImVec2(cx, cy + q * 0.8f), C(col));
        else
            dl->AddTriangleFilled(ImVec2(cx - q * 0.6f, cy - q), ImVec2(cx - q * 0.6f, cy + q), ImVec2(cx + q * 0.8f, cy), C(col));
        textAt(dl, capFont(), ImVec2(tp.x + S(22.0f), cy - lineHeight(capFont()) * 0.5f), col, "Details");
    }

    if (a.detailsOpen)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::Surface);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12.0f), S(10.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(6.0f));
        ImGui::BeginChild("##details", ImVec2(w, S(176.0f)), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
        ImGui::PopStyleVar(2);
        ImGui::PushFont(capFont());
        {
            std::lock_guard<std::mutex> g(a.pipe.mtx);
            kvRow("Injector", std::string(Brand::kInjectorName) + " v" + Brand::kVersion);
            if (sel)
            {
                kvRow("Process", sel->exeName + "  PID " + std::to_string(sel->pid) + (sel->is64 ? "  x64" : "  32-bit"));
                kvRow("Window", sel->title);
                kvRow("Path", sel->exePath);
                kvRow("Identified", sel->how.empty() ? std::string("-") : sel->how);
                kvRow("Version", sel->version.empty() ? std::string("not stated by the command line or window title")
                                                      : sel->version + " (from the " + sel->versionSource + ", not verified)",
                      sel->version.empty() || sel->version == kExpectedVersion ? Col::Text : Col::Warning);
                if (!sel->launcherBrand.empty()) kvRow("Launcher", sel->launcherBrand + " (as passed to the game)");
            }
            else
                kvRow("Process", "none selected");
            for (const auto& t : a.targets)
                if (t.pid != a.selPid && !t.usable())
                    kvRow("Skipped", t.exeName + " PID " + std::to_string(t.pid) + ": " + (t.note.empty() ? std::string("not usable") : t.note));
            kvRow("Release", ini::trim(a.cfg.releaseUrl));
            kvRow("SHA-256", inj::normalizeSha256(a.cfg.expectedSha256));
            kvRow("Config", utf8Of(a.cfg.iniPath));
            kvRow("Cache", utf8Of(a.cfg.cacheDir));
            if (!a.check.message.empty()) kvRow("Check", a.check.message, a.check.canRun() ? Col::TextDim : Col::Warning);
            if (!a.configNote.empty()) kvRow("Note", a.configNote, Col::Warning);

            ImGui::Dummy(ImVec2(0, S(4.0f)));
            const ImVec2 lp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y), C(Col::Border));
            ImGui::Dummy(ImVec2(0, S(4.0f)));

            if (a.pipe.log.empty())
                captionText(Col::TextFaint, "No activity yet.", false);
            for (auto& e : a.pipe.log)
            {
                const ImVec4 c = e.first == 1 ? Col::Accent : e.first == 2 ? Col::Danger : e.first == 3 ? Col::Warning : Col::TextDim;
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(e.second.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            if (a.pipe.log.size() != a.logSeen) { ImGui::SetScrollHereY(1.0f); a.logSeen = a.pipe.log.size(); }
        }
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor();
    }

    // The window is exactly as tall as the layout needs (title bar + body + bottom padding).
    a.wantH = (S(kTitleH) + ImGui::GetCursorPosY() + padB) / g_scale;

    ImGui::EndChild();
    ImGui::End();
}

// Sets the window height (logical px) without moving it off the work area.
static void applyHeight(HWND hwnd, float logicalH)
{
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    int h = static_cast<int>(logicalH * g_dpi + 0.5f);
    if (h > workH) h = workH;
    int top = r.top;
    if (top + h > mi.rcWork.bottom) top = mi.rcWork.bottom - h;
    if (top < mi.rcWork.top) top = mi.rcWork.top;
    SetWindowPos(hwnd, nullptr, r.left, top, r.right - r.left, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// ── Entry point ──────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    // One injector at a time (two would race on the same cache files).
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\CairnInjector.Instance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND other = FindWindowW(L"CairnInjectorWindow", nullptr))
        {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        CloseHandle(instanceMutex);
        return 0;
    }

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"CairnInjectorWindow";
    RegisterClassExW(&wc);

    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
    if (g_dpi < 0.5f) g_dpi = 1.0f;

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    const int winW = static_cast<int>(kWinW * g_dpi);
    const int winH = static_cast<int>(kWinHGuess * g_dpi);
    const int workW = mi.rcWork.right - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    const int useH = winH > workH ? workH : winH;
    const int x = mi.rcWork.left + (workW - winW) / 2;
    const int y = mi.rcWork.top + (workH - useH) / 2;

    // WS_POPUP: no system title bar, no border. WS_MINIMIZEBOX/WS_SYSMENU keep taskbar minimise/restore working.
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Cairn Injector",
                                WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                                x, y, winW, useH, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    {
        // Drop shadow and (Windows 11) rounded corners for the borderless window.
        const MARGINS m = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &m);
        const int corner = 2;                       // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));          // DWMWA_WINDOW_CORNER_PREFERENCE
        const COLORREF border = RGB(53, 67, 71);    // matches UI::Col::BorderStrong
        DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));          // DWMWA_BORDER_COLOR
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    if (!createDevice(hwnd))
    {
        MessageBoxW(hwnd, L"Could not create a Direct3D 11 device.", L"Cairn Injector", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    UI::g_scale = g_dpi;
    UI::loadFonts(io, 16.0f * g_dpi);
    UI::applyStyle(UI::g_scale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    auto app = std::make_unique<App>();
    app->cfg = inj::loadConfig(&app->configNote);
    app->check = inj::checkConfig(app->cfg);

    // Startup cleanup: interrupted downloads and cached DLLs no running game is holding.
    inj::cleanCache(app->cfg);

    app->lastScan = -10.0;

    Settings::load();
    Sound::init();
    UI::g_sfxHandler = &Sound::play;

    const auto t0 = std::chrono::steady_clock::now();
    float appliedH = kWinHGuess;
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (IsIconic(hwnd)) { Sleep(50); continue; }

        if (g_dpiChanged.exchange(false))
        {
            g_dpi = g_newDpi;
            UI::g_scale = g_dpi;
            ImGui_ImplDX11_InvalidateDeviceObjects();     // font atlas is rebuilt at the new size
            UI::loadFonts(io, 16.0f * g_dpi);
            UI::applyStyle(g_dpi);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        drawUi(*app, hwnd, t);
        ImGui::Render();


        const float clear[4] = { UI::Col::Bg.x, UI::Col::Bg.y, UI::Col::Bg.z, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);

        // The layout decides the height (collapsed vs. details open). Applied between frames; the next
        // frame is drawn at the new size.
        if (app->wantH > 100.0f && std::fabs(app->wantH - appliedH) > 0.75f)
        {
            appliedH = app->wantH;
            applyHeight(hwnd, appliedH);
        }
    }

    // Closing the injector never unloads the client from the game. It only stops our own work.
    app->pipe.progress.cancel = true;
    if (app->worker.joinable()) app->worker.join();
    inj::cleanCache(app->cfg);   // best effort: a DLL still mapped by the game is left in place
    UI::g_sfxHandler = nullptr;
    Sound::shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    if (instanceMutex) CloseHandle(instanceMutex);
    return 0;
}
