#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <filesystem>
#include <string>

struct lua_State;

namespace noitaqs
{
    void InitializeGameMessages(HMODULE module);
    void ShutdownGameMessages();
    void SetGameMessageBaseDirectory(const std::filesystem::path& baseDir);
    void QueueGameMessage(const std::wstring& message, bool important = false);
    void LogAndQueue(const std::wstring& message, bool important = false);

    // Request a quicksave to be executed on the next Lua thread tick.
    // Safe to call from any thread.
    void RequestSaveTrigger();

    void DrainGameMessages(lua_State* state);
}
