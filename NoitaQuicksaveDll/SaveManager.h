#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

namespace noitaqs
{
    void InitializeSaveManager(HMODULE module);
    bool HasQuicksaveBackup();
    void Quicksave();
    void Quickload();
    void RestartNoita();

    // Signal that the next process exit should trigger a quicksave file copy.
    void SetQuicksavePending();

    // Called from DLL_PROCESS_DETACH (process termination only) to copy the save files
    // and launch a new Noita process. Uses only Win32 APIs — no C++ runtime.
    void FinalizePendingQuicksave();
}
