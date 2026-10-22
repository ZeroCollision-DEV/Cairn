#pragma once
#include "../shared/Sfx.h"

// Centralised UI sound playback.
//  * Clips are embedded in the DLL (SoundData.inl) -- no files to locate.
//  * Playback is asynchronous (PlaySound + SND_ASYNC) so it never blocks the
//    render thread.
//  * Enabled/volume come from Settings; volume is applied by scaling a copy of
//    the PCM data (the embedded originals are never modified).
//  * After repeated playback failures (no audio device, etc.) sound disables
//    itself instead of retrying on every click.
namespace Sound
{
    void init();
    void shutdown();
    void play(UI::Sfx sfx);

    // False if the embedded clips could not be parsed or playback keeps failing.
    bool available();
}
