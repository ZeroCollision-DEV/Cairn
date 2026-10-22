#pragma once
#include <Windows.h>
#include <functional>
#include "imgui.h"

class Hooks
{
public:
    static Hooks& instance();

    // Set up the wglSwapBuffers hook.
    // Must be called before the first frame to catch it.
    bool init();
    void shutdown();

    bool isActive() const { return m_hookOK; }

    // Callbacks (wired by Client)
    std::function<void()> onRender;
    std::function<void()> onTickFallback;

    // Menu visibility
    bool menuVisible() const { return m_menuVisible; }
    void setMenuVisible(bool v) { m_menuVisible = v; }

private:
    Hooks() = default;

    HMODULE m_opengl32   = nullptr;
    void*   m_origSwap   = nullptr;
    bool    m_hookOK     = false;

    // ImGui state
    bool    m_imguiInit      = false;
    bool    m_imguiFailed    = false;
    bool    m_firstFrameDone = false;
    int     m_imguiRetries   = 0;
    HWND    m_hwnd           = nullptr;
    WNDPROC m_origWndProc    = nullptr;
    bool    m_menuVisible    = false;

    void initImGui(HDC hdc);
    void renderImGui();
    void setupWndProcHook(HWND hwnd);

    static BOOL   WINAPI hkSwapBuffers(HDC hdc);
    static LRESULT WINAPI hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
