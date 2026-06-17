#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace Console {
    inline HANDLE Out = nullptr;

    inline void Write(const char* text) {
        OutputDebugStringA(text);

        if (!Out || Out == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;
        WriteFile(Out, text, (DWORD)strlen(text), &written, nullptr);
    }

    inline void Init() {
        AllocConsole();
        SetConsoleTitleA("Esyf");

        Out = CreateFileA(
            "CONOUT$",
            GENERIC_WRITE,
            FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
    }

    inline void Print(const char* fmt, ...) {
        char buffer[512]{};

        va_list args;
        va_start(args, fmt);
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
        va_end(args);

        Write(buffer);
    }
}