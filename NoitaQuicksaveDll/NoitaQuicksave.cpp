#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <process.h>

#include "GameMessages.h"
#include "Logger.h"
#include "SaveManager.h"
#include "Utility.h"

#include <stdexcept>
#include <string>

namespace
{
    constexpr int VK_F5_KEY = 0x74;
    constexpr int VK_F9_KEY = 0x78;

    HMODULE g_module = nullptr;

    void LogException(const wchar_t* prefix, const std::exception& ex)
    {
        noitaqs::LogAndQueue(std::wstring(prefix) + noitaqs::Widen(ex.what()), true);
    }

    unsigned __stdcall PollThread(void*)
    {
        try
        {
            noitaqs::InitializeSaveManager(g_module);
            noitaqs::LogAndQueue(L"Loaded. F5 = quicksave  |  F9 = quickload (restarts Noita)");
        }
        catch (const std::exception& ex)
        {
            LogException(L"Initialization FAILED: ", ex);
        }

        SHORT previousF5 = 0;
        SHORT previousF9 = 0;

        for (;;)
        {
            try
            {
                SHORT f5 = GetAsyncKeyState(VK_F5_KEY);
                SHORT f9 = GetAsyncKeyState(VK_F9_KEY);

                if ((f5 & 0x8000) != 0 && (previousF5 & 0x8000) == 0)
                {
                    noitaqs::Quicksave();
                    noitaqs::LogAndQueue(L"Quicksave created.");
                }

                if ((f9 & 0x8000) != 0 && (previousF9 & 0x8000) == 0)
                {
                    if (!noitaqs::HasQuicksaveBackup())
                    {
                        noitaqs::LogAndQueue(L"Quickload ignored: no quicksave found. Press F5 first.", true);
                    }
                    else
                    {
                        noitaqs::LogAndQueue(L"Quickload: restoring save...");
                        noitaqs::Quickload();
                        noitaqs::LogAndQueue(L"Quickload restored. Restarting Noita...", true);
                        noitaqs::RestartNoita();
                    }
                }

                previousF5 = f5;
                previousF9 = f9;
            }
            catch (const std::exception& ex)
            {
                LogException(L"Error: ", ex);
            }

            Sleep(15);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        noitaqs::InitializeGameMessages(module);

        uintptr_t thread = _beginthreadex(nullptr, 0, PollThread, nullptr, 0, nullptr);
        if (thread != 0)
            CloseHandle(reinterpret_cast<HANDLE>(thread));
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        noitaqs::ShutdownGameMessages();
        noitaqs::ShutdownLogging();
    }

    return TRUE;
}
