using NoitaQuicksaveHelper;

WriteSentinel();

var save = new SaveManager();

// ---------------------------------------------------------------------------
// ASP.NET Minimal API — localhost only, port 9518
// ---------------------------------------------------------------------------
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls("http://127.0.0.1:9518");
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
// Start the web server on the thread pool; keep the main thread free for the
// Win32 message loop that WH_KEYBOARD_LL requires.
// ---------------------------------------------------------------------------
var lifetime = app.Services.GetRequiredService<IHostApplicationLifetime>();
lifetime.ApplicationStopping.Register(KeyboardHook.StopMessageLoop);

// Fire-and-forget: ASP.NET runs on thread-pool threads.
var serverTask = app.StartAsync();
await serverTask;  // wait until the server is actually listening before hooking

Console.WriteLine("NoitaQuicksaveHelper running on http://127.0.0.1:9518");

if (OperatingSystem.IsWindows())
{
    Console.WriteLine("Start Noita now. F5 = quicksave, F9 = quickload.");

    // A plain static HttpClient is sufficient for fire-and-forget localhost calls.
    var http = new HttpClient();

    using var hook = new KeyboardHook(
        onF5: () => { _ = http.PostAsync("http://127.0.0.1:9518/quicksave", null); },
        onF9: () => { _ = http.PostAsync("http://127.0.0.1:9518/quickload", null); }
    );

    // Block the main thread on a Win32 message loop.
    // This is what delivers WH_KEYBOARD_LL callbacks; without it every
    // keypress system-wide lags ~300 ms while Windows times out waiting.
    KeyboardHook.RunMessageLoop();

    await app.StopAsync();
}
else
{
    Console.WriteLine("Linux: no keyboard hook available.");
    Console.WriteLine("Bind F5/F9 in xbindkeys to:");
    Console.WriteLine("  curl -X POST http://127.0.0.1:9518/quicksave");
    Console.WriteLine("  curl -X POST http://127.0.0.1:9518/quickload");

    await app.WaitForShutdownAsync();
}

// ---------------------------------------------------------------------------

static void WriteSentinel()
{
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

    var fallback = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NoitaQuicksaveHelper", "helper_running.txt");
    Directory.CreateDirectory(Path.GetDirectoryName(fallback)!);
    File.WriteAllText(fallback, DateTime.UtcNow.ToString("o"));
}
