-- Quick Save / Load — safe Lua mod
--
-- This mod does NO file I/O itself. All heavy lifting (file copy, keyboard
-- interception) is done by NoitaQuicksaveHelper.exe running alongside the game.
-- The helper communicates back via two synthetic keypresses:
--   VK_PAUSE  (0x13) → quicksave was completed; show confirmation message.
--   VK_SCROLL (0x91) → quickload files are in place; trigger world reload.
--
-- SDL2 scancodes used by InputIsKeyJustDown (from SDL_scancode.h):
local KEY_PAUSE  = 72    -- SDL_SCANCODE_PAUSE
local KEY_SCROLL = 71    -- SDL_SCANCODE_SCROLLLOCK

local warned_no_helper = false

local function helper_is_present()
    -- The C# helper writes this file before the game starts.
    -- ModTextFileGetContent reads from the VFS snapshotted at game startup.
    local content = ModTextFileGetContent("data/helper_running.txt")
    return content ~= nil and content ~= ""
end

local function player_is_alive()
    local players = EntityGetWithTag("player_unit")
    return players ~= nil and #players > 0
end

function OnWorldInitialized()
    -- GamePrint is not visible during OnModInit (HUD not ready yet).
    if not helper_is_present() then
        GamePrint("[QuickSave] WARNING: NoitaQuicksaveHelper.exe not detected.")
        GamePrint("[QuickSave] Start the helper before launching Noita.")
        warned_no_helper = true
    end
end

function OnWorldPostUpdate()
    if InputIsKeyJustDown(KEY_PAUSE) then
        -- C# helper finished copying save00 → backup.
        GamePrint("Quick save created.")
    end

    if InputIsKeyJustDown(KEY_SCROLL) then
        -- C# helper finished copying backup → save00; reload world from disk.
        if not player_is_alive() then
            -- Dead players cannot reliably reload; the game would respawn them.
            -- The C# helper should ideally not send this signal while dead,
            -- but guard here too.
            GamePrint("[QuickSave] Cannot reload while dead.")
            return
        end
        GamePrint("Loading quick save...")
        GameReloadActiveWorldFromSave()
    end
end
