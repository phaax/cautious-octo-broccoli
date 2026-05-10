using NoitaQuicksaveHelper;

// ---------------------------------------------------------------------------
// Write sentinel so the safe Lua mod can confirm the helper is running.
// This must happen BEFORE the game's VFS is snapshotted (i.e. before launch).
// The user is expected to start this helper first, then launch Noita.
// ---------------------------------------------------------------------------
WriteSentinel();

var save = new SaveManager();

// ---------------------------------------------------------------------------
// ASP.NET Minimal API — localhost only, port 9518
// ---------------------------------------------------------------------------
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls("http://127.0.0.1:9518");
// Suppress the default "Now listening on …" banner clutter
builder.Logging.SetMinimumLevel(LogLevel.Warning);

var app = builder.Build();

app.MapGet("/status", () => Results.Ok(new
{
    hasSave = save.HasSave,
    savedAt = save.SavedAt?.ToString("o"),
}));

app.MapPost("/quicksave", () =>
{
    try
    {
        save.Quicksave();
        SignalSender.SendSaveAck();
        return Results.Ok("Quicksave complete.");
    }
    catch (Exception ex)
    {
        return Results.Problem(ex.Message);
    }
});

app.MapPost("/quickload", () =>
{
    if (!save.HasSave)
        return Results.BadRequest("No quicksave found.");

    try
    {
        save.Quickload();
        SignalSender.SendLoadSignal();
        return Results.Ok("Quickload complete.");
    }
    catch (Exception ex)
    {
        return Results.Problem(ex.Message);
    }
});

// ---------------------------------------------------------------------------
// Global keyboard hook — intercept F5 / F9 before Noita sees them
// ---------------------------------------------------------------------------
KeyboardHook? hook = null;
if (OperatingSystem.IsWindows())
{
    hook = new KeyboardHook(
        onF5: () => app.Services.GetRequiredService<IHttpClientFactory>()
                        // Reuse HTTP API so all logic is in one place.
                        // Fire-and-forget; hook callback must return quickly.
                        .CreateClient().PostAsync("http://127.0.0.1:9518/quicksave", null),
        onF9: () => app.Services.GetRequiredService<IHttpClientFactory>()
                        .CreateClient().PostAsync("http://127.0.0.1:9518/quickload", null)
    );
}
else
{
    Console.WriteLine("Linux: keyboard hook not available. " +
                      "Bind F5/F9 to curl calls via xbindkeys:");
    Console.WriteLine("  curl -X POST http://127.0.0.1:9518/quicksave");
    Console.WriteLine("  curl -X POST http://127.0.0.1:9518/quickload");
}

Console.WriteLine("NoitaQuicksaveHelper running on http://127.0.0.1:9518");
Console.WriteLine("Start Noita now. F5 = quicksave, F9 = quickload.");

await app.RunAsync();

hook?.Dispose();

// ---------------------------------------------------------------------------

static void WriteSentinel()
{
    // Walk up from the executable to find the mod's data directory.
    // Expected layout: repo/NoitaQuicksaveHelper/bin/… and repo/noita-quicksave/data/
    var exe    = AppContext.BaseDirectory;
    var search = new DirectoryInfo(exe);

    while (search != null)
    {
        var sentinel = Path.Combine(search.FullName, "noita-quicksave", "data", "helper_running.txt");
        if (Directory.Exists(Path.GetDirectoryName(sentinel)))
        {
            File.WriteAllText(sentinel, DateTime.UtcNow.ToString("o"));
            return;
        }
        search = search.Parent;
    }

    // If not found (e.g. installed separately), write to a well-known location.
    var fallback = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NoitaQuicksaveHelper", "helper_running.txt");
    Directory.CreateDirectory(Path.GetDirectoryName(fallback)!);
    File.WriteAllText(fallback, DateTime.UtcNow.ToString("o"));
}
