#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace
{
    std::wstring g_path;
    bool g_ready = false;

    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
            return std::string();

        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                                static_cast<int>(text.size()),
                                                nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            return std::string();

        std::string out(static_cast<size_t>(needed), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                              static_cast<int>(text.size()),
                              &out[0], needed, nullptr, nullptr);
        return out;
    }

    void AppendUtf8(const std::string& bytes)
    {
        if (!g_ready || bytes.empty())
            return;

        HANDLE file = ::CreateFileW(g_path.c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;
        ::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        ::CloseHandle(file);
    }
}

namespace hostlog
{
    void Init(const std::wstring& path)
    {
        g_path = path;
        g_ready = !g_path.empty();
        Write(L"=== hostdll attached (pid=%lu) ===",
              static_cast<unsigned long>(::GetCurrentProcessId()));
    }

    const std::wstring& Path()
    {
        return g_path;
    }

    void Write(const wchar_t* format, ...)
    {
        if (!g_ready || format == nullptr)
            return;

        wchar_t message[2048];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
        va_end(args);

        SYSTEMTIME now;
        ::GetLocalTime(&now);

        wchar_t line[2304];
        _snwprintf_s(line, _countof(line), _TRUNCATE,
                     L"[%02u:%02u:%02u.%03u tid=%lu] %ls\r\n",
                     static_cast<unsigned>(now.wHour),
                     static_cast<unsigned>(now.wMinute),
                     static_cast<unsigned>(now.wSecond),
                     static_cast<unsigned>(now.wMilliseconds),
                     static_cast<unsigned long>(::GetCurrentThreadId()),
                     message);

        AppendUtf8(ToUtf8(line));
    }
}
