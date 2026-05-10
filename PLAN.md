# Plan: Noita Quick Save / Load Mod

## 1. Goal

Add F5 (quicksave) and F9 (quickload) hotkeys to Noita as a two-component system:

- A **safe** Noita Lua mod (no unsafe mode required, no warnings shown to players).
- A small **C# helper** (`NoitaQuicksaveHelper.exe`) that runs alongside the game.

Constraints:
- Must work with **Steam Cloud Sync enabled**.
- Lua mod must remain in **safe mode** (no `request_no_api_restrictions`).

---

## 2. Why a Two-Component Design

Safe-mode Lua strips `io`, `os`, `socket`, and every other library that touches
the file system or network. This means the Lua mod cannot:

- Copy or delete folders.
- Call a localhost HTTP server.
- Detect F5/F9 reliably at the OS level (key interception is impossible in safe Lua).

The only feasible safe-mode design is therefore **C# handles everything heavy;
it signals the Lua mod via a synthetic keypress**. The Lua mod's sole
responsibility is to call `GameReloadActiveWorldFromSave()` at the right moment
— something only in-game Lua can do.

---

## 3. How Noita Saves

Save state lives in a folder of XML and binary files:

| Platform | Path |
|----------|------|
| Windows  | `%APPDATA%\..\LocalLow\Nolla_Games_Noita\save00\` |
| Linux (Proton) | `~/.steam/steam/steamapps/compatdata/881100/pfx/drive_c/users/steamuser/AppData/LocalLow/Nolla_Games_Noita/save00/` |

Noita flushes state to disk at floor/area transitions and on a normal "Save and
exit". During active gameplay the in-memory state is ahead of what is on disk.

---

## 4. Steam Cloud Compatibility

Steam Cloud syncs `save00` (and likely `save01`–`save06`) **only on game launch
and game exit** — never during active gameplay.

### Strategy

Store the quicksave backup **outside the Steam Cloud sync path**:

| Platform | Backup location |
|----------|----------------|
| Windows  | `%USERPROFILE%\Documents\NoitaQuicksave\` |
| Linux    | `~/NoitaQuicksave/` |

This folder is never touched by Steam. Because:

1. **Quicksave (F5)** happens mid-session → copy save00 to Documents backup. Cloud
   is not running. Cloud eventually syncs save00 as-is when the game exits. ✓
2. **Quickload (F9)** happens mid-session → swap Documents backup into save00, hot-
   reload via `GameReloadActiveWorldFromSave()`, **no game exit**. Cloud runs next
   time the game exits, at which point save00 contains the restored (correct) state
   — it uploads that. ✓

### The one remaining edge case

If the user quicksaves (F5) and then exits the game *without* quickloading, Steam
Cloud uploads the current save00. On the next session, a quickload restores the
Documents backup, which may be "older" than what Cloud has. Steam Cloud compares
mtimes and may offer a conflict dialog.

**Fix**: When restoring, the C# helper `touch`es (updates mtime to `DateTime.UtcNow`)
all copied files before the game engine reads them. Steam Cloud then sees them as
"newer than cloud" and uploads without conflict.

---

## 5. Architecture

```
┌─────────────────────────────────────────┐
│  NoitaQuicksaveHelper.exe (C#)          │
│                                         │
│  ┌──────────────────────────────────┐   │
│  │  Global keyboard hook (Win32)    │   │  F5 / F9
│  │  SetWindowsHookEx(WH_KEYBOARD_LL)│<──┤─────────
│  └──────────────┬───────────────────┘   │
│                 │                       │
│  ┌──────────────▼───────────────────┐   │
│  │  localhost HTTP API (ASP.NET     │   │
│  │  Minimal API, port 9518)         │   │
│  │  POST /quicksave                 │   │
│  │  POST /quickload                 │   │
│  │  GET  /status                    │   │
│  └──────────────┬───────────────────┘   │
│                 │                       │
│  ┌──────────────▼───────────────────┐   │
│  │  SaveManager                     │   │
│  │  • Resolves save00 path          │   │
│  │  • Copies files to/from          │   │
│  │    Documents\NoitaQuicksave\     │   │
│  │  • Touches mtimes on restore     │   │
│  │  • Sends synthetic keypress to   │   │
│  │    Noita window                  │   │
│  └──────────────────────────────────┘   │
└─────────────────────────────────────────┘
             │ synthetic
             │ keypress
             ▼
┌─────────────────────────────────────────┐
│  Noita (game process)                   │
│                                         │
│  ┌──────────────────────────────────┐   │
│  │  noita-quicksave (safe Lua mod)  │   │
│  │                                  │   │
│  │  OnWorldPostUpdate()             │   │
│  │    if Scroll Lock just pressed:  │   │
│  │      GameReloadActiveWorldFrom   │   │
│  │        Save()                    │   │
│  │    if Pause just pressed:        │   │
│  │      GamePrint("Saved.")         │   │
│  └──────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

### Signal keys (C# → Lua)

| Event | Synthetic key sent to Noita |
|-------|-----------------------------|
| Quicksave complete | `Pause` (VK 0x13) |
| Quickload: begin restore | `Scroll Lock` (VK 0x91) |

These keys have no default binding in Noita. The safe Lua mod polls for them every
frame with `InputIsKeyJustDown`. The C# helper sends them via `SendInput` (Windows)
or `xdotool key` (Linux) targeted at the Noita window, after all file operations
are complete.

---

## 6. Component Breakdown

### 6a. C# Helper — `NoitaQuicksaveHelper`

**Tech stack**: .NET 8, minimal dependencies.
- `SaveManager.cs` — path resolution, file copy, mtime touch
- `KeyboardHook.cs` — `SetWindowsHookEx(WH_KEYBOARD_LL)` global hook
- `SignalSender.cs` — `FindWindow` + `SendInput` to post keys to Noita
- `Program.cs` — ASP.NET Minimal API wiring, system tray icon (optional)

**HTTP API**:

```
POST /quicksave           → copy save00 → backup; send Pause key to Noita
POST /quickload           → copy backup → save00 (touch mtimes); send Scroll Lock to Noita
GET  /status              → { "hasSave": bool, "savedAt": "ISO8601 or null" }
```

The API is deliberately minimal. Third-party tools (AutoHotkey scripts, game launchers,
stream decks) can call it to trigger saves without needing the keyboard hook.

**File copy logic**:

```csharp
// Quicksave
Directory.Delete(backupPath, recursive: true);  // clear old backup
CopyDirectory(save00Path, backupPath);

// Quickload  
Directory.Delete(save00Path, recursive: true);
CopyDirectory(backupPath, save00Path);
TouchAllFiles(save00Path);  // set mtime = UtcNow → avoids Cloud conflict
```

**Key interception**: The global hook intercepts F5/F9 before they reach Noita and
calls the API internally. The game never sees F5 or F9 — no interference with any
existing Noita bindings.

### 6b. Safe Lua Mod — `noita-quicksave`

**Tech stack**: Noita safe-mode Lua (5.1 subset). No file I/O, no networking.

```
noita-quicksave/
├── mod.xml        -- no request_no_api_restrictions
└── init.lua       -- OnWorldPostUpdate, key detection, GamePrint
```

The mod does exactly two things:
1. Detect the `Scroll Lock` signal → call `GameReloadActiveWorldFromSave()`
2. Detect the `Pause` signal → call `GamePrint("Quick save created.")`

It shows a `GamePrint` warning on startup if the C# helper is not detected
(checked by looking for a sentinel file the helper writes to the mod directory
on launch — see below).

---

## 7. Helper-Present Detection

On startup, the C# helper writes a small sentinel file:

```
%LOCALAPPDATA%\NoitaQuicksaveHelper\running.txt
```

The Lua mod cannot read arbitrary files in safe mode, so this check is done
differently: the helper also writes into the mod's own directory at
`mods/noita-quicksave/data/helper_running.txt`. The safe Lua mod reads this
using `ModTextFileGetContent("data/helper_running.txt")`.

> **Important**: `ModTextFileGetContent` reads from Noita's virtual file system,
> which is populated at game startup from disk. The sentinel file must therefore
> be written by the C# helper **before the game launches**, not while it is running.
> The helper should be started first; a system tray app that auto-starts with
> Windows is the natural fit.

If the sentinel file is missing (helper not started before the game), the mod
prints a one-time warning: `"NoitaQuicksaveHelper not running. Start it before launching Noita."`.

---

## 8. Quicksave/Quickload Sequence (Full)

### Quicksave (F5)

```
1. C# global hook intercepts F5 (Noita never sees it).
2. C# calls POST /quicksave internally.
3. SaveManager:
   a. Delete Documents\NoitaQuicksave\ (if exists).
   b. Recursively copy save00\ → Documents\NoitaQuicksave\.
   c. Write metadata: { savedAt: UtcNow, noitaVersion: ... }
4. SignalSender sends VK_PAUSE to the Noita window.
5. Safe Lua mod detects Pause key → GamePrint("Quick save created.").
```

### Quickload (F9)

```
1. C# global hook intercepts F9 (Noita never sees it).
2. C# calls POST /quickload internally.
3. SaveManager:
   a. Verify Documents\NoitaQuicksave\ exists; abort with error if not.
   b. Delete save00\.
   c. Recursively copy Documents\NoitaQuicksave\ → save00\.
   d. Touch (mtime = UtcNow) all copied files.
4. SignalSender sends VK_SCROLL to the Noita window.
5. Safe Lua mod detects Scroll Lock key → GameReloadActiveWorldFromSave().
   (World reloads in-place; no game restart needed.)
```

---

## 9. Edge Cases

| Case | Handling |
|------|---------|
| Helper not running | Mod prints warning at startup; F5/F9 have no effect (keys not intercepted) |
| No quicksave exists when F9 pressed | Helper returns 400 from API; does not send signal key; no reload triggered |
| Player is dead | Lua mod guards: `EntityGetWithTag("player_unit")` empty → ignore Scroll Lock signal |
| Quicksave while in Pause menu | Allowed; save00 contains valid state |
| Steam Cloud conflict on restore | Prevented by touching file mtimes after copy |
| Game crashes between F5 and F9 | Backup in Documents is intact; user can F9 on next launch |
| Large world (50 k+ files) | Copy is async in C#; signal sent after copy completes — not before |
| Linux | Use `cp -a` + `find . -exec touch {} \;` in `SaveManager`; `xdotool key` for signal |

---

## 10. File & Project Structure

```
repo/
├── PLAN.md
├── noita-quicksave/           ← safe Lua mod (drop into Noita/mods/)
│   ├── mod.xml
│   └── init.lua
└── NoitaQuicksaveHelper/      ← C# project (built and run separately)
    ├── NoitaQuicksaveHelper.csproj
    ├── Program.cs             ← ASP.NET Minimal API + tray icon entry
    ├── SaveManager.cs         ← path resolution, copy, touch
    ├── KeyboardHook.cs        ← Win32 low-level keyboard hook
    └── SignalSender.cs        ← FindWindow + SendInput
```

---

## 11. Implementation Steps

1. **C# scaffold**: Create .NET 8 console/worker project with ASP.NET Minimal API.
2. **SaveManager**: Implement path detection (Windows/Linux), `CopyDirectory`,
   `TouchAllFiles`, and metadata write/read.
3. **HTTP API**: Wire `POST /quicksave`, `POST /quickload`, `GET /status`.
4. **KeyboardHook**: Install `WH_KEYBOARD_LL`; intercept F5 / F9; call API.
5. **SignalSender**: `FindWindow("MainWindow", null)` → `SendInput` with VK_PAUSE /
   VK_SCROLL.
6. **Sentinel file**: Write `mods/noita-quicksave/data/helper_running.txt` on helper start.
7. **Lua mod**: Update `init.lua` to poll Pause/Scroll Lock keys; add helper-missing
   warning.
8. **Test quicksave flow**: Verify backup appears in Documents; game prints "Saved."
9. **Test quickload flow**: Verify save00 is replaced; world hot-reloads correctly.
10. **Test Steam Cloud**: Launch with Cloud enabled; quicksave; exit; relaunch;
    quickload; exit — confirm no Cloud conflict dialog appears.
11. **Linux pass**: Swap Win32 calls for `xdotool`; verify paths.

---

## 12. Known Limitations

- **C# helper must start before Noita** for the sentinel file detection to work.
  Auto-start on login (Windows startup folder, `.desktop` autostart on Linux) is
  recommended.
- **`GameReloadActiveWorldFromSave()` behaviour** should be verified: if it triggers
  a full game restart rather than a hot-reload, the Lua mod signal is unnecessary
  and C# can simply restart `noita.exe` directly.
- **Checkpoint lag**: Quicksave captures the last on-disk state, which may lag a
  few seconds behind the true in-memory state for very recent pickups/events.
  Triggering a floor transition before pressing F5 guarantees a complete flush.
- **No multiple slots** in v1; previous quicksave is overwritten on each F5 press.
- **Antivirus**: A global keyboard hook may be flagged by aggressive AV software.
  The helper should be signed or clearly documented as open source.
