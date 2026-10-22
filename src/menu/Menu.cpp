#include "Menu.h"
#include "Theme.h"

#include "../shared/Brand.h"
#include "../shared/UiTheme.h"
#include "../shared/Widgets.h"

#include "../core/Client.h"
#include "../core/Hooks.h"
#include "../core/JVM.h"
#include "../core/Profile.h"
#include "../core/Settings.h"
#include "../core/Sound.h"
#include "../modules/Module.h"
#include "../modules/ModuleManager.h"
#include "../utils/Logger.h"

#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace UI;

namespace
{
    // Human-readable name of a virtual-key code for the keybind button.
    std::string keyName(int vk)
    {
        if (vk <= 0) return "Unbound";
        LONG lp = static_cast<LONG>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC) << 16);
        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_INSERT ||
            vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT)
            lp |= (1 << 24);   // extended key
        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lp, buf, 64) > 0)
        {
            char out[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
            return out;
        }
        return "Key " + std::to_string(vk);
    }

    constexpr float kPi = 3.14159265358979323846f;

    std::string lowerCopy(const std::string& s)
    {
        std::string o = s;
        std::transform(o.begin(), o.end(), o.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return o;
    }

    // Truncates `s` with "..." so it fits in maxW pixels (current font).
    std::string ellipsize(const std::string& s, float maxW)
    {
        if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
        std::string t = s;
        while (!t.empty() && ImGui::CalcTextSize((t + "...").c_str()).x > maxW)
            t.pop_back();
        return t + "...";
    }

    std::string plural(size_t n, const char* one, const char* many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    // Rounded surface container that grows with its content.
    void beginPanel(const char* id, float maxWidth)
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float w = (maxWidth > 0.0f && maxWidth < avail) ? maxWidth : avail;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16.0f), S(14.0f)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::Surface);
        ImGui::PushStyleColor(ImGuiCol_Border, Col::Border);
        ImGui::BeginChild(id, ImVec2(w, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void endPanel() { ImGui::EndChild(); }

    void infoRow(const char* label, const std::string& value)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine(S(150.0f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
    }

    size_t countEnabled()
    {
        size_t n = 0;
        for (const auto& m : ModuleManager::instance().all())
            if (m->isEnabled()) n++;
        return n;
    }
}

Menu& Menu::instance()
{
    static Menu inst;
    return inst;
}

// ════════════════════════════════════════════════════════════
//  Frame entry
// ════════════════════════════════════════════════════════════
void Menu::render()
{
    ImGuiIO& io = ImGui::GetIO();

    // Fade in/out. Input capture follows Hooks::menuVisible() immediately; only the
    // drawing lingers while the alpha eases back to zero.
    const bool wantVisible = Hooks::instance().menuVisible();
    const float target = wantVisible ? 1.0f : 0.0f;
    m_alpha += (target - m_alpha) * (1.0f - std::exp(-16.0f * io.DeltaTime));
    if (std::fabs(target - m_alpha) < 0.01f) m_alpha = target;
    if (m_alpha <= 0.0f) return;

    // Resolution-aware scale (fonts are baked once; sizes follow the display height).
    const float sc = computeScale(io.DisplaySize);
    if (std::fabs(sc - m_appliedScale) > 0.01f)
    {
        g_scale = sc;
        io.FontGlobalScale = sc;
        applyStyle(sc);
        m_appliedScale = sc;
    }

    Settings::saveIfDirty();

    const ImVec2 disp = io.DisplaySize;
    const ImVec2 minSize(std::min(S(680.0f), disp.x - S(16.0f)),
                         std::min(S(540.0f), disp.y - S(16.0f)));
    ImGui::SetNextWindowSizeConstraints(minSize, disp);
    ImGui::SetNextWindowSize(ImVec2(std::min(S(1000.0f), disp.x - S(48.0f)),
                                    std::min(S(660.0f),  disp.y - S(48.0f))),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, Col::BorderStrong);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;

    const bool open = ImGui::Begin("Cairn##menu", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);   // keeps Alpha for the contents

    if (open)
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const float sbW = S(212.0f);
        dl->AddRectFilled(wp, ImVec2(wp.x + sbW, wp.y + ws.y), C(Col::Sidebar),
                          S(12.0f), ImDrawFlags_RoundCornersLeft);
        dl->AddLine(ImVec2(wp.x + sbW, wp.y + S(1.0f)),
                    ImVec2(wp.x + sbW, wp.y + ws.y - S(1.0f)), C(Col::Border));

        // ── Sidebar ─────────────────────────────────────────
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10.0f), S(16.0f)));
        ImGui::BeginChild("##sidebar", ImVec2(sbW, ws.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        renderSidebar(sbW, ws.y);
        ImGui::EndChild();

        // ── Main column ─────────────────────────────────────
        ImGui::SetCursorPos(ImVec2(sbW, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##main", ImVec2(ws.x - sbW, ws.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        {
            const ImVec2 mp = ImGui::GetWindowPos();
            const ImVec2 ms = ImGui::GetWindowSize();
            ImDrawList* mdl = ImGui::GetWindowDrawList();

            const float topH = S(56.0f);
            const float statusH = S(30.0f);
            const float bodyH = ms.y - topH - statusH;

            const bool showDetails = (m_page == Page::Modules) && m_selected != nullptr;
            const float detailW = S(300.0f);
            const bool detailsFull = showDetails && (ms.x - detailW < S(380.0f));
            const float listW = showDetails ? (detailsFull ? 0.0f : ms.x - detailW) : ms.x;

            // dividers
            mdl->AddLine(ImVec2(mp.x, mp.y + topH), ImVec2(mp.x + ms.x, mp.y + topH), C(Col::Border));
            mdl->AddLine(ImVec2(mp.x, mp.y + topH + bodyH), ImVec2(mp.x + ms.x, mp.y + topH + bodyH), C(Col::Border));
            if (showDetails && !detailsFull)
                mdl->AddLine(ImVec2(mp.x + listW, mp.y + topH), ImVec2(mp.x + listW, mp.y + topH + bodyH), C(Col::Border));

            // top bar
            ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild("##top", ImVec2(ms.x, topH), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            renderTopBar(ms.x);
            ImGui::EndChild();

            // page content
            if (listW > 0.0f)
            {
                ImGui::SetCursorPos(ImVec2(0.0f, topH));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24.0f), S(20.0f)));
                ImGui::BeginChild("##content", ImVec2(listW, bodyH), ImGuiChildFlags_None);
                ImGui::PopStyleVar();
                if (m_page == Page::Modules) renderModulesPage();
                else                         renderSettingsPage();
                ImGui::EndChild();
            }

            // details panel
            if (showDetails)
            {
                ImGui::SetCursorPos(ImVec2(detailsFull ? 0.0f : listW, topH));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
                ImGui::BeginChild("##details", ImVec2(detailsFull ? ms.x : detailW, bodyH), ImGuiChildFlags_None);
                ImGui::PopStyleVar();
                renderDetails(detailsFull);
                ImGui::EndChild();
            }

            // status bar
            ImGui::SetCursorPos(ImVec2(0.0f, topH + bodyH));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild("##status", ImVec2(ms.x, statusH), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            renderStatusBar(ms.x, statusH);
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar();   // Alpha
}

// ════════════════════════════════════════════════════════════
//  Sidebar: brand, navigation, categories, profile
// ════════════════════════════════════════════════════════════
void Menu::renderSidebar(float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool ready = Client::instance().isReady();

    // Brand
    const ImVec2 c0 = ImGui::GetCursorScreenPos();
    const float logo = S(38.0f);
    Brand::drawLogo(dl, ImVec2(c0.x + S(6.0f) + logo * 0.5f, c0.y + logo * 0.5f), logo);

    ImGui::SetCursorScreenPos(ImVec2(c0.x + S(6.0f) + logo + S(10.0f), c0.y + S(1.0f)));
    ImGui::PushFont(g_fonts.title);
    ImGui::SetWindowFontScale(0.72f);
    ImGui::TextUnformatted(Brand::kClientName);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(c0.x + S(6.0f) + logo + S(10.0f), c0.y + S(23.0f)));
    ImGui::PushFont(g_fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
    ImGui::TextUnformatted(Brand::kTargetGame);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(c0.x, c0.y + logo + S(22.0f)));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.0f), S(3.0f)));

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(12.0f));
    SectionLabel("CLIENT");
    ImGui::Dummy(ImVec2(0.0f, S(2.0f)));

    if (NavItem("Modules", m_page == Page::Modules)) m_page = Page::Modules;
    if (NavItem("Settings", m_page == Page::Settings)) m_page = Page::Settings;

    ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(12.0f));
    SectionLabel("CATEGORIES");
    ImGui::Dummy(ImVec2(0.0f, S(2.0f)));

    // Per-category totals come straight from the registry.
    size_t perCat[6] = {};
    if (ready)
        for (const auto& m : ModuleManager::instance().all())
        {
            const int ci = static_cast<int>(m->category());
            if (ci >= 0 && ci < 6) perCat[ci]++;
        }

    for (int i = 0; i < 6; i++)
    {
        char count[16];
        if (ready) std::snprintf(count, sizeof(count), "%zu", perCat[i]);
        else       std::snprintf(count, sizeof(count), "-");

        const bool sel = (m_page == Page::Modules && m_category == i);
        if (NavItem(categoryName(static_cast<Category>(i)), sel, count))
        {
            m_page = Page::Modules;
            m_category = (m_category == i) ? -1 : i;
        }
    }
    ImGui::PopStyleVar();

    // Profile pinned to the bottom-left
    const float profH = S(62.0f);
    const ImVec2 wp = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(wp.x + S(10.0f), wp.y + height - profH - S(8.0f)),
                ImVec2(wp.x + width - S(10.0f), wp.y + height - profH - S(8.0f)), C(Col::Border));
    ImGui::SetCursorPos(ImVec2(S(10.0f), height - profH - S(4.0f)));
    renderProfile(width - S(20.0f));
}

