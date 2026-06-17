#pragma once
#include <Windows.h>
#include <string>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstdlib>

#include "Console.h"
#include "MinHook.h"

namespace Lua {
    struct lua_State;
    typedef int(*lua_CFunction)(lua_State* L);

    inline std::queue<std::string> g_queue;
    inline std::mutex g_mutex;
    inline lua_State* g_State = nullptr;

    struct LuaThread {
        lua_State* T;
        double resumeAt;
    };

    inline std::vector<LuaThread> g_threads;
    inline bool g_resuming = false;

    using lua_gettop_t = int(__fastcall*)(lua_State* L);
    using lua_type_t = int(__fastcall*)(lua_State* L, int index);

    typedef int(__fastcall* luau_load_t)(
        lua_State* L,
        const char* chunkname,
        void* bytecode,
        int size,
        void* env,
        void* ctx1,
        void* ctx2,
        void* ctx3,
        int flags
        );

    typedef void* (__fastcall* luau_compile_t)(
        void* Src,
        size_t Size,
        void* Options,
        void* Out
        );

    typedef __int64(__fastcall* lua_pcall_t)(
        lua_State* L,
        int nargs,
        int nresults,
        int errfunc
        );

    typedef void(__fastcall* lua_pushcclosurek_t)(lua_State* L, lua_CFunction fn, const char* debugname, int nup, lua_CFunction cont);
    typedef void(__fastcall* lua_rawsetfield_t)(lua_State* L, int index, const char* k);
    typedef lua_State* (__fastcall* lua_newthread_t)(lua_State* L);
    typedef int(__fastcall* lua_resume_t)(lua_State* L, lua_State* from, int nargs);
    typedef int(__fastcall* lua_status_t)(lua_State* L);
    typedef void(__fastcall* lua_settop_t)(lua_State* L, int idx);
    typedef const char* (__fastcall* lua_tolstring_t)(lua_State* L, int index, size_t* len);
    typedef int(__fastcall* lua_yield_t)(lua_State* L, int nresults);
    typedef double(__fastcall* lua_tonumberx_t)(
        lua_State* L,
        int index,
        int* isnum
        );
    typedef __int64(__fastcall* lua_pushnumber_t)(lua_State* L, double n);

    inline lua_type_t fp_lua_type = nullptr;
    inline lua_gettop_t fp_lua_gettop = nullptr;
    inline lua_settop_t fp_lua_settop = nullptr;
    inline luau_load_t fp_luau_load = nullptr;
    inline luau_compile_t fp_luau_compile = nullptr;
    inline lua_pcall_t fp_lua_pcall = nullptr;
    inline lua_tolstring_t fp_lua_tolstring = nullptr;
    inline lua_newthread_t fp_lua_newthread = nullptr;
    inline lua_resume_t fp_lua_resume = nullptr;
    inline lua_status_t fp_lua_status = nullptr;
    inline lua_pushcclosurek_t fp_lua_pushcclosurek = nullptr;
    inline lua_rawsetfield_t fp_lua_rawsetfield = nullptr;
    inline lua_yield_t fp_lua_yield = nullptr;
    inline lua_tonumberx_t fp_lua_tonumberx = nullptr;
    inline lua_pushnumber_t fp_lua_pushnumber = nullptr;

#define lua_pop(L,n) fp_lua_settop(L, -(n)-1)
#define LUA_OK 0
#define LUA_YIELD 1
#define LUA_GLOBALSINDEX   (-10002)

    void RegisterFunction(lua_State* L, const char* name, lua_CFunction func) {
        // Function by fxkemoney
        fp_lua_pushcclosurek(L, func, name, 0, nullptr);
        fp_lua_rawsetfield(L, LUA_GLOBALSINDEX, name);
    }

    inline double Now()
    {
        return GetTickCount64() / 1000.0;
    }

    inline const char* GetLuaError(lua_State* L)
    {
        if (!fp_lua_tolstring)
            return "missing lua_tolstring";

        return fp_lua_tolstring(L, -1, nullptr);
    }

    inline double GetYieldDelay(lua_State* T)
    {
        double delay = 0.03;

        if (fp_lua_tonumberx)
        {
            int isnum = 0;
            double n = fp_lua_tonumberx(T, -1, &isnum);

            if (isnum && n >= 0.0)
                delay = n;
        }

        if (fp_lua_settop)
            lua_pop(T, 1);

        return delay;
    }

    inline int CompileString(lua_State* L, const std::string& code)
    {
        if (!L || !fp_luau_compile || !fp_luau_load)
            return 1;

        uint8_t out[8] = {};

        void* bytecode = fp_luau_compile(
            (void*)code.c_str(),
            code.size(),
            nullptr,
            out
        );

        if (!bytecode)
            return 1;

        uint32_t size = *(uint32_t*)out;

        return fp_luau_load(
            L,
            "chunk",
            bytecode,
            size,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            0
        );
    }

    inline void TrackYielded(lua_State* T)
    {
        for (auto& th : g_threads)
        {
            if (th.T == T)
                return;
        }

        double delay = GetYieldDelay(T);
        g_threads.push_back({ T, Now() + delay });
    }

