#include <Windows.h>
#include "core/Client.h"
#include "utils/Logger.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        Client::instance().startThread();
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        // lpReserved != nullptr means the process is terminating: other threads are already gone
        // and the loader lock is held, so only persist settings. An explicit FreeLibrary does the
        // full shutdown (the injector does not currently offer unloading).
        if (lpReserved) Client::instance().saveOnExit();
        else            Client::instance().shutdown();
        break;
    }
    }
    return TRUE;
}
