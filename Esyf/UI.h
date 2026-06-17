#pragma once
#include <windows.h>
#include <richedit.h>
#include <commdlg.h>
#include <fstream>
#include <string>
#include "Lua.h"
#include "LuaScan.h"

namespace UI
{
    inline HMODULE hModule = nullptr;
    inline HWND hWindow = nullptr;
    inline HWND hRun = nullptr;
    inline HWND hClear = nullptr;
    inline HWND hEditor = nullptr;
    inline HANDLE hThread = nullptr;

    inline Lua::lua_State* g_L = nullptr;

    constexpr int ID_RUN = 101;
    constexpr int ID_CLEAR = 102;
    constexpr int ID_EDITOR = 103;

    constexpr int ID_FILE_OPEN = 200;
    constexpr int ID_FILE_SAVE = 201;
    constexpr int ID_LUA_RESCAN = 202;

    inline void SetupMenu(HWND hwnd)
    {
        HMENU menu = CreateMenu();
        HMENU file = CreatePopupMenu();
        HMENU lua = CreatePopupMenu();

        AppendMenuA(file, MF_STRING, ID_FILE_OPEN, "Open File");
        AppendMenuA(file, MF_STRING, ID_FILE_SAVE, "Save To File");

        AppendMenuA(lua, MF_STRING, ID_LUA_RESCAN, "Rescan");

        AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "File");
        AppendMenuA(menu, MF_POPUP, (UINT_PTR)lua, "Lua");

        SetMenu(hwnd, menu);
    }

    inline void ApplyFonts()
    {
        HFONT editorFont = CreateFontA(
            16, 0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            ANSI_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN,
            "Consolas"
        );

        SendMessageA(hEditor, WM_SETFONT, (WPARAM)editorFont, TRUE);
    }

    inline LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case ID_RUN:
            {
                int len = GetWindowTextLengthA(hEditor);
                if (len <= 0 || !g_L)
                    break;

                std::string code(len + 1, '\0');
                GetWindowTextA(hEditor, code.data(), len + 1);

                Lua::PushScript(g_L, code.c_str());
                break;
            }

            case ID_CLEAR:
                SetWindowTextA(hEditor, "");
                break;

            case ID_FILE_OPEN:
            {
                char file[MAX_PATH]{};

                OPENFILENAMEA ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrFilter = "Lua Files\0*.lua\0All Files\0*.*\0";
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

                if (GetOpenFileNameA(&ofn))
                {
                    std::ifstream in(file, std::ios::binary);

                    if (in)
                    {
                        std::string text(
                            (std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>()
                        );

                        SetWindowTextA(hEditor, text.c_str());
                    }
                }

                break;
            }

            case ID_FILE_SAVE:
            {
                char file[MAX_PATH]{};

                OPENFILENAMEA ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrFilter = "Lua Files\0*.lua\0All Files\0*.*\0";
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

                if (GetSaveFileNameA(&ofn))
                {
                    int len = GetWindowTextLengthA(hEditor);
                    std::string text(len + 1, '\0');

                    GetWindowTextA(hEditor, text.data(), len + 1);

                    std::ofstream out(file, std::ios::binary);

                    if (out)
                        out << text.c_str();
                }

                break;
            }

            case ID_LUA_RESCAN:
                g_L = LuaScan::ReScan();
                break;
            }

            break;
        }

        case WM_CLOSE:
            //DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            //hWindow = nullptr;
            //hThread = nullptr;
            //PostQuitMessage(0);
            break;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }

        return 0;
    }

    inline DWORD WINAPI ThreadProc(LPVOID)
    {
        LoadLibraryA("Msftedit.dll");

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hModule;
        wc.lpszClassName = "Esyf_UI";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        RegisterClassExA(&wc);

        RECT rc = { 0, 0, 485, 345 };

        AdjustWindowRect(&rc,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            TRUE
        );

        hWindow = CreateWindowExA(
            WS_EX_TOPMOST,
            "Esyf_UI",
            "Esyf",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr,
            nullptr,
            hModule,
            nullptr
        );

        if (!hWindow)
            return 0;

        SetupMenu(hWindow);

        hEditor = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "RICHEDIT50W",
            "",
            WS_VISIBLE | WS_CHILD |
            WS_VSCROLL | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_WANTRETURN,
            20, 20, 445, 255,
            hWindow,
            (HMENU)ID_EDITOR,
            hModule,
            nullptr
        );

        hRun = CreateWindowA(
            "BUTTON",
            "Run",
            WS_VISIBLE | WS_CHILD,
            20, 285, 215, 34,
            hWindow,
            (HMENU)ID_RUN,
            hModule,
            nullptr
        );

        hClear = CreateWindowA(
            "BUTTON",
            "Clear",
            WS_VISIBLE | WS_CHILD,
            250, 285, 215, 34,
            hWindow,
            (HMENU)ID_CLEAR,
            hModule,
            nullptr
        );

        SetWindowTextA(hEditor, "// code editor");
        ApplyFonts();

        ShowWindow(hWindow, SW_SHOW);
        SetForegroundWindow(hWindow);
        UpdateWindow(hWindow);

        MSG msg{};
        while (GetMessageA(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        return 0;
    }

    inline void Show(HMODULE module, Lua::lua_State* L)
    {
        if (hThread)
            return;

        hModule = module;
        g_L = L;

        hThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);

        if (!hThread)
            MessageBoxA(nullptr, "CreateThread failed", "debug", MB_OK);
    }
}