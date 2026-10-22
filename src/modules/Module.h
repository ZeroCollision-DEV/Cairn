#pragma once
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <atomic>

// ── Categories ───────────────────────────────────────────────
enum class Category : int
{
    Movement = 0,
    Combat   = 1,
    Player   = 2,
    Render   = 3,
    World    = 4,
    Misc     = 5
};

inline const char* categoryName(Category c)
{
    switch (c)
    {
    case Category::Movement: return "Movement";
    case Category::Combat:   return "Combat";
    case Category::Player:   return "Player";
    case Category::Render:   return "Render";
    case Category::World:    return "World";
    case Category::Misc:     return "Misc";
    }
    return "Unknown";
}

// ── Setting types ────────────────────────────────────────────
enum class SettingType
{
    Bool,
    Float,
    Int,
    Keybind,
    Choice      // pick one of `options`; the selected index is `intVal`
};

// How far along a module is. Shown in the menu so unfinished work is never presented as working.
enum class Maturity
{
    Experimental,   // implemented, but its game-side calls have not been verified in-game
    Unfinished      // placeholder: cannot be enabled
};

struct Setting
{
    std::string name;
    SettingType type;
    bool   boolVal   = false;
    float  floatVal  = 0.0f;
    float  floatMin  = 0.0f;
    float  floatMax  = 1.0f;
    int    intVal    = 0;
    int    intMin    = 0;
    int    intMax    = 100;
    int    keyVal    = 0;
    std::vector<std::string> options;   // Choice only
};

// ── Base Module ──────────────────────────────────────────────
class Module
{
public:
    Module(std::string name, std::string desc, Category cat)
        : m_name(std::move(name)), m_desc(std::move(desc)), m_category(cat) {}
    virtual ~Module() = default;

    virtual void onEnable()  {}
    virtual void onDisable() {}
    virtual void onTick()    {}
    virtual void onRender()  {}

    // Prefer ModuleManager::setEnabled(): it wraps the callbacks in a JNI local frame and a
    // try/catch. Unfinished modules refuse to enable.
    void setEnabled(bool enabled)
    {
        if (m_enabled.load() == enabled) return;
        if (enabled && m_maturity == Maturity::Unfinished) return;
        m_enabled = enabled;
        if (enabled) onEnable();
        else onDisable();
    }

    bool isEnabled() const { return m_enabled.load(); }
    void toggle() { setEnabled(!m_enabled.load()); }
    bool canEnable() const { return m_maturity != Maturity::Unfinished; }

    Maturity maturity() const { return m_maturity; }
    // One-line explanation shown as a tooltip next to the maturity badge (may be empty).
    const std::string& statusNote() const { return m_statusNote; }

    const std::string& name() const { return m_name; }
    const std::string& desc() const { return m_desc; }
    Category category() const { return m_category; }

    // Settings
    Setting& addBool(const std::string& n, bool def)
    {
        m_settings.push_back({n, SettingType::Bool, def});
        return m_settings.back();
    }
    Setting& addFloat(const std::string& n, float def, float min, float max)
    {
        m_settings.push_back({n, SettingType::Float, false, def, min, max});
        return m_settings.back();
    }
    Setting& addInt(const std::string& n, int def, int min, int max)
    {
        m_settings.push_back({n, SettingType::Int, false, 0, 0, 0, def, min, max});
        return m_settings.back();
    }
    Setting& addKey(const std::string& n, int def)
    {
        m_settings.push_back({n, SettingType::Keybind, false, 0, 0, 0, 0, 0, 0, def});
        return m_settings.back();
    }

    Setting& addChoice(const std::string& n, std::vector<std::string> opts, int def)
    {
        Setting s;
        s.name = n;
        s.type = SettingType::Choice;
        s.intMin = 0;
        s.intMax = opts.empty() ? 0 : static_cast<int>(opts.size()) - 1;
        s.intVal = def < s.intMin ? s.intMin : (def > s.intMax ? s.intMax : def);
        s.options = std::move(opts);
        m_settings.push_back(std::move(s));
        return m_settings.back();
    }

    std::vector<Setting>& settings() { return m_settings; }

    Setting* getSetting(const std::string& n)
    {
        for (auto& s : m_settings)
            if (s.name == n) return &s;
        return nullptr;
    }

    bool   getBool(const std::string& n, bool def = false)
    {
        auto* s = getSetting(n); return s ? s->boolVal : def;
    }
    float  getFloat(const std::string& n, float def = 0)
    {
        auto* s = getSetting(n); return s ? s->floatVal : def;
    }
    int    getInt(const std::string& n, int def = 0)
    {
        auto* s = getSetting(n); return s ? s->intVal : def;
    }

protected:
    void setMaturity(Maturity m, std::string note = {}) { m_maturity = m; m_statusNote = std::move(note); }

    std::string       m_name;
    std::string       m_desc;
    Category          m_category;
    std::atomic<bool> m_enabled{false};
    Maturity          m_maturity = Maturity::Experimental;
    std::string       m_statusNote = "Not yet verified in-game.";
    std::vector<Setting> m_settings;
};
