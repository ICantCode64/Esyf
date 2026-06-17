#pragma once
#include <Windows.h>
#include <string>
#include "Console.h"
#include "Lua.h"

namespace Pipe {
    inline HANDLE ThreadHandle = nullptr;
    inline Lua::lua_State* State = nullptr;
    inline const char* PipeName = R"(\\.\pipe\Esyf)";

    inline void ExecLine(const std::string& code) {
        if (!State)
            return;

        if (code.empty())
            return;

        Lua::PushScript(State, code);
    }

    inline DWORD WINAPI ServerThread(LPVOID) {
        while (true) {
            HANDLE pipe = CreateNamedPipeA(
                PipeName,
                PIPE_ACCESS_INBOUND,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1,
                4096,
                4096,
                0,
                nullptr
            );

            if (pipe == INVALID_HANDLE_VALUE) {
                Console::Write("Pipe failed\r\n");
                Sleep(1000);
                continue;
            }

            BOOL connected = ConnectNamedPipe(pipe, nullptr) ?
                TRUE :
                GetLastError() == ERROR_PIPE_CONNECTED;

            if (connected) {
                char buffer[4096]{};
                DWORD read = 0;

                while (ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0) {
                    buffer[read] = 0;

                    std::string code(buffer);

                    while (!code.empty() && (code.back() == '\n' || code.back() == '\r'))
                        code.pop_back();

                    ExecLine(code);

                    ZeroMemory(buffer, sizeof(buffer));
                    read = 0;
                }
            }

            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }

        return 0;
    }

    inline void Start(Lua::lua_State* L) {
        State = L;

        if (ThreadHandle)
            return;

        ThreadHandle = CreateThread(
            nullptr,
            0,
            ServerThread,
            nullptr,
            0,
            nullptr
        );
    }
}