void Menu::renderProfile(float width)
{
    const Profile::Info info = Profile::info();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float h = S(54.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##profile", ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const float hov = animate(ImGui::GetItemID() * 31u + 7u, hovered ? 1.0f : 0.0f, 14.0f);

    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), C(WithAlpha(Col::SurfaceHover, 0.8f * hov)), S(10.0f));

    const float av = S(34.0f);
    const ImVec2 ac(p.x + S(10.0f) + av * 0.5f, p.y + h * 0.5f);
    const ImVec2 tl(ac.x - av * 0.5f, ac.y - av * 0.5f);
    const ImVec2 br(ac.x + av * 0.5f, ac.y + av * 0.5f);

    const ImTextureID tex = info.hasAvatar ? Profile::avatarTexture() : (ImTextureID)(intptr_t)0;
    if (tex)
    {
        dl->AddImageRounded(tex, tl, br, ImVec2(0, 0), ImVec2(1, 1), C(ImVec4(1, 1, 1, 1)), av * 0.5f);
        dl->AddCircle(ac, av * 0.5f, C(Col::BorderStrong), 32, 1.0f);
    }
    else
    {
        dl->AddCircleFilled(ac, av * 0.5f, C(Col::SurfaceRaised), 32);
        dl->AddCircle(ac, av * 0.5f, C(Col::BorderStrong), 32, 1.0f);
        if (info.configured && !info.displayName.empty())
        {
            char letter[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(info.displayName[0]))), 0 };
            ImGui::PushFont(g_fonts.semibold);
            const ImVec2 ls = ImGui::CalcTextSize(letter);
            dl->AddText(ImVec2(ac.x - ls.x * 0.5f, ac.y - ls.y * 0.5f), C(Col::Text), letter);
            ImGui::PopFont();
        }
        else
        {
            // neutral "no profile" silhouette
            const ImU32 g = C(Col::TextFaint);
            dl->AddCircleFilled(ImVec2(ac.x, ac.y - av * 0.10f), av * 0.13f, g, 16);
            dl->PathArcTo(ImVec2(ac.x, ac.y + av * 0.30f), av * 0.23f, kPi, kPi * 2.0f, 16);
            dl->PathFillConvex(g);
        }
    }

    const float tx = ac.x + av * 0.5f + S(10.0f);
    const float maxW = p.x + width - S(8.0f) - tx;
    ImGui::PushFont(g_fonts.semibold);
    const std::string name = info.configured ? ellipsize(info.displayName, maxW) : "Not connected";
    dl->AddText(ImVec2(tx, ac.y - ImGui::GetTextLineHeight() + S(1.0f)), C(info.configured ? Col::Text : Col::TextDim), name.c_str());
    ImGui::PopFont();

    ImGui::PushFont(g_fonts.caption);
    const std::string sub = info.configured ? info.source : "Optional profile";
    dl->AddText(ImVec2(tx, ac.y + S(1.0f)), C(Col::TextFaint), ellipsize(sub, maxW).c_str());
    ImGui::PopFont();

    if (hovered)
    {
        const std::string tip = info.configured
            ? "Local profile - edit " + Profile::filePath() + " to change the name or avatar."
            : "No profile configured (optional). Edit " + Profile::filePath() + " to add a display name and avatar.";
        ImGui::SetTooltip("%s", tip.c_str());
    }
}

