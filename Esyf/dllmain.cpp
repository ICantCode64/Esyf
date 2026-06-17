#include <Windows.h>
#include "Globals.h"
#include "Lua.h"
#include "LuaScan.h"
#include "UI.h"

HMODULE g_Module = nullptr;
Lua::lua_State* g_LuaState = nullptr;

DWORD WINAPI MainThread(LPVOID)
{
    Console::Init();

    g_LuaState = LuaScan::ReScan();

    if (!g_LuaState)
        return 0;

    Lua::Init(g_LuaState);
    UI::Show(g_Module, g_LuaState);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_Module = hModule;

        DisableThreadLibraryCalls(hModule);

        CreateThread(
            nullptr,
            0,
            MainThread,
            nullptr,
            0,
            nullptr
        );
    }

    return TRUE;
}