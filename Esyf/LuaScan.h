#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include "Console.h"
#include "Lua.h"
#include "Pipe.h"
#include "Globals.h"

namespace LuaScan {
    using lua_State = void;
    using lua_gettop_t = int(__fastcall*)(lua_State* L);
    using lua_status_t = int(__fastcall*)(lua_State* L);

    struct Hit {
        uintptr_t addr;
        int score;
        int top;
        int status;
    };

    inline bool IsReadableProtect(DWORD p) {
        if (p & PAGE_GUARD) return false;
        if (p & PAGE_NOACCESS) return false;

        p &= 0xFF;

        return p == PAGE_READONLY ||
            p == PAGE_READWRITE ||
            p == PAGE_WRITECOPY ||
            p == PAGE_EXECUTE_READ ||
            p == PAGE_EXECUTE_READWRITE ||
            p == PAGE_EXECUTE_WRITECOPY;
    }

    inline bool LooksPtr(uint64_t x) {
        return x > 0x10000 && x < 0x0000800000000000;
    }

    inline bool ClosePtr(uint64_t a, uint64_t b, uint64_t maxdiff) {
        return a > b ? (a - b) < maxdiff : (b - a) < maxdiff;
    }

    inline uint64_t Qword(const uint8_t* b, size_t off) {
        uint64_t v = 0;
        memcpy(&v, b + off, sizeof(v));
        return v;
    }

