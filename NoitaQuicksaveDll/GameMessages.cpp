#include "GameMessages.h"

#include "Logger.h"
#include "SaveFinder.h"
#include "Utility.h"

#include <atomic>
#include <deque>
#include <stdexcept>

namespace fs = std::filesystem;

#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:lua_call=_lua_call")
#pragma comment(linker, "/EXPORT:lua_pcall=_lua_pcall")
#else
#pragma comment(linker, "/EXPORT:lua_call")
#pragma comment(linker, "/EXPORT:lua_pcall")
#endif

namespace noitaqs
{
    namespace
    {
        constexpr int LUA_GLOBALSINDEX = -10002;
        constexpr int LUA_TFUNCTION = 6;

        struct QueuedMessage
        {
            std::wstring text;
            bool important;
        };

        using LuaCallFn = void(__cdecl*)(lua_State*, int, int);
        using LuaGetFieldFn = void(__cdecl*)(lua_State*, int, const char*);
        using LuaGetTopFn = int(__cdecl*)(lua_State*);
        using LuaPCallFn = int(__cdecl*)(lua_State*, int, int, int);
        using LuaPushStringFn = void(__cdecl*)(lua_State*, const char*);
        using LuaSetTopFn = void(__cdecl*)(lua_State*, int);
        using LuaTypeFn = int(__cdecl*)(lua_State*, int);

        HMODULE g_module = nullptr;
        HMODULE g_luaOriginal = nullptr;
        CRITICAL_SECTION g_messageLock;
        bool g_messageLockReady = false;
        fs::path g_baseDir;
        std::deque<QueuedMessage> g_messages;
        std::atomic<bool> g_saveTriggerPending { false };

        LuaCallFn g_realLuaCall = nullptr;
        LuaGetFieldFn g_realLuaGetField = nullptr;
        LuaGetTopFn g_realLuaGetTop = nullptr;
        LuaPCallFn g_realLuaPCall = nullptr;
        LuaPushStringFn g_realLuaPushString = nullptr;
        LuaSetTopFn g_realLuaSetTop = nullptr;
        LuaTypeFn g_realLuaType = nullptr;

        HMODULE GetOriginalLuaModule()
        {
            if (g_luaOriginal != nullptr)
                return g_luaOriginal;

            fs::path baseDir = g_baseDir.empty() ? GetModuleDirectory(g_module) : g_baseDir;
            fs::path originalPath = baseDir / L"lua51_orig.dll";
            g_luaOriginal = LoadLibraryW(originalPath.c_str());
            if (g_luaOriginal == nullptr)
                g_luaOriginal = GetModuleHandleW(L"lua51_orig.dll");

            return g_luaOriginal;
        }

        FARPROC RequireLuaProc(const char* name)
        {
            HMODULE lua = GetOriginalLuaModule();
            if (lua == nullptr)
                throw std::runtime_error("lua51_orig.dll is not loaded.");

            FARPROC proc = GetProcAddress(lua, name);
            if (proc == nullptr)
                throw std::runtime_error("A required Lua function is missing.");

            return proc;
        }

        void ResolveLuaFunctions()
        {
            if (g_realLuaPCall != nullptr)
                return;

            g_realLuaCall = reinterpret_cast<LuaCallFn>(RequireLuaProc("lua_call"));
            g_realLuaGetField = reinterpret_cast<LuaGetFieldFn>(RequireLuaProc("lua_getfield"));
            g_realLuaGetTop = reinterpret_cast<LuaGetTopFn>(RequireLuaProc("lua_gettop"));
            g_realLuaPCall = reinterpret_cast<LuaPCallFn>(RequireLuaProc("lua_pcall"));
            g_realLuaPushString = reinterpret_cast<LuaPushStringFn>(RequireLuaProc("lua_pushstring"));
            g_realLuaSetTop = reinterpret_cast<LuaSetTopFn>(RequireLuaProc("lua_settop"));
            g_realLuaType = reinterpret_cast<LuaTypeFn>(RequireLuaProc("lua_type"));
        }

        bool TryTakeQueuedMessage(QueuedMessage& message)
        {
            if (!g_messageLockReady)
                return false;

            EnterCriticalSection(&g_messageLock);

            bool hasMessage = !g_messages.empty();
            if (hasMessage)
            {
                message = g_messages.front();
                g_messages.pop_front();
            }

            LeaveCriticalSection(&g_messageLock);
            return hasMessage;
        }

        void RestoreQueuedMessage(const QueuedMessage& message)
        {
            if (!g_messageLockReady)
                return;

            EnterCriticalSection(&g_messageLock);
            g_messages.push_front(message);
            LeaveCriticalSection(&g_messageLock);
        }

        bool HasLuaFunction(lua_State* state, const char* name, int stackTop)
        {
            g_realLuaGetField(state, LUA_GLOBALSINDEX, name);
            bool hasFunction = g_realLuaType(state, -1) == LUA_TFUNCTION;
            g_realLuaSetTop(state, stackTop);
            return hasFunction;
        }

