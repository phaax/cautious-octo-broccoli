using System.Runtime.InteropServices;

namespace NoitaQuicksaveHelper;

/// <summary>
/// Sends a synthetic keypress to the Noita window so the safe Lua mod
/// can react without any file I/O on its side.
/// </summary>
public static class SignalSender
{
    // Virtual key codes
    private const ushort VK_PAUSE  = 0x13;  // → quicksave acknowledged
    private const ushort VK_SCROLL = 0x91;  // → quickload: reload world

    public static void SendSaveAck()    => SendKey(VK_PAUSE);
    public static void SendLoadSignal() => SendKey(VK_SCROLL);

    // -------------------------------------------------------------------------
    // Win32 implementation
    // -------------------------------------------------------------------------

    [DllImport("user32.dll")] private static extern IntPtr FindWindow(string? cls, string? title);
    [DllImport("user32.dll")] private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public uint       type;
        public KEYBDINPUT ki;
        // pad to union size (mouse/hardware structs are larger on some platforms)
        private long      _pad1;
        private long      _pad2;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT
    {
        public ushort wVk;
        public ushort wScan;
        public uint   dwFlags;
        public uint   time;
        public IntPtr dwExtraInfo;
    }

    private const uint INPUT_KEYBOARD  = 1;
    private const uint KEYEVENTF_KEYUP = 0x0002;

    private static void SendKey(ushort vk)
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            SendKeyLinux(vk);
            return;
        }

        var inputs = new[]
        {
            new INPUT { type = INPUT_KEYBOARD, ki = new KEYBDINPUT { wVk = vk } },
            new INPUT { type = INPUT_KEYBOARD, ki = new KEYBDINPUT { wVk = vk, dwFlags = KEYEVENTF_KEYUP } },
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    // -------------------------------------------------------------------------
    // Linux implementation (xdotool)
    // -------------------------------------------------------------------------

    private static void SendKeyLinux(ushort vk)
    {
        // Map VK codes to xdotool key names
        var keyName = vk switch
        {
            VK_PAUSE  => "Pause",
            VK_SCROLL => "Scroll_Lock",
            _         => throw new ArgumentException($"Unmapped VK: {vk}")
        };

        // xdotool must be installed; target Noita window by name
        var psi = new System.Diagnostics.ProcessStartInfo("xdotool",
            $"search --name \"Noita\" key {keyName}")
        {
            RedirectStandardOutput = true,
            UseShellExecute = false,
        };
        System.Diagnostics.Process.Start(psi)?.WaitForExit(2000);
    }
}
