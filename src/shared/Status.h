#pragma once
// Client -> injector initialization report.
//
// The injector can only see that LoadLibrary returned; it cannot see whether the client
// got as far as hooking the renderer or finding Minecraft. So the DLL publishes a small
// status block in a named file mapping, `Local\CairnStatus.<pid>`, and the injector reads
// it. Plain data only: no pointers, no strings that outlive the process.
//
// The mapping is created by the DLL inside the game process and stays open until the DLL
// unloads (or the game exits). The injector opens it read-only and never writes to it.

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Status
{
    inline constexpr uint32_t kMagic   = 0x4E524143;   // "CARN"
    inline constexpr uint32_t kVersion = 1;

    enum class State : uint32_t
    {
        None          = 0,
        Loaded        = 1,   // DllMain ran, init thread started
        HookInstalled = 2,   // wglSwapBuffers hook is in place
        JvmAttached   = 3,   // JNI/JVMTI attached, still resolving MinecraftClient
        Ready         = 4,   // MinecraftClient resolved and modules registered
        Limited       = 5,   // hook is up but the JVM / MinecraftClient was unavailable
        Failed        = 6,   // a required step failed (see detail)
        Unloading     = 7,
    };

    enum Flags : uint32_t
    {
        FlagFirstFrame = 1u << 0,   // the render hook has drawn at least one frame
        FlagTickHook   = 1u << 1,   // JVMTI tick breakpoint is active
    };

    struct Block
    {
        uint32_t magic;
        uint32_t version;
        uint32_t pid;
        uint32_t state;        // State
        uint32_t flags;        // Flags
        uint32_t modules;      // registered module count
        uint64_t heartbeatMs;  // GetTickCount64() of the last update; proves the init thread is alive
        char     clientVersion[16];
        char     detail[112];  // short human-readable note for the failure / limited states
    };
    static_assert(sizeof(Block) <= 4096, "status block must fit one page");

    inline void mappingName(DWORD pid, wchar_t* out, size_t n)
    {
        swprintf_s(out, n, L"Local\\CairnStatus.%lu", static_cast<unsigned long>(pid));
    }

    // ── Writer (client side) ─────────────────────────────────
    class Publisher
    {
    public:
        bool open(const char* clientVersion)
        {
            if (m_view) return true;
            wchar_t name[64];
            mappingName(GetCurrentProcessId(), name, 64);
            m_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, name);
            if (!m_map) return false;
            m_view = static_cast<Block*>(MapViewOfFile(m_map, FILE_MAP_WRITE, 0, 0, sizeof(Block)));
            if (!m_view) { CloseHandle(m_map); m_map = nullptr; return false; }
            Block b{};
            b.magic = kMagic;
            b.version = kVersion;
            b.pid = GetCurrentProcessId();
            b.heartbeatMs = GetTickCount64();
            strncpy_s(b.clientVersion, clientVersion, _TRUNCATE);
            std::memcpy(const_cast<Block*>(m_view), &b, sizeof(b));
            return true;
        }

        void set(State s, uint32_t modules = 0, const char* detail = nullptr)
        {
            if (!m_view) return;
            if (detail) strncpy_s(const_cast<char*>(m_view->detail), sizeof(m_view->detail), detail, _TRUNCATE);
            m_view->modules = modules;
            m_view->heartbeatMs = GetTickCount64();
            MemoryBarrier();
            m_view->state = static_cast<uint32_t>(s);   // published last
        }

        void addFlag(Flags f)
        {
            if (!m_view) return;
            InterlockedOr(reinterpret_cast<volatile LONG*>(&m_view->flags), static_cast<LONG>(f));
        }

        void beat()
        {
            if (m_view) m_view->heartbeatMs = GetTickCount64();
        }

        void close()
        {
            if (m_view) { UnmapViewOfFile(const_cast<Block*>(m_view)); m_view = nullptr; }
            if (m_map)  { CloseHandle(m_map); m_map = nullptr; }
        }

    private:
        HANDLE         m_map  = nullptr;
        volatile Block* m_view = nullptr;
    };

    // ── Reader (injector side) ───────────────────────────────
    // Returns false if the target process has no (valid) status mapping.
    inline bool read(DWORD pid, Block& out)
    {
        wchar_t name[64];
        mappingName(pid, name, 64);
        HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (!h) return false;
        const void* v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(Block));
        bool ok = false;
        if (v)
        {
            std::memcpy(&out, v, sizeof(Block));
            ok = out.magic == kMagic && out.version == kVersion && out.pid == pid;
            UnmapViewOfFile(v);
        }
        CloseHandle(h);
        return ok;
    }
}
