#include "hostdll.h"
#include "log.h"
#include "forward.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
    HMODULE g_selfModule = nullptr;
    HMODULE g_pluginModule = nullptr;

    // The plugin's InitializePlugin takes a second, "deprecated" std::vector<__int64>
    // argument by value. It is never read by the current CPKLoader build, so an
    // empty vector is the correct thing to hand over. Kept in static storage so a
    // stray pointer retained by the plugin can never dangle.
    std::vector<__int64> g_deprecatedArgs;

    // Signature of CPKLoader!InitializePlugin. The plugin is called with the base
    // address of the running executable, exactly like the original ModdingAPI did.
    using InitializePluginFn = void(__stdcall*)(void* moduleBase, void* deprecatedVector);

    // Signature of CPKLoader!InitializeHooks. The second argument is the host's hook
    // API (ModdingAPI's HookFunctions::HookPlugin), which the plugin uses to replace
    // the game's CPK-loading routine with its own.
    using InitializeHooksFn = void(__stdcall*)(void* moduleBase, void* hookFunction);

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

    // ---- hook API handed to the plugin --------------------------------------
    //
    // CPKLoader does NOT install its detours by itself. It exports InitializeHooks
    // and expects the host to pass a function with this exact contract:
    //
    //     bool (*)(void* toHook, void* ourFunct, int len)
    //
    // Without a real implementation the plugin parses its .cpk.info files, prints
    // the priorities and returns -- so not a single CPK is ever mounted. That is
    // precisely what the 22:28 run showed.
    struct PluginHook
    {
        void* address;
        void* originalBytes;
        int length;
    };

    std::vector<PluginHook> g_pluginHooks;

    // Equivalent of ModdingAPI's HookFunctions::HookPlugin (Hook(..., isPlugin=true)).
    //
    // 14-byte absolute jmp stub written over the target prologue:
    //     FF 25 00 00 00 00        jmp qword ptr [rip+0]
    //     <8-byte absolute target>
    // The overwritten prologue is copied to a trampoline that ends with the same
    // stub jumping back to (toHook + len), so the original code stays reachable and
    // the plugin's detour can call through to it.
    bool __cdecl HookPluginImpl(void* toHook, void* ourFunct, int len)
    {
        constexpr int kStubSize = 14;

        hostlog::Write(L"hook      : requested target=0x%p replacement=0x%p len=%d",
                       toHook, ourFunct, len);

        if (toHook == nullptr || ourFunct == nullptr || len < kStubSize)
        {
            hostlog::Write(L"hook      : rejected (target=0x%p replacement=0x%p len=%d)",
                           toHook, ourFunct, len);
            return false;
        }

        const SIZE_T length = static_cast<SIZE_T>(len);

        void* trampoline = ::VirtualAlloc(nullptr, length + kStubSize,
                                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        void* original = ::VirtualAlloc(nullptr, length,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (trampoline == nullptr || original == nullptr)
        {
            hostlog::Write(L"hook      : VirtualAlloc failed, GetLastError=%lu",
                           static_cast<unsigned long>(::GetLastError()));
            if (trampoline != nullptr)
                ::VirtualFree(trampoline, 0, MEM_RELEASE);
            if (original != nullptr)
                ::VirtualFree(original, 0, MEM_RELEASE);
            return false;
        }

        ::memcpy(original, toHook, length);

        DWORD oldProtect = 0;
        if (!::VirtualProtect(toHook, length, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            hostlog::Write(L"hook      : VirtualProtect failed, GetLastError=%lu",
                           static_cast<unsigned long>(::GetLastError()));
            return false;
        }

        BYTE stub[kStubSize] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

        // trampoline: [original prologue][jmp toHook+len]
        const DWORD_PTR resume = reinterpret_cast<DWORD_PTR>(toHook) + static_cast<DWORD_PTR>(len);
        ::memcpy(stub + 6, &resume, sizeof(resume));
        ::memcpy(trampoline, toHook, length);
        ::memcpy(static_cast<BYTE*>(trampoline) + len, stub, kStubSize);

        // target: [jmp ourFunct][nop padding]
        ::memcpy(stub + 6, &ourFunct, sizeof(ourFunct));
        ::memcpy(toHook, stub, kStubSize);
        for (int i = kStubSize; i < len; ++i)
            static_cast<BYTE*>(toHook)[i] = 0x90;

        ::VirtualProtect(toHook, length, oldProtect, &oldProtect);

        PluginHook record = { toHook, original, len };
        g_pluginHooks.push_back(record);

        hostlog::Write(L"hook      : 0x%p -> 0x%p (len=%d, trampoline=0x%p)",
                       toHook, ourFunct, len, trampoline);
        return true;
    }

    bool IsDirectory(const std::wstring& path)
    {
        const DWORD attributes = ::GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    std::wstring ReadPathHint(const std::wstring& gameDir)
    {
        // moddingapi_path.txt: written by the tooling next to the game exe.
        // MO2 keeps the real mods tree OUTSIDE the game root
        // (<MO2>/NARUTO X BORUTO/mods/...), so the hardcoded relative probes
        // can never find it. The hint file carries the absolute path.
        const std::wstring hint = Join(gameDir, L"moddingapi_path.txt");
        ::HANDLE f = ::CreateFileW(hint.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return std::wstring();

        std::wstring data;
        char buffer[1024] = {};
        ::DWORD read = 0;
        if (::ReadFile(f, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0)
        {
            // UTF-8 (with or without BOM) -> wide. Paths here are ASCII-safe
            // enough, but the MO2 tree contains full-width chars, so decode
            // properly via MultiByteToWideChar.
            const char* p = buffer;
            int n = static_cast<int>(read);
            if (n >= 3 && static_cast<unsigned char>(p[0]) == 0xEF
                && static_cast<unsigned char>(p[1]) == 0xBB
                && static_cast<unsigned char>(p[2]) == 0xBF)
            {
                p += 3;
                n -= 3;
            }
            const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, p, n,
                                                   nullptr, 0);
            if (wlen > 0)
            {
                data.resize(wlen);
                ::MultiByteToWideChar(CP_UTF8, 0, p, n, &data[0], wlen);
            }
        }
        ::CloseHandle(f);

        // trim whitespace / newlines
        while (!data.empty() &&
               (data.back() == L'\r' || data.back() == L'\n'
                || data.back() == L' ' || data.back() == L'\t'))
            data.pop_back();
        return data;
    }

    std::wstring FindApiRoot(const std::wstring& gameDir)
    {
        std::wstring found;

        // 1) absolute hint file (the MO2 layout)
        const std::wstring hint = ReadPathHint(gameDir);
        if (!hint.empty())
        {
            const bool exists = IsDirectory(hint);
            hostlog::Write(L"probe     : %ls -> %ls",
                           hint.c_str(), exists ? L"FOUND" : L"missing");
            if (exists)
                found = hint;
        }

        // 2) relative fallbacks (flat unpack + versioned mod folder)
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
        g_pluginModule = plugin;

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

        // Second half of the plugin contract. InitializePlugin only registers the
        // mods / cpk list; the actual detour of the game's CPK routine is installed
        // here, through the hook function we hand over.
        const auto initializeHooks = reinterpret_cast<InitializeHooksFn>(
            ::GetProcAddress(plugin, "InitializeHooks"));
        if (initializeHooks == nullptr)
        {
            hostlog::Write(L"RESULT: export InitializeHooks not found, plugin cannot mount anything");
            return;
        }

        hostlog::Write(L"calling   : InitializeHooks(moduleBase=0x%p, hookPlugin=0x%p)",
                       moduleBase, reinterpret_cast<void*>(&HookPluginImpl));

        const ULONGLONG hookStarted = ::GetTickCount64();
        initializeHooks(moduleBase, reinterpret_cast<void*>(&HookPluginImpl));

        hostlog::Write(L"RESULT: InitializeHooks returned after %llu ms, %llu hook(s) installed",
                       static_cast<unsigned long long>(::GetTickCount64() - hookStarted),
                       static_cast<unsigned long long>(g_pluginHooks.size()));
    }
}

// Exception filter: logs code + faulting address (and the offset into CPKLoader so
// the crashing routine can be located offline) and lets the process continue.
// GetExceptionInformation() is only legal in a filter expression, hence the filter.
static int ReportHostException(const EXCEPTION_POINTERS* info)
{
    const void* address = nullptr;
    DWORD code = 0;
    if (info != nullptr && info->ExceptionRecord != nullptr)
    {
        address = info->ExceptionRecord->ExceptionAddress;
        code = info->ExceptionRecord->ExceptionCode;
    }

    hostlog::Write(L"RESULT: exception 0x%08lX at 0x%p in host thread, plugin aborted",
                   static_cast<unsigned long>(code), address);

    const DWORD_PTR base = reinterpret_cast<DWORD_PTR>(g_pluginModule);
    const DWORD_PTR crash = reinterpret_cast<DWORD_PTR>(address);
    if (base != 0 && crash >= base && crash < base + 0x800000)
    {
        hostlog::Write(L"RVA       : CPKLoader.dll+0x%llx",
                       static_cast<unsigned long long>(crash - base));
    }
    else if (crash != 0)
    {
        hostlog::Write(L"RVA       : outside CPKLoader.dll (base=0x%p)", reinterpret_cast<void*>(base));
    }

    return EXCEPTION_EXECUTE_HANDLER;
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
    __except (ReportHostException(::GetExceptionInformation()))
    {
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