// ════════════════════════════════════════════════════════════
//  Top bar
// ════════════════════════════════════════════════════════════
void Menu::renderTopBar(float width)
{
    const float h = S(56.0f);

    ImGui::PushFont(g_fonts.semibold);
    ImGui::SetCursorPos(ImVec2(S(24.0f), (h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextUnformatted(m_page == Page::Modules ? "Modules" : "Settings");
    ImGui::PopFont();

    const float btn = S(28.0f);
    float x = width - S(16.0f) - btn;
    ImGui::SetCursorPos(ImVec2(x, (h - btn) * 0.5f));
    if (CloseButton("##close", btn))
        Client::instance().toggleMenu();
    Tooltip("Close (Right Shift)");

    if (m_page == Page::Modules)
    {
        const float sw = std::min(S(300.0f), std::max(S(140.0f), width * 0.42f));
        x -= S(10.0f) + sw;
        ImGui::SetCursorPos(ImVec2(x, (h - S(36.0f)) * 0.5f));
        SearchBox("##search", m_search, sizeof(m_search), "Search modules or settings...", sw);
    }
}

// ════════════════════════════════════════════════════════════
//  Modules page
// ════════════════════════════════════════════════════════════
bool Menu::matchesSearch(Module* m) const
{
    if (m_search[0] == '\0') return true;
    const std::string q = lowerCopy(m_search);

    if (lowerCopy(m->name()).find(q) != std::string::npos) return true;
    if (lowerCopy(PrettyName(m->name())).find(q) != std::string::npos) return true;
    if (lowerCopy(m->desc()).find(q) != std::string::npos) return true;
    for (const auto& s : m->settings())
    {
        if (lowerCopy(s.name).find(q) != std::string::npos) return true;
        if (lowerCopy(PrettyName(s.name)).find(q) != std::string::npos) return true;
    }
    return false;
}

void Menu::renderModulesPage()
{
    const bool ready = Client::instance().isReady();
    auto& mgr = ModuleManager::instance();

    // Scope = category selection; shown = scope after chips + search.
    std::vector<Module*> scope;
    if (ready)
        for (const auto& up : mgr.all())
        {
            Module* m = up.get();
            if (m_category < 0 || static_cast<int>(m->category()) == m_category)
                scope.push_back(m);
        }
    std::sort(scope.begin(), scope.end(), [](Module* a, Module* b)
        { return _stricmp(a->name().c_str(), b->name().c_str()) < 0; });

    size_t enabledInScope = 0;
    for (Module* m : scope) if (m->isEnabled()) enabledInScope++;

    std::vector<Module*> shown;
    for (Module* m : scope)
    {
        if (m_filter == Filter::Enabled && !m->isEnabled()) continue;
        if (m_filter == Filter::Favorites && !Settings::isFavorite(m->name())) continue;
        if (!matchesSearch(m)) continue;
        shown.push_back(m);
    }

    // Heading
    SectionLabel("MODULE LIBRARY");
    const char* heading = (m_category < 0) ? "All modules"
                                           : categoryName(static_cast<Category>(m_category));
    ImGui::PushFont(g_fonts.title);
    ImGui::TextUnformatted(heading);
    ImGui::PopFont();

    std::string sub;
    if (!ready) sub = "Starting up...";
    else
    {
        sub = plural(scope.size(), "module", "modules") + " \xC2\xB7 " + std::to_string(enabledInScope) + " enabled";
        if (shown.size() != scope.size()) sub += " \xC2\xB7 " + std::to_string(shown.size()) + " shown";
    }
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
    ImGui::TextUnformatted(sub.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

    // Filter chips
    if (Chip("All", m_filter == Filter::All))           m_filter = Filter::All;
    ImGui::SameLine(0.0f, S(8.0f));
    if (Chip("Enabled", m_filter == Filter::Enabled))   m_filter = Filter::Enabled;
    ImGui::SameLine(0.0f, S(8.0f));
    if (Chip("Favorites", m_filter == Filter::Favorites)) m_filter = Filter::Favorites;
    ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

    // Empty / loading states (honest about why there is nothing to show)
    auto emptyState = [](const char* title, const char* body)
    {
        ImGui::Dummy(ImVec2(0.0f, S(36.0f)));
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(g_fonts.semibold);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (w - ImGui::CalcTextSize(title).x) * 0.5f));
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        const float bw = std::min(w, ImGui::CalcTextSize(body).x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (w - bw) * 0.5f));
        ImGui::TextUnformatted(body);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    };

    if (!ready)
    {
        emptyState("Starting up", "The client is still attaching to the game. Modules appear once it finishes.");
        return;
    }
    if (mgr.size() == 0)
    {
        emptyState("No modules loaded",
            "Modules only register after the JVM attaches to Minecraft. Check the log file for details.");
        return;
    }
    if (shown.empty())
    {
        emptyState("No modules match", "Try clearing the search, the category or the filter chip.");
        return;
    }

    // Responsive card grid
    const float availW = ImGui::GetContentRegionAvail().x;
    const float gap = S(12.0f);
    const float minCard = S(250.0f);
    const int cols = std::max(1, static_cast<int>((availW + gap) / (minCard + gap)));
    const float cardW = (availW - gap * (cols - 1)) / cols;
    const float cardH = S(100.0f);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    int idx = 0;
    for (Module* m : shown)
    {
        const int col = idx % cols;
        const int row = idx / cols;
        renderCard(m, ImVec2(origin.x + col * (cardW + gap), origin.y + row * (cardH + gap)),
                   ImVec2(cardW, cardH));
        idx++;
    }
    const int rows = (idx + cols - 1) / cols;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (cardH + gap)));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

