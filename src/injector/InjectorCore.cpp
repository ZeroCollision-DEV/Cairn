#include "InjectorCore.h"
#include "../shared/Brand.h"
#include "../shared/Ini.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace inj
{
    // ════════════════════════════════════════════════════════
    //  Small helpers
    // ════════════════════════════════════════════════════════
    namespace
    {
        std::wstring widen(const std::string& s)
        {
            if (s.empty()) return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (n <= 0) return {};
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
            return w;
        }

        std::string narrow(const std::wstring& w)
        {
            if (w.empty()) return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
            if (n <= 0) return {};
            std::string s(static_cast<size_t>(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
            return s;
        }

        std::string pathUtf8(const fs::path& p) { return narrow(p.wstring()); }

        std::wstring lowerW(std::wstring s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
            return s;
        }

        bool endsWithNoCase(const std::string& s, const std::string& suffix)
        {
            if (s.size() < suffix.size()) return false;
            return ini::lower(s.substr(s.size() - suffix.size())) == suffix;
        }

        // A bare file name ending in .dll, no separators / reserved characters.
        bool isSafeDllName(const std::string& name, std::string* why)
        {
            auto bad = [&](const char* m) { if (why) *why = m; return false; };
            if (name.empty()) return bad("The configured DLL file name is empty.");
            if (name.size() > 100) return bad("The configured DLL file name is too long.");
            if (name.find_first_of("\\/:*?\"<>|") != std::string::npos)
                return bad("The configured DLL file name must not contain path separators or reserved characters.");
            if (name == "." || name == "..") return bad("The configured DLL file name is invalid.");
            if (!endsWithNoCase(name, ".dll")) return bad("The configured DLL file name must end in .dll.");
            return true;
        }

        std::string winErrorText(DWORD code)
        {
            wchar_t* buf = nullptr;
            const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                           nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
            std::string out = (n && buf) ? narrow(std::wstring(buf, n)) : std::string();
            if (buf) LocalFree(buf);
            while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ')) out.pop_back();
            if (out.empty()) out = "error " + std::to_string(code);
            return out;
        }

        std::string winHttpErrorText(DWORD code)
        {
            switch (code)
            {
            case ERROR_WINHTTP_NAME_NOT_RESOLVED:
                return "Could not resolve the server name. Check the URL and your internet connection.";
            case ERROR_WINHTTP_CANNOT_CONNECT:
                return "Could not connect to the server. Check your internet connection or firewall.";
            case ERROR_WINHTTP_TIMEOUT:
                return "The connection timed out.";
            case ERROR_WINHTTP_CONNECTION_ERROR:
                return "The connection to the server was interrupted.";
            case ERROR_WINHTTP_SECURE_FAILURE:
            case ERROR_WINHTTP_SECURE_INVALID_CERT:
            case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
            case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
            case ERROR_WINHTTP_SECURE_INVALID_CA:
                return "The server's TLS certificate could not be verified. The download was refused.";
            case ERROR_WINHTTP_INVALID_URL:
            case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
                return "The download URL is not valid.";
            case ERROR_WINHTTP_REDIRECT_FAILED:
                return "The server redirected the request in a way that could not be followed.";
            default:
                return "Network error: " + winErrorText(code) + " (code " + std::to_string(code) + ").";
            }
        }
    }

    std::string formatBytes(std::uint64_t b)
    {
        char buf[48];
        if (b < 1024ull) std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(b));
        else if (b < 1024ull * 1024ull) std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
        else std::snprintf(buf, sizeof(buf), "%.2f MB", b / (1024.0 * 1024.0));
        return buf;
    }

    std::string normalizeSha256(const std::string& text)
    {
        std::string out;
        for (unsigned char c : text)
        {
            if (std::isspace(c) || c == ':') continue;
            out.push_back(static_cast<char>(std::tolower(c)));
        }
        if (out.size() != 64) return {};
        for (char c : out)
            if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        return out;
    }

    // ════════════════════════════════════════════════════════
    //  Configuration
    // ════════════════════════════════════════════════════════
    namespace
    {
        std::string renderConfig(const Config& c)
        {
            std::ostringstream o;
            o << "; Cairn Injector configuration\n"
                 "; Lines starting with ';' are comments. Edit this file with any text editor.\n\n"
                 "[release]\n"
                 "; Official HTTPS URL of the Cairn client DLL, a GitHub release asset:\n"
                 ";   https://github.com/<owner>/<repo>/releases/download/<tag>/cairn.dll\n"
                 "; Leave empty until the release exists: nothing is downloaded without it.\n"
              << "url=" << c.releaseUrl << "\n"
                 "; Trusted SHA-256 of that exact DLL (64 hex characters). REQUIRED: the injector\n"
                 "; will not load a DLL unless its hash matches. Update it for every new release.\n"
              << "sha256=" << c.expectedSha256 << "\n"
                 "; File name used inside the cache folder (must end in .dll).\n"
              << "filename=" << c.dllFileName << "\n"
                 "; Require a github.com release URL. Set to false only to use another HTTPS host.\n"
              << "strict_github_url=" << (c.strictGithubUrl ? "true" : "false") << "\n\n"
                 "[target]\n"
                 "; Process to look for, and text its window title must contain.\n"
              << "process=" << c.processName << "\n"
              << "window_title_contains=" << c.windowTitleContains << "\n\n"
                 "[developer]\n"
                 "; NOT part of the normal workflow. Path to a locally built DLL (for example\n"
                 "; bin\\cairn.dll, relative to the Cairn folder) to load instead of downloading.\n"
                 "; Leave empty for normal use. A sha256 set above is still enforced.\n"
              << "local_dll=" << c.devLocalDll << "\n";
            return o.str();
        }

        bool writeConfig(const Config& cfg, std::string* error)
        {
            std::error_code ec;
            fs::create_directories(cfg.configDir, ec);
            std::ofstream f(cfg.iniPath, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!f)
            {
                if (error) *error = "Could not write " + pathUtf8(cfg.iniPath);
                return false;
            }
            f << renderConfig(cfg);
            f.flush();
            if (!f)
            {
                if (error) *error = "Writing " + pathUtf8(cfg.iniPath) + " failed.";
                return false;
            }
            return true;
        }

        fs::path localAppDataCache()
        {
            wchar_t buf[MAX_PATH] = {};
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                n = GetEnvironmentVariableW(L"TEMP", buf, MAX_PATH);
            fs::path base = (n > 0 && n < MAX_PATH) ? fs::path(buf) : fs::temp_directory_path();
            return base / L"Cairn" / L"cache";
        }

        // Empty string = acceptable. Otherwise a message explaining what is wrong.
        std::string releaseUrlProblem(const std::string& urlIn, bool strict)
        {
            const std::string url = ini::trim(urlIn);
            if (ini::lower(url.substr(0, 8)) != "https://")
                return "The release URL must start with https:// (plain http is never used).";
            const std::string rest = url.substr(8);
            const size_t slash = rest.find('/');
            const std::string host = ini::lower(rest.substr(0, slash));
            if (host.empty() || host.find('@') != std::string::npos || host.find(' ') != std::string::npos)
                return "The release URL has no valid host name.";
            if (!strict) return {};

            if (host != "github.com")
                return "The release URL must be on github.com (https://github.com/<owner>/<repo>/releases/download/<tag>/<file>.dll). "
                       "Set strict_github_url=false only if you really host the DLL elsewhere.";
            if (url.find_first_of("?#") != std::string::npos)
                return "The release URL must not contain a query string or fragment.";
            std::vector<std::string> seg;
            std::string cur;
            for (char c : slash == std::string::npos ? std::string() : rest.substr(slash + 1))
            {
                if (c == '/') { seg.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            seg.push_back(cur);
            // owner/repo/releases/download/<tag>/<asset>  or  owner/repo/releases/latest/download/<asset>
            const bool tagged = seg.size() == 6 && seg[2] == "releases" && seg[3] == "download";
            const bool latest = seg.size() == 6 && seg[2] == "releases" && seg[3] == "latest" && seg[4] == "download";
            if (!tagged && !latest)
                return "That is not a GitHub release-asset URL. Expected https://github.com/<owner>/<repo>/releases/download/<tag>/<file>.dll";
            for (const auto& s : seg)
                if (s.empty()) return "The release URL contains an empty path segment.";
            if (!endsWithNoCase(seg.back(), ".dll"))
                return "The release asset must be a .dll file.";
            return {};
        }
    }

    ConfigCheck checkConfig(const Config& c)
    {
        ConfigCheck r;
        const std::string hash = ini::trim(c.expectedSha256);
        if (!hash.empty() && normalizeSha256(hash).empty())
        {
            r.state = ConfigState::BadHash;
            r.message = "The configured SHA-256 is not valid (needs 64 hex characters). Fix [release] sha256 in config\\injector.ini.";
            return r;
        }
        if (!ini::trim(c.devLocalDll).empty())
        {
            r.state = ConfigState::DeveloperMode;
            r.message = hash.empty()
                ? "Developer mode: a local build will be loaded and its integrity is NOT pinned."
                : "Developer mode: a local build will be loaded; the configured SHA-256 is enforced.";
            return r;
        }
        if (ini::trim(c.releaseUrl).empty())
        {
            r.state = ConfigState::NoUrl;
            r.message = "No release URL is configured yet. Set [release] url in config\\injector.ini once the official release exists.";
            return r;
        }
        const std::string problem = releaseUrlProblem(c.releaseUrl, c.strictGithubUrl);
        if (!problem.empty())
        {
            r.state = ConfigState::BadUrl;
            r.message = problem;
            return r;
        }
        if (hash.empty())
        {
            r.state = ConfigState::NoHash;
            r.message = "No trusted SHA-256 is configured. Set [release] sha256 in config\\injector.ini - the DLL is never loaded without it.";
            return r;
        }
        r.state = ConfigState::Ready;
        r.message = "Release source configured.";
        return r;
    }

    Config loadConfig(std::string* note)
    {
        Config c;

        wchar_t exe[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
        const fs::path exeDir = fs::path(exe).parent_path();

        // Packaged layout is <root>\bin\CairnInjector.exe with <root>\config next to bin.
        c.rootDir = (lowerW(exeDir.filename().wstring()) == L"bin") ? exeDir.parent_path() : exeDir;
        c.configDir = c.rootDir / L"config";
        c.cacheDir = localAppDataCache();
        c.iniPath = c.configDir / L"injector.ini";

        std::error_code ec;
        if (!fs::exists(c.iniPath, ec))
        {
            std::string err;
            if (!writeConfig(c, &err) && note) *note = "Could not create the default config: " + err;
            return c;
        }

        ini::Map m;
        if (!ini::readFile(c.iniPath, m))
        {
            if (note) *note = "Could not read " + pathUtf8(c.iniPath) + "; using defaults.";
            return c;
        }

        const bool hasRelease = m.count("release.url") || m.count("release.sha256");
        if (!hasRelease)
        {
            // Config from before the rename ([download]/[local]): carry the URL and hash over,
            // keep a copy of the old file, and write the new layout. The old local-DLL path is
            // deliberately not carried over.
            c.releaseUrl = ini::get(m, "download.url");
            c.expectedSha256 = ini::get(m, "download.sha256");
            c.processName = ini::get(m, "target.process", c.processName);
            c.windowTitleContains = ini::get(m, "target.window_title_contains", c.windowTitleContains);
            const fs::path backup = c.iniPath.wstring() + L".pre-cairn";
            if (!fs::exists(backup, ec)) fs::copy_file(c.iniPath, backup, ec);
            std::string err;
            if (writeConfig(c, &err))
            {
                if (note) *note = "Config migrated to the new layout (old copy: injector.ini.pre-cairn).";
            }
            else if (note) *note = "Could not migrate the old config: " + err;
            return c;
        }

        c.releaseUrl = ini::get(m, "release.url");
        c.expectedSha256 = ini::get(m, "release.sha256");
        const std::string fname = ini::get(m, "release.filename", c.dllFileName);
        std::string why;
        if (isSafeDllName(fname, &why)) c.dllFileName = fname;
        else if (note) *note = why + " Using \"" + c.dllFileName + "\" instead.";
        c.strictGithubUrl = ini::getBool(m, "release.strict_github_url", true);
        c.processName = ini::get(m, "target.process", c.processName);
        c.windowTitleContains = ini::get(m, "target.window_title_contains", c.windowTitleContains);
        c.devLocalDll = ini::get(m, "developer.local_dll");
        return c;
    }

    // ════════════════════════════════════════════════════════
    //  Validation
    // ════════════════════════════════════════════════════════
    namespace
    {
        bool sha256File(const fs::path& path, std::string& hex, std::string& err)
        {
            BCRYPT_ALG_HANDLE alg = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            bool ok = false;
            do
            {
                if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
                { err = "Windows SHA-256 provider is unavailable."; break; }

                DWORD objLen = 0, got = 0;
                if (!BCRYPT_SUCCESS(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &got, 0)))
                { err = "Could not initialise SHA-256."; break; }

                std::vector<UCHAR> obj(objLen);
                if (!BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0)))
                { err = "Could not initialise SHA-256."; break; }

                std::ifstream f(path, std::ios::in | std::ios::binary);
                if (!f) { err = "Could not open the file for hashing."; break; }

                std::vector<char> buf(1 << 20);
                bool readOk = true;
                while (f)
                {
                    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                    const std::streamsize n = f.gcount();
                    if (n <= 0) break;
                    if (!BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()),
                                                       static_cast<ULONG>(n), 0)))
                    { readOk = false; break; }
                }
                if (!readOk || (f.bad())) { err = "Reading the file failed while hashing."; break; }

                UCHAR digest[32] = {};
                if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0)))
                { err = "Could not finish SHA-256."; break; }

                static const char* kHex = "0123456789abcdef";
                hex.clear();
                for (UCHAR b : digest) { hex.push_back(kHex[b >> 4]); hex.push_back(kHex[b & 0xF]); }
                ok = true;
            } while (false);

            if (hash) BCryptDestroyHash(hash);
            if (alg) BCryptCloseAlgorithmProvider(alg, 0);
            return ok;
        }
    }

    Validation validateDll(const fs::path& file, const std::string& expectedRaw, bool requireHash)
    {
        Validation v;
        std::error_code ec;

        if (!fs::exists(file, ec) || !fs::is_regular_file(file, ec))
        {
            v.message = "File not found: " + pathUtf8(file);
            return v;
        }
        v.sizeBytes = fs::file_size(file, ec);
        if (ec) { v.message = "Could not read the file size: " + ec.message(); return v; }
        if (v.sizeBytes < 4096)
        {
            v.message = "The file is only " + formatBytes(v.sizeBytes) + " - too small to be a valid DLL. "
                        "The download may be incomplete or an error page.";
            return v;
        }

        // PE header checks
        {
            std::ifstream f(file, std::ios::in | std::ios::binary);
            if (!f) { v.message = "Could not open the file for reading."; return v; }
            std::vector<unsigned char> head(1 << 16);
            f.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<size_t>(f.gcount()));

            if (head.size() < sizeof(IMAGE_DOS_HEADER) ||
                reinterpret_cast<IMAGE_DOS_HEADER*>(head.data())->e_magic != IMAGE_DOS_SIGNATURE)
            { v.message = "Not a Windows executable (missing MZ header)."; return v; }

            const LONG off = reinterpret_cast<IMAGE_DOS_HEADER*>(head.data())->e_lfanew;
            if (off <= 0 || static_cast<size_t>(off) + sizeof(IMAGE_NT_HEADERS64) > head.size())
            { v.message = "Corrupt or truncated PE header."; return v; }

            const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(head.data() + off);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
            { v.message = "Corrupt PE header (bad signature)."; return v; }
            if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                char b[96];
                std::snprintf(b, sizeof(b), "The DLL is not 64-bit (machine type 0x%04X). Minecraft needs an x64 build.",
                              static_cast<unsigned>(nt->FileHeader.Machine));
                v.message = b;
                v.wrongArch = true;
                return v;
            }
            if (!(nt->FileHeader.Characteristics & IMAGE_FILE_DLL))
            { v.message = "This is an executable, not a DLL."; return v; }
        }

        std::string err;
        if (!sha256File(file, v.sha256, err)) { v.message = err; return v; }

        const std::string trimmed = ini::trim(expectedRaw);
        if (trimmed.empty() && requireHash)
        {
            v.message = "No trusted SHA-256 is configured, so this DLL cannot be verified and will not be loaded.";
            return v;
        }
        const std::string shortHash = v.sha256.substr(0, 12);
        if (!trimmed.empty())
        {
            const std::string expected = normalizeSha256(trimmed);
            if (expected.empty())
            {
                v.message = "The expected SHA-256 is not valid (need 64 hex characters). Fix or clear it.";
                return v;
            }
            if (expected != v.sha256)
            {
                v.hash = HashState::Mismatch;
                v.message = "SHA-256 mismatch - the file is not the expected build.\nExpected " +
                            expected.substr(0, 16) + "...  got " + v.sha256.substr(0, 16) + "...";
                return v;
            }
            v.hash = HashState::Verified;
            v.ok = true;
            v.message = "Valid x64 DLL (" + formatBytes(v.sizeBytes) + "). SHA-256 matches the expected value.";
            return v;
        }

        v.hash = HashState::NotChecked;
        v.ok = true;
        v.message = "Valid x64 DLL (" + formatBytes(v.sizeBytes) + ", SHA-256 " + shortHash +
                    "...). Integrity NOT verified: no expected hash is set.";
        return v;
    }

    // ════════════════════════════════════════════════════════
    //  Download (WinHTTP, HTTPS only)
    // ════════════════════════════════════════════════════════
    namespace
    {
        struct HInet
        {
            HINTERNET h = nullptr;
            ~HInet() { if (h) WinHttpCloseHandle(h); }
        };
    }

    DownloadResult downloadFile(const std::string& url, const fs::path& destDir,
                                const std::string& fileName, Progress& prog)
    {
        DownloadResult r;
        prog.received = 0;
        prog.total = 0;

        std::string why;
        if (!isSafeDllName(fileName, &why)) { r.error = why; return r; }

        const std::string trimmedUrl = ini::trim(url);
        if (trimmedUrl.empty()) { r.error = "No download URL is set."; return r; }

        const std::wstring wurl = widen(trimmedUrl);
        URL_COMPONENTSW uc = {};
        wchar_t host[256] = {}, path[4096] = {}, extra[2048] = {};
        uc.dwStructSize = sizeof(uc);
        uc.lpszHostName = host;      uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path;       uc.dwUrlPathLength = 4096;
        uc.lpszExtraInfo = extra;    uc.dwExtraInfoLength = 2048;
        uc.dwSchemeLength = static_cast<DWORD>(-1);
        if (wurl.empty() || !WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || host[0] == L'\0')
        { r.error = "The download URL is not valid. It should look like https://example.com/path/cairn.dll"; return r; }
        if (uc.nScheme != INTERNET_SCHEME_HTTPS)
        { r.error = "Only https:// download URLs are supported (the URL must start with https://)."; return r; }

        std::error_code ec;
        fs::create_directories(destDir, ec);
        if (ec) { r.error = "Could not create the cache folder " + pathUtf8(destDir) + ": " + ec.message(); return r; }
        const fs::path tmp = destDir / (widen(fileName) + L".download");

        const std::wstring ua = L"CairnInjector/" + widen(Brand::kVersion);
        HInet session;
        session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session.h)
            session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session.h) { r.error = winHttpErrorText(GetLastError()); return r; }
        WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 30000);
        {
            DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            proto |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));
            DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;   // never downgrade to http
            WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        }

        HInet conn;
        conn.h = WinHttpConnect(session.h, host, uc.nPort, 0);
        if (!conn.h) { r.error = winHttpErrorText(GetLastError()); return r; }

        const std::wstring object = std::wstring(path) + extra;
        HInet req;
        req.h = WinHttpOpenRequest(conn.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!req.h) { r.error = winHttpErrorText(GetLastError()); return r; }

        if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req.h, nullptr))
        { r.error = winHttpErrorText(GetLastError()); return r; }

        DWORD status = 0, sz = sizeof(status);
        if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX))
        { r.error = "The server sent an unreadable response."; return r; }
        if (status != 200)
        {
            r.error = "The server answered with HTTP " + std::to_string(status) + ".";
            if (status == 404) r.error += " The file was not found - check the URL.";
            else if (status == 401 || status == 403) r.error += " Access to the file was refused.";
            return r;
        }

        wchar_t lenBuf[32] = {};
        DWORD lenSz = sizeof(lenBuf);
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                lenBuf, &lenSz, WINHTTP_NO_HEADER_INDEX))
        {
            const unsigned long long total = _wcstoui64(lenBuf, nullptr, 10);
            prog.total = total;
        }

        constexpr std::uint64_t kMaxBytes = 256ull * 1024 * 1024;
        std::ofstream out(tmp, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!out) { r.error = "Could not write to " + pathUtf8(tmp) + "."; return r; }

        auto abort = [&](const std::string& msg)
        {
            out.close();
            std::error_code e2;
            fs::remove(tmp, e2);
            r.error = msg;
            return r;
        };

        std::vector<char> buf(64 * 1024);
        for (;;)
        {
            if (prog.cancel.load()) return abort("Download cancelled.");

            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req.h, &avail))
                return abort(winHttpErrorText(GetLastError()));
            if (avail == 0) break;

            DWORD got = 0;
            const DWORD want = avail < buf.size() ? avail : static_cast<DWORD>(buf.size());
            if (!WinHttpReadData(req.h, buf.data(), want, &got))
                return abort(winHttpErrorText(GetLastError()));
            if (got == 0) break;

            out.write(buf.data(), got);
            if (!out) return abort("Writing the downloaded file failed (disk full or no permission).");

            prog.received += got;
            if (prog.received.load() > kMaxBytes)
                return abort("The download is larger than the 256 MB safety limit and was stopped.");
        }
        out.close();

        const std::uint64_t got = prog.received.load();
        const std::uint64_t expect = prog.total.load();
        if (got == 0) return abort("The server returned an empty file.");
        if (expect != 0 && got != expect)
            return abort("The download is incomplete (received " + formatBytes(got) + " of " + formatBytes(expect) + ").");

        r.ok = true;
        r.file = tmp;
        r.bytes = got;
        return r;
    }

    // ════════════════════════════════════════════════════════
    //  Cache (staging, reuse, cleanup)
    // ════════════════════════════════════════════════════════
    namespace
    {
        // "<stem>.dll", "<stem>.dll.download" (interrupted download) and "<stem>.<8 hex>.dll"
        // (fallback name when the primary file is locked). `n` is lower-case.
        bool isCairnCacheName(const std::wstring& n, const std::wstring& stem)
        {
            if (n == stem + L".dll" || n == stem + L".dll.download") return true;
            const std::wstring pre = stem + L".";
            if (n.size() == pre.size() + 8 + 4 && n.compare(0, pre.size(), pre) == 0 &&
                n.compare(n.size() - 4, 4, L".dll") == 0)
            {
                for (size_t i = pre.size(); i < pre.size() + 8; i++)
                    if (!std::iswxdigit(n[i])) return false;
                return true;
            }
            return false;
        }

        std::wstring stemOf(const std::string& dllFileName)
        {
            return lowerW(fs::path(widen(dllFileName)).stem().wstring());
        }
    }

    StagedDll stageDll(const Config& cfg, const fs::path& temp, const std::string& sha256)
    {
        StagedDll r;
        if (sha256.size() < 8) { r.error = "Internal error: missing SHA-256 for staging."; return r; }

        const std::wstring primary = widen(cfg.dllFileName);
        const std::wstring fallback = fs::path(primary).stem().wstring() + L"." + widen(sha256.substr(0, 8)) + L".dll";
        const fs::path candidates[2] = { cfg.cacheDir / primary, cfg.cacheDir / fallback };

        DWORD lastErr = 0;
        for (int i = 0; i < 2; i++)
        {
            if (MoveFileExW(temp.c_str(), candidates[i].c_str(), MOVEFILE_REPLACE_EXISTING))
            {
                r.ok = true;
                r.path = candidates[i];
                r.usedFallbackName = (i == 1);
                return r;
            }
            lastErr = GetLastError();

            // Locked by a running game: reuse it when it is byte-identical (same SHA-256).
            std::error_code ec;
            std::string h, err;
            if (fs::exists(candidates[i], ec) && sha256File(candidates[i], h, err) && h == sha256)
            {
                fs::remove(temp, ec);
                r.ok = true;
                r.path = candidates[i];
                r.usedFallbackName = (i == 1);
                return r;
            }
        }
        r.error = "Could not save the validated DLL into the cache (" + pathUtf8(cfg.cacheDir) + "): " +
                  winErrorText(lastErr) + ". Close other Minecraft instances that still have an old Cairn loaded and try again.";
        return r;
    }

    bool copyToTemp(const Config& cfg, const fs::path& source, fs::path* temp, std::string* error)
    {
        std::error_code ec;
        fs::create_directories(cfg.cacheDir, ec);
        if (ec) { if (error) *error = "Could not create the cache folder: " + ec.message(); return false; }
        const fs::path dst = cfg.cacheDir / (widen(cfg.dllFileName) + L".download");
        if (!fs::exists(source, ec) || !fs::is_regular_file(source, ec))
        { if (error) *error = "The developer DLL was not found: " + pathUtf8(source); return false; }
        fs::copy_file(source, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { if (error) *error = "Could not copy the developer DLL into the cache: " + ec.message(); return false; }
        if (temp) *temp = dst;
        return true;
    }

    CleanupReport cleanCache(const Config& cfg, const fs::path& keep)
    {
        CleanupReport rep;
        const std::wstring stem = stemOf(cfg.dllFileName);

        std::vector<fs::path> victims;
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((cfg.cacheDir / L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return rep;
        do
        {
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if (isCairnCacheName(lowerW(fd.cFileName), stem))
                victims.push_back(cfg.cacheDir / fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);

        for (const fs::path& v : victims)
        {
            if (!keep.empty() && _wcsicmp(v.c_str(), keep.c_str()) == 0) continue;
            if (DeleteFileW(v.c_str())) rep.removed++;
            else rep.kept++;   // mapped by a running game (or otherwise busy): retried next run
        }
        return rep;
    }

    // ════════════════════════════════════════════════════════
    //  Target detection
    // ════════════════════════════════════════════════════════
    namespace
    {
        unsigned long long startTimeOf(HANDLE proc)
        {
            FILETIME created = {}, exited = {}, kernel = {}, user = {};
            if (!GetProcessTimes(proc, &created, &exited, &kernel, &user)) return 0;
            return (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        }

        // "Minecraft* 1.21.11 - Singleplayer" -> "1.21.11". Only what the window title literally says;
        // launchers and mods can change the title, so callers must not treat this as authoritative.
        std::string versionFromTitle(const std::wstring& title)
        {
            const std::wstring low = lowerW(title);
            size_t i = low.find(L"minecraft");
            if (i == std::wstring::npos) return {};
            i += 9;
            while (i < title.size() && (title[i] == L'*' || title[i] == L' ')) i++;
            size_t j = i;
            bool dot = false;
            while (j < title.size() && (iswdigit(title[j]) || title[j] == L'.'))
            {
                if (title[j] == L'.') dot = true;
                j++;
            }
            if (j == i || !dot || !iswdigit(title[i])) return {};
            return narrow(title.substr(i, j - i));
        }
    }

    std::vector<Target> findTargets(const Config& cfg)
    {
        std::vector<Target> out;
        const std::wstring procName = widen(cfg.processName);
        if (procName.empty()) return out;

        std::set<DWORD> pids;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe))
            do
            {
                if (_wcsicmp(pe.szExeFile, procName.c_str()) == 0) pids.insert(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        CloseHandle(snap);
        if (pids.empty()) return out;

        struct Ctx
        {
            const std::set<DWORD>* pids;
            std::wstring needle;
            std::map<DWORD, std::wstring> titles;
        } ctx{ &pids, lowerW(widen(cfg.windowTitleContains)), {} };

        EnumWindows(+[](HWND hw, LPARAM lp) -> BOOL
        {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (!IsWindowVisible(hw)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hw, &pid);
            if (!c->pids->count(pid)) return TRUE;
            wchar_t t[512] = {};
            const int n = GetWindowTextW(hw, t, 512);
            if (n <= 0) return TRUE;
            const std::wstring title(t, static_cast<size_t>(n));
            if (c->needle.empty() || lowerW(title).find(c->needle) != std::wstring::npos)
                if (!c->titles.count(pid)) c->titles[pid] = title;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        for (DWORD pid : pids)
        {
            Target t;
            t.pid = pid;
            t.exeName = cfg.processName;
            auto it = ctx.titles.find(pid);
            t.titleMatched = (it != ctx.titles.end());
            t.title = t.titleMatched ? narrow(it->second) : std::string();
            if (!t.titleMatched)
                t.kind = TargetKind::NoWindow;
            else if (lowerW(it->second).find(L"launcher") != std::wstring::npos)
                t.kind = TargetKind::Launcher;
            else
                t.kind = TargetKind::Game;
            if (t.kind == TargetKind::Game) t.version = versionFromTitle(it->second);

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h)
            {
                t.accessible = false;
                t.note = "Cannot open this process (run the injector as the same user, or as administrator).";
            }
            else
            {
                BOOL wow = FALSE;
                if (IsWow64Process(h, &wow) && wow)
                {
                    t.is64 = false;
                    t.note = "32-bit process - the client DLL is 64-bit only.";
                }
                wchar_t img[MAX_PATH * 2] = {};
                DWORD len = static_cast<DWORD>(sizeof(img) / sizeof(img[0]));
                if (QueryFullProcessImageNameW(h, 0, img, &len)) t.exePath = narrow(std::wstring(img, len));
                t.startTime = startTimeOf(h);
                CloseHandle(h);
            }
            if (t.note.empty())
            {
                if (t.kind == TargetKind::Launcher) t.note = "Launcher window, not the game.";
                else if (t.kind == TargetKind::NoWindow) t.note = "No Minecraft window yet.";
            }
            Status::Block sb{};
            if (Status::read(pid, sb))
            {
                t.clientPresent = true;
                t.clientState = static_cast<Status::State>(sb.state);
            }
            out.push_back(std::move(t));
        }

        std::stable_sort(out.begin(), out.end(), [](const Target& a, const Target& b)
        {
            if (a.kind != b.kind) return a.kind < b.kind;   // games, then launchers, then windowless
            return a.pid < b.pid;
        });
        return out;
    }

    bool revalidateTarget(const Config& cfg, unsigned long pid, unsigned long long startTime,
                          Target* out, std::string* why)
    {
        auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
        for (const Target& t : findTargets(cfg))
        {
            if (t.pid != pid) continue;
            if (startTime != 0 && t.startTime != 0 && t.startTime != startTime)
                return fail("The selected Minecraft instance closed, and another process now uses its ID. Refresh and select again.");
            if (!t.usable())
                return fail(t.note.empty() ? "The selected process cannot be used." : t.note);
            if (out) *out = t;
            return true;
        }
        return fail("The selected Minecraft instance is no longer running.");
    }

    // ════════════════════════════════════════════════════════
    //  Loading
    // ════════════════════════════════════════════════════════
    namespace
    {
        // Returns the loaded module's full path if a module named `dllName` is in the process.
        bool findModule(DWORD pid, const std::wstring& dllName, std::wstring* fullPath)
        {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
            if (snap == INVALID_HANDLE_VALUE) return false;
            MODULEENTRY32W me = {};
            me.dwSize = sizeof(me);
            bool found = false;
            if (Module32FirstW(snap, &me))
                do
                {
                    if (_wcsicmp(me.szModule, dllName.c_str()) == 0)
                    {
                        found = true;
                        if (fullPath) *fullPath = me.szExePath;
                        break;
                    }
                } while (Module32NextW(snap, &me));
            CloseHandle(snap);
            return found;
        }

        // Same, but matches the cairn module family ("cairn.dll", "cairn.<8 hex>.dll").
        bool findModuleStem(DWORD pid, const std::wstring& stem, std::wstring* fullPath)
        {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
            if (snap == INVALID_HANDLE_VALUE) return false;
            MODULEENTRY32W me = {};
            me.dwSize = sizeof(me);
            bool found = false;
            if (Module32FirstW(snap, &me))
                do
                {
                    if (isCairnCacheName(lowerW(me.szModule), stem))
                    {
                        found = true;
                        if (fullPath) *fullPath = me.szExePath;
                        break;
                    }
                } while (Module32NextW(snap, &me));
            CloseHandle(snap);
            return found;
        }
    }

    LoadResult loadDllIntoProcess(unsigned long pid, const fs::path& dllIn, const std::string& moduleStem,
                                  unsigned long long expectedStartTime)
    {
        LoadResult r;

        std::error_code ec;
        const fs::path dll = fs::absolute(dllIn, ec);
        if (ec || !fs::exists(dll))
        { r.message = "The DLL file no longer exists: " + pathUtf8(dllIn); return r; }

        const std::wstring dllName = dll.filename().wstring();

        std::wstring existing;
        if (findModuleStem(pid, lowerW(widen(moduleStem)), &existing))
        {
            r.alreadyLoaded = true;
            r.message = "Cairn is already loaded in this process (" + narrow(existing) + "). Nothing was injected.";
            return r;
        }

        HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                  PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
        if (!proc)
        {
            const DWORD e = GetLastError();
            if (e == ERROR_ACCESS_DENIED)
                r.message = "Access denied opening the Minecraft process. Run Cairn Injector as the same user that started "
                            "the game (as administrator if the game runs elevated).";
            else if (e == ERROR_INVALID_PARAMETER)
                r.message = "The Minecraft process is no longer running.";
            else
                r.message = "Could not open the Minecraft process: " + winErrorText(e) + ".";
            return r;
        }

        BOOL wow = FALSE;
        if (IsWow64Process(proc, &wow) && wow)
        {
            CloseHandle(proc);
            r.message = "The target is a 32-bit process; the client DLL is 64-bit only.";
            return r;
        }
        if (expectedStartTime != 0)
        {
            const unsigned long long now = startTimeOf(proc);
            if (now != 0 && now != expectedStartTime)
            {
                CloseHandle(proc);
                r.message = "The selected Minecraft instance closed and its process ID was reused. Nothing was injected.";
                return r;
            }
        }

        const std::wstring wpath = dll.wstring();
        const SIZE_T bytes = (wpath.size() + 1) * sizeof(wchar_t);

        LPVOID remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote)
        {
            r.message = "Could not allocate memory in the target process: " + winErrorText(GetLastError()) + ".";
            CloseHandle(proc);
            return r;
        }

        if (!WriteProcessMemory(proc, remote, wpath.c_str(), bytes, nullptr))
        {
            r.message = "Could not write to the target process: " + winErrorText(GetLastError()) + ".";
            VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
            CloseHandle(proc);
            return r;
        }

        // kernel32 is mapped at the same address in every x64 process of a boot session.
        const auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (!loadLib)
        {
            r.message = "Could not resolve LoadLibraryW.";
            VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
            CloseHandle(proc);
            return r;
        }

        HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
        if (!thread)
        {
            r.message = "Could not start the loader thread in the target process: " + winErrorText(GetLastError()) + ".";
            VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
            CloseHandle(proc);
            return r;
        }

        const DWORD wait = WaitForSingleObject(thread, 15000);
        DWORD exitCode = 0;
        if (wait == WAIT_OBJECT_0) GetExitCodeThread(thread, &exitCode);
        CloseHandle(thread);

        if (wait != WAIT_OBJECT_0)
        {
            // Leave the remote buffer allocated: the thread may still be using it.
            r.message = "Timed out waiting for the game to load the DLL (15 s). The game may be frozen or busy.";
            CloseHandle(proc);
            return r;
        }
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);

        if (exitCode == 0)
        {
            r.message = "LoadLibrary failed inside the game (returned null). Common causes: a missing dependency, "
                        "a blocked or quarantined file (antivirus), or the DLL is not compatible with this process.";
            return r;
        }

        // Do not trust the exit code alone: confirm the module is really there.
        for (int i = 0; i < 15; i++)
        {
            std::wstring loadedPath;
            if (findModule(pid, dllName, &loadedPath))
            {
                r.ok = true;
                r.message = narrow(dllName) + " is loaded in process " + std::to_string(pid) + ".";
                return r;
            }
            Sleep(200);
        }
        r.message = "LoadLibrary reported success, but the module could not be confirmed in the target process.";
        return r;
    }

    // ════════════════════════════════════════════════════════
    //  Initialization check
    // ════════════════════════════════════════════════════════
    namespace
    {
        std::string describeStep(const Status::Block& b)
        {
            switch (static_cast<Status::State>(b.state))
            {
            case Status::State::Loaded:        return "loaded, installing the renderer hook";
            case Status::State::HookInstalled: return "renderer hook installed, attaching to the JVM";
            case Status::State::JvmAttached:   return "JVM attached, looking up Minecraft";
            case Status::State::Ready:         return "ready";
            case Status::State::Limited:       return "limited";
            case Status::State::Failed:        return "failed";
            case Status::State::Unloading:     return "unloading";
            default:                           return "starting";
            }
        }
    }

    InitResult waitForClientInit(unsigned long pid, unsigned timeoutMs, const std::atomic<bool>& cancel,
                                 const std::function<void(const Status::Block&)>& onState)
    {
        InitResult r;
        HANDLE proc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));

        const ULONGLONG start = GetTickCount64();
        ULONGLONG readySince = 0;
        std::uint32_t lastState = 0xFFFFFFFFu, lastFlags = 0xFFFFFFFFu;

        for (;;)
        {
            if (cancel.load()) { r.outcome = InitOutcome::Cancelled; r.message = "Cancelled."; break; }
            if (proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0)
            { r.outcome = InitOutcome::ProcessExited; r.message = "Minecraft closed before Cairn finished starting."; break; }

            Status::Block b{};
            if (Status::read(static_cast<DWORD>(pid), b))
            {
                r.block = b;
                r.haveBlock = true;
                if (b.state != lastState || b.flags != lastFlags)
                {
                    lastState = b.state;
                    lastFlags = b.flags;
                    if (onState) onState(b);
                }

                const auto st = static_cast<Status::State>(b.state);
                if (st == Status::State::Failed)
                {
                    r.outcome = InitOutcome::ClientFailed;
                    r.message = std::string("The client reported an error: ") + (b.detail[0] ? b.detail : "initialization failed") + ".";
                    break;
                }
                if (st == Status::State::Limited)
                {
                    r.outcome = InitOutcome::Limited;
                    r.message = std::string("Cairn loaded, but it could not reach Minecraft: ") +
                                (b.detail[0] ? b.detail : "the JVM was not available") + ".";
                    break;
                }
                if (st == Status::State::Unloading)
                {
                    r.outcome = InitOutcome::ClientFailed;
                    r.message = "The client is unloading.";
                    break;
                }
                if (st == Status::State::Ready)
                {
                    if (readySince == 0) readySince = GetTickCount64();
                    const bool frame = (b.flags & Status::FlagFirstFrame) != 0;
                    // Give the render hook a moment to draw its first frame; it cannot while the
                    // game window is minimised, so do not wait for it forever.
                    if (frame || GetTickCount64() - readySince > 3000)
                    {
                        r.outcome = InitOutcome::Initialized;
                        r.message = "Cairn is running (" + std::to_string(b.modules) + " modules registered" +
                                    std::string(frame ? "" : "; overlay appears on the next rendered frame") + ").";
                        break;
                    }
                }
            }

            if (GetTickCount64() - start > timeoutMs)
            {
                r.outcome = InitOutcome::NoReport;
                if (r.haveBlock)
                    r.message = std::string("Cairn started but did not finish initializing in time (last step: ") +
                                describeStep(r.block) + ").";
                else
                    r.message = "The DLL is loaded, but it never reported that it started. It may be an older build without "
                                "status reporting, or it failed before startup. Check %TEMP%\\cairn.log.";
                break;
            }
            Sleep(150);
        }

        if (proc) CloseHandle(proc);
        return r;
    }
}
