#include "Profile.h"
#include "../shared/Ini.h"
#include "../utils/Paths.h"
#include "../utils/Logger.h"

#include <Windows.h>
#include <gl/GL.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <fstream>
#include <mutex>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr int kAvatarPx = 128;

    std::mutex g_mu;
    Profile::Info g_info;
    std::vector<unsigned char> g_pixels;   // RGBA8, kAvatarPx^2, waiting for GL upload
    GLuint g_tex = 0;

    std::filesystem::path iniPath() { return Paths::dataDir() / L"profile.ini"; }

    std::filesystem::path pathFromUtf8(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (n <= 0) return std::filesystem::path(s);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
        return std::filesystem::path(w);
    }

    void writeTemplateIfMissing()
    {
        std::error_code ec;
        if (std::filesystem::exists(iniPath(), ec)) return;
        std::ofstream f(iniPath(), std::ios::out | std::ios::trunc);
        if (!f) return;
        f << "; Cairn local profile (optional).\n"
             "; Shown in the bottom-left of the menu. Nothing here is sent anywhere.\n"
             "; Discord integration is not implemented -- this is a locally configured profile.\n"
             "[profile]\n"
             "display_name=\n"
             "; Optional avatar image (PNG/JPG/BMP). Absolute path, or relative to this file.\n"
             "avatar=\n";
    }

    // Decodes + centre-crops + scales an image to kAvatarPx RGBA. Returns false on any failure.
    bool decodeAvatar(const std::filesystem::path& file, std::vector<unsigned char>& rgba)
    {
        const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // S_OK / S_FALSE both need a matching CoUninitialize; RPC_E_CHANGED_MODE does not
        // (COM is already initialised on this thread with another model, which is fine).
        const bool needUninit = SUCCEEDED(hrInit);

        bool ok = false;
        {
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> conv;
            ComPtr<IWICBitmapClipper> clipper;
            ComPtr<IWICBitmapScaler> scaler;

            do
            {
                if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(&factory)))) break;
                if (FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder))) break;
                if (FAILED(decoder->GetFrame(0, &frame))) break;

                UINT w = 0, h = 0;
                if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) break;

                if (FAILED(factory->CreateFormatConverter(&conv))) break;
                if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                            WICBitmapDitherTypeNone, nullptr, 0.0,
                                            WICBitmapPaletteTypeCustom))) break;

                const UINT side = w < h ? w : h;
                WICRect r = { static_cast<INT>((w - side) / 2), static_cast<INT>((h - side) / 2),
                              static_cast<INT>(side), static_cast<INT>(side) };
                if (FAILED(factory->CreateBitmapClipper(&clipper))) break;
                if (FAILED(clipper->Initialize(conv.Get(), &r))) break;

                if (FAILED(factory->CreateBitmapScaler(&scaler))) break;
                if (FAILED(scaler->Initialize(clipper.Get(), kAvatarPx, kAvatarPx,
                                              WICBitmapInterpolationModeHighQualityCubic))) break;

                rgba.assign(static_cast<size_t>(kAvatarPx) * kAvatarPx * 4, 0);
                if (FAILED(scaler->CopyPixels(nullptr, kAvatarPx * 4,
                                              static_cast<UINT>(rgba.size()), rgba.data()))) break;
                ok = true;
            } while (false);
        }

        if (needUninit) CoUninitialize();
        return ok;
    }
}

namespace Profile
{
    std::string filePath() { return Paths::toUtf8(iniPath()); }

    void refresh()
    {
        writeTemplateIfMissing();

        Info next;
        std::vector<unsigned char> pixels;

        ini::Map m;
        if (ini::readFile(iniPath(), m))
        {
            next.displayName = ini::get(m, "profile.display_name");
            const std::string avatar = ini::get(m, "profile.avatar");
            next.configured = !next.displayName.empty();
            if (next.configured) next.source = "Local profile";

            if (next.configured && !avatar.empty())
            {
                std::filesystem::path p = pathFromUtf8(avatar);
                if (p.is_relative()) p = Paths::dataDir() / p;
                std::error_code ec;
                if (!std::filesystem::exists(p, ec))
                    LOG_WARN("Profile: avatar file not found: " + avatar);
                else if (decodeAvatar(p, pixels))
                    next.hasAvatar = true;
                else
                    LOG_WARN("Profile: could not decode avatar image: " + avatar);
            }
        }

        std::lock_guard<std::mutex> lk(g_mu);
        g_info = next;
        g_pixels = std::move(pixels);
        // A previously uploaded texture stays valid until replaced in avatarTexture().
    }

    Info info()
    {
        std::lock_guard<std::mutex> lk(g_mu);
        return g_info;
    }

    ImTextureID avatarTexture()
    {
        std::lock_guard<std::mutex> lk(g_mu);

        if (!g_info.hasAvatar)
            return (ImTextureID)(intptr_t)0;

        if (!g_pixels.empty())
        {
            // Save the GL state we touch: the game owns this context.
            GLint prevTex = 0, prevAlign = 4, prevRow = 0, prevSkipPx = 0, prevSkipRows = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRow);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &prevSkipPx);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &prevSkipRows);

            if (g_tex == 0) glGenTextures(1, &g_tex);
            glBindTexture(GL_TEXTURE_2D, g_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAvatarPx, kAvatarPx, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, g_pixels.data());

            glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRow);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, prevSkipPx);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, prevSkipRows);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));

            g_pixels.clear();
            g_pixels.shrink_to_fit();
        }

        if (g_tex == 0) return (ImTextureID)(intptr_t)0;
        return (ImTextureID)(intptr_t)g_tex;
    }
}
