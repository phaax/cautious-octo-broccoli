using System.Runtime.InteropServices;

namespace NoitaQuicksaveDll;

internal class SaveManager
{
    private readonly string _save00;
    private readonly string _backup;

    public bool HasSave => Directory.Exists(_backup);

    public SaveManager()
    {
        _save00  = ResolveSave00();
        _backup  = ResolveBackupDir();
    }

    public void Quicksave()
    {
        if (Directory.Exists(_backup))
            Directory.Delete(_backup, recursive: true);

        CopyDirectory(_save00, _backup);
    }

    public void Quickload()
    {
        if (!Directory.Exists(_backup))
            throw new InvalidOperationException("Backup directory not found.");

        if (Directory.Exists(_save00))
            Directory.Delete(_save00, recursive: true);

        CopyDirectory(_backup, _save00);
        TouchAllFiles(_save00); // newer mtimes prevent Steam Cloud conflict
    }

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
            var home = Environment.GetEnvironmentVariable("HOME")!;
            noitaRoot = Path.Combine(home,
                ".steam/steam/steamapps/compatdata/881100/pfx",
                "drive_c/users/steamuser/AppData/LocalLow/Nolla_Games_Noita");
        }

        return Path.Combine(noitaRoot, "save00");
    }

    private static string ResolveBackupDir()
    {
        return Path.Combine(AppContext.BaseDirectory, "NoitaQuicksave");
    }

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
            File.SetLastWriteTimeUtc(file, now);
    }
}
