#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

namespace Paths
{
    // %APPDATA%\Cairn  (falls back to the temp folder if APPDATA is unset).
    // Holds client.ini and profile.ini. The log stays in %TEMP% (see Logger.h).
    inline const std::filesystem::path& dataDir()
    {
        static std::filesystem::path cached;
        if (cached.empty())
        {
            wchar_t buf[MAX_PATH] = {};
            DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
            std::error_code ec;
            std::filesystem::path base = (n > 0 && n < MAX_PATH)
                ? std::filesystem::path(buf)
                : std::filesystem::temp_directory_path(ec);
            cached = base / L"Cairn";
            const bool fresh = !std::filesystem::exists(cached, ec);
            std::filesystem::create_directories(cached, ec);
            if (fresh)
            {
                // One-time migration from the pre-rename data folder. Copy, never move or
                // delete: the old folder is left untouched.
                const std::filesystem::path legacy = base / L"YTClient";
                for (const wchar_t* f : { L"client.ini", L"profile.ini" })
                {
                    if (std::filesystem::exists(legacy / f, ec))
                        std::filesystem::copy_file(legacy / f, cached / f,
                            std::filesystem::copy_options::skip_existing, ec);
                }
            }
        }
        return cached;
    }

    inline std::string toUtf8(const std::filesystem::path& p)
    {
        const auto u8 = p.u8string();
        return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
    }
}
