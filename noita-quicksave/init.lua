-- Quick Save / Load mod for Noita
-- Requires: Unsafe mods enabled in Noita's mod settings
-- F5 = quicksave, F9 = quickload

dofile("mods/noita-quicksave/files/quicksave.lua")

local KEY_F5 = 114
local KEY_F9 = 120

-- Minimum seconds between consecutive save/load actions (prevents double-press).
local COOLDOWN_SECS = 1.0
local last_action_time = 0

local function cooldown_ok()
    local now = GameGetRealWorldTimeDelta and
        (last_action_time + COOLDOWN_SECS < GameGetFrameNum() / 60)
        or true
    return now
end

local function player_is_alive()
    local players = EntityGetWithTag("player_unit")
    return players and #players > 0
end

function OnModInit()
    qs_init()
end

function OnWorldPostUpdate()
    if InputIsKeyJustDown(KEY_F5) then
        if not player_is_alive() then
            GamePrint("Cannot quicksave while dead.")
            return
        end
        last_action_time = GameGetFrameNum()
        qs_save()

    elseif InputIsKeyJustDown(KEY_F9) then
        last_action_time = GameGetFrameNum()
        qs_load()
    end
end
