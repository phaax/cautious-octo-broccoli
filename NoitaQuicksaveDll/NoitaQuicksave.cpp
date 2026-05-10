#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <process.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    constexpr int VK_F5_KEY = 0x74;
    constexpr int VK_F9_KEY = 0x78;

    HMODULE g_module = nullptr;
    CRITICAL_SECTION g_logLock;
    bool g_logLockReady = false;

    fs::path g_baseDir;
    fs::path g_logPath;
    fs::path g_save00;
    fs::path g_backup;

    std::wstring Widen(const char* value)
    {
        if (value == nullptr || *value == '\0')
            return L"";

        int length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
        if (length <= 0)
            length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
        if (length <= 0)
            return L"(unprintable error)";

        std::wstring result(static_cast<size_t>(length - 1), L'\0');
        UINT codePage = MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), length) > 0
            ? CP_UTF8
            : CP_ACP;

        if (codePage == CP_ACP)
            MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), length);

        return result;
    }

    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
            return {};

        int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            return {};

        std::string result(static_cast<size_t>(length - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr);
        return result;
    }

    fs::path GetModuleDirectory(HMODULE module)
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length == 0 || length == std::size(buffer))
            return fs::current_path();

        return fs::path(buffer).parent_path();
    }

    fs::path ResolveSave00()
    {
        wchar_t appData[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
        if (length == 0 || length >= std::size(appData))
            throw std::runtime_error("APPDATA is not available.");

        return fs::path(appData).parent_path()
            / L"LocalLow"
            / L"Nolla_Games_Noita"
            / L"save00";
    }

    void AppendUtf8Line(const fs::path& path, const std::wstring& line)
    {
        std::string text = ToUtf8(line + L"\r\n");
        HANDLE file = CreateFileW(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;
        WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(file);
    }

    void Log(const std::wstring& message)
    {
        std::wstring line = L"[NoitaQS] " + message;

        if (g_logLockReady)
            EnterCriticalSection(&g_logLock);

        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        if (console != INVALID_HANDLE_VALUE && console != nullptr)
        {
            DWORD written = 0;
            WriteConsoleW(console, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
            WriteConsoleW(console, L"\r\n", 2, &written, nullptr);
        }

        AppendUtf8Line(g_logPath, line);

        if (g_logLockReady)
            LeaveCriticalSection(&g_logLock);
    }

    void InitPaths()
    {
        g_baseDir = GetModuleDirectory(g_module);
        g_logPath = g_baseDir / L"noita_quicksave.log";
        g_save00 = ResolveSave00();
        g_backup = g_baseDir / L"NoitaQuicksave";

        SYSTEMTIME now{};
        GetLocalTime(&now);

        wchar_t started[128]{};
        swprintf_s(
            started,
            L"Log started %04hu-%02hu-%02hu %02hu:%02hu:%02hu",
            now.wYear,
            now.wMonth,
            now.wDay,
            now.wHour,
            now.wMinute,
            now.wSecond);

        DeleteFileW(g_logPath.c_str());
        Log(started);
    }

    void CopyDirectory(const fs::path& source, const fs::path& destination)
    {
        if (!fs::exists(source))
            throw std::runtime_error("Noita save00 directory was not found.");

        fs::create_directories(destination);

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(source))
        {
            fs::path relative = fs::relative(entry.path(), source);
            fs::path target = destination / relative;

            if (entry.is_directory())
            {
                fs::create_directories(target);
            }
            else if (entry.is_regular_file())
            {
                fs::create_directories(target.parent_path());
                fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
            }
        }
    }

    void TouchAllFiles(const fs::path& root)
    {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;

            HANDLE file = CreateFileW(
                entry.path().c_str(),
                FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);

            if (file == INVALID_HANDLE_VALUE)
                continue;

            SetFileTime(file, nullptr, nullptr, &now);
            CloseHandle(file);
        }
    }

    void Quicksave()
    {
        if (fs::exists(g_backup))
            fs::remove_all(g_backup);

        CopyDirectory(g_save00, g_backup);
    }

    void Quickload()
    {
        if (!fs::exists(g_backup))
            throw std::runtime_error("Backup directory not found.");

        if (fs::exists(g_save00))
            fs::remove_all(g_save00);

        CopyDirectory(g_backup, g_save00);
        TouchAllFiles(g_save00);
    }

    void RestartNoita()
    {
        wchar_t exePath[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
        if (length == 0 || length == std::size(exePath))
            throw std::runtime_error("Could not resolve noita.exe path.");

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);

        PROCESS_INFORMATION process{};
        BOOL created = CreateProcessW(
            exePath,
            nullptr,
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            g_baseDir.c_str(),
            &startup,
            &process);

        if (!created)
            throw std::runtime_error("Could not restart Noita.");

        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);

        TerminateProcess(GetCurrentProcess(), 0);
    }

    void LogException(const wchar_t* prefix, const std::exception& ex)
    {
        Log(std::wstring(prefix) + Widen(ex.what()));
    }

    unsigned __stdcall PollThread(void*)
    {
        AllocConsole();

        try
        {
            InitPaths();
            Log(L"Loaded. F5 = quicksave  |  F9 = quickload (restarts Noita)");
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
                    Quicksave();
                    Log(L"Quicksave created.");
                }

                if ((f9 & 0x8000) != 0 && (previousF9 & 0x8000) == 0)
                {
                    if (!fs::exists(g_backup))
                    {
                        Log(L"Quickload ignored: no quicksave found. Press F5 first.");
                    }
                    else
                    {
                        Log(L"Quickload: restoring save...");
                        Quickload();
                        Log(L"Quickload: restarting Noita...");
                        RestartNoita();
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
        InitializeCriticalSection(&g_logLock);
        g_logLockReady = true;

        uintptr_t thread = _beginthreadex(nullptr, 0, PollThread, nullptr, 0, nullptr);
        if (thread != 0)
            CloseHandle(reinterpret_cast<HANDLE>(thread));
    }
    else if (reason == DLL_PROCESS_DETACH && g_logLockReady)
    {
        DeleteCriticalSection(&g_logLock);
        g_logLockReady = false;
    }

    return TRUE;
}
