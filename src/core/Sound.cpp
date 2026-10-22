#include "Sound.h"
#include "Settings.h"
#include "../utils/Logger.h"

#include <Windows.h>
#include <mmsystem.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "SoundData.inl"

#pragma comment(lib, "winmm.lib")

namespace
{
    struct Clip
    {
        const unsigned char* src = nullptr;
        size_t size = 0;
        std::vector<unsigned char> scaled;   // what PlaySound reads from
        size_t dataOffset = 0;
        size_t dataLength = 0;
        bool valid = false;
        DWORD lastPlay = 0;
    };

    constexpr int kClipCount = 7; // matches UI::Sfx

    std::array<Clip, kClipCount> g_clips;
    std::mutex g_mu;
    bool  g_inited = false;
    float g_builtVolume = -1.0f;
    int   g_failures = 0;
    constexpr int kMaxFailures = 5;

    uint32_t rd32(const unsigned char* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    uint16_t rd16(const unsigned char* p)
    {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }

    // Validates a canonical PCM/16-bit RIFF/WAVE blob and locates its sample data.
    bool parseWav(Clip& c)
    {
        const unsigned char* d = c.src;
        const size_t n = c.size;
        if (n < 44 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0)
            return false;

        bool fmtOk = false;
        size_t off = 12;
        while (off + 8 <= n)
        {
            const uint32_t len = rd32(d + off + 4);
            if (std::memcmp(d + off, "fmt ", 4) == 0 && off + 8 + 16 <= n)
            {
                fmtOk = (rd16(d + off + 8) == 1) && (rd16(d + off + 22) == 16); // PCM, 16-bit
            }
            else if (std::memcmp(d + off, "data", 4) == 0)
            {
                if (!fmtOk || off + 8 + len > n) return false;
                c.dataOffset = off + 8;
                c.dataLength = len;
                return true;
            }
            off += 8 + static_cast<size_t>(len) + (len & 1u);
        }
        return false;
    }

    void rebuild(float volume)
    {
        // Perceptual-ish curve: quiet settings stay genuinely quiet.
        const float gain = volume * volume;
        for (auto& c : g_clips)
        {
            if (!c.valid) continue;
            c.scaled.assign(c.src, c.src + c.size);
            for (size_t i = 0; i + 1 < c.dataLength; i += 2)
            {
                int16_t s;
                std::memcpy(&s, &c.scaled[c.dataOffset + i], sizeof(s));
                const float v = static_cast<float>(s) * gain;
                s = static_cast<int16_t>(v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v));
                std::memcpy(&c.scaled[c.dataOffset + i], &s, sizeof(s));
            }
        }
        g_builtVolume = volume;
    }
}

namespace Sound
{
    void init()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_inited) return;

        auto set = [](UI::Sfx s, const unsigned char* data, size_t size)
        {
            Clip& c = g_clips[static_cast<int>(s)];
            c.src = data;
            c.size = size;
            c.valid = parseWav(c);
            if (!c.valid) LOG_WARN("Sound: embedded clip " + std::to_string(static_cast<int>(s)) + " is not valid PCM16 WAV");
        };

        set(UI::Sfx::Open,      SoundData::kOpen,      sizeof(SoundData::kOpen));
        set(UI::Sfx::Close,     SoundData::kClose,     sizeof(SoundData::kClose));
        set(UI::Sfx::Click,     SoundData::kClick,     sizeof(SoundData::kClick));
        set(UI::Sfx::ToggleOn,  SoundData::kToggleOn,  sizeof(SoundData::kToggleOn));
        set(UI::Sfx::ToggleOff, SoundData::kToggleOff, sizeof(SoundData::kToggleOff));
        set(UI::Sfx::Tab,       SoundData::kTab,       sizeof(SoundData::kTab));
        set(UI::Sfx::Error,     SoundData::kError,     sizeof(SoundData::kError));

        g_inited = true;
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        // Stop anything still reading from our buffers before they are freed.
        PlaySoundW(nullptr, nullptr, 0);
        g_inited = false;
    }

    bool available()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_inited || g_failures >= kMaxFailures) return false;
        for (const auto& c : g_clips) if (c.valid) return true;
        return false;
    }

    void play(UI::Sfx sfx)
    {
        if (!Settings::soundsEnabled()) return;
        const float vol = Settings::soundVolume();
        if (vol <= 0.001f) return;

        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_inited || g_failures >= kMaxFailures) return;

        const int idx = static_cast<int>(sfx);
        if (idx < 0 || idx >= kClipCount) return;
        Clip& c = g_clips[idx];
        if (!c.valid) return;

        // Rapid repeats of the same cue (e.g. dragging over chips) just sound harsh.
        const DWORD now = GetTickCount();
        if (now - c.lastPlay < 35) return;
        c.lastPlay = now;

        if (std::fabs(vol - g_builtVolume) > 0.004f)
        {
            PlaySoundW(nullptr, nullptr, 0);   // stop playback before touching buffers
            rebuild(vol);
        }

        if (!PlaySoundW(reinterpret_cast<LPCWSTR>(c.scaled.data()), nullptr,
                        SND_MEMORY | SND_ASYNC | SND_NODEFAULT))
        {
            if (++g_failures == kMaxFailures)
                LOG_WARN("Sound: playback failed repeatedly; UI sounds disabled for this session");
        }
        else
        {
            g_failures = 0;
        }
    }
}
