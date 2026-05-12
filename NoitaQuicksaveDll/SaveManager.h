#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

namespace noitaqs
{
    void InitializeSaveManager(HMODULE module);
    bool HasQuicksaveBackup();

    // In-process copy of save00 → backup. Used by the in-place quicksave path
    // after Noita's native SavePlayer/SaveWorldState have flushed the two XML
    // files. Must be called on a thread that won't race with Noita's own
    // chunk writers — practically, the main game thread between Lua calls.
    void CopySave00ToBackup();

    void Quickload();
    void RestartNoita();

    // Signal that the next process exit should trigger a quicksave file copy.
    void SetQuicksavePending();

    // True when SetQuicksavePending was called and we haven't yet finalized.
    bool IsQuicksavePending();

    // Pre-create the replacement Noita process in CREATE_SUSPENDED state. Called
    // from the main game thread before posting WM_CLOSE so DLL_PROCESS_DETACH only
    // has to ResumeThread (which is loader-safe). Returns false if creation failed;
    // FinalizePendingQuicksave will fall back to CreateProcessW in that case.
    bool PrepareSuspendedRestart();

    // Called from DLL_PROCESS_DETACH (process termination only) to copy the save files
    // and launch a new Noita process. Uses only Win32 APIs — no C++ runtime.
    void FinalizePendingQuicksave();
}
