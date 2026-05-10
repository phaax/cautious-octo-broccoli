#include "SaveFinder.h"

#include "Logger.h"
#include "SaveManager.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace noitaqs
{
    namespace
    {
        using SaveFn = void(__cdecl*)();

        SaveFn g_saveWorldStateFn = nullptr;
        SaveFn g_savePlayerFn = nullptr;
        SaveFn g_comprehensiveSaveFn = nullptr;

        struct EnumWindowsParam { DWORD pid; HWND result; };

        BOOL CALLBACK FindMainWindowCb(HWND hwnd, LPARAM lParam)
        {
            auto* p = reinterpret_cast<EnumWindowsParam*>(lParam);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == p->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr)
            {
                p->result = hwnd;
                return FALSE;
            }
            return TRUE;
        }

        HWND GetNoitaHwnd()
        {
            EnumWindowsParam p{ GetCurrentProcessId(), nullptr };
            EnumWindows(FindMainWindowCb, reinterpret_cast<LPARAM>(&p));
            return p.result;
        }

        const uint8_t* FindBytes(const uint8_t* start, size_t size, const uint8_t* needle, size_t needleLen)
        {
            if (needleLen == 0 || size < needleLen)
                return nullptr;

            for (size_t i = 0; i <= size - needleLen; ++i)
            {
                if (memcmp(start + i, needle, needleLen) == 0)
                    return start + i;
            }
            return nullptr;
        }

        // Finds the start of the function that contains `pushInstr` by walking
        // backwards looking for MSVC Release-mode function boundaries.
        //
        // MSVC Release builds with /O2 use frame pointer omission, so the classic
        // "push ebp; mov ebp, esp" prologue (55 8B EC) is rare. Instead we rely on
        // the padding that MSVC inserts between functions: sequences of 0xCC (int3)
        // bytes. Three or more consecutive CCs are an unambiguous boundary — they
        // can't appear as instruction stream in valid code (int3 is a 1-byte trap and
        // three in a row would never be generated intentionally).
        //
        // Fallback: the traditional 55 8B EC prologue is also accepted.
        const uint8_t* FindFunctionStart(const uint8_t* pushInstr, size_t maxSearch)
        {
            // Walk backwards looking for 3+ consecutive CC bytes (int3 padding)
            for (size_t j = 3; j <= maxSearch; ++j)
            {
                const uint8_t* p = pushInstr - j;
                if (p[0] == 0xCC && p[1] == 0xCC && p[2] == 0xCC)
                {
                    // Walk forward past the entire CC run to reach the function start
                    const uint8_t* fnStart = p + 3;
                    while (fnStart < pushInstr && *fnStart == 0xCC)
                        ++fnStart;
                    return fnStart;
                }

                // Also accept classic frame-pointer prologue as a quicker find
                if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC)
                    return p;
            }
            return nullptr;
        }

        // Scans the .text section for a 32-bit `push <stringVA>` instruction
        // (opcode 0x68 + 4-byte immediate), then walks backwards to find the
        // enclosing function's start address.
        SaveFn FindFunctionViaString(
            const uint8_t* moduleBase,
            size_t moduleSize,
            const uint8_t* textStart,
            size_t textSize,
            const char* searchString,
            size_t maxPrologueSearch = 16384)
        {
            const auto* needle = reinterpret_cast<const uint8_t*>(searchString);
            size_t needleLen = strlen(searchString);

            const uint8_t* stringAddr = FindBytes(moduleBase, moduleSize, needle, needleLen);
            if (stringAddr == nullptr)
            {
                Log(std::wstring(L"[SaveFinder] String not found: ")
                    + std::wstring(searchString, searchString + needleLen));
                return nullptr;
            }

            uintptr_t stringVA = reinterpret_cast<uintptr_t>(stringAddr);

            for (size_t i = 0; i + 5 <= textSize; ++i)
            {
                if (textStart[i] != 0x68)
                    continue;

                uintptr_t operand = *reinterpret_cast<const uint32_t*>(textStart + i + 1);
                if (operand != stringVA)
                    continue;

                const uint8_t* pushInstr = textStart + i;
                size_t backLimit = (i < maxPrologueSearch) ? i : maxPrologueSearch;
                const uint8_t* fnStart = FindFunctionStart(pushInstr, backLimit);

                if (fnStart != nullptr)
                    return reinterpret_cast<SaveFn>(fnStart);

                Log(L"[SaveFinder] Found string reference but could not locate function boundary.");
                return nullptr;
            }

            Log(std::wstring(L"[SaveFinder] No code reference found for: ")
                + std::wstring(searchString, searchString + needleLen));
            return nullptr;
        }

        // Returns true if the function starting at fnStart contains an E8 CALL to targetVA.
        // Stops scanning when it hits 3+ consecutive CC bytes (next function's padding).
        bool FunctionContainsCallTo(const uint8_t* fnStart, size_t maxScan, uintptr_t targetVA)
        {
            for (size_t i = 0; i + 5 <= maxScan; ++i)
            {
                if (fnStart[i] == 0xCC && i + 2 < maxScan
                    && fnStart[i + 1] == 0xCC && fnStart[i + 2] == 0xCC)
                    break;

                if (fnStart[i] == 0xE8)
                {
                    int32_t rel = *reinterpret_cast<const int32_t*>(fnStart + i + 1);
                    uintptr_t calledVA = reinterpret_cast<uintptr_t>(fnStart + i + 5) + static_cast<uintptr_t>(rel);
                    if (calledVA == targetVA)
                        return true;
                }
            }
            return false;
        }

        // Scans .text for a function that contains E8 calls to BOTH playerFn and worldStateFn.
        // That function sets up the context both sub-functions need and can be called directly.
        SaveFn FindComprehensiveSaveFunction(
            const uint8_t* textStart, size_t textSize,
            const uint8_t* playerFn, const uint8_t* worldStateFn)
        {
            uintptr_t playerVA     = reinterpret_cast<uintptr_t>(playerFn);
            uintptr_t worldStateVA = reinterpret_cast<uintptr_t>(worldStateFn);

            for (size_t i = 0; i + 5 <= textSize; ++i)
            {
                if (textStart[i] != 0xE8)
                    continue;

                int32_t rel = *reinterpret_cast<const int32_t*>(textStart + i + 1);
                uintptr_t calledVA = reinterpret_cast<uintptr_t>(textStart + i + 5) + static_cast<uintptr_t>(rel);

                if (calledVA != playerVA)
                    continue;

                // Found a call to SavePlayer — locate its enclosing function.
                const uint8_t* callSite = textStart + i;
                size_t backLimit = (i < 16384) ? i : static_cast<size_t>(16384);
                const uint8_t* fnStart = FindFunctionStart(callSite, backLimit);
                if (fnStart == nullptr)
                    continue;

                // Check if the same function also calls SaveWorldState.
                size_t fnScanLimit = static_cast<size_t>(65536);
                if (FunctionContainsCallTo(fnStart, fnScanLimit, worldStateVA))
                    return reinterpret_cast<SaveFn>(fnStart);
            }

            return nullptr;
        }

        std::pair<const uint8_t*, size_t> FindSection(const uint8_t* base, const char* name)
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return {};

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return {};

            const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
            WORD count = nt->FileHeader.NumberOfSections;

            for (WORD i = 0; i < count; ++i)
            {
                if (strncmp(reinterpret_cast<const char*>(sections[i].Name), name, IMAGE_SIZEOF_SHORT_NAME) == 0)
                    return { base + sections[i].VirtualAddress, sections[i].Misc.VirtualSize };
            }
            return {};
        }
    }

    void InitSaveFinder()
    {
        const auto* moduleBase = reinterpret_cast<const uint8_t*>(GetModuleHandle(nullptr));
        if (moduleBase == nullptr)
        {
            Log(L"[SaveFinder] Could not get noita.exe base address.");
            return;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
        size_t moduleSize = nt->OptionalHeader.SizeOfImage;

        auto [textStart, textSize] = FindSection(moduleBase, ".text");
        if (textStart == nullptr)
        {
            Log(L"[SaveFinder] Could not locate .text section.");
            return;
        }

        // Player save: the first "??SAV/player.xml" push in .text is 151 bytes into
        // the player serialization function, well within the 16KB back-scan window.
        g_savePlayerFn = FindFunctionViaString(
            moduleBase, moduleSize, textStart, textSize,
            "??SAV/player.xml");

        if (g_savePlayerFn != nullptr)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"[SaveFinder] Located SavePlayer at 0x%p.", g_savePlayerFn);
            Log(buf);
        }
        else
            Log(L"[SaveFinder] WARNING: SavePlayer function not found; player.xml will be stale in quicksaves.");

        g_saveWorldStateFn = FindFunctionViaString(
            moduleBase, moduleSize, textStart, textSize,
            "Saving world_state.xml - ");

        if (g_saveWorldStateFn != nullptr)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"[SaveFinder] Located SaveWorldState at 0x%p.", g_saveWorldStateFn);
            Log(buf);
        }
        else
            Log(L"[SaveFinder] WARNING: SaveWorldState function not found; world_state.xml may be stale in quicksaves.");

        if (g_savePlayerFn != nullptr && g_saveWorldStateFn != nullptr)
        {
            g_comprehensiveSaveFn = FindComprehensiveSaveFunction(
                textStart, textSize,
                reinterpret_cast<const uint8_t*>(g_savePlayerFn),
                reinterpret_cast<const uint8_t*>(g_saveWorldStateFn));

            if (g_comprehensiveSaveFn != nullptr)
            {
                wchar_t buf[64];
                swprintf_s(buf, L"[SaveFinder] Located ComprehensiveSave at 0x%p.", g_comprehensiveSaveFn);
                Log(buf);
            }
            else
                Log(L"[SaveFinder] WARNING: ComprehensiveSave function not found; will fall back to individual calls.");
        }
    }

    void TriggerNativeSave()
    {
        // The save is written during WinMain cleanup after the message loop exits, not
        // synchronously inside the WM_CLOSE handler. We post WM_CLOSE (async) so we
        // don't block mid-Lua-hook, and let DLL_PROCESS_DETACH copy the files once the
        // process has fully exited and the save is guaranteed to be on disk.
        HWND hwnd = GetNoitaHwnd();
        if (hwnd == nullptr)
        {
            Log(L"[SaveFinder] Could not find Noita window; cannot trigger save.");
            return;
        }

        wchar_t buf[64];
        swprintf_s(buf, L"[SaveFinder] Posting WM_CLOSE to HWND 0x%p.", reinterpret_cast<void*>(hwnd));
        Log(buf);

        // Signal DLL_PROCESS_DETACH to copy the files once the process fully exits
        // (the save is written during WinMain cleanup, after the message loop exits,
        // not synchronously inside the WM_CLOSE handler itself).
        SetQuicksavePending();
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}
