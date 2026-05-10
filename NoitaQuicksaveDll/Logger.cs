namespace NoitaQuicksaveDll;

internal static class Logger
{
    private static readonly string _logPath =
        Path.Combine(AppContext.BaseDirectory, "noita_quicksave.log");

    static Logger()
    {
        try { File.WriteAllText(_logPath, $"[NoitaQS] Log started {DateTime.Now:yyyy-MM-dd HH:mm:ss}\n"); }
        catch { /* can't log the log failure */ }
    }

    public static void Log(string message)
    {
        var line = $"[NoitaQS] {message}";
        Console.WriteLine(line);
        try { File.AppendAllText(_logPath, line + "\n"); }
        catch { }
    }
}
