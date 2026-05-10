#include "SaveManager.h"

#include "GameMessages.h"
#include "Logger.h"
#include "Utility.h"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace noitaqs
{
    namespace
    {
        fs::path g_baseDir;
        fs::path g_save00;
        fs::path g_backup;

        // Raw Win32 path copies — safe to access from DLL_PROCESS_DETACH.
        wchar_t g_save00Raw[MAX_PATH * 2] = {};
        wchar_t g_backupRaw[MAX_PATH * 2] = {};

        volatile LONG g_quicksavePending = 0;

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

        bool IsSaveDirectory(const fs::path& path)
        {
            return fs::exists(path / L"world");
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
    }

    void InitializeSaveManager(HMODULE module)
    {
        g_baseDir = GetModuleDirectory(module);
        g_save00 = ResolveSave00();

        g_backup = g_baseDir / L"NoitaQuicksave" / L"save00";

        // Store raw wchar_t copies for use in DLL_PROCESS_DETACH.
        wcsncpy_s(g_save00Raw, g_save00.c_str(), _TRUNCATE);
        wcsncpy_s(g_backupRaw, g_backup.c_str(), _TRUNCATE);

        SetGameMessageBaseDirectory(g_baseDir);
        InitializeLogging(g_baseDir / L"noita_quicksave.log");
    }

    bool HasQuicksaveBackup()
    {
        return IsSaveDirectory(g_backup);
    }

    void Quicksave()
    {
        if (fs::exists(g_backup))
            fs::remove_all(g_backup);

        CopyDirectory(g_save00, g_backup);
    }

    void Quickload()
    {
        if (!HasQuicksaveBackup())
            throw std::runtime_error("Backup directory not found.");

        if (fs::exists(g_save00))
            fs::remove_all(g_save00);

        CopyDirectory(g_backup, g_save00);
        TouchAllFiles(g_save00);
    }

    void SetQuicksavePending()
    {
        InterlockedExchange(&g_quicksavePending, 1);
    }

    // Win32-only recursive directory removal (safe in DLL_PROCESS_DETACH).
    static void Win32RemoveDir(const wchar_t* path)
    {
        wchar_t pattern[MAX_PATH * 2];
        if (swprintf_s(pattern, L"%s\\*", path) < 0) return;

        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return;

        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            wchar_t child[MAX_PATH * 2];
            if (swprintf_s(child, L"%s\\%s", path, fd.cFileName) < 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                Win32RemoveDir(child);
            else
                DeleteFileW(child);
        } while (FindNextFileW(h, &fd));

        FindClose(h);
        RemoveDirectoryW(path);
    }

    // Win32-only recursive directory copy (safe in DLL_PROCESS_DETACH).
    static void Win32CopyDir(const wchar_t* src, const wchar_t* dst)
    {
        CreateDirectoryW(dst, nullptr);

        wchar_t pattern[MAX_PATH * 2];
        if (swprintf_s(pattern, L"%s\\*", src) < 0) return;

        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return;

        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            wchar_t srcChild[MAX_PATH * 2], dstChild[MAX_PATH * 2];
            if (swprintf_s(srcChild, L"%s\\%s", src, fd.cFileName) < 0) continue;
            if (swprintf_s(dstChild, L"%s\\%s", dst, fd.cFileName) < 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                Win32CopyDir(srcChild, dstChild);
            else
                CopyFileW(srcChild, dstChild, FALSE);
        } while (FindNextFileW(h, &fd));

        FindClose(h);
    }

    void FinalizePendingQuicksave()
    {
        if (InterlockedCompareExchange(&g_quicksavePending, 0, 1) == 0)
            return;
        if (g_save00Raw[0] == L'\0' || g_backupRaw[0] == L'\0')
            return;

        Win32RemoveDir(g_backupRaw);
        Win32CopyDir(g_save00Raw, g_backupRaw);

        // Relaunch Noita. GetModuleFileNameW is safe in DLL_PROCESS_DETACH.
        wchar_t exePath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath))) == 0)
            return;

        // Derive the working directory (directory containing noita.exe).
        wchar_t workDir[MAX_PATH]{};
        wcsncpy_s(workDir, exePath, _TRUNCATE);
        wchar_t* lastSlash = wcsrchr(workDir, L'\\');
        if (lastSlash) *lastSlash = L'\0';

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        CreateProcessW(exePath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, workDir, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
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
}
