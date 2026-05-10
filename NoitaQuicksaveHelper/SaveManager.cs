using System.Runtime.InteropServices;
using System.Text.Json;

namespace NoitaQuicksaveHelper;

public class SaveManager
{
    private readonly string _save00;
    private readonly string _backup;
    private readonly string _metaFile;

    public bool HasSave => Directory.Exists(_backup);
    public DateTime? SavedAt => ReadMeta()?.SavedAt;

    public SaveManager()
    {
        _save00  = ResolveSave00();
        _backup  = ResolveBackupDir();
        _metaFile = Path.Combine(_backup, ".quicksave_meta.json");
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    public void Quicksave()
    {
        if (Directory.Exists(_backup))
            Directory.Delete(_backup, recursive: true);

        CopyDirectory(_save00, _backup);
        WriteMeta(new SaveMeta { SavedAt = DateTime.UtcNow });
    }

    public void Quickload()
    {
        if (!Directory.Exists(_backup))
            throw new InvalidOperationException("No quicksave backup found.");

        if (Directory.Exists(_save00))
            Directory.Delete(_save00, recursive: true);

        CopyDirectory(_backup, _save00);
        TouchAllFiles(_save00);  // newer mtime prevents Steam Cloud conflict
    }

    // -------------------------------------------------------------------------
    // Path resolution
    // -------------------------------------------------------------------------

    private static string ResolveSave00()
    {
        string noitaRoot;

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            var appData  = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            var localLow = Path.Combine(Path.GetDirectoryName(appData)!, "LocalLow");
            noitaRoot    = Path.Combine(localLow, "Nolla_Games_Noita");
        }
        else
        {
            // Linux: Proton path
            var home = Environment.GetEnvironmentVariable("HOME")!;
            noitaRoot = Path.Combine(
                home,
                ".steam/steam/steamapps/compatdata/881100/pfx",
                "drive_c/users/steamuser/AppData/LocalLow/Nolla_Games_Noita");
        }

        return Path.Combine(noitaRoot, "save00");
    }

    private static string ResolveBackupDir()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            var docs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
            return Path.Combine(docs, "NoitaQuicksave");
        }
        else
        {
            var home = Environment.GetEnvironmentVariable("HOME")!;
            return Path.Combine(home, "NoitaQuicksave");
        }
    }

    // -------------------------------------------------------------------------
    // File helpers
    // -------------------------------------------------------------------------

    private static void CopyDirectory(string src, string dst)
    {
        Directory.CreateDirectory(dst);

        foreach (var file in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
        {
            var rel     = Path.GetRelativePath(src, file);
            var dstFile = Path.Combine(dst, rel);
            Directory.CreateDirectory(Path.GetDirectoryName(dstFile)!);
            File.Copy(file, dstFile, overwrite: true);
        }
    }

    private static void TouchAllFiles(string root)
    {
        var now = DateTime.UtcNow;
        foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
        {
            File.SetLastWriteTimeUtc(file, now);
        }
    }

    // -------------------------------------------------------------------------
    // Metadata
    // -------------------------------------------------------------------------

    private void WriteMeta(SaveMeta meta)
    {
        File.WriteAllText(_metaFile, JsonSerializer.Serialize(meta));
    }

    private SaveMeta? ReadMeta()
    {
        if (!File.Exists(_metaFile)) return null;
        try { return JsonSerializer.Deserialize<SaveMeta>(File.ReadAllText(_metaFile)); }
        catch { return null; }
    }

    private record SaveMeta
    {
        public DateTime SavedAt { get; init; }
    }
}
