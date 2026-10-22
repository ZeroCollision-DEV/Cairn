#include "Settings.h"
#include "../shared/Ini.h"
#include "../utils/Paths.h"
#include "../utils/Logger.h"

#include <Windows.h>
#include <fstream>
#include <mutex>
#include <map>
#include <set>

namespace
{
    std::mutex g_mu;
    bool  g_sounds  = true;
    float g_volume  = 0.6f;
    bool  g_console = false;
    bool  g_restoreEnabled = false;
    std::map<std::string, std::string> g_modules;
    std::set<std::string> g_favorites;
    bool  g_dirty = false;
    DWORD g_lastSave = 0;

    std::filesystem::path iniPath() { return Paths::dataDir() / L"client.ini"; }

    float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    void saveLocked()
    {
        std::ofstream f(iniPath(), std::ios::out | std::ios::trunc);
        if (!f)
        {
            LOG_WARN("Settings: could not write " + Paths::toUtf8(iniPath()));
            return;
        }
        f << "; Cairn settings. Edited by the in-game menu; safe to edit by hand.\n";
        f << "[ui]\n";
        f << "sounds=" << (g_sounds ? 1 : 0) << "\n";
        f << "volume=" << g_volume << "\n";
        f << "; 1 = re-enable the modules that were on last session (after the game world is available)\n";
        f << "restore_enabled=" << (g_restoreEnabled ? 1 : 0) << "\n\n";
        f << "[debug]\n";
        f << "; 1 = open a console window inside the game process on injection\n";
        f << "console=" << (g_console ? 1 : 0) << "\n\n";
        f << "[favorites]\n";
        for (const auto& name : g_favorites) f << name << "=1\n";
        f << "\n[modules]\n";
        for (const auto& kv : g_modules) f << kv.first << "=" << kv.second << "\n";
        f.flush();
        if (!f) LOG_WARN("Settings: writing client.ini failed");
        g_dirty = false;
        g_lastSave = GetTickCount();
    }
}

namespace Settings
{
    std::string filePath() { return Paths::toUtf8(iniPath()); }

    void load()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        ini::Map m;
        if (!ini::readFile(iniPath(), m)) return;   // first run: keep defaults

        g_sounds  = ini::getBool(m, "ui.sounds", true);
        g_volume  = clamp01(ini::getFloat(m, "ui.volume", 0.6f));
        g_console = ini::getBool(m, "debug.console", false);
        g_restoreEnabled = ini::getBool(m, "ui.restore_enabled", false);

        g_modules.clear();
        {
            const std::string mp = "modules.";
            for (const auto& kv : m)
                if (kv.first.rfind(mp, 0) == 0) g_modules[kv.first.substr(mp.size())] = kv.second;
        }

        g_favorites.clear();
        const std::string prefix = "favorites.";
        for (const auto& kv : m)
            if (kv.first.rfind(prefix, 0) == 0 && ini::getBool(m, kv.first, false))
                g_favorites.insert(kv.first.substr(prefix.size()));
    }

    void save()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        saveLocked();
    }

    void saveIfDirty()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_dirty && GetTickCount() - g_lastSave > 1000) saveLocked();
    }

    void flush()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_dirty) saveLocked();
    }

    bool soundsEnabled() { std::lock_guard<std::mutex> lk(g_mu); return g_sounds; }
    void setSoundsEnabled(bool on)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_sounds != on) { g_sounds = on; g_dirty = true; }
    }

    float soundVolume() { std::lock_guard<std::mutex> lk(g_mu); return g_volume; }
    void setSoundVolume(float v)
    {
        v = clamp01(v);
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_volume != v) { g_volume = v; g_dirty = true; }
    }

    bool debugConsole() { std::lock_guard<std::mutex> lk(g_mu); return g_console; }
    void setDebugConsole(bool on)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_console != on) { g_console = on; g_dirty = true; }
    }

    bool restoreEnabled() { std::lock_guard<std::mutex> lk(g_mu); return g_restoreEnabled; }
    void setRestoreEnabled(bool on)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_restoreEnabled != on) { g_restoreEnabled = on; g_dirty = true; }
    }

    std::string moduleValue(const std::string& key, const std::string& def)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_modules.find(key);
        return it == g_modules.end() ? def : it->second;
    }
    bool hasModuleValue(const std::string& key)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        return g_modules.count(key) != 0;
    }
    void setModuleValue(const std::string& key, const std::string& value)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_modules.find(key);
        if (it == g_modules.end()) { g_modules[key] = value; g_dirty = true; }
        else if (it->second != value) { it->second = value; g_dirty = true; }
    }

    bool isFavorite(const std::string& name)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        return g_favorites.count(name) != 0;
    }

    void setFavorite(const std::string& name, bool fav)
    {
        std::lock_guard<std::mutex> lk(g_mu);
        const bool changed = fav ? g_favorites.insert(name).second : (g_favorites.erase(name) != 0);
        if (changed) g_dirty = true;
    }
}
