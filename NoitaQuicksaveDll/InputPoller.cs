using System.Runtime.InteropServices;

namespace NoitaQuicksaveDll;

internal static unsafe partial class InputPoller
{
    private const int VK_F5 = 0x74;
    private const int VK_F9 = 0x78;

    private static readonly SaveManager _save = new();

    public static void Run()
    {
        short prevF5 = 0, prevF9 = 0;

        while (true)
        {
            try
            {
                short f5 = GetAsyncKeyState(VK_F5);
                short f9 = GetAsyncKeyState(VK_F9);

                if ((f5 & 0x8000) != 0 && (prevF5 & 0x8000) == 0) DoQuicksave();
                if ((f9 & 0x8000) != 0 && (prevF9 & 0x8000) == 0) DoQuickload();

                prevF5 = f5;
                prevF9 = f9;
            }
            catch (Exception ex)
            {
                Logger.Log($"Error: {ex.Message}");
            }

            Thread.Sleep(15); // ~67 Hz polling; negligible CPU
        }
    }

    private static void DoQuicksave()
    {
        try
        {
            _save.Quicksave();
            Logger.Log($"{DateTime.Now:HH:mm:ss} Quicksave created.");
        }
        catch (Exception ex)
        {
            Logger.Log($"Quicksave FAILED: {ex.Message}");
        }
    }

    private static void DoQuickload()
    {
        if (!_save.HasSave)
        {
            Logger.Log("Quickload ignored: no quicksave found. Press F5 first.");
            return;
        }
        try
        {
            Logger.Log($"{DateTime.Now:HH:mm:ss} Quickload: restoring save...");
            _save.Quickload();

            Logger.Log("Quickload: restarting Noita...");
            RestartNoita();
        }
        catch (Exception ex)
        {
            Logger.Log($"Quickload FAILED: {ex.Message}");
        }
    }

    private static void RestartNoita()
    {
        // Resolve the path to the current executable (noita.exe) from inside
        // the process — no external config or search needed.
        char* buf = stackalloc char[260];
        GetModuleFileName(0, buf, 260);
        string exePath = new string(buf);

        STARTUPINFOW si = default;
        si.cb = (uint)sizeof(STARTUPINFOW);
        CreateProcess(exePath, null, null, null, 0, 0, null, null, &si, out PROCESS_INFORMATION pi);
        if (pi.hProcess != 0) CloseHandle(pi.hProcess);
        if (pi.hThread  != 0) CloseHandle(pi.hThread);

        // Kill this Noita instance immediately — do NOT let it run its normal
        // exit path, which would save in-memory state and overwrite the
        // restored save00.
        TerminateProcess(GetCurrentProcess(), 0);
    }

    // -------------------------------------------------------------------------
    // P/Invoke
    // -------------------------------------------------------------------------

    [LibraryImport("user32.dll")]
    private static partial short GetAsyncKeyState(int vKey);

    [LibraryImport("kernel32.dll")]
    private static partial uint GetModuleFileName(nint hModule, char* lpFilename, uint nSize);

    [LibraryImport("kernel32.dll")]
    private static partial nint GetCurrentProcess();

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool TerminateProcess(nint hProcess, uint uExitCode);

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool CloseHandle(nint hObject);

    [LibraryImport("kernel32.dll", StringMarshalling = StringMarshalling.Utf16)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool CreateProcess(
        string? lpApplicationName, char* lpCommandLine,
        void* lpProcessAttributes, void* lpThreadAttributes,
        int bInheritHandles, uint dwCreationFlags,
        void* lpEnvironment, char* lpCurrentDirectory,
        STARTUPINFOW* lpStartupInfo, out PROCESS_INFORMATION lpProcessInformation);

    [StructLayout(LayoutKind.Sequential)]
    private struct STARTUPINFOW
    {
        public uint cb;
        private nint _lpReserved, _lpDesktop, _lpTitle;
        private uint _dwX, _dwY, _dwXSize, _dwYSize;
        private uint _dwXCountChars, _dwYCountChars, _dwFillAttribute, _dwFlags;
        private ushort _wShowWindow, _cbReserved2;
        private nint _lpReserved2, _hStdInput, _hStdOutput, _hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION
    {
        public nint hProcess, hThread;
        public uint dwProcessId, dwThreadId;
    }
}
