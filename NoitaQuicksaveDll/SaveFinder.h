#pragma once

namespace noitaqs
{
    // Scans noita.exe in memory for known save-related functions using debug strings
    // embedded in the binary. Must be called once from the main DLL init path.
    void InitSaveFinder();

    // Posts WM_CLOSE to the game window so the game's own exit handler flushes
    // all save files. Sets a pending-quicksave flag so DLL_PROCESS_DETACH copies
    // the files and relaunches Noita once the process has fully exited.
    // Must be called on the main game thread.
    void TriggerNativeSave();
}