void Menu::renderCard(Module* m, ImVec2 p0, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    const float pad = S(14.0f);
    const float rad = S(10.0f);

    ImGui::PushID(m);

    ImGui::SetCursorScreenPos(p0);
    ImGui::SetNextItemAllowOverlap();
    const bool bodyClicked = ImGui::InvisibleButton("##card", size);
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsMouseHoveringRect(p0, p1) && ImGui::IsWindowHovered();

    const bool enabled = m->isEnabled();
    const bool selected = (m == m_selected);
    const float hv = animate(id * 31u + 1u, hovered ? 1.0f : 0.0f, 14.0f);
    const float en = animate(id * 31u + 2u, enabled ? 1.0f : 0.0f, 12.0f);
    const float se = animate(id * 31u + 3u, selected ? 1.0f : 0.0f, 14.0f);

    ImVec4 bg = Mix(Col::Surface, Col::SurfaceHover, hv);
    bg = Mix(bg, Mix(Col::Surface, Col::Accent, 0.08f), en * 0.9f);
    ImVec4 border = Mix(Col::Border, Col::BorderStrong, hv);
    border = Mix(border, Col::Accent, en * 0.40f);
    border = Mix(border, Col::Accent, se * 0.80f);

    dl->AddRectFilled(p0, p1, C(bg), rad);
    dl->AddRect(p0, p1, C(border), rad, 0, 1.0f);

    // Slim accent rail: enabled modules in teal, the selected module in amber.
    {
        const float railA = Mix(Col::Accent, Col::Accent2, se).w;
        const ImVec4 railC = Mix(Col::Accent, Col::Accent2, se);
        const float railT = en > se ? en : se;
        (void)railA;
        if (railT > 0.01f)
            dl->AddRectFilled(ImVec2(p0.x, p0.y + rad), ImVec2(p0.x + S(3.0f), p1.y - rad), C(railC, railT), S(2.0f));
    }

    // Name + description
    const float tw = S(38.0f), th = S(20.0f);
    ImGui::PushFont(g_fonts.semibold);
    dl->AddText(ImVec2(p0.x + pad, p0.y + pad),
                C(m->canEnable() ? Col::Text : Col::TextDim),
                ellipsize(m->name(), size.x - pad * 2.0f - tw - S(10.0f)).c_str());
    ImGui::PopFont();

    dl->AddText(ImVec2(p0.x + pad, p0.y + pad + S(24.0f)), C(Col::TextDim),
                ellipsize(m->desc(), size.x - pad * 2.0f).c_str());

    // Toggle (top-right)
    ImGui::SetCursorScreenPos(ImVec2(p1.x - pad - tw, p0.y + pad - S(1.0f)));
    bool state = enabled;
    ImGui::BeginDisabled(!m->canEnable());
    if (Toggle("##toggle", &state, ImVec2(tw, th)))
        ModuleManager::instance().setEnabled(*m, state);
    ImGui::EndDisabled();
    if (!m->canEnable()) Tooltip(m->statusNote().c_str());

    // Settings chip (bottom-left): count comes from the module's real settings list
    const size_t nset = m->settings().size();
    const float rowY = p1.y - pad - S(26.0f);
    char label[32];
    if (nset == 0) std::snprintf(label, sizeof(label), "No settings");
    else           std::snprintf(label, sizeof(label), "%zu setting%s", nset, nset == 1 ? "" : "s");

    ImGui::PushFont(g_fonts.caption);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 chipSize(ts.x + (nset ? S(34.0f) : S(4.0f)), S(26.0f));
    ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, rowY));
    if (nset > 0)
    {
        const bool clicked = ImGui::InvisibleButton("##settings", chipSize);
        const bool chipHov = ImGui::IsItemHovered();
        const float ch = animate(ImGui::GetItemID() * 31u + 5u, chipHov ? 1.0f : 0.0f, 16.0f);
        const ImVec2 c0 = ImGui::GetItemRectMin();
        const ImVec2 c1 = ImGui::GetItemRectMax();
        dl->AddRectFilled(c0, c1, C(Mix(Col::SurfaceRaised, Col::BorderStrong, ch * 0.6f)), S(7.0f));
        dl->AddText(ImVec2(c0.x + S(10.0f), c0.y + (chipSize.y - ts.y) * 0.5f),
                    C(Mix(Col::TextDim, Col::Text, ch)), label);
        // chevron
        const float cx = c1.x - S(12.0f), cy = (c0.y + c1.y) * 0.5f, d = S(3.0f);
        const ImU32 cc = C(Mix(Col::TextFaint, Col::Accent, ch));
        dl->AddLine(ImVec2(cx - d * 0.6f, cy - d), ImVec2(cx + d * 0.6f, cy), cc, 1.5f);
        dl->AddLine(ImVec2(cx + d * 0.6f, cy), ImVec2(cx - d * 0.6f, cy + d), cc, 1.5f);
        if (clicked)
        {
            m_selected = m;
            playSfx(Sfx::Click);
        }
    }
    else
    {
        dl->AddText(ImVec2(p0.x + pad, rowY + (chipSize.y - ts.y) * 0.5f), C(Col::TextFaint), label);
    }
    ImGui::PopFont();

    // Favourite star (bottom-right)
    const float starSize = S(26.0f);
    ImGui::SetCursorScreenPos(ImVec2(p1.x - pad - starSize, rowY));
    const bool fav = Settings::isFavorite(m->name());
    if (StarButton("##fav", fav, starSize))
        Settings::setFavorite(m->name(), !fav);
    Tooltip(fav ? "Remove from favorites" : "Add to favorites");

    ImGui::PopID();

    if (bodyClicked)
    {
        m_selected = selected ? nullptr : m;
        playSfx(Sfx::Click);
    }
}

