#include "Widgets.h"
#include "imgui_internal.h"   // ImGui::FindRenderedTextEnd

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace UI
{
    namespace
    {
        // Distinct animation channels derived from a widget's item id.
        constexpr ImGuiID kChanHover = 0x9E3779B1u;
        constexpr ImGuiID kChanState = 0x85EBCA6Bu;
        constexpr ImGuiID kChanPress = 0xC2B2AE35u;
        constexpr float   kPi        = 3.14159265358979323846f;

        inline ImGuiID chan(ImGuiID id, ImGuiID c) { return id * 31u + c; }

        inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    }

    // ── Toggle ──────────────────────────────────────────────────
    bool Toggle(const char* strId, bool* v, ImVec2 size)
    {
        if (size.x <= 0.0f) size = ImVec2(S(38.0f), S(20.0f));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        bool pressed = ImGui::InvisibleButton(strId, size);
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();

        bool changed = false;
        if (pressed)
        {
            *v = !*v;
            changed = true;
            playSfx(*v ? Sfx::ToggleOn : Sfx::ToggleOff);
        }

        const float t   = animate(chan(id, kChanState), *v ? 1.0f : 0.0f, 18.0f);
        const float hov = animate(chan(id, kChanHover), hovered ? 1.0f : 0.0f, 14.0f);

        const ImVec4 off = Mix(Col::SurfaceRaised, Col::BorderStrong, hov * 0.55f);
        const ImVec4 bg  = Mix(off, hovered ? Col::AccentHover : Col::Accent, t);
        const float rad  = size.y * 0.5f;
        const ImVec2 p1(p.x + size.x, p.y + size.y);

        dl->AddRectFilled(p, p1, C(bg), rad);
        dl->AddRect(p, p1, C(Mix(Col::BorderStrong, Col::Accent, t)), rad, 0, 1.0f);

        const float knobR = rad - size.y * 0.20f;
        const float kx = p.x + rad + t * (size.x - size.y);
        const ImVec4 knob = Mix(Col::TextDim, Col::AccentText, t);
        dl->AddCircleFilled(ImVec2(kx, p.y + rad), knobR, C(knob), 20);

        return changed;
    }

    bool ToggleRow(const char* label, bool* v)
    {
        ImGui::PushID(label);
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float tw = S(38.0f), th = S(20.0f);
        const float rowH = th + S(4.0f);

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));

        ImGui::SetCursorScreenPos(ImVec2(p.x + w - tw, p.y + (rowH - th) * 0.5f));
        const bool changed = Toggle("##toggle", v, ImVec2(tw, th));

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
        ImGui::Dummy(ImVec2(w, S(2.0f)));
        ImGui::PopID();
        return changed;
    }

    // ── Button ──────────────────────────────────────────────────
    bool Button(const char* label, ImVec2 size, BtnStyle style)
    {
        ImGui::PushID(label);
        const char* textEnd = ImGui::FindRenderedTextEnd(label);

        ImGui::PushFont(g_fonts.semibold);
        const ImVec2 ts = ImGui::CalcTextSize(label, textEnd);

        if (size.x <= 0.0f) size.x = ts.x + S(28.0f);
        if (size.y <= 0.0f) size.y = S(32.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##btn", size);
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float hov = animate(chan(id, kChanHover), hovered ? 1.0f : 0.0f, 16.0f);

        ImVec4 bg, border, text;
        switch (style)
        {
        case BtnStyle::Primary:
            bg = Mix(Col::Accent, Col::AccentHover, hov);
            border = WithAlpha(Col::Accent, 0.0f);
            text = Col::AccentText;
            break;
        case BtnStyle::Danger:
            bg = Mix(WithAlpha(Col::Danger, 0.16f), WithAlpha(Col::Danger, 0.30f), hov);
            border = WithAlpha(Col::Danger, 0.45f);
            text = Col::Danger;
            break;
        case BtnStyle::Ghost:
            bg = WithAlpha(Col::SurfaceHover, hov);
            border = WithAlpha(Col::Border, 0.0f);
            text = Mix(Col::TextDim, Col::Text, hov);
            break;
        case BtnStyle::Secondary:
        default:
            bg = Mix(Col::SurfaceRaised, Col::BorderStrong, hov * 0.6f);
            border = Mix(Col::Border, Col::BorderStrong, hov);
            text = Col::Text;
            break;
        }
        if (active) bg = Mix(bg, ImVec4(0, 0, 0, bg.w), 0.15f);

        const ImVec2 p1(p.x + size.x, p.y + size.y);
        const float rad = S(8.0f);
        dl->AddRectFilled(p, p1, C(bg), rad);
        if (border.w > 0.0f) dl->AddRect(p, p1, C(border), rad, 0, 1.0f);

        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
            C(text), label, textEnd);

        ImGui::PopFont();
        ImGui::PopID();

        if (pressed) playSfx(Sfx::Click);
        return pressed;
    }

    // ── Chip ────────────────────────────────────────────────────
    bool Chip(const char* label, bool selected)
    {
        ImGui::PushID(label);
        const char* textEnd = ImGui::FindRenderedTextEnd(label);

        ImGui::PushFont(g_fonts.semibold);
        const ImVec2 ts = ImGui::CalcTextSize(label, textEnd);
        const ImVec2 size(ts.x + S(28.0f), S(30.0f));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##chip", size);
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();
        const float hov = animate(chan(id, kChanHover), hovered ? 1.0f : 0.0f, 16.0f);
        const float sel = animate(chan(id, kChanState), selected ? 1.0f : 0.0f, 16.0f);

        const ImVec4 offBg = Mix(Col::Surface, Col::SurfaceHover, hov);
        const ImVec4 bg = Mix(offBg, Col::Accent, sel);
        const ImVec4 border = Mix(Mix(Col::Border, Col::BorderStrong, hov), Col::Accent, sel);
        const ImVec4 text = Mix(Mix(Col::TextDim, Col::Text, hov), Col::AccentText, sel);

        const ImVec2 p1(p.x + size.x, p.y + size.y);
        dl->AddRectFilled(p, p1, C(bg), size.y * 0.5f);
        dl->AddRect(p, p1, C(border), size.y * 0.5f, 0, 1.0f);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
            C(text), label, textEnd);

        ImGui::PopFont();
        ImGui::PopID();

        if (pressed) playSfx(Sfx::Tab);
        return pressed;
    }

    // ── Sidebar nav item ────────────────────────────────────────
    bool NavItem(const char* label, bool selected, const char* trailing)
    {
        ImGui::PushID(label);
        const char* textEnd = ImGui::FindRenderedTextEnd(label);
        const ImVec2 size(ImGui::GetContentRegionAvail().x, S(34.0f));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##nav", size);
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();
        const float hov = animate(chan(id, kChanHover), hovered ? 1.0f : 0.0f, 16.0f);
        const float sel = animate(chan(id, kChanState), selected ? 1.0f : 0.0f, 16.0f);

        const ImVec4 bgHover = WithAlpha(Col::SurfaceHover, 0.75f * hov);
        const ImVec4 bgSel = WithAlpha(Col::Accent, 0.13f * sel);
        const ImVec2 p1(p.x + size.x, p.y + size.y);
        dl->AddRectFilled(p, p1, C(bgHover), S(8.0f));
        dl->AddRectFilled(p, p1, C(bgSel), S(8.0f));

        const ImVec4 text = Mix(Mix(Col::TextDim, Col::Text, hov), Col::Accent, sel);
        const ImVec2 ts = ImGui::CalcTextSize(label, textEnd);
        const float padX = S(12.0f);

        ImGui::PushFont(selected ? g_fonts.semibold : g_fonts.regular);
        dl->AddText(ImVec2(p.x + padX, p.y + (size.y - ts.y) * 0.5f), C(text), label, textEnd);
        ImGui::PopFont();

        if (trailing && trailing[0])
        {
            ImGui::PushFont(g_fonts.caption);
            const ImVec2 trs = ImGui::CalcTextSize(trailing);
            dl->AddText(ImVec2(p1.x - padX - trs.x, p.y + (size.y - trs.y) * 0.5f),
                C(Mix(Col::TextFaint, Col::TextDim, sel)), trailing);
            ImGui::PopFont();
        }

        ImGui::PopID();
        if (pressed) playSfx(Sfx::Tab);
        return pressed;
    }

    // ── Icon buttons ────────────────────────────────────────────
    bool StarButton(const char* strId, bool filled, float size)
    {
        if (size <= 0.0f) size = S(28.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(strId, ImVec2(size, size));
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();
        const float hov = animate(chan(id, kChanHover), hovered ? 1.0f : 0.0f, 16.0f);
        const float fill = animate(chan(id, kChanState), filled ? 1.0f : 0.0f, 16.0f);

        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
            C(WithAlpha(Col::SurfaceHover, hov)), S(7.0f));

        const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f + size * 0.02f);
        const float ro = size * 0.31f;
        const float ri = ro * 0.46f;
        ImVec2 pts[10];
        for (int i = 0; i < 10; i++)
        {
            const float ang = -kPi * 0.5f + i * (kPi / 5.0f);
            const float r = (i % 2 == 0) ? ro : ri;
            pts[i] = ImVec2(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r);
        }

        const ImVec4 outline = Mix(Mix(Col::TextFaint, Col::TextDim, hov), Col::Accent2, fill);
        if (fill > 0.01f)
        {
            const ImU32 fc = C(Col::Accent2, fill);
            for (int i = 0; i < 10; i++)
                dl->AddTriangleFilled(c, pts[i], pts[(i + 1) % 10], fc);
        }
        dl->AddPolyline(pts, 10, C(outline), ImDrawFlags_Closed, 1.4f);

        if (pressed) playSfx(Sfx::Click);
        return pressed;
    }

    bool CloseButton(const char* strId, float size)
    {
        if (size <= 0.0f) size = S(28.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(strId, ImVec2(size, size));
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();
        const float hov = animate(chan(id, kChanHover), hovered ? 1.0f : 0.0f, 16.0f);

        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
            C(WithAlpha(Col::SurfaceHover, hov)), S(7.0f));

        const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
        const float d = size * 0.18f;
        const ImU32 col = C(Mix(Col::TextDim, Col::Text, hov));
        dl->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), col, 1.6f);
        dl->AddLine(ImVec2(c.x - d, c.y + d), ImVec2(c.x + d, c.y - d), col, 1.6f);

        if (pressed) playSfx(Sfx::Click);
        return pressed;
    }

    // ── Sliders ─────────────────────────────────────────────────
    namespace
    {
        bool sliderCore(const char* label, float* v, float lo, float hi, const char* fmt, bool isInt)
        {
            ImGui::PushID(label);
            ImDrawList* dl = ImGui::GetWindowDrawList();

            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float lineH = ImGui::GetTextLineHeight();
            const float knobR = S(7.0f);
            const float trackH = S(5.0f);
            const float areaH = knobR * 2.0f + S(4.0f);
            const float trackTop = p.y + lineH + S(4.0f);

            ImGui::SetCursorScreenPos(ImVec2(p.x, trackTop));
            ImGui::InvisibleButton("##track", ImVec2(w, areaH));
            const ImGuiID id = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            const float x0 = p.x + knobR;
            const float x1 = p.x + w - knobR;
            const float span = std::max(1.0f, x1 - x0);

            bool changed = false;
            if (active && hi > lo)
            {
                const float t = clamp01((ImGui::GetIO().MousePos.x - x0) / span);
                float nv = lo + t * (hi - lo);
                if (isInt) nv = std::round(nv);
                if (nv != *v) { *v = nv; changed = true; }
            }

            const float cur = std::min(hi, std::max(lo, *v));
            const float t = (hi > lo) ? (cur - lo) / (hi - lo) : 0.0f;
            const float cy = trackTop + areaH * 0.5f;
            const float kx = x0 + t * span;
            const float grow = animate(chan(id, kChanHover), (hovered || active) ? 1.0f : 0.0f, 16.0f);

            // text row
            char val[48];
            if (isInt) std::snprintf(val, sizeof(val), "%d", static_cast<int>(std::lround(cur)));
            else       std::snprintf(val, sizeof(val), fmt, cur);

            dl->AddText(p, C(Col::Text), label, ImGui::FindRenderedTextEnd(label));
            const ImVec2 vs = ImGui::CalcTextSize(val);
            dl->AddText(ImVec2(p.x + w - vs.x, p.y), C(Col::Accent), val);

            // track
            dl->AddRectFilled(ImVec2(x0, cy - trackH * 0.5f), ImVec2(x1, cy + trackH * 0.5f),
                C(Col::SurfaceRaised), trackH * 0.5f);
            dl->AddRect(ImVec2(x0, cy - trackH * 0.5f), ImVec2(x1, cy + trackH * 0.5f),
                C(Col::Border), trackH * 0.5f, 0, 1.0f);
            if (kx > x0)
                dl->AddRectFilled(ImVec2(x0, cy - trackH * 0.5f), ImVec2(kx, cy + trackH * 0.5f),
                    C(Col::Accent), trackH * 0.5f);

            const float kr = knobR * (1.0f + 0.12f * grow);
            dl->AddCircleFilled(ImVec2(kx, cy), kr, C(Col::Text), 24);
            dl->AddCircle(ImVec2(kx, cy), kr, C(Col::Accent), 24, 2.0f);

            ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
            ImGui::PopID();
            return changed;
        }
    }

    bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt)
    {
        return sliderCore(label, v, lo, hi, fmt, false);
    }

    bool SliderInt(const char* label, int* v, int lo, int hi)
    {
        float f = static_cast<float>(*v);
        const bool changed = sliderCore(label, &f, static_cast<float>(lo), static_cast<float>(hi), "%d", true);
        if (changed) *v = static_cast<int>(std::lround(f));
        return changed;
    }

    // ── Text fields ─────────────────────────────────────────────
    namespace
    {
        bool fieldCore(const char* id, char* buf, size_t n, const char* hint, float width,
                       bool searchIcon, bool password)
        {
            const float fh = S(36.0f);
            const float padL = searchIcon ? S(36.0f) : S(12.0f);
            const float padY = std::max(0.0f, (fh - ImGui::GetFontSize()) * 0.5f);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padL, padY));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Col::Surface);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Col::SurfaceHover);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Col::SurfaceHover);
            ImGui::PushStyleColor(ImGuiCol_Border, Col::Border);

            ImGui::SetNextItemWidth(width > 0.0f ? width : -FLT_MIN);
            const ImGuiInputTextFlags flags = password ? ImGuiInputTextFlags_Password : 0;
            const bool changed = ImGui::InputTextWithHint(id, hint, buf, n, flags);

            const ImVec2 rmin = ImGui::GetItemRectMin();
            const ImVec2 rmax = ImGui::GetItemRectMax();
            const bool active = ImGui::IsItemActive();

            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(3);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (active)
                dl->AddRect(rmin, rmax, C(Col::Accent, 0.75f), S(8.0f), 0, 1.0f);

            if (searchIcon)
            {
                const ImVec2 c(rmin.x + S(18.0f), (rmin.y + rmax.y) * 0.5f - S(1.0f));
                const float r = S(5.0f);
                const ImU32 col = C(active ? Col::Accent : Col::TextDim);
                dl->AddCircle(c, r, col, 16, 1.6f);
                dl->AddLine(ImVec2(c.x + r * 0.72f, c.y + r * 0.72f),
                            ImVec2(c.x + r * 1.55f, c.y + r * 1.55f), col, 1.6f);
            }
            return changed;
        }
    }

    bool SearchBox(const char* id, char* buf, size_t n, const char* hint, float width)
    {
        return fieldCore(id, buf, n, hint, width, true, false);
    }

    bool TextField(const char* id, char* buf, size_t n, const char* hint, float width, bool password)
    {
        return fieldCore(id, buf, n, hint, width, false, password);
    }

    // ── Small pieces ────────────────────────────────────────────
    void SectionLabel(const char* text)
    {
        ImGui::PushFont(g_fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextFaint);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void Badge(const char* text, const ImVec4& color)
    {
        ImGui::PushFont(g_fonts.caption);
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 size(ts.x + S(14.0f), ts.y + S(6.0f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), C(WithAlpha(color, 0.16f)), size.y * 0.5f);
        dl->AddText(ImVec2(p.x + S(7.0f), p.y + S(3.0f)), C(color), text);
        ImGui::Dummy(size);
        ImGui::PopFont();
    }

    void StatusDot(const ImVec4& color, float radius)
    {
        if (radius <= 0.0f) radius = S(4.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(p.x + radius, p.y + h * 0.5f), radius, C(color), 16);
        ImGui::Dummy(ImVec2(radius * 2.0f, h));
    }

    void Tooltip(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", text);
    }

    void Spinner(float radius, const ImVec4& color)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 c(p.x + radius, p.y + radius);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float t = static_cast<float>(ImGui::GetTime());
        const float start = std::fmod(t * 4.5f, kPi * 2.0f);
        const int segs = 24;
        dl->PathClear();
        for (int i = 0; i <= segs; i++)
        {
            const float a = start + (static_cast<float>(i) / segs) * kPi * 1.45f;
            dl->PathLineTo(ImVec2(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius));
        }
        dl->PathStroke(C(color), 0, std::max(1.5f, radius * 0.22f));
        ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
    }

    void ProgressBar(float fraction, ImVec2 size)
    {
        if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
        if (size.y <= 0.0f) size.y = S(6.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p.x + size.x, p.y + size.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rad = size.y * 0.5f;

        dl->AddRectFilled(p, p1, C(Col::SurfaceRaised), rad);
        if (fraction >= 0.0f)
        {
            const float f = clamp01(fraction);
            if (f > 0.0f)
                dl->AddRectFilled(p, ImVec2(p.x + size.x * f, p1.y), C(Col::Accent), rad);
        }
        else
        {
            const float t = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.9f, 1.0f);
            const float segW = size.x * 0.28f;
            const float x = p.x + (size.x + segW) * t - segW;
            const float xa = std::max(p.x, x);
            const float xb = std::min(p1.x, x + segW);
            if (xb > xa)
                dl->AddRectFilled(ImVec2(xa, p.y), ImVec2(xb, p1.y), C(Col::Accent), rad);
        }
        ImGui::Dummy(size);
    }

    std::string PrettyName(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 4);
        for (size_t i = 0; i < s.size(); i++)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (i > 0)
            {
                const unsigned char p = static_cast<unsigned char>(s[i - 1]);
                if (std::isupper(c) && (std::islower(p) || std::isdigit(p))) out.push_back(' ');
            }
            out.push_back(static_cast<char>(c));
        }
        return out;
    }
}
