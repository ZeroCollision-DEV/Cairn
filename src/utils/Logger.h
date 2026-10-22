#pragma once
#include <Windows.h>
#include <fstream>
#include <string>
#include <ctime>
#include <mutex>

namespace Logger
{
    inline std::recursive_mutex g_logMutex;
    inline std::ofstream g_logFile;
    inline bool g_initialized = false;
    inline std::string g_logPath;

    inline void writeLine(const std::string& msg)
    {
        std::lock_guard<std::recursive_mutex> lock(g_logMutex);
        if (!g_initialized) return;
        time_t now = time(nullptr);
        tm tmv{};
        localtime_s(&tmv, &now);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
        g_logFile << "[" << ts << "] " << msg << "\n";
        g_logFile.flush();
    }

    inline void init()
    {
        std::lock_guard<std::recursive_mutex> lock(g_logMutex);
        if (g_initialized) return;

        // Write the log to %TEMP% so it's always findable
        char tempPath[MAX_PATH] = {};
        DWORD len = GetTempPathA(MAX_PATH, tempPath);
        if (len == 0 || len > MAX_PATH)
        {
            // Fallback to the executable's directory
            GetModuleFileNameA(nullptr, tempPath, MAX_PATH);
            char* slash = strrchr(tempPath, '\\');
            if (slash) slash[1] = '\0';
        }
        g_logPath = std::string(tempPath) + "cairn.log";
        g_logFile.open(g_logPath, std::ios::out | std::ios::trunc);
        g_initialized = true;
        writeLine("[Logger] Initialized. Log at: " + g_logPath);
    }

    inline const std::string& getLogPath() { return g_logPath; }

    inline void log(const std::string& msg)    { writeLine(msg); }
    inline void info(const std::string& msg)  { writeLine("[INFO]  " + msg); }
    inline void warn(const std::string& msg)  { writeLine("[WARN]  " + msg); }
    inline void error(const std::string& msg) { writeLine("[ERROR] " + msg); }
}

#define LOG_INFO(msg)  Logger::info(msg)
#define LOG_WARN(msg)  Logger::warn(msg)
#define LOG_ERROR(msg) Logger::error(msg)
