-- quicksave.lua  –  file-system logic for the Quick Save / Load mod

local GLOBALS_KEY = "noita_qs_exists"
local QS_SLOT     = "save_quicksave"

local IS_WINDOWS = package.config:sub(1, 1) == "\\"

-- ---------------------------------------------------------------------------
-- Path helpers
-- ---------------------------------------------------------------------------

local function get_save_root()
    if IS_WINDOWS then
        -- %APPDATA% points to …/Roaming; LocalLow is a sibling directory.
        local appdata = os.getenv("APPDATA")
        local localLow = appdata:gsub("Roaming$", "LocalLow")
        return localLow .. "\\Nolla_Games_Noita\\"
    else
        -- Linux via Proton; HOME is the Linux home directory.
        local home = os.getenv("HOME")
        return home ..
            "/.steam/steam/steamapps/compatdata/881100/pfx/" ..
            "drive_c/users/steamuser/AppData/LocalLow/Nolla_Games_Noita/"
    end
end

local function sep()
    return IS_WINDOWS and "\\" or "/"
end

local function paths()
    local root     = get_save_root()
    local s        = sep()
    local save00   = root .. "save00"
    local quicksave = root .. QS_SLOT
    return save00, quicksave
end

-- ---------------------------------------------------------------------------
-- Shell commands
-- ---------------------------------------------------------------------------

local function cmd_copy(src, dst)
    if IS_WINDOWS then
        -- /E = include subdirs, /I = assume dst is a dir, /Y = no prompts, /Q = quiet
        return string.format('xcopy /E /I /Y /Q "%s\\*" "%s\\"', src, dst)
    else
        -- -a preserves timestamps/permissions and copies recursively
        return string.format('mkdir -p "%s" && cp -a "%s/." "%s/"', dst, src, dst)
    end
end

local function cmd_delete(path)
    if IS_WINDOWS then
        return string.format('rmdir /S /Q "%s"', path)
    else
        return string.format('rm -rf "%s"', path)
    end
end

local function shell(cmd)
    local ok = os.execute(cmd)
    return ok == true or ok == 0
end

-- ---------------------------------------------------------------------------
-- Public API
-- ---------------------------------------------------------------------------

function qs_init()
    -- Restore the "quicksave exists" flag from GlobalsGetValue so the mod
    -- survives restarts without relying solely on disk presence.
    local flag = GlobalsGetValue(GLOBALS_KEY, "0")
    -- (Flag is already correct; nothing else to do on init.)
end

function qs_save()
    local save00, quicksave = paths()

    -- Step 1: Copy current save00 → quicksave slot.
    -- This captures the most recent on-disk checkpoint.
    shell(cmd_delete(quicksave))
    local ok = shell(cmd_copy(save00, quicksave))

    if not ok then
        GamePrint("Quick save FAILED – check mod is set to Unsafe.")
        return
    end

    -- Step 2: Mark quicksave as available.
    GlobalsSetValue(GLOBALS_KEY, "1")
    GamePrint("Quick save created.")

    -- Step 3: Trigger Noita's own save so the in-memory state (spells fired,
    -- items picked up since the last floor transition) is also written to disk.
    -- NOTE: GameSave() may save-and-quit. If it does, the next launch will
    -- start from save00, and the user can press F9 to restore from our copy.
    GameSave()
end

function qs_load()
    -- Guard: nothing to load yet.
    if GlobalsGetValue(GLOBALS_KEY, "0") ~= "1" then
        GamePrint("No quick save found. Press F5 first.")
        return
    end

    local save00, quicksave = paths()

    GamePrint("Loading quick save...")

    -- Swap quicksave slot → save00.
    shell(cmd_delete(save00))
    local ok = shell(cmd_copy(quicksave, save00))

    if not ok then
        GamePrint("Quick load FAILED – save00 may be in a bad state. Restore from backup manually.")
        return
    end

    -- Ask Noita to reload the world from the now-updated save00.
    -- This hot-reloads the world without a full game restart.
    GameReloadActiveWorldFromSave()
end
