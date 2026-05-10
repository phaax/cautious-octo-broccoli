# Plan: Noita Quick Save / Load Mod

## 1. Goal

Add F5 (quicksave) and F9 (quickload) hotkeys to Noita as a self-contained in-game
Lua mod. No external tools, no AutoHotkey.

---

## 2. How Noita Saves

Noita writes its run state to a folder of XML and binary files on disk. The primary
slot is `save00`. The folder is always at:

| Platform | Path |
|----------|------|
| Windows  | `%APPDATA%\..\LocalLow\Nolla_Games_Noita\save00\` |
| Linux (Proton) | `~/.steam/steam/steamapps/compatdata/881100/pfx/drive_c/users/steamuser/AppData/LocalLow/Nolla_Games_Noita/save00/` |

Key files inside `save00`:

```
save00/
├── world_state.xml        -- orbs, flags, world-level state
├── player.xml             -- player stats, inventory, spells
├── stats/                 -- run statistics
├── persistent/            -- bones, carried-over data
└── world/                 -- chunk data and entity state
```

Noita does **not** stream writes during active gameplay. State is flushed to disk:
- On a normal "Save and exit"
- On floor/area transitions (partial flush)
- When the game calls its internal save trigger

Slots `save01`–`save06` are reserved for mods. We will use `save_quicksave` as an
out-of-band backup folder alongside the normal slots.

---

## 3. Core Technical Constraint

Standard Noita Lua mods cannot touch the file system. To copy folders the mod
must declare itself **unsafe** (`request_no_api_restrictions="1"` in `mod.xml`),
which unlocks the full Lua `io` and `os` libraries including `os.execute()`.

Users must enable "Unsafe mods" in Noita's mod settings screen before the game
will load the mod. This is the same requirement used by many popular mods.

---

## 4. Architecture

```
noita-quicksave/
├── mod.xml              -- metadata; declares unsafe
├── init.lua             -- Noita lifecycle hooks, key polling
└── files/
    └── quicksave.lua    -- all save/load logic
```

### Data flow

```
User presses F5
    └─> init.lua::OnWorldPostUpdate detects InputIsKeyJustDown(KEY_F5)
        └─> quicksave.lua::do_quicksave()
            ├── Call GameSave()            -- flushes in-memory state to save00
            ├── os.execute(copy command)   -- copy save00 → save_quicksave
            └── GlobalsSetValue("qs_exists", "1")

User presses F9
    └─> init.lua::OnWorldPostUpdate detects InputIsKeyJustDown(KEY_F9)
        └─> quicksave.lua::do_quickload()
            ├── Guard: check GlobalsGetValue("qs_exists") == "1"
            ├── os.execute(delete save00)
            ├── os.execute(copy save_quicksave → save00)
            └── GameReloadActiveWorldFromSave()   -- hot-reload world from disk
```

---

## 5. Lua API Surface Used

| API | Purpose |
|-----|---------|
| `InputIsKeyJustDown(keycode)` | Detect single-frame keypress for F5/F9 |
| `OnWorldPostUpdate()` | Per-frame hook; polls input |
| `OnModInit()` | Detect existing quicksave on startup |
| `GameSave()` | Flush current in-memory state to save00 |
| `GameReloadActiveWorldFromSave()` | Reload world from disk after file swap |
| `GamePrint(msg)` | HUD notification to the player |
| `GlobalsSetValue/GetValue(key, val)` | Persist quicksave-exists flag across sessions |
| `os.execute(cmd)` | Shell file copy/delete (requires unsafe mode) |
| `package.config:sub(1,1)` | Detect Windows vs. Linux at runtime |
| `os.getenv("APPDATA")` / `os.getenv("HOME")` | Build platform-correct paths |

Key codes are SDL scancodes as used by LOVE2D (the engine Noita runs on):
- F5 = 114
- F9 = 120

---

## 6. Save Mechanism Detail

### 6a. Quicksave

`GameSave()` triggers Noita's internal save routine, writing the full in-memory
world state to `save00`. Crucially, the game may exit after calling this function.

Two sub-strategies depending on observed behaviour:

**Strategy A – GameSave flushes without quitting**
```
1. Call GameSave()        -- state written to save00
2. os.execute(copy cmd)   -- copy save00 → save_quicksave
3. Show "Saved" message
```

**Strategy B – GameSave saves then quits (more likely)**
```
1. Set flag qs_pending_copy = true
2. Register OnGamePreQuit (or poll in OnWorldPreUpdate for game-exit signal)
3. In that hook: os.execute(copy cmd) before the process dies
   -- OR --
   Reverse the order: copy save00 first (capturing last checkpoint), then
   call GameSave() so the next launch starts from the freshly-saved state.
   The quicksave slot will be one-checkpoint behind at worst.