    int __fastcall CustomWait(lua_State* L)
    {
        double t = 0.03;

        if (!fp_lua_yield || !fp_lua_pushnumber)
            return 0;

        if (fp_lua_tonumberx)
        {
            int isnum = 0;
            double n = fp_lua_tonumberx(L, 1, &isnum);

            if (isnum && n >= 0.0)
                t = n;
        }

        fp_lua_pushnumber(L, t);

        return fp_lua_yield(L, 1);
    }

    inline void ExecThread(lua_State* L, const std::string& code)
    {
        if (!L || !fp_lua_newthread || !fp_lua_resume)
            return;

        lua_State* T = fp_lua_newthread(L);
        RegisterFunction(T, "wait", CustomWait);

        int status = CompileString(T, code);

        if (status != LUA_OK)
        {
            const char* err = GetLuaError(T);
            Console::Write((std::string("[-] Compile failed: ") + (err ? err : "unknown") + "\n").c_str());
            return;
        }

        int r = fp_lua_resume(T, nullptr, 0);

        if (r == LUA_YIELD)
        {
            TrackYielded(T);
        }
        else if (r != LUA_OK)
        {
            const char* err = GetLuaError(T);
            Console::Write((std::string("[-] Runtime error: ") + (err ? err : "unknown") + "\n").c_str());
        }
    }

    inline void ResumeThreads()
    {
        double now = Now();
        std::vector<LuaThread> keep;

        keep.reserve(g_threads.size());

        for (auto& th : g_threads)
        {
            lua_State* T = th.T;

            if (!T)
                continue;

            if (fp_lua_status && fp_lua_status(T) != LUA_YIELD)
                continue;

            if (now < th.resumeAt)
            {
                keep.push_back(th);
                continue;
            }

            int r = fp_lua_resume(T, nullptr, 0);

            if (r == LUA_YIELD)
            {
                double delay = GetYieldDelay(T);
                keep.push_back({ T, Now() + delay });
            }
            else if (r != LUA_OK)
            {
                const char* err = GetLuaError(T);
                Console::Write((std::string("[-] Thread error: ") + (err ? err : "unknown") + "\n").c_str());
            }
        }

        g_threads.swap(keep);
    }

    inline void ProcessQueue()
    {
        if (!g_State)
            return;

        std::queue<std::string> local;

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            if (g_queue.empty())
                return;

            std::swap(local, g_queue);
        }

        while (!local.empty())
        {
            ExecThread(g_State, local.front());
            local.pop();
        }
    }

    int __fastcall hk_lua_type(lua_State* L, int index)
    {
        g_State = L;

        if (!g_resuming)
        {
            g_resuming = true;
            ProcessQueue();
            ResumeThreads();
            g_resuming = false;
        }

        return fp_lua_type(L, index);
    }

    inline void Init(lua_State* L)
    {
        if (MH_Initialize() != MH_OK)
            return;

        HMODULE hMod_VM = GetModuleHandleA("Luau.VM.dll");
        if (!hMod_VM)
            return;

        HMODULE hMod_Compiler = LoadLibraryA("Luau.Compiler.dll");
        if (!hMod_Compiler)
            return;

        fp_luau_load = (luau_load_t)GetProcAddress(hMod_VM, "luau_load");
        fp_luau_compile = (luau_compile_t)GetProcAddress(hMod_Compiler, "luau_compile");

        fp_lua_pcall = (lua_pcall_t)GetProcAddress(hMod_VM, "lua_pcall");
        fp_lua_gettop = (lua_gettop_t)GetProcAddress(hMod_VM, "lua_gettop");
        fp_lua_settop = (lua_settop_t)GetProcAddress(hMod_VM, "lua_settop");
        fp_lua_tolstring = (lua_tolstring_t)GetProcAddress(hMod_VM, "lua_tolstring");

        fp_lua_newthread = (lua_newthread_t)GetProcAddress(hMod_VM, "lua_newthread");
        fp_lua_resume = (lua_resume_t)GetProcAddress(hMod_VM, "lua_resume");
        fp_lua_status = (lua_status_t)GetProcAddress(hMod_VM, "lua_status");

        fp_lua_pushcclosurek = (lua_pushcclosurek_t)GetProcAddress(hMod_VM, "lua_pushcclosurek");
        fp_lua_rawsetfield = (lua_rawsetfield_t)GetProcAddress(hMod_VM, "lua_rawsetfield");

        fp_lua_yield = (lua_yield_t)GetProcAddress(hMod_VM, "lua_yield");
        fp_lua_tonumberx = (lua_tonumberx_t)GetProcAddress(hMod_VM, "lua_tonumberx");
        fp_lua_pushnumber = (lua_pushnumber_t)GetProcAddress(hMod_VM, "lua_pushnumber");

        if (!fp_luau_load || !fp_luau_compile || !fp_lua_settop ||
            !fp_lua_tolstring || !fp_lua_newthread || !fp_lua_resume)
        {
            Console::Write("[-] Missing Lua function\n");
            return;
        }

        void* target = GetProcAddress(hMod_VM, "lua_type");
        if (!target)
            return;

        g_State = L;

        MH_CreateHook(
            target,
            &hk_lua_type,
            reinterpret_cast<LPVOID*>(&fp_lua_type)
        );

        MH_EnableHook(target);
    }

    inline void PushScript(lua_State* L, const std::string& code)
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (L)
            g_State = L;

        g_queue.push(code);
    }
}