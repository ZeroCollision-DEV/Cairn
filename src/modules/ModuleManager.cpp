#include "ModuleManager.h"
#include "../utils/Logger.h"
#include "../core/JVM.h"
#include "../core/Settings.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    // Pushes a JNI local-reference frame for the current thread; popped on scope exit.
    // Modules create local refs freely, and render/native threads never return to Java
    // to have them released.
    struct JniFrame
    {
        JNIEnv* env = nullptr;
        bool ok = false;
        explicit JniFrame(int capacity = 128)
        {
            env = JVM::instance().env();
            ok = env && env->PushLocalFrame(capacity) == 0;
        }
        ~JniFrame()
        {
            if (ok)
            {
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->PopLocalFrame(nullptr);
            }
        }
    };

    std::string fmtFloat(float v)
    {
        char b[48];
        std::snprintf(b, sizeof(b), "%.6g", v);
        return b;
    }
}

ModuleManager& ModuleManager::instance()
{
    static ModuleManager inst;
    return inst;
}

void ModuleManager::finalize()
{
    buildCategories();
}

void ModuleManager::buildCategories()
{
    m_byCategory.clear();
    for (auto& mod : m_modules)
        m_byCategory[mod->category()].push_back(mod.get());
    m_categorized = true;
}

void ModuleManager::tickAll()
{
    if (!m_categorized) buildCategories();

    for (auto& mod : m_modules)
    {
        if (mod->isEnabled())
        {
            try { mod->onTick(); }
            catch (...) { LOG_WARN("Exception in " + mod->name() + "::onTick"); }
        }
    }
}

void ModuleManager::renderAll()
{
    if (!m_categorized) buildCategories();

    for (auto& mod : m_modules)
    {
        if (mod->isEnabled())
        {
            JniFrame frame;
            try { mod->onRender(); }
            catch (...) { LOG_WARN("Exception in " + mod->name() + "::onRender"); }
        }
    }
}

bool ModuleManager::setEnabled(Module& m, bool on)
{
    if (on && !m.canEnable())
    {
        LOG_WARN(m.name() + " is unfinished and cannot be enabled");
        return false;
    }
    JniFrame frame;
    try { m.setEnabled(on); }
    catch (...) { LOG_WARN("Exception in " + m.name() + (on ? "::onEnable" : "::onDisable")); }
    return m.isEnabled();
}

void ModuleManager::disableAll()
{
    for (auto& mod : m_modules)
        if (mod->isEnabled()) setEnabled(*mod, false);
}

void ModuleManager::pollKeybinds(bool blocked)
{
    DWORD fgPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
    const bool active = !blocked && fgPid == GetCurrentProcessId();

    for (auto& mod : m_modules)
    {
        for (auto& st : mod->settings())
        {
            if (st.type != SettingType::Keybind || st.keyVal <= 0 || st.keyVal >= 256) continue;
            const bool down = (GetAsyncKeyState(st.keyVal) & 0x8000) != 0;
            if (active && down && !m_keyDown[st.keyVal])
                setEnabled(*mod, !mod->isEnabled());
            m_keyDown[st.keyVal] = down;
        }
    }
}

void ModuleManager::saveState()
{
    for (auto& mod : m_modules)
    {
        const std::string& n = mod->name();
        Settings::setModuleValue(n + ".enabled", mod->isEnabled() ? "1" : "0");
        for (const auto& st : mod->settings())
        {
            const std::string key = n + "." + st.name;
            switch (st.type)
            {
            case SettingType::Bool:    Settings::setModuleValue(key, st.boolVal ? "1" : "0"); break;
            case SettingType::Float:   Settings::setModuleValue(key, fmtFloat(st.floatVal)); break;
            case SettingType::Int:
            case SettingType::Choice:  Settings::setModuleValue(key, std::to_string(st.intVal)); break;
            case SettingType::Keybind: Settings::setModuleValue(key, std::to_string(st.keyVal)); break;
            }
        }
    }
}

void ModuleManager::restoreSettings()
{
    for (auto& mod : m_modules)
    {
        for (auto& st : mod->settings())
        {
            const std::string key = mod->name() + "." + st.name;
            if (!Settings::hasModuleValue(key)) continue;
            const std::string v = Settings::moduleValue(key);
            // Malformed or out-of-range values are ignored/clamped, never fatal.
            char* end = nullptr;
            switch (st.type)
            {
            case SettingType::Bool:
                st.boolVal = (v == "1" || v == "true");
                break;
            case SettingType::Float:
            {
                const float f = std::strtof(v.c_str(), &end);
                if (end != v.c_str() && std::isfinite(f)) st.floatVal = std::clamp(f, st.floatMin, st.floatMax);
                break;
            }
            case SettingType::Int:
            case SettingType::Choice:
            {
                const long l = std::strtol(v.c_str(), &end, 10);
                if (end != v.c_str()) st.intVal = static_cast<int>(std::clamp<long>(l, st.intMin, st.intMax));
                break;
            }
            case SettingType::Keybind:
            {
                const long l = std::strtol(v.c_str(), &end, 10);
                if (end != v.c_str() && l >= 0 && l < 256) st.keyVal = static_cast<int>(l);
                break;
            }
            }
        }
    }
}

void ModuleManager::restoreEnabled()
{
    if (m_enabledRestored) return;
    m_enabledRestored = true;
    if (!Settings::restoreEnabled()) return;
    for (auto& mod : m_modules)
        if (Settings::moduleValue(mod->name() + ".enabled") == "1" && mod->canEnable())
            setEnabled(*mod, true);
}

std::vector<Module*>& ModuleManager::getByCategory(Category cat)
{
    if (!m_categorized) buildCategories();
    return m_byCategory[cat];
}

Module* ModuleManager::find(const std::string& name)
{
    for (auto& mod : m_modules)
        if (mod->name() == name) return mod.get();
    return nullptr;
}
