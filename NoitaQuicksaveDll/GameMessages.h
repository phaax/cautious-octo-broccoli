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
    void QueueGameMessage(const std::wstring& message, bool important = false);
    void LogAndQueue(const std::wstring& message, bool important = false);

    // Request a quicksave to be executed on the next Lua thread tick.
    // Safe to call from any thread.
    void RequestSaveTrigger();

    // Drains the save trigger (always) and the message queue (only when allowPublish
    // is true — callers set this to false when the originating pcall returned an
    // error, so we don't pump Lua mid-error).
    void DrainGameMessages(lua_State* state, bool allowPublish = true);
}
