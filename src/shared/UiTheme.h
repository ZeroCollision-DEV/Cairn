#pragma once
// Shared visual language for the Cairn menu and the Cairn Injector.
// One palette, one style function, one font loader -- both apps call these.

#include "imgui.h"
#include "Sfx.h"

namespace UI
{
    // ── Palette: graphite surfaces, one teal accent, one lantern-amber secondary ──
    // Tokens (keep in sync with tools/gen_assets.py and docs/DESIGN.md):
    //   Bg / Sidebar / Surface / SurfaceHover / SurfaceRaised   -> backgrounds and elevation
    //   Border / BorderStrong                                    -> separators and outlines
    //   Text / TextDim / TextFaint / Disabled                    -> text hierarchy
    //   Accent / AccentHover / Active / AccentText / Selected    -> primary accent (teal) states
    //   Accent2 / Accent2Hover                                   -> secondary accent (amber): favourites, notes
    //   Success / Warning / Danger                               -> status colours
    namespace Col
    {
        inline const ImVec4 Bg           (0.063f, 0.078f, 0.082f, 1.0f);   // #101415
        inline const ImVec4 Sidebar      (0.047f, 0.063f, 0.067f, 1.0f);   // #0C1011
        inline const ImVec4 Surface      (0.086f, 0.110f, 0.118f, 1.0f);   // #161C1E
        inline const ImVec4 SurfaceHover (0.106f, 0.137f, 0.149f, 1.0f);   // #1B2326
        inline const ImVec4 SurfaceRaised(0.129f, 0.169f, 0.180f, 1.0f);   // #212B2E
        inline const ImVec4 Border       (0.149f, 0.188f, 0.200f, 1.0f);   // #263033
        inline const ImVec4 BorderStrong (0.208f, 0.263f, 0.278f, 1.0f);   // #354347

        inline const ImVec4 Text         (0.906f, 0.933f, 0.925f, 1.0f);   // #E7EEEC
        inline const ImVec4 TextDim      (0.561f, 0.627f, 0.616f, 1.0f);   // #8FA09D
        inline const ImVec4 TextFaint    (0.369f, 0.435f, 0.424f, 1.0f);   // #5E6F6C
        inline const ImVec4 Disabled     (0.294f, 0.345f, 0.337f, 1.0f);   // #4B5856

        inline const ImVec4 Accent       (0.239f, 0.718f, 0.639f, 1.0f);   // #3DB7A3 teal
        inline const ImVec4 AccentHover  (0.361f, 0.796f, 0.722f, 1.0f);   // #5CCBB8
        inline const ImVec4 Active       (0.180f, 0.592f, 0.525f, 1.0f);   // #2E9786 pressed
        inline const ImVec4 AccentText   (0.024f, 0.125f, 0.106f, 1.0f);   // #06201B on-accent text
        inline const ImVec4 Selected     (0.239f, 0.718f, 0.639f, 0.16f);  // teal wash for selected rows/cards

        inline const ImVec4 Accent2      (0.910f, 0.706f, 0.353f, 1.0f);   // #E8B45A lantern amber
        inline const ImVec4 Accent2Hover (0.941f, 0.773f, 0.478f, 1.0f);   // #F0C57A

        inline const ImVec4 Success      (0.482f, 0.788f, 0.435f, 1.0f);   // #7BC96F
        inline const ImVec4 Warning      (0.890f, 0.627f, 0.294f, 1.0f);   // #E3A04B
        inline const ImVec4 Danger       (0.898f, 0.349f, 0.310f, 1.0f);   // #E5594F
    }

    inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    inline ImVec4 WithAlpha(const ImVec4& c, float a)
    {
        return ImVec4(c.x, c.y, c.z, a);
    }

    // Packed colour for draw-list calls. Multiplies in the current style alpha so
    // custom-drawn widgets fade with their window and dim inside BeginDisabled().
    inline ImU32 C(const ImVec4& c, float alphaMul = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(c.x, c.y, c.z, c.w * alphaMul * ImGui::GetStyle().Alpha));
    }

    // ── Scale ───────────────────────────────────────────────────
    inline float g_scale = 1.0f;
    inline float S(float v) { return v * g_scale; }
    float computeScale(const ImVec2& displaySize);

    // ── Fonts ───────────────────────────────────────────────────
    struct Fonts
    {
        ImFont* regular  = nullptr;
        ImFont* semibold = nullptr;
        ImFont* title    = nullptr;
        ImFont* caption  = nullptr;   // NOT "small": rpcndr.h (via d3d11/dxgi) does #define small char
    };
    inline Fonts g_fonts;

    // Loads Segoe UI from the Windows fonts folder (falls back to ImGui's default
    // font). Call before the backend creates its font texture.
    void loadFonts(ImGuiIO& io, float basePx = 16.0f);

    // Applies spacing/rounding/colours to the current ImGui context.
    void applyStyle(float scale);

    // Exponential ease of a per-widget float towards `target` (per-window storage).
    float animate(ImGuiID id, float target, float speed = 14.0f);
}