```

Strategy B (copy-then-save) is the pragmatic fallback:
- Copy save00 → save_quicksave now  (captures last floor-transition checkpoint)
- Call GameSave() so the game saves current state and exits cleanly
- On next launch the user can quickload, which restores to the checkpoint copy

The difference is at most one floor's worth of progress. This matches the behaviour
of the popular external quick-save tools and is acceptable for a v1 release.

### 6b. Quickload

```
1. Verify save_quicksave exists (via GlobalsGetValue flag)
2. Delete contents of save00
3. Copy save_quicksave → save00
4. Call GameReloadActiveWorldFromSave()
```

`GameReloadActiveWorldFromSave()` tells the engine to discard in-memory world
state and re-initialise from disk — no game restart required. The player is
teleported back to the state captured in the quicksave.

---

## 7. Platform File Commands

### Windows

```lua
-- Copy (recursive, quiet)
string.format('xcopy /E /I /Y /Q "%s\\*" "%s\\"', src, dst)

-- Delete folder
string.format('rmdir /S /Q "%s"', path)
```

### Linux (Proton)

```lua
-- Copy
string.format('cp -a "%s/." "%s/"', src, dst)

-- Delete folder
string.format('rm -rf "%s"', path)
```

Path detection at runtime:

```lua
local IS_WINDOWS = package.config:sub(1,1) == "\\"

local function get_save_root()
    if IS_WINDOWS then
        -- %APPDATA% = …/Roaming; LocalLow is a sibling
        local appdata = os.getenv("APPDATA"):gsub("Roaming$", "LocalLow")
        return appdata .. "\\Nolla_Games_Noita\\"
    else
        local home = os.getenv("HOME")
        return home ..
            "/.steam/steam/steamapps/compatdata/881100/pfx/" ..
            "drive_c/users/steamuser/AppData/LocalLow/Nolla_Games_Noita/"
    end
end
```

---

## 8. User-Facing Behaviour

| Action | Key | On-screen feedback |
|--------|-----|-------------------|
| Quicksave | F5 | `GamePrint("Quick save created.")` |
| Quickload | F9 | `GamePrint("Loading quick save...")` then world reloads |
| Quickload with no save | F9 | `GamePrint("No quick save found. Press F5 first.")` |
| Quicksave while dead | F5 | Block with `GamePrint("Cannot save while dead.")` |

Player death detection: check that the player entity still exists via
`EntityGetWithTag("player_unit")` returning a non-empty table.

---

## 9. Edge Cases

| Case | Handling |
|------|---------|
| Player is dead | Block quicksave; quickload is still allowed |
| No quicksave exists | Guard in quickload; message user |
| Mid-air / mid-combat | Allowed — state is captured as-is |
| Steam Cloud Sync | Mod should warn in README to disable Steam Cloud or to use the mod's backup slot which is outside the synced save slots |
| Corrupt quicksave | On failed `GameReloadActiveWorldFromSave()`, print error; save_quicksave folder remains intact |
| Multiple quicksave slots | Out of scope for v1; can add named slots (F6–F8) in a later pass |

---

## 10. Implementation Steps

1. **Scaffold** – create `mod.xml` and empty `init.lua` / `files/quicksave.lua`.

2. **Key detection** – in `OnWorldPostUpdate`, poll `InputIsKeyJustDown` for F5
   and F9; print a message to confirm detection works before wiring logic.

3. **Path resolution** – implement and unit-test `get_save_root()` on both
   platforms (print paths to confirm before running file operations).

4. **Copy command** – implement `do_quicksave()` with the copy-then-save
   strategy; verify the backup folder appears on disk.

5. **Load command** – implement `do_quickload()`; verify the folder swap and
   that `GameReloadActiveWorldFromSave()` returns to the captured state.

6. **Death guard** – check player entity before allowing quicksave.

7. **HUD polish** – consistent `GamePrint` messages; brief cooldown (≈1 s) to
   prevent accidental double-presses.

8. **README** – note the unsafe mod requirement and Steam Cloud advice.

---

## 11. Known Limitations

- **Unsafe mod required.** Users must enable "Unsafe mods" in the mod settings.
- **Checkpoint lag.** If GameSave exits the game, the backup may lag one floor
  transition behind the true in-memory state. This is inherent to Noita's
  architecture and matches external tool behaviour.
- **Steam Cloud conflicts.** Manually swapping save00 while Steam Cloud is active
  can cause conflicts. Recommend disabling Steam Cloud for the save slot.
- **Parallel worlds / NG+.** The mod saves and restores the entire save00 folder,
  which includes parallel-world data. Quickloading into a different parallel-world
  branch is not supported and may corrupt the run.
- **No slot history.** v1 keeps exactly one quicksave. Previous quicksaves are
  overwritten on each F5 press.
