#pragma once
// Reusable, animated widgets in the shared Cairn visual language.
// All widgets are drawn with the window draw list using the UI::Col palette and
// scale with UI::S(). They raise UI::Sfx events (see Sfx.h) on interaction.

#include "imgui.h"
#include "UiTheme.h"
#include <string>

namespace UI
{
    enum class BtnStyle { Primary, Secondary, Ghost, Danger };

    // Pill switch. Returns true on the frame it flips; *v holds the new value.
    bool Toggle(const char* id, bool* v, ImVec2 size = ImVec2(0, 0));

    // Label on the left, switch on the right, full row width.
    bool ToggleRow(const char* label, bool* v);

    bool Button(const char* label, ImVec2 size = ImVec2(0, 0), BtnStyle style = BtnStyle::Secondary);

    // Filter chip. Selected chips are filled with the accent colour.
    bool Chip(const char* label, bool selected);

    // Full-width sidebar entry with an optional right-aligned trailing label.
    bool NavItem(const char* label, bool selected, const char* trailing = nullptr);

    bool StarButton(const char* id, bool filled, float size = 0.0f);
    bool CloseButton(const char* id, float size = 0.0f);

    // Label + value above a custom track. Full available width.
    bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f");
    bool SliderInt(const char* label, int* v, int lo, int hi);

    // Rounded text field with a magnifier icon. Returns true when the text changed.
    bool SearchBox(const char* id, char* buf, size_t bufSize, const char* hint, float width);

    // Rounded single-line text field (no icon).
    bool TextField(const char* id, char* buf, size_t bufSize, const char* hint, float width, bool password = false);

    void SectionLabel(const char* text);                         // small caps-style dim label
    void Badge(const char* text, const ImVec4& color);           // inline pill
    void StatusDot(const ImVec4& color, float radius = 0.0f);    // inline dot (use SameLine after)
    void Tooltip(const char* text);                              // hover tooltip for the last item
    void Spinner(float radius, const ImVec4& color);
    void ProgressBar(float fraction, ImVec2 size);               // fraction < 0 => indeterminate

    // "ThroughWalls" -> "Through Walls"
    std::string PrettyName(const std::string& s);
}
