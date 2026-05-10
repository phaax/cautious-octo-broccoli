using System.Diagnostics;
using System.Text.Json;

namespace NoitaQuicksaveHelper;

public class NoitaController
{
    private static readonly string ConfigFile = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NoitaQuicksaveHelper", "config.json");

    private string? _exePath;

    public bool IsRunning => Process.GetProcessesByName("noita").Length > 0;

    /// <summary>
    /// Returns the Noita exe path, caching it from the live process when possible.
    /// Persists the path to disk so quickload works even when Noita is not running.
    /// </summary>
    public string? ExePath
    {
        get
        {
            // Prefer a live process — gives us the authoritative path.
            var proc = Process.GetProcessesByName("noita").FirstOrDefault();
            if (proc != null)
            {
                try
                {
                    var path = proc.MainModule?.FileName;
                    if (path != null && File.Exists(path))
                    {
                        _exePath = path;
                        PersistPath(path);
                    }
                }
                catch { /* access denied on some Windows configs — ignore */ }
            }

            if (_exePath != null && File.Exists(_exePath))
                return _exePath;

            // Fall back to the last path we persisted.
            _exePath = LoadPersistedPath();
            return _exePath;
        }
    }

    /// <summary>
    /// Sends WM_CLOSE to Noita's main window (same as clicking the X button),
    /// which triggers Noita's normal save-and-exit flow. Waits for the process
    /// to fully exit before returning so callers can safely write to save00.
    /// </summary>
    public async Task CloseAndWaitAsync()
    {
        var procs = Process.GetProcessesByName("noita");
        if (procs.Length == 0) return;

        foreach (var p in procs)
            p.CloseMainWindow();

        // Give Noita up to 20 s to save and exit cleanly.
        var deadline = DateTime.UtcNow.AddSeconds(20);
        while (DateTime.UtcNow < deadline)
        {
            if (Process.GetProcessesByName("noita").Length == 0) return;
            await Task.Delay(300);
        }

        // Force-kill anything that didn't exit in time.
        foreach (var p in Process.GetProcessesByName("noita"))
        {
            Console.WriteLine($"WARNING: Noita did not exit cleanly; force-killing PID {p.Id}.");
            p.Kill();
        }
    }

    public void Launch()
    {
        var path = ExePath
            ?? throw new InvalidOperationException(
                "Noita executable not found. Launch Noita at least once before using quickload.");

        Process.Start(new ProcessStartInfo(path)
        {
            WorkingDirectory = Path.GetDirectoryName(path),
            UseShellExecute  = true,
        });

        Console.WriteLine($"Noita launched: {path}");
    }

    // -------------------------------------------------------------------------

    private static void PersistPath(string path)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ConfigFile)!);
            File.WriteAllText(ConfigFile, JsonSerializer.Serialize(new { noitaExePath = path }));
        }
        catch { }
    }

    private static string? LoadPersistedPath()
    {
        try
        {
            if (!File.Exists(ConfigFile)) return null;
            var doc  = JsonDocument.Parse(File.ReadAllText(ConfigFile));
            var path = doc.RootElement.GetProperty("noitaExePath").GetString();
            return path != null && File.Exists(path) ? path : null;
        }
        catch { return null; }
    }
}
