#pragma once
#include <Windows.h>
#include <atomic>
#include <thread>
#include "../shared/Status.h"

class Client
{
public:
    static Client& instance();

    void startThread();  // Called from DllMain — spawns the background thread
    void shutdown();     // Called from DllMain — signals exit + cleans up

    bool isRunning() const { return m_running.load(); }
    // True once startup has finished (modules registered, or JVM init failed).
    // The menu only reads the module registry after this is set.
    bool isReady()   const { return m_ready.load(); }

    void saveOnExit();    // process termination: persist settings only
    void toggleMenu();    // plays the open/close sound
    void onFirstFrame();  // Called from hkSwapBuffers (render thread)

    // For HUD overlay
    const char* hudMessage() const { return m_hudMessage; }

private:
    Client() = default;
    ~Client() = default;

    void run();

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ready{false};
    std::thread       m_thread;
    Status::Publisher m_status;   // init report read by the injector (see shared/Status.h)

    DWORD m_lastTickTime   = 0;
    DWORD m_tickAccumulator = 0;

    char  m_hudMessage[256] = {};
    DWORD m_hudSince = 0;      // GetTickCount() when the HUD message was shown

    bool  m_menuWasOpen = false;
    bool  m_clipSaved   = false;
    RECT  m_savedClip   = {};

    void onRender();
    void onTick();
    void onTickFallback();

    bool initJVM();
    bool initTickHook();
    void initModules();
};
