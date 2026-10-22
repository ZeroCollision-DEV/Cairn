#pragma once
// UI sound events. Widgets raise these through playSfx(); the host application
// (the client) installs a handler that actually plays audio. If no handler is
// installed (e.g. in the injector) sound events are simply ignored.

namespace UI
{
    enum class Sfx
    {
        Open,
        Close,
        Click,
        ToggleOn,
        ToggleOff,
        Tab,
        Error
    };

    using SfxHandler = void (*)(Sfx);

    inline SfxHandler g_sfxHandler = nullptr;

    inline void playSfx(Sfx s)
    {
        if (g_sfxHandler) g_sfxHandler(s);
    }
}
