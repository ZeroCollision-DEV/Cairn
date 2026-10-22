#include "Hooks.h"
#include "Client.h"
#include "../utils/Logger.h"
#include "../menu/Theme.h"
#include "../shared/UiTheme.h"

#include <MinHook.h>
#include <gl/GL.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

Hooks& Hooks::instance()
{
    static Hooks inst;
    return inst;
}

// ════════════════════════════════════════════════════════════
//  Init — called from the background init thread.
//  Sets up the MinHook on wglSwapBuffers so that every
//  subsequent frame hits our hook.
// ════════════════════════════════════════════════════════════
bool Hooks::init()
{
    m_opengl32 = GetModuleHandleA("opengl32.dll");
    if (!m_opengl32)
    {
        OutputDebugStringA("cairn: opengl32.dll not loaded\n");
        return false;
    }

    void* target = (void*)GetProcAddress(m_opengl32, "wglSwapBuffers");
    if (!target)
    {
        OutputDebugStringA("cairn: wglSwapBuffers not found\n");
        return false;
    }

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "cairn: MH_Initialize failed: %d\n", s);
        OutputDebugStringA(buf);
        return false;
    }

    s = MH_CreateHook(target, (void*)&hkSwapBuffers, &m_origSwap);
    if (s != MH_OK)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "cairn: MH_CreateHook failed: %d\n", s);
        OutputDebugStringA(buf);
        return false;
    }

    s = MH_EnableHook(target);
    if (s != MH_OK)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "cairn: MH_EnableHook failed: %d\n", s);
        OutputDebugStringA(buf);
        return false;
    }

    m_hookOK = true;
    OutputDebugStringA("cairn: wglSwapBuffers hooked\n");
    return true;
}

void Hooks::shutdown()
{
    if (!m_hookOK) return;

    // Order matters on an explicit unload: stop new input and new frames from entering our
    // code first, give any call already in flight time to leave, and only then tear ImGui down.
    // (There is no eject feature; unloading is best-effort and not exercised.)

    // 1. Put the game's WndProc back -- but only if ours is still the active one; if something
    //    else subclassed the window after us, replacing it would break their chain.
    if (m_origWndProc && m_hwnd && GetWindowLongPtr(m_hwnd, GWLP_WNDPROC) == (LONG_PTR)&hkWndProc)
        SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, (LONG_PTR)m_origWndProc);

    // 2. Stop intercepting wglSwapBuffers.
    void* target = (void*)GetProcAddress(m_opengl32, "wglSwapBuffers");
    if (target) MH_DisableHook(target);
    m_hookOK = false;
    onRender = nullptr;
    onTickFallback = nullptr;

    // 3. Let a frame that was already inside the hook finish.
    Sleep(150);
    MH_Uninitialize();

    // 4. ImGui teardown. The OpenGL context is not current on this thread, so GL object
    //    deletion may be skipped by the driver (a small leak at worst).
    if (m_imguiInit)
    {
        m_imguiInit = false;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    OutputDebugStringA("cairn: shutdown\n");
}

// ════════════════════════════════════════════════════════════
//  ImGui init — runs on the RENDER THREAD inside hkSwapBuffers.
//  The OpenGL context is guaranteed to be current here.
// ════════════════════════════════════════════════════════════
void Hooks::initImGui(HDC hdc)
{
    // Safety: we must have a valid GL context
    if (!wglGetCurrentContext())
    {
        OutputDebugStringA("cairn: initImGui: no GL context, will retry\n");
        return;  // m_imguiInit stays false, hkSwapBuffers will retry
    }

    OutputDebugStringA("cairn: initImGui starting\n");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // don't write imgui.ini

    // Fonts: Segoe UI from the Windows fonts folder (falls back to ImGui's default).
    UI::loadFonts(io);

    Theme::apply();

    // Get the game window from the DC
    m_hwnd = WindowFromDC(hdc);

    // Validate the window handle
    if (!m_hwnd || m_hwnd == GetDesktopWindow())
    {
        OutputDebugStringA("cairn: initImGui: invalid HWND\n");
        m_imguiFailed = true;
        return;
    }

    char className[128] = {};
    GetClassNameA(m_hwnd, className, sizeof(className));
    OutputDebugStringA(("cairn: game HWND class: " + std::string(className) + "\n").c_str());

    // Init ImGui backends
    if (!ImGui_ImplWin32_Init(m_hwnd))
    {
        OutputDebugStringA("cairn: ImGui_ImplWin32_Init failed\n");
        m_imguiFailed = true;
        return;
    }

    if (!ImGui_ImplOpenGL3_Init(nullptr))
    {
        OutputDebugStringA("cairn: ImGui_ImplOpenGL3_Init failed\n");
        m_imguiFailed = true;
        return;
    }

    // Hook WndProc for input capture
    setupWndProcHook(m_hwnd);

    m_imguiInit = true;
    OutputDebugStringA("cairn: ImGui initialized successfully\n");
}

void Hooks::setupWndProcHook(HWND hwnd)
{
    m_origWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)&hkWndProc);
    OutputDebugStringA("cairn: WndProc hooked\n");
}

// ════════════════════════════════════════════════════════════
//  ImGui render — called every frame if ImGui is initialized
// ════════════════════════════════════════════════════════════
void Hooks::renderImGui()
{
    if (!m_imguiInit) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (onRender)
        onRender();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ════════════════════════════════════════════════════════════
//  wglSwapBuffers hook — runs on the RENDER THREAD every frame
// ════════════════════════════════════════════════════════════
BOOL WINAPI Hooks::hkSwapBuffers(HDC hdc)
{
    auto& self = instance();

    // ── First-frame init ─────────────────────────────────────
    if (!self.m_imguiInit && !self.m_imguiFailed)
    {
        if (!self.m_firstFrameDone)
        {
            self.m_firstFrameDone = true;
            OutputDebugStringA("cairn: first frame received\n");

            // Wire callbacks on the render thread (GL context is current here)
            Client::instance().onFirstFrame();
        }

        // Try to init ImGui. If the GL context isn't ready yet,
        // we'll retry on the next frame (up to 300 retries).
        self.initImGui(hdc);
        self.m_imguiRetries++;

        if (self.m_imguiRetries > 300)
        {
            OutputDebugStringA("cairn: initImGui gave up after 300 retries\n");
            self.m_imguiFailed = true;
        }

        // If we just succeeded, log it and let renderImGui run below
        if (self.m_imguiInit)
        {
            OutputDebugStringA("cairn: ImGui ready, HUD will now render\n");
        }
    }

    // ── Render HUD / menu ────────────────────────────────────
    self.renderImGui();

    // ── Frame-counting tick fallback ──────────────────────────
    if (self.onTickFallback)
        self.onTickFallback();

    // ── Call the original wglSwapBuffers ──────────────────────
    typedef BOOL(WINAPI* FnSwap)(HDC);
    return ((FnSwap)self.m_origSwap)(hdc);
}

// ════════════════════════════════════════════════════════════
//  WndProc hook — routes input to ImGui when menu is open
// ════════════════════════════════════════════════════════════
LRESULT WINAPI Hooks::hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto& self = instance();

    if (self.m_imguiInit)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        if (self.m_menuVisible)
        {
            switch (msg)
            {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MOUSEWHEEL:
            case WM_MOUSEMOVE:
            case WM_CHAR:
            case WM_INPUT:
                return true;
            }
        }
    }

    if (self.m_origWndProc)
        return CallWindowProc(self.m_origWndProc, hWnd, msg, wParam, lParam);

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
