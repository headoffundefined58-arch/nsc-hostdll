#include "hostdll.h"
#include "log.h"
#include "forward.h"

#include <windows.h>

#include <string>
#include <vector>

namespace
{
    HMODULE g_selfModule = nullptr;

    // The plugin's InitializePlugin takes a second, "deprecated" std::vector<__int64>
    // argument by value. It is never read by the current CPKLoader build, so an
    // empty vector is the correct thing to hand over. Kept in static storage so a
    // stray pointer retained by the plugin can never dangle.
    std::vector<__int64> g_deprecatedArgs;

    // Signature of CPKLoader!InitializePlugin. The plugin is called with the base
    // address of the running executable, exactly like the original ModdingAPI did.
    using InitializePluginFn = void(__stdcall*)(void* moduleBase, void* deprecatedVector);

    constexpr const wchar_t* kPluginRelativePath = L"mods\\base_game\\CPKLoader.dll";
    constexpr const wchar_t* kLogRelativePath    = L"moddingapi\\hostdll.log";
    constexpr const wchar_t* kSafeModeFileName   = L"safemode";

    // moddingapi roots, in order. The first one is the flat layout used when the
    // API is unpacked into the game root; the second is the versioned mod folder
    // layout used on disk by the MO2 setup.
    constexpr const wchar_t* kApiRoots[] = {
        L"moddingapi",
        L"mods\\_NSC ModdingAPI 1.71\\moddingapi",
    };

    std::wstring GetSelfPath()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = ::GetModuleFileNameW(g_selfModule, &buffer[0],
                                                      static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return std::wstring();

            if (length < buffer.size())
            {
                buffer.resize(length);
                return buffer;
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    std::wstring ParentDir(const std::wstring& filePath)
    {
        const size_t pos = filePath.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? std::wstring() : filePath.substr(0, pos);
    }

    std::wstring Join(const std::wstring& base, const wchar_t* relative)
    {
        if (base.empty())
            return std::wstring(relative);

        const wchar_t tail = base.back();
        if (tail == L'\\' || tail == L'/')
            return base + relative;

        return base + L"\\" + relative;
    }

    bool PathExists(const std::wstring& path)
    {
        return !path.empty() &&
               ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool IsDirectory(const std::wstring& path)
    {
        const DWORD attributes = ::GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    std::wstring FindApiRoot(const std::wstring& gameDir)
    {
        std::wstring found;

        for (const wchar_t* relative : kApiRoots)
        {
            const std::wstring candidate = Join(gameDir, relative);
            const bool exists = IsDirectory(candidate);
            hostlog::Write(L"probe     : %ls -> %ls",
                           candidate.c_str(), exists ? L"FOUND" : L"missing");

            if (exists && found.empty())
                found = candidate;
        }

        return found;
    }

    void RunHost()
    {
        const std::wstring selfPath = GetSelfPath();
        const std::wstring gameDir = ParentDir(selfPath);

        if (gameDir.empty())
            return;

        std::wstring logPath = Join(gameDir, kLogRelativePath);
        if (!IsDirectory(Join(gameDir, L"moddingapi")))
            logPath = Join(gameDir, L"hostdll.log");

        hostlog::Init(logPath);

        hostlog::Write(L"self      : %ls", selfPath.c_str());
        hostlog::Write(L"game dir  : %ls", gameDir.c_str());
        hostlog::Write(L"log file  : %ls", hostlog::Path().c_str());

        const std::wstring apiRoot = FindApiRoot(gameDir);
        if (apiRoot.empty())
        {
            hostlog::Write(L"RESULT: no moddingapi directory found, nothing loaded");
            return;
        }

        hostlog::Write(L"api root  : %ls", apiRoot.c_str());

        const std::wstring safeModePath = Join(apiRoot, kSafeModeFileName);
        if (PathExists(safeModePath))
        {
            hostlog::Write(L"safemode  : %ls present", safeModePath.c_str());
            hostlog::Write(L"RESULT: safemode, plugin NOT loaded");
            return;
        }

        const std::wstring pluginPath = Join(apiRoot, kPluginRelativePath);
        if (!PathExists(pluginPath))
        {
            hostlog::Write(L"RESULT: plugin not found: %ls", pluginPath.c_str());
            return;
        }

        hostlog::Write(L"loading   : %ls", pluginPath.c_str());
        ::SetLastError(0);

        HMODULE plugin = ::LoadLibraryW(pluginPath.c_str());
        if (plugin == nullptr)
        {
            hostlog::Write(L"RESULT: LoadLibraryW failed, GetLastError=%lu",
                           static_cast<unsigned long>(::GetLastError()));
            return;
        }

        hostlog::Write(L"loaded    : base=0x%p", static_cast<void*>(plugin));

        const auto initializePlugin = reinterpret_cast<InitializePluginFn>(
            ::GetProcAddress(plugin, "InitializePlugin"));
        if (initializePlugin == nullptr)
        {
            hostlog::Write(L"RESULT: export InitializePlugin not found, GetLastError=%lu",
                           static_cast<unsigned long>(::GetLastError()));
            return;
        }

        void* moduleBase = static_cast<void*>(::GetModuleHandleW(nullptr));
        g_deprecatedArgs.clear();

        hostlog::Write(L"calling   : InitializePlugin(moduleBase=0x%p, deprecated=0x%p)",
                       moduleBase, static_cast<void*>(&g_deprecatedArgs));

        const ULONGLONG started = ::GetTickCount64();
        initializePlugin(moduleBase, &g_deprecatedArgs);

        hostlog::Write(L"RESULT: InitializePlugin returned after %llu ms",
                       static_cast<unsigned long long>(::GetTickCount64() - started));
    }
}

DWORD WINAPI HostdllWorker(LPVOID reserved)
{
    (void)reserved;

    // Nothing may escape this thread: a proxy DLL that throws into the game is
    // worse than one that silently does nothing.
    __try
    {
        RunHost();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        hostlog::Write(L"RESULT: unhandled exception 0x%08lX in host thread, plugin aborted",
                       static_cast<unsigned long>(::GetExceptionCode()));
    }

    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        g_selfModule = instance;
        ::DisableThreadLibraryCalls(instance);

        // Only CreateThread() here: LoadLibrary() must not run under the loader
        // lock, which is exactly what the worker thread avoids.
        const HANDLE thread = ::CreateThread(nullptr, 0, &HostdllWorker, nullptr, 0, nullptr);
        if (thread != nullptr)
            ::CloseHandle(thread);
    }

    return TRUE;
}
