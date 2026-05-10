using NoitaQuicksaveHelper;

var save   = new SaveManager();
var noita  = new NoitaController();

// ---------------------------------------------------------------------------
// ASP.NET Minimal API — localhost only, port 9518
// ---------------------------------------------------------------------------
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls("http://127.0.0.1:9518");
builder.Logging.SetMinimumLevel(LogLevel.Warning);

var app = builder.Build();

// GET /status — check state without side-effects
app.MapGet("/status", () => Results.Ok(new
{
    hasSave       = save.HasSave,
    savedAt       = save.SavedAt?.ToString("o"),
    noitaRunning  = noita.IsRunning,
    noitaExePath  = noita.ExePath,
}));

// POST /quicksave — snapshot save00; game keeps running
app.MapPost("/quicksave", async () =>
{
    if (!noita.IsRunning)
        return Results.BadRequest("Noita is not running. Nothing to save.");

    try
    {
        save.Quicksave();
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Quicksave created.");
        return Results.Ok("Quicksave created.");
    }
    catch (Exception ex)
    {
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Quicksave FAILED: {ex.Message}");
        return Results.Problem(ex.Message);
    }
});

// POST /quickload — restore snapshot, restart Noita
app.MapPost("/quickload", async () =>
{
    if (!save.HasSave)
        return Results.BadRequest("No quicksave found. Press F5 in-game first.");

    try
    {
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Quickload: closing Noita...");
        await noita.CloseAndWaitAsync();

        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Quickload: restoring save...");
        save.Quickload();

        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Quickload: launching Noita...");
        noita.Launch();

        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Quickload complete.");
        return Results.Ok("Quickload complete. Noita is restarting.");
    }
    catch (Exception ex)
    {
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Quickload FAILED: {ex.Message}");
        return Results.Problem(ex.Message);
    }
});

// ---------------------------------------------------------------------------
// Start web server, then block the main thread on the Win32 message loop
// (required for WH_KEYBOARD_LL to deliver callbacks without system-wide lag).
// ---------------------------------------------------------------------------
await app.StartAsync();

Console.WriteLine("NoitaQuicksaveHelper running on http://127.0.0.1:9518");
Console.WriteLine("F5 = quicksave (instant)  |  F9 = quickload (restarts Noita)");
Console.WriteLine("Press Ctrl-C to exit.");

if (OperatingSystem.IsWindows())
{
    var http = new HttpClient();

    Console.CancelKeyPress += (_, e) =>
    {
        e.Cancel = true;
        KeyboardHook.StopMessageLoop();
    };

    using var hook = new KeyboardHook(
        onF5: () => { _ = http.PostAsync("http://127.0.0.1:9518/quicksave", null); },
        onF9: () => { _ = http.PostAsync("http://127.0.0.1:9518/quickload", null); }
    );

    KeyboardHook.RunMessageLoop();
    await app.StopAsync();
}
else
{
    Console.WriteLine("Linux: bind F5/F9 via xbindkeys:");
    Console.WriteLine("  curl -X POST http://127.0.0.1:9518/quicksave");
    Console.WriteLine("  curl -X POST http://127.0.0.1:9518/quickload");
    await app.WaitForShutdownAsync();
}
