#include "GameMessages.h"

#include "Logger.h"
#include "SaveFinder.h"
#include "SaveManager.h"
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

            fs::path originalPath = g_baseDir / L"lua51_orig.dll";
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

        // Resolve the module directory synchronously on the loader thread. The
        // poll thread and Noita's main thread later read g_baseDir without
        // synchronization, so it must be fully written before any thread starts.
        g_baseDir = GetModuleDirectory(module);

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

    void QueueGameMessage(const std::wstring& message, bool important)
    {
        if (!g_messageLockReady)
            return;

        EnterCriticalSection(&g_messageLock);

        // Drop new messages at the cap rather than popping the front — drains that
        // failed to publish push their messages back to the front, and popping front
        // would silently delete those retries.
        constexpr size_t maxQueuedMessages = 16;
        if (g_messages.size() < maxQueuedMessages)
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

    // Re-entry guard: PublishGameMessage runs Lua code (GamePrint), which can
    // call back into Noita C functions that import lua_call/lua_pcall. Those
    // imports resolve to our hooks, which would recursively call DrainGameMessages.
    // Without this guard, a flood of queued messages could blow the stack.
    thread_local bool t_inDrain = false;

    struct DrainGuard
    {
        bool engaged = false;
        ~DrainGuard() { if (engaged) t_inDrain = false; }
    };

    void DrainGameMessages(lua_State* state, bool allowPublish)
    {
        if (state == nullptr || t_inDrain)
            return;

        DrainGuard guard;
        t_inDrain = true;
        guard.engaged = true;

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

        // Publishing is suppressed when the originating pcall failed — pumping Lua
        // mid-error is risky — but the save trigger above always runs.
        if (allowPublish)
        {
            QueuedMessage message{};
            for (int i = 0; i < 4 && TryTakeQueuedMessage(message); ++i)
            {
                try
                {
                    if (!PublishGameMessage(state, message))
                    {
                        RestoreQueuedMessage(message);
                        break;
                    }
                }
                catch (...)
                {
                    RestoreQueuedMessage(message);
                    break;
                }
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
        // Always honor pending save triggers, but suppress message publishing on
        // pcall failure to avoid pumping Lua mid-error.
        noitaqs::DrainGameMessages(state, result == 0);
        return result;
    }
    catch (...)
    {
        return 2;
    }
}
