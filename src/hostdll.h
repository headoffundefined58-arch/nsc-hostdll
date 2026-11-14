#pragma once

#include <windows.h>

// Implemented in hostdll.cpp. Spawned from DllMain (DLL_PROCESS_ATTACH) so that
// nothing but CreateThread() runs under the loader lock.
DWORD WINAPI HostdllWorker(LPVOID reserved);
