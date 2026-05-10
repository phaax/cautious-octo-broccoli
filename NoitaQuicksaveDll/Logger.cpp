#include "Logger.h"

#include "Utility.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

namespace noitaqs
{
    namespace
    {
        CRITICAL_SECTION g_logLock;
        bool g_logLockReady = false;
        fs::path g_logPath;

        void AppendUtf8Line(const fs::path& path, const std::wstring& line)
        {
            if (path.empty())
                return;

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
    }

    void InitializeLogging(const fs::path& logPath)
    {
        if (!g_logLockReady)
        {
            InitializeCriticalSection(&g_logLock);
            g_logLockReady = true;
        }

        g_logPath = logPath;
    }

    void ShutdownLogging()
    {
        if (g_logLockReady)
        {
            DeleteCriticalSection(&g_logLock);
            g_logLockReady = false;
        }
    }

    void Log(const std::wstring& message)
    {
        std::wstring line = L"[NoitaQS] " + message;

        if (g_logLockReady)
            EnterCriticalSection(&g_logLock);

        AppendUtf8Line(g_logPath, line);

        if (g_logLockReady)
            LeaveCriticalSection(&g_logLock);
    }
}