// ════════════════════════════════════════════════════════════
//  Details / module configuration panel
// ════════════════════════════════════════════════════════════
void Menu::renderDetails(bool fullWidth)
{
    Module* m = m_selected;
    if (!m) return;

    if (fullWidth)
    {
        if (Button("Back to modules", ImVec2(0, 0), BtnStyle::Ghost)) m_selected = nullptr;
        ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
    }
    else
    {
        SectionLabel("MODULE");
        ImGui::SameLine(ImGui::GetWindowWidth() - S(20.0f) - S(28.0f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8.0f));
        if (CloseButton("##closeDetails")) m_selected = nullptr;
    }
    if (!m_selected) return;

    ImGui::PushFont(g_fonts.title);
    ImGui::SetWindowFontScale(0.8f);
    ImGui::TextUnformatted(m->name().c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();

    Badge(categoryName(m->category()), Col::Accent);
    ImGui::SameLine();
    Badge(m->maturity() == Maturity::Unfinished ? "Unfinished" : "Experimental",
          m->maturity() == Maturity::Unfinished ? Col::Danger : Col::Accent2);
    if (!m->statusNote().empty()) Tooltip(m->statusNote().c_str());

    ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(m->desc().c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

    bool state = m->isEnabled();
    ImGui::BeginDisabled(!m->canEnable());
    if (ToggleRow("Enabled", &state))
        ModuleManager::instance().setEnabled(*m, state);
    ImGui::EndDisabled();
    if (!m->statusNote().empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, m->canEnable() ? Col::TextFaint : Col::Danger);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(m->statusNote().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
    const ImVec2 lp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y), C(Col::Border));
    ImGui::Dummy(ImVec2(0.0f, S(10.0f)));

    SectionLabel("SETTINGS");
    ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

    auto& settings = m->settings();
    if (settings.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextFaint);
        ImGui::TextUnformatted("This module has no settings.");
        ImGui::PopStyleColor();
        return;
    }

    for (size_t i = 0; i < settings.size(); i++)
    {
        Setting& s = settings[i];
        const std::string pretty = PrettyName(s.name);
        ImGui::PushID(static_cast<int>(i));

        switch (s.type)
        {
        case SettingType::Bool:
            ToggleRow(pretty.c_str(), &s.boolVal);
            break;
        case SettingType::Float:
            SliderFloat(pretty.c_str(), &s.floatVal, s.floatMin, s.floatMax, "%.2f");
            break;
        case SettingType::Int:
            SliderInt(pretty.c_str(), &s.intVal, s.intMin, s.intMax);
            break;
        case SettingType::Choice:
        {
            ImGui::TextUnformatted(pretty.c_str());
            const int n = static_cast<int>(s.options.size());
            for (int o = 0; o < n; o++)
            {
                if (o) ImGui::SameLine();
                if (Chip(s.options[o].c_str(), s.intVal == o)) s.intVal = o;
            }
            break;
        }
        case SettingType::Keybind:
        {
            ImGui::TextUnformatted((pretty + " (toggle key)").c_str());
            const bool listening = (m_bindTarget == &s);
            std::string label = listening ? "Press a key..." : keyName(s.keyVal);
            if (Button(label.c_str(), ImVec2(S(150.0f), 0), BtnStyle::Secondary))
                m_bindTarget = listening ? nullptr : &s;
            if (listening)
            {
                // Esc / Backspace clears the binding; any other key binds it.
                for (int vk = 8; vk < 255; vk++)
                {
                    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_RSHIFT) continue;
                    if (!(GetAsyncKeyState(vk) & 0x8000)) continue;
                    s.keyVal = (vk == VK_ESCAPE || vk == VK_BACK) ? 0 : vk;
                    m_bindTarget = nullptr;
                    playSfx(Sfx::Click);
                    break;
                }
            }
            else
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, Col::TextFaint);
                ImGui::TextUnformatted(s.keyVal ? "click to rebind" : "unbound");
                ImGui::PopStyleColor();
            }
            break;
        }
        }

        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
    }
}

