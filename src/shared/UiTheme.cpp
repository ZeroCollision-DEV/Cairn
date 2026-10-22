#include "UiTheme.h"

#include <Windows.h>
#include <cmath>
#include <string>

namespace UI
{
    float computeScale(const ImVec2& display)
    {
        float s = display.y / 1080.0f;
        if (s < 0.85f) s = 0.85f;
        if (s > 1.75f) s = 1.75f;
        return s;
    }

    void loadFonts(ImGuiIO& io, float px)
    {
        io.Fonts->Clear();

        char win[MAX_PATH] = {};
        UINT n = GetWindowsDirectoryA(win, MAX_PATH);
        std::string dir = (n > 0 && n < MAX_PATH) ? std::string(win) + "\\Fonts\\"
                                                  : std::string("C:\\Windows\\Fonts\\");

        auto load = [&](const char* file, float size) -> ImFont*
        {
            ImFontConfig cfg;
            cfg.OversampleH = 2;
            cfg.OversampleV = 1;
            return io.Fonts->AddFontFromFileTTF((dir + file).c_str(), size, &cfg);
        };

        ImFont* regular = load("segoeui.ttf", px);
        if (!regular)
        {
            // Segoe UI missing: keep ImGui's built-in font for everything.
            regular = io.Fonts->AddFontDefault();
            g_fonts.regular = g_fonts.semibold = g_fonts.title = g_fonts.caption = regular;
            io.FontDefault = regular;
            return;
        }

        ImFont* semibold = load("seguisb.ttf", px);
        if (!semibold) semibold = load("segoeuib.ttf", px);
        if (!semibold) semibold = regular;

        ImFont* title = load("seguisb.ttf", px * 1.75f);
        if (!title) title = load("segoeuib.ttf", px * 1.75f);
        if (!title) title = semibold;

        ImFont* caption = load("segoeui.ttf", px * 0.80f);
        if (!caption) caption = regular;

        g_fonts.regular  = regular;
        g_fonts.semibold = semibold;
        g_fonts.title    = title;
        g_fonts.caption    = caption;
        io.FontDefault   = regular;
    }

    void applyStyle(float sc)
    {
        ImGuiStyle& st = ImGui::GetStyle();

        st.WindowPadding    = ImVec2(12.0f * sc, 10.0f * sc);
        st.FramePadding     = ImVec2(10.0f * sc, 6.0f * sc);
        st.ItemSpacing      = ImVec2(8.0f * sc, 8.0f * sc);
        st.ItemInnerSpacing = ImVec2(6.0f * sc, 4.0f * sc);
        st.IndentSpacing    = 16.0f * sc;
        st.ScrollbarSize    = 8.0f * sc;
        st.GrabMinSize      = 10.0f * sc;

        st.WindowRounding    = 12.0f * sc;
        st.ChildRounding     = 8.0f * sc;
        st.FrameRounding     = 8.0f * sc;
        st.PopupRounding     = 8.0f * sc;
        st.ScrollbarRounding = 6.0f * sc;
        st.GrabRounding      = 6.0f * sc;

        st.WindowBorderSize = 1.0f;
        st.ChildBorderSize  = 0.0f;
        st.FrameBorderSize  = 0.0f;
        st.PopupBorderSize  = 1.0f;

        st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        st.DisabledAlpha    = 0.45f;

        ImVec4* c = st.Colors;
        const ImVec4 clear(0, 0, 0, 0);

        c[ImGuiCol_Text]           = Col::Text;
        c[ImGuiCol_TextDisabled]   = Col::TextDim;
        c[ImGuiCol_WindowBg]       = Col::Bg;
        c[ImGuiCol_ChildBg]        = clear;
        c[ImGuiCol_PopupBg]        = Col::SurfaceRaised;
        c[ImGuiCol_Border]         = Col::Border;
        c[ImGuiCol_BorderShadow]   = clear;

        c[ImGuiCol_FrameBg]        = Col::Surface;
        c[ImGuiCol_FrameBgHovered] = Col::SurfaceHover;
        c[ImGuiCol_FrameBgActive]  = Col::SurfaceRaised;

        c[ImGuiCol_TitleBg]          = Col::Sidebar;
        c[ImGuiCol_TitleBgActive]    = Col::Sidebar;
        c[ImGuiCol_TitleBgCollapsed] = Col::Sidebar;

        c[ImGuiCol_CheckMark]        = Col::Accent;
        c[ImGuiCol_SliderGrab]       = Col::Accent;
        c[ImGuiCol_SliderGrabActive] = Col::AccentHover;

        c[ImGuiCol_Button]         = Col::SurfaceRaised;
        c[ImGuiCol_ButtonHovered]  = Col::SurfaceHover;
        c[ImGuiCol_ButtonActive]   = Col::BorderStrong;

        c[ImGuiCol_Header]         = WithAlpha(Col::Accent, 0.14f);
        c[ImGuiCol_HeaderHovered]  = WithAlpha(Col::Accent, 0.24f);
        c[ImGuiCol_HeaderActive]   = WithAlpha(Col::Accent, 0.34f);

        c[ImGuiCol_Separator]        = Col::Border;
        c[ImGuiCol_SeparatorHovered] = WithAlpha(Col::Accent, 0.5f);
        c[ImGuiCol_SeparatorActive]  = Col::Accent;

        c[ImGuiCol_ResizeGrip]        = clear;
        c[ImGuiCol_ResizeGripHovered] = WithAlpha(Col::Accent, 0.35f);
        c[ImGuiCol_ResizeGripActive]  = WithAlpha(Col::Accent, 0.6f);

        c[ImGuiCol_ScrollbarBg]          = clear;
        c[ImGuiCol_ScrollbarGrab]        = Col::BorderStrong;
        c[ImGuiCol_ScrollbarGrabHovered] = Col::TextFaint;
        c[ImGuiCol_ScrollbarGrabActive]  = Col::TextDim;

        c[ImGuiCol_TextSelectedBg]   = WithAlpha(Col::Accent, 0.35f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
    }

    float animate(ImGuiID id, float target, float speed)
    {
        ImGuiStorage* store = ImGui::GetStateStorage();
        float cur = store->GetFloat(id, target);
        float dt = ImGui::GetIO().DeltaTime;
        cur += (target - cur) * (1.0f - std::exp(-speed * dt));
        if (std::fabs(target - cur) < 0.002f) cur = target;
        store->SetFloat(id, cur);
        return cur;
    }
}
