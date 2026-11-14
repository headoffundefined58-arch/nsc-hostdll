#pragma once

#include <string>

// Dead-simple append-only logger. One line per call, UTF-8, CRLF.
// Deliberately avoids iostream so it stays usable from a worker thread started
// inside DllMain.
namespace hostlog
{
    // Sets the log file (created / appended to on every write).
    void Init(const std::wstring& path);

    const std::wstring& Path();

    // printf-style (wide). Use %ls for wchar_t* arguments.
    void Write(const wchar_t* format, ...);
}
