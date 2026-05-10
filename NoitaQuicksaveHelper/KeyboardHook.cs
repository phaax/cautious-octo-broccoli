using System.Runtime.InteropServices;

namespace NoitaQuicksaveHelper;

/// <summary>
/// Low-level global keyboard hook (Windows only).
/// Intercepts F5 and F9 before they reach Noita.
/// On Linux, use the HTTP API directly (e.g. from an xbindkeys rule).
/// </summary>
public sealed class KeyboardHook : IDisposable
{
    private const int    WH_KEYBOARD_LL = 13;
    private const int    WM_KEYDOWN     = 0x0100;
    private const int    VK_F5          = 0x74;
    private const int    VK_F9          = 0x78;

    private readonly HookProc        _proc;
    private readonly IntPtr          _hookId;
    private readonly Action          _onF5;
    private readonly Action          _onF9;

    public KeyboardHook(Action onF5, Action onF9)
    {
        _onF5  = onF5;
        _onF9  = onF9;
        _proc  = HookCallback;   // keep delegate alive
        _hookId = SetHook(_proc);
    }

    private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0 && wParam == WM_KEYDOWN)
        {
            var vk = Marshal.ReadInt32(lParam);
            if (vk == VK_F5) { _onF5(); return (IntPtr)1; }  // swallow
            if (vk == VK_F9) { _onF9(); return (IntPtr)1; }  // swallow
        }
        return CallNextHookEx(_hookId, nCode, wParam, lParam);
    }

    private static IntPtr SetHook(HookProc proc)
    {
        using var cur = System.Diagnostics.Process.GetCurrentProcess();
        using var mod = cur.MainModule!;
        return SetWindowsHookEx(WH_KEYBOARD_LL, proc, GetModuleHandle(mod.ModuleName!), 0);
    }

    public void Dispose() => UnhookWindowsHookEx(_hookId);

    // -------------------------------------------------------------------------
    // P/Invoke
    // -------------------------------------------------------------------------

    private delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")] private static extern IntPtr SetWindowsHookEx(int id, HookProc proc, IntPtr mod, uint threadId);
    [DllImport("user32.dll")] private static extern bool   UnhookWindowsHookEx(IntPtr id);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr id, int code, IntPtr w, IntPtr l);
    [DllImport("kernel32.dll", CharSet = CharSet.Auto)] private static extern IntPtr GetModuleHandle(string name);
}
