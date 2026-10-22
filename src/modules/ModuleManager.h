#pragma once
#include "Module.h"
#include <vector>
#include <memory>
#include <functional>
#include <map>

class ModuleManager
{
public:
    static ModuleManager& instance();

    template<typename T, typename... Args>
    void add(Args&&... args)
    {
        auto mod = std::make_unique<T>(std::forward<Args>(args)...);
        m_modules.push_back(std::move(mod));
        m_categorized = false;   // category cache is rebuilt on demand / by finalize()
    }

    // Call once after all modules are registered (before other threads read the
    // registry): builds the per-category lists eagerly so no thread builds them lazily.
    void finalize();

    void tickAll();       // game tick thread
    void renderAll();     // render thread

    // Enable/disable through the manager: runs the module's callbacks inside a JNI local
    // frame with exceptions contained. Returns the resulting state.
    bool setEnabled(Module& m, bool on);

    // ── Persistence (client.ini [modules]) ───────────────────
    // saveState(): copy every module's enabled flag and setting values into Settings.
    // restoreSettings(): apply stored setting values (clamped to each setting's range).
    // restoreEnabled(): re-enable modules saved as on; called once the world exists.
    void saveState();
    void restoreSettings();
    void restoreEnabled();
    bool enabledRestored() const { return m_enabledRestored; }

    // Toggles modules whose "Keybind" setting (SettingType::Keybind) is a virtual-key code.
    // Render thread, once per frame. Does nothing while `blocked` is true (menu open, text input, a game
    // screen open) or while the game window is not the foreground window.
    void pollKeybinds(bool blocked);

    // Disables every enabled module (runs onDisable so e.g. Fly/FullBright undo their changes).
    void disableAll();

    std::vector<Module*>& getByCategory(Category cat);

    const std::vector<std::unique_ptr<Module>>& all() const { return m_modules; }

    size_t size() const { return m_modules.size(); }

    Module* find(const std::string& name);

private:
    ModuleManager() = default;

    std::vector<std::unique_ptr<Module>> m_modules;
    std::map<Category, std::vector<Module*>> m_byCategory;
    bool m_categorized = false;
    bool m_enabledRestored = false;
    bool m_keyDown[256] = {};

    void buildCategories();
};
