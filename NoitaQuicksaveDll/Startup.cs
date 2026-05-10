using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace NoitaQuicksaveDll;

internal static partial class Startup
{
    [ModuleInitializer]
    internal static void Initialize()
    {
        AllocConsole();
        Logger.Log("Loaded. F5 = quicksave  |  F9 = quickload (restarts Noita)");
        new Thread(InputPoller.Run) { IsBackground = true, Name = "NoitaQS" }.Start();
    }

    [LibraryImport("kernel32.dll")]
    private static partial int AllocConsole();
}