// ════════════════════════════════════════════════════════════
//  Settings page
// ════════════════════════════════════════════════════════════
void Menu::renderSettingsPage()
{
    const float maxW = S(640.0f);

    SectionLabel("PREFERENCES");
    ImGui::PushFont(g_fonts.title);
    ImGui::TextUnformatted("Settings");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

    // Modules
    SectionLabel("MODULES");
    beginPanel("##panelModules", maxW);
    {
        bool restore = Settings::restoreEnabled();
        if (ToggleRow("Re-enable modules on startup", &restore)) Settings::setRestoreEnabled(restore);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextFaint);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("Module settings are always remembered. This controls whether the modules that were on last session switch on again once a world is loaded. Off by default.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    endPanel();

    ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
    // Sound
    SectionLabel("SOUND");
    beginPanel("##panelSound", maxW);
    {
        bool on = Settings::soundsEnabled();
        if (ToggleRow("UI sounds", &on)) Settings::setSoundsEnabled(on);

        ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
        const bool audioOk = Sound::available();
        if (!on || !audioOk) ImGui::BeginDisabled();
        int pct = static_cast<int>(std::lround(Settings::soundVolume() * 100.0f));
        if (SliderInt("Volume (%)", &pct, 0, 100)) Settings::setSoundVolume(pct / 100.0f);
        ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
        if (Button("Play test sound", ImVec2(0, 0), BtnStyle::Secondary))
            playSfx(Sfx::ToggleOn);
        if (!on || !audioOk) ImGui::EndDisabled();

        if (!audioOk)
        {
            ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
            ImGui::PushStyleColor(ImGuiCol_Text, Col::Danger);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted("Audio output is unavailable. UI sounds are disabled for this session.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }
    endPanel();

    ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
    SectionLabel("DIAGNOSTICS");
    beginPanel("##panelDiag", maxW);
    {
        bool con = Settings::debugConsole();
        if (ToggleRow("Debug console", &con)) Settings::setDebugConsole(con);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextFaint);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("Opens a console window inside the game process. Takes effect the next time the client is injected.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    endPanel();

    ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
    SectionLabel("ABOUT");
    beginPanel("##panelAbout", maxW);
    {
        infoRow("Version", Brand::kVersion);
        infoRow("Target game", Brand::kTargetGame);
        infoRow("Menu key", "Right Shift");
        infoRow("Settings file", Settings::filePath());
        infoRow("Profile file", Profile::filePath());
        infoRow("Log file", Logger::getLogPath().empty() ? std::string("(not initialised)") : Logger::getLogPath());
    }
    endPanel();
}

// ════════════════════════════════════════════════════════════
//  Status bar (all values derived from live state)
// ════════════════════════════════════════════════════════════
void Menu::renderStatusBar(float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    dl->AddRectFilled(wp, ImVec2(wp.x + width, wp.y + height), C(Col::Sidebar),
                      S(12.0f), ImDrawFlags_RoundCornersBottomRight);

    auto& mgr = ModuleManager::instance();
    const bool ready = Client::instance().isReady();
    const bool jvm = JVM::instance().vm() != nullptr;
    const size_t total = ready ? mgr.size() : 0;
    const size_t enabled = ready ? countEnabled() : 0;

    const char* status;
    ImVec4 dot;
    if (!ready)                         { status = "Starting...";               dot = Col::Accent2; }
    else if (!jvm || total == 0)        { status = "Limited - JVM not attached"; dot = Col::Danger; }
    else                                { status = "Client active";             dot = Col::Success; }

    ImGui::PushFont(g_fonts.caption);
    const float cy = wp.y + height * 0.5f;
    const float lineH = ImGui::GetTextLineHeight();

    float x = wp.x + S(24.0f);
    dl->AddCircleFilled(ImVec2(x + S(4.0f), cy), S(3.5f), C(dot), 16);
    x += S(16.0f);
    dl->AddText(ImVec2(x, cy - lineH * 0.5f), C(Col::TextDim), status);
    x += ImGui::CalcTextSize(status).x + S(18.0f);

    char mid[96];
    std::snprintf(mid, sizeof(mid), "%zu of %zu modules enabled", enabled, total);
    dl->AddText(ImVec2(x, cy - lineH * 0.5f), C(Col::TextDim), mid);
    x += ImGui::CalcTextSize(mid).x + S(18.0f);

    if (ready && jvm)
    {
        const char* tick = JVM::instance().jvmti() ? "Tick: JVMTI hook" : "Tick: frame fallback";
        dl->AddText(ImVec2(x, cy - lineH * 0.5f), C(Col::TextFaint), tick);
    }

    char right[64];
    std::snprintf(right, sizeof(right), "%.0f FPS  \xC2\xB7  v%s", ImGui::GetIO().Framerate, Brand::kVersion);
    const float rw = ImGui::CalcTextSize(right).x;
    dl->AddText(ImVec2(wp.x + width - S(24.0f) - rw, cy - lineH * 0.5f), C(Col::TextFaint), right);
    ImGui::PopFont();
}
