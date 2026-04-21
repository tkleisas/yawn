-- Ableton Move controller script for YAWN (MVP)
--
-- Confident parts (Ableton conventions — same across Push family):
--   • Pads send velocity-sensitive notes 36..67
--   • 8 encoders send relative CC 71..78 on channel 0
-- Unverified parts are marked TODO — they're best guesses, fill in real
-- values by pressing the button and checking YAWN's MIDI monitor panel.
--
-- When a CC/note isn't recognised, we log it once (throttled) so you can
-- watch the YAWN log and quickly build up the mapping.

local scales = require("scales")
local pads   = require("pads")

-- ── Move MIDI map (adjust as you learn the real values) ────────────────────

-- Encoders: relative, Ableton convention (Push uses the same range).
local ENCODER_CCS = { 71, 72, 73, 74, 75, 76, 77, 78 }

-- Encoder sensitivity (normalized units per tick).
local ENCODER_SENSITIVITY = 0.05

-- Transport. Push 1 uses these CCs; Move may differ.
-- TODO: VERIFY — press Play/Record on Move, watch YAWN's MIDI monitor
-- and replace the guesses with real values.
local CC_PLAY       = 85   -- guessed (Push convention)
local CC_RECORD     = 86   -- guessed
local CC_SHIFT      = 49   -- guessed

-- Scene launch / track select — unknown on Move. Left commented out until
-- you read the real CCs off the hardware.
-- local CC_SCENE_LAUNCH_BASE = ?
-- local CC_TRACK_SELECT_BASE = ?

-- ── State ──────────────────────────────────────────────────────────────────

local shift_held  = false
local param_page  = 0    -- 8 params per page
local last_track  = -1

-- Throttled logging for unknown CCs
local unknown_cc_seen = {}

-- ── Helpers ────────────────────────────────────────────────────────────────

local function log_once(tag, msg)
    if not unknown_cc_seen[tag] then
        unknown_cc_seen[tag] = true
        yawn.log(msg)
    end
end

local function current_param_count()
    return yawn.get_device_param_count("instrument", 0) or 0
end

local function clamped_page()
    local n = current_param_count()
    local max_page = math.max(0, math.ceil(n / 8) - 1)
    if param_page > max_page then param_page = max_page end
    return param_page, max_page
end

-- Apply a relative encoder delta (two's-complement 7-bit: values > 63 are
-- negative). Normalized parameter increments.
local function apply_encoder(cc, value)
    local page, _ = clamped_page()
    local encoder_idx = -1
    for i, e in ipairs(ENCODER_CCS) do
        if e == cc then encoder_idx = i - 1; break end
    end
    if encoder_idx < 0 then return false end

    local param_idx = page * 8 + encoder_idx
    if param_idx >= current_param_count() then return true end

    local delta_raw = value
    if delta_raw > 63 then delta_raw = delta_raw - 128 end
    local delta = delta_raw * ENCODER_SENSITIVITY
    if shift_held then delta = delta * 0.1 end  -- fine mode

    local minv = yawn.get_device_param_min("instrument", 0, param_idx) or 0
    local maxv = yawn.get_device_param_max("instrument", 0, param_idx) or 1
    local curr = yawn.get_device_param_value("instrument", 0, param_idx) or 0
    local range = maxv - minv
    if range <= 0 then return true end
    local new_val = curr + delta * range
    if new_val < minv then new_val = minv end
    if new_val > maxv then new_val = maxv end
    yawn.set_device_param("instrument", 0, param_idx, new_val)
    return true
end

-- ── Lua callbacks ──────────────────────────────────────────────────────────

function on_connect()
    yawn.log("[Move] Connected; MVP mode (no LED/display feedback yet)")
    pads.recompute(scales)
end

function on_disconnect()
    yawn.log("[Move] Disconnected")
end

-- MIDI dispatch. We see raw bytes; interpret by status nibble.
function on_midi(data)
    if #data < 1 then return end
    local status = data[1]
    local st_hi  = status - (status % 16)  -- high nibble (type)
    -- local channel = status % 16       -- low nibble (channel, unused for now)

    -- Note On. The C++ binding treats velocity=0 as Note Off internally,
    -- so both on-pad-press (vel > 0) and on-pad-release (running-status
    -- vel=0) fall through the same call.
    if st_hi == 0x90 and #data >= 3 then
        local note = data[2]
        local vel  = data[3]
        local idx = pads.hw_note_to_index(note)
        if idx then
            local mapped = pads.index_to_note(idx)
            if mapped then
                local track = yawn.get_selected_track()
                yawn.send_note_to_track(track, mapped, vel, 0)
            end
            return
        end
        -- Not a pad: log for discovery
        log_once("note_" .. note,
            string.format("[Move] unknown Note On: note=%d vel=%d", note, vel))
        return
    end

    -- Note Off (explicit 0x80 form)
    if st_hi == 0x80 and #data >= 3 then
        local note = data[2]
        local idx = pads.hw_note_to_index(note)
        if idx then
            local mapped = pads.index_to_note(idx)
            if mapped then
                local track = yawn.get_selected_track()
                yawn.send_note_to_track(track, mapped, 0, 0)
            end
        end
        return
    end

    -- Control Change
    if st_hi == 0xB0 and #data >= 3 then
        local cc    = data[2]
        local value = data[3]

        -- Encoders
        for _, e in ipairs(ENCODER_CCS) do
            if cc == e then apply_encoder(cc, value); return end
        end

        -- Shift (held modifier)
        if cc == CC_SHIFT then
            shift_held = (value > 0)
            return
        end

        -- Transport: Play toggles, Record toggles (press only — value > 0)
        if cc == CC_PLAY and value > 0 then
            yawn.set_playing(not yawn.is_playing())
            return
        end
        if cc == CC_RECORD and value > 0 then
            yawn.set_recording(not yawn.is_recording())
            return
        end

        -- Unmapped CC → log once per (cc,press) so you can discover the map
        if value > 0 then
            log_once("cc_" .. cc,
                string.format("[Move] unknown CC: %d value=%d", cc, value))
        end
        return
    end

    -- Pitch bend, aftertouch, etc — log for discovery
    if st_hi == 0xE0 then
        log_once("pb", "[Move] pitch bend received (not yet handled)")
        return
    end
end

-- 30 Hz tick for periodic work (display refresh, animations). Nothing yet
-- in MVP, but we track selected-track change to recompute pads if we ever
-- wire per-track scales.
function on_tick()
    local t = yawn.get_selected_track()
    if t ~= last_track then
        last_track = t
    end
end
