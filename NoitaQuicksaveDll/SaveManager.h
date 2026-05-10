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
}
