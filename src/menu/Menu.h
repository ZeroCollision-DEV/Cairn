#pragma once
#include "imgui.h"
#include <string>
#include <vector>
#include "../modules/Module.h"

// The in-game menu. Pure view over ModuleManager / Settings / Client state:
// every count and status string it shows is derived from live data each frame,
// nothing is cached or hardcoded.
class Menu
{
public:
    static Menu& instance();

    // Call every frame from the render thread (cheap when hidden).
    void render();

    bool isVisibleOrFading() const { return m_alpha > 0.0f; }

private:
    Menu() = default;

    enum class Page   { Modules, Settings };
    enum class Filter { All, Enabled, Favorites };

    Page    m_page     = Page::Modules;
    Filter  m_filter   = Filter::All;
    int     m_category = -1;              // -1 = all categories, else (int)Category
    char    m_search[96] = {};
    Setting* m_bindTarget = nullptr;      // keybind setting currently waiting for a key press
    Module* m_selected = nullptr;         // module shown in the details panel
    float   m_alpha    = 0.0f;            // open/close fade
    float   m_appliedScale = 0.0f;

    void renderSidebar(float width, float height);
    void renderProfile(float width);
    void renderTopBar(float width);
    void renderModulesPage();
    void renderSettingsPage();
    void renderCard(Module* m, ImVec2 pos, ImVec2 size);
    void renderDetails(bool fullWidth);
    void renderStatusBar(float width, float height);

    bool matchesSearch(Module* m) const;
};