    inline bool LocalReadable(uintptr_t addr, size_t size = 8) {
        MEMORY_BASIC_INFORMATION mbi{};

        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))
            return false;

        if (mbi.State != MEM_COMMIT)
            return false;

        if (!IsReadableProtect(mbi.Protect))
            return false;

        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        uintptr_t end = base + mbi.RegionSize;

        return addr >= base && addr + size <= end;
    }

    inline int ScoreCandidate(const uint8_t* b) {
        uint64_t q00 = Qword(b, 0x00);
        uint64_t q08 = Qword(b, 0x08);
        uint64_t q10 = Qword(b, 0x10);
        uint64_t q18 = Qword(b, 0x18);
        uint64_t q20 = Qword(b, 0x20);
        uint64_t q28 = Qword(b, 0x28);
        uint64_t q30 = Qword(b, 0x30);
        uint64_t q38 = Qword(b, 0x38);
        uint64_t q40 = Qword(b, 0x40);
        uint64_t q48 = Qword(b, 0x48);
        uint64_t q50 = Qword(b, 0x50);
        uint64_t q58 = Qword(b, 0x58);
        uint64_t q60 = Qword(b, 0x60);
        uint64_t q68 = Qword(b, 0x68);
        uint64_t q70 = Qword(b, 0x70);
        uint64_t q78 = Qword(b, 0x78);

        if (q48 != 0x000000080000002DULL) return 0;
        if ((q00 & 0xFF) != 0x0A) return 0;
        if (q20 != q40) return 0;
        if (q60 != 0) return 0;

        if (!LooksPtr(q08)) return 0;
        if (!LooksPtr(q10)) return 0;
        if (!LooksPtr(q18)) return 0;
        if (!LooksPtr(q20)) return 0;
        if (!LooksPtr(q28)) return 0;
        if (!LooksPtr(q30)) return 0;
        if (!LooksPtr(q38)) return 0;
        if (!LooksPtr(q58)) return 0;

        if (q68 != 0 && !LooksPtr(q68)) return 0;

        int score = 0;

        if (q00 == 0x0A || q00 == 0x10A || q00 == 0x20A || q00 == 0x40A)
            score += 12;
        else
            score += 3;

        score += 26;

        if ((q50 >> 32) <= 0x100)
            score += 3;

        if (ClosePtr(q08, q10, 0x1000))
            score += 10;
        else if (ClosePtr(q08, q10, 0x100000))
            score += 4;

        if (ClosePtr(q20, q38, 0x100000))
            score += 6;

        if (q68 != 0 && ClosePtr(q18, q68, 0x100000))
            score += 6;

        if (q08 != q10)
            score += 2;

        if (q70 == 0 || LooksPtr(q70))
            score += 2;

        if (q78 == 0)
            score += 2;

        return score;
    }

    inline int SafeGetTop(lua_gettop_t fn, uintptr_t L) {
        if (!fn)
            return -999;

        __try {
            int top = fn((lua_State*)L);

            if (top >= 0 && top < 100000)
                return top;

            return -2;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    inline int SafeStatus(lua_status_t fn, uintptr_t L) {
        if (!fn)
            return -999;

        __try {
            int status = fn((lua_State*)L);

            if (status >= 0 && status < 100)
                return status;

            return -2;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    inline bool BasicBlockCheck(uintptr_t addr) {
        if (!LocalReadable(addr, 0x100))
            return false;

        auto b = (const uint8_t*)addr;

        return ((Qword(b, 0x00) & 0xFF) == 0x0A) &&
            Qword(b, 0x20) == Qword(b, 0x40) &&
            Qword(b, 0x48) == 0x000000080000002DULL &&
            Qword(b, 0x60) == 0;
    }

    inline void ResolveLua(lua_gettop_t* gettop, lua_status_t* status) {
        *gettop = nullptr;
        *status = nullptr;

        HMODULE mod = GetModuleHandleA("Luau.VM.dll");

        if (!mod)
            return;

        *gettop = (lua_gettop_t)GetProcAddress(mod, "lua_gettop");
        *status = (lua_status_t)GetProcAddress(mod, "lua_status");
    }

    inline bool Confirmed(const Hit& h) {
        return h.top >= 0 && h.status >= 0 && h.status < 100;
    }

    inline std::vector<Hit> ScanStates(lua_gettop_t lua_gettop, lua_status_t lua_status) {
        std::vector<Hit> hits;

        SYSTEM_INFO si{};
        GetSystemInfo(&si);

        uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
        uintptr_t max = (uintptr_t)si.lpMaximumApplicationAddress;

        const uint64_t needle = 0x000000080000002DULL;

        MEMORY_BASIC_INFORMATION mbi{};

        while (addr < max) {
            if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))
                break;

            uintptr_t base = (uintptr_t)mbi.BaseAddress;
            size_t size = mbi.RegionSize;

            addr = base + size;

            if (mbi.State != MEM_COMMIT) continue;
            if (!IsReadableProtect(mbi.Protect)) continue;
            if (mbi.Type != MEM_PRIVATE) continue;
            if (mbi.Protect != PAGE_READWRITE) continue;
            if (size < 0x100 || size > 0x4000000) continue;

            const uint8_t* mem = (const uint8_t*)base;

            for (size_t i = 0x48; i + 0x100 < size; i += 8) {
                uint64_t v = *(const uint64_t*)(mem + i);

                if (v != needle)
                    continue;

                uintptr_t obj = base + i - 0x48;

                if (!BasicBlockCheck(obj))
                    continue;

                int score = ScoreCandidate((const uint8_t*)obj);

                if (score < 55)
                    continue;

                int top = SafeGetTop(lua_gettop, obj);
                int status = SafeStatus(lua_status, obj);

                Hit h{ obj, score, top, status };
                hits.push_back(h);

                if (Confirmed(h))
                    return hits;
            }
        }

        return hits;
    }

    inline Hit ChooseBest(std::vector<Hit>& hits) {
        if (hits.empty())
            return {};

        auto better = [](const Hit& a, const Hit& b) {
            if (Confirmed(a) != Confirmed(b))
                return !Confirmed(a);

            if ((a.top >= 0) != (b.top >= 0))
                return a.top < 0;

            if (a.score != b.score)
                return a.score < b.score;

            return a.addr < b.addr;
            };

        return *std::max_element(hits.begin(), hits.end(), better);
    }

    inline Lua::lua_State* ReScan(bool msg = true)
    {
        Hit best{};

        lua_gettop_t lua_gettop = nullptr;
        lua_status_t lua_status = nullptr;

        ResolveLua(&lua_gettop, &lua_status);
        
        if (msg == true) {
            Console::Write("[+] Started scanning...\r\n");
        }
        
        for (;;)
        {
            auto hits = ScanStates(lua_gettop, lua_status);
            best = ChooseBest(hits);

            if (best.addr)
                break;

            Console::Write("[-] Lua state not found, retrying...\r\n");
            Sleep(100);
        }

        int add = 0x80;

        if (!Confirmed(best) && BasicBlockCheck(best.addr + add))
        {
            int next_top = SafeGetTop(lua_gettop, best.addr + add);
            int next_status = SafeStatus(lua_status, best.addr + add);

            if (next_top >= 0 && next_status >= 0)
            {
                best.addr += add;
                best.score = ScoreCandidate((const uint8_t*)best.addr);
                best.top = next_top;
                best.status = next_status;
            }
        }

        Console::Print("[+] Lua state: 0x%p\r\n", (void*)best.addr);

        Lua::lua_State* L = (Lua::lua_State*)best.addr;
        g_LuaState = L;

        return L;
    }
}