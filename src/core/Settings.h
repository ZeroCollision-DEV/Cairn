#pragma once
#include <string>

// Persistent client settings (%APPDATA%\Cairn\client.ini).
// Stores UI preferences, favourites and module state ([modules] section, keys like
// "KillAura.Range" / "KillAura.enabled"). Module enable-state is only re-applied at
// startup if ui.restore_enabled is on (default off).
namespace Settings
{
    void load();
    void save();
    void saveIfDirty();   // debounced (at most once per second)
    void flush();         // save immediately if anything changed

    bool  soundsEnabled();
    void  setSoundsEnabled(bool on);
    float soundVolume();            // 0..1
    void  setSoundVolume(float v);

    bool debugConsole();            // opens a console window in the game process
    void setDebugConsole(bool on);

    bool isFavorite(const std::string& moduleName);
    void setFavorite(const std::string& moduleName, bool fav);

    // Whether modules that were enabled last session are re-enabled on startup.
    bool restoreEnabled();
    void setRestoreEnabled(bool on);

    // Generic per-module key/value store (values are plain text). Used by ModuleManager.
    std::string moduleValue(const std::string& key, const std::string& def = "");
    bool        hasModuleValue(const std::string& key);
    void        setModuleValue(const std::string& key, const std::string& value);

    std::string filePath();
}