        bool PublishGameMessage(lua_State* state, const QueuedMessage& message)
        {
            ResolveLuaFunctions();

            int stackTop = g_realLuaGetTop(state);
            const char* functionName = message.important ? "GamePrintImportant" : "GamePrint";
            if (!HasLuaFunction(state, functionName, stackTop))
                return false;

            std::string text = ToUtf8(message.text);
            g_realLuaGetField(state, LUA_GLOBALSINDEX, functionName);
            if (message.important)
            {
                g_realLuaPushString(state, "Noita Quick Save");
                g_realLuaPushString(state, text.c_str());
                g_realLuaPCall(state, 2, 0, 0);
            }
            else
            {
                g_realLuaPushString(state, text.c_str());
                g_realLuaPCall(state, 1, 0, 0);
            }

            g_realLuaSetTop(state, stackTop);
            return true;
        }
    }

    void InitializeGameMessages(HMODULE module)
    {
        g_module = module;

        if (!g_messageLockReady)
        {
            InitializeCriticalSection(&g_messageLock);
            g_messageLockReady = true;
        }
    }

    void ShutdownGameMessages()
    {
        if (g_messageLockReady)
        {
            DeleteCriticalSection(&g_messageLock);
            g_messageLockReady = false;
        }
    }

    void SetGameMessageBaseDirectory(const fs::path& baseDir)
    {
        g_baseDir = baseDir;
    }

    void QueueGameMessage(const std::wstring& message, bool important)
    {
        if (!g_messageLockReady)
            return;

        EnterCriticalSection(&g_messageLock);

        constexpr size_t maxQueuedMessages = 16;
        while (g_messages.size() >= maxQueuedMessages)
            g_messages.pop_front();

        g_messages.push_back({ message, important });

        LeaveCriticalSection(&g_messageLock);
    }

    void LogAndQueue(const std::wstring& message, bool important)
    {
        if (important)
            Log(message);

        QueueGameMessage(message, important);
    }

    void RequestSaveTrigger()
    {
        g_saveTriggerPending.store(true, std::memory_order_relaxed);
    }

    void DrainGameMessages(lua_State* state)
    {
        if (state == nullptr)
            return;

        if (g_saveTriggerPending.exchange(false, std::memory_order_relaxed))
        {
            try
            {
                // Flush session numbers via the Lua API
                ResolveLuaFunctions();
                int top = g_realLuaGetTop(state);
                g_realLuaGetField(state, LUA_GLOBALSINDEX, "SessionNumbersSave");
                if (g_realLuaType(state, -1) == LUA_TFUNCTION)
                    g_realLuaPCall(state, 0, 0, 0);
                g_realLuaSetTop(state, top);

                // Post WM_CLOSE — the game will write all save files during its exit
                // sequence. DLL_PROCESS_DETACH will copy the files and relaunch Noita
                // once the process has fully exited and the save is complete.
                TriggerNativeSave();
                LogAndQueue(L"Quicksave in progress. Noita will restart...", true);
            }
            catch (const std::exception& ex)
            {
                LogAndQueue(std::wstring(L"Quicksave FAILED: ") + Widen(ex.what()), true);
            }
            catch (...)
            {
                LogAndQueue(L"Quicksave FAILED: unknown error.", true);
            }
        }

        QueuedMessage message{};
        for (int i = 0; i < 4 && TryTakeQueuedMessage(message); ++i)
        {
            try
            {
                if (!PublishGameMessage(state, message))
                {
                    RestoreQueuedMessage(message);
                    return;
                }
            }
            catch (...)
            {
                RestoreQueuedMessage(message);
                return;
            }
        }
    }

    void CallOriginalLua(lua_State* state, int nargs, int nresults)
    {
        ResolveLuaFunctions();
        g_realLuaCall(state, nargs, nresults);
    }

    int PCallOriginalLua(lua_State* state, int nargs, int nresults, int errfunc)
    {
        ResolveLuaFunctions();
        return g_realLuaPCall(state, nargs, nresults, errfunc);
    }
}

extern "C" __declspec(dllexport) void __cdecl lua_call(lua_State* state, int nargs, int nresults)
{
    try
    {
        noitaqs::CallOriginalLua(state, nargs, nresults);
        noitaqs::DrainGameMessages(state);
    }
    catch (...)
    {
    }
}

extern "C" __declspec(dllexport) int __cdecl lua_pcall(lua_State* state, int nargs, int nresults, int errfunc)
{
    try
    {
        int result = noitaqs::PCallOriginalLua(state, nargs, nresults, errfunc);
        if (result == 0)
            noitaqs::DrainGameMessages(state);
        return result;
    }
    catch (...)
    {
        return 2;
    }
}
