-- pads.lua — Pad mode state machine for Push 1
-- Handles note (chromatic/scale), drum (4x4), and session (stub) modes.

local pads = {}

-- ── Mode constants ──────────────────────────────────────────────────────────

pads.MODE_NOTE    = 1
pads.MODE_DRUM    = 2
pads.MODE_SESSION = 3

pads.SUBMODE_CHROMATIC = 1
pads.SUBMODE_SCALE     = 2

-- ── Pad grid constants ──────────────────────────────────────────────────────

pads.PAD_GRID_START = 36
pads.PAD_GRID_END   = 99
pads.PAD_COLS       = 8
pads.PAD_ROWS       = 8

-- ── State ───────────────────────────────────────────────────────────────────

pads.mode         = pads.MODE_NOTE
pads.note_submode = pads.SUBMODE_CHROMATIC

-- Scale state
pads.root_note    = 0     -- 0=C, 1=C#, ... 11=B
pads.scale_index  = 1     -- index into scales.catalog
pads.octave       = 3     -- base octave
pads.row_interval = 5     -- semitones between rows (5 = perfect 4th)

-- Drum state
pads.drum_bank    = 0     -- 0-based bank (each bank = 16 notes)

-- Computed note mapping: pad_index (0-63) → MIDI note (or nil if inactive)
pads.pad_notes = {}

-- Active notes tracking for cleanup on mode switch: {midi_note = true}
pads.active_notes = {}

-- Flag: set true to trigger LED + grid recompute
pads.needs_recompute = true

-- ── Scale grid helpers ──────────────────────────────────────────────────────

-- Convert a semitone offset to the nearest scale degree <= that offset
local function semitones_to_degree(semitones, intervals, tpo)
    local n = #intervals
    if n == 0 then return 0 end

    local degree = 0
    local octaves = math.floor(semitones / tpo)
    local remainder = semitones % tpo

    degree = octaves * n

    -- Walk through intervals to find how many fit in the remainder
    for i = 1, n do
        if intervals[i] <= remainder then
            degree = degree + 1
        else
            break
        end
    end

    -- intervals[1] is always 0 (root), which we already counted if remainder >= 0
    -- Adjust: we want degrees where interval < remainder for non-root
    -- Actually, let's recount properly
    degree = octaves * n
    for i = 2, n do  -- skip root (index 1, interval 0)
        if intervals[i] <= remainder then
            degree = degree + 1
        else
            break
        end
    end

    return degree
end

-- ── Grid computation ────────────────────────────────────────────────────────

function pads.compute_chromatic_grid()
    pads.pad_notes = {}
    for i = 0, 63 do
        pads.pad_notes[i] = pads.PAD_GRID_START + i
    end
end

function pads.compute_scale_grid(scales_data)
    pads.pad_notes = {}

    local scale = scales_data.catalog[pads.scale_index]
    if not scale then return end

    local intervals = scale.intervals
    local tpo = scale.tones_per_octave or scales_data.tones_per_octave or 12
    local n = #intervals
    if n == 0 then return end

    -- Row interval in scale degrees
    local row_degrees = semitones_to_degree(pads.row_interval, intervals, tpo)
    if row_degrees < 1 then row_degrees = 1 end

    for row = 0, 7 do
        for col = 0, 7 do
            local pad_idx = row * 8 + col

            -- Total scale degree from bottom-left origin
            local degree = row * row_degrees + col

            -- Split into octave offset and degree within scale
            local oct_offset = math.floor(degree / n)
            local deg_in_scale = degree % n

            -- MIDI note = base + octave jumps + interval
            local base_midi = (pads.octave + 1) * tpo + pads.root_note
            local midi_note = base_midi + oct_offset * tpo + intervals[deg_in_scale + 1]

            if midi_note >= 0 and midi_note <= 127 then
                pads.pad_notes[pad_idx] = midi_note
            end
        end
    end
end

function pads.compute_drum_grid()
    pads.pad_notes = {}
    local base = 36 + pads.drum_bank * 16

    -- 4x4 grid in bottom-left (rows 0-3, cols 0-3)
    for row = 0, 7 do
        for col = 0, 7 do
            local pad_idx = row * 8 + col
            if row < 4 and col < 4 then
                local drum_note = base + row * 4 + col
                if drum_note >= 0 and drum_note <= 127 then
                    pads.pad_notes[pad_idx] = drum_note
                end
            end
        end
    end
end

function pads.compute_note_grid(scales_data)
    if pads.mode == pads.MODE_DRUM then
        pads.compute_drum_grid()
    elseif pads.mode == pads.MODE_NOTE then
        if pads.note_submode == pads.SUBMODE_SCALE then
            pads.compute_scale_grid(scales_data)
        else
            pads.compute_chromatic_grid()
        end
    end
    -- MODE_SESSION: no note mapping
end

-- ── Note handling ───────────────────────────────────────────────────────────

-- Send note-offs for all active notes (call before mode switch)
function pads.cleanup_active_notes()
    local track = yawn.get_selected_track()
    for note, _ in pairs(pads.active_notes) do
        yawn.send_note_to_track(track, note, 0, 0)
    end
    pads.active_notes = {}
end

-- Handle a pad press/release. Returns true if consumed.
function pads.handle_pad_note(hw_note, velocity, channel)
    local pad_idx = hw_note - pads.PAD_GRID_START
    if pad_idx < 0 or pad_idx > 63 then return false end

    if pads.mode == pads.MODE_SESSION then
        -- Stub: future clip launching
        -- local row = math.floor(pad_idx / 8)
        -- local col = pad_idx % 8
        return true
    end

    local mapped_note = pads.pad_notes[pad_idx]
    if not mapped_note then return true end  -- inactive pad, swallow

    local track = yawn.get_selected_track()
    yawn.send_note_to_track(track, mapped_note, velocity, channel)

    -- Track active notes
    if velocity > 0 then
        pads.active_notes[mapped_note] = true
    else
        pads.active_notes[mapped_note] = nil
    end

    return true
end

-- Handle polyphonic aftertouch remapping
function pads.handle_aftertouch(hw_note, pressure)
    local pad_idx = hw_note - pads.PAD_GRID_START
    if pad_idx < 0 or pad_idx > 63 then return false end

    local mapped_note = pads.pad_notes[pad_idx]
    if not mapped_note then return true end

    -- Forward aftertouch with remapped note
    yawn.midi_send(0xA0, mapped_note, pressure)
    return true
end

-- ── Auto-switching ──────────────────────────────────────────────────────────

function pads.auto_switch_mode(track)
    local inst_id = yawn.get_instrument_id(track)
    if not inst_id then return false end

    local changed = false

    if inst_id == "drumrack" or inst_id == "drumslop" then
        if pads.mode ~= pads.MODE_DRUM then
            pads.cleanup_active_notes()
            pads.mode = pads.MODE_DRUM
            pads.drum_bank = 0
            changed = true
        end
    else
        if pads.mode == pads.MODE_DRUM then
            pads.cleanup_active_notes()
            pads.mode = pads.MODE_NOTE
            changed = true
        end
    end

    return changed
end

-- ── LED colors ──────────────────────────────────────────────────────────────

-- Push 1 velocity-to-color values
local LED_ROOT       = 127   -- bright: root note pads
local LED_IN_SCALE   = 122   -- medium: other in-scale pads
local LED_CHROMATIC  = 118   -- dim: chromatic mode (all pads)
local LED_DRUM_ON    = 125   -- active drum pad
local LED_DRUM_OFF   = 0     -- inactive area
local LED_OFF        = 0

function pads.get_pad_led_color(pad_idx)
    if pads.mode == pads.MODE_DRUM then
        if pads.pad_notes[pad_idx] then
            return LED_DRUM_ON
        else
            return LED_DRUM_OFF
        end
    elseif pads.mode == pads.MODE_NOTE then
        if pads.note_submode == pads.SUBMODE_SCALE then
            local midi = pads.pad_notes[pad_idx]
            if midi then
                if midi % 12 == pads.root_note then
                    return LED_ROOT
                else
                    return LED_IN_SCALE
                end
            else
                return LED_OFF
            end
        else
            return LED_CHROMATIC
        end
    elseif pads.mode == pads.MODE_SESSION then
        return LED_OFF  -- stub
    end
    return LED_OFF
end

-- Update all pad LEDs to reflect current mode
function pads.update_leds()
    for row = 0, 7 do
        for col = 0, 7 do
            local pad_idx = row * 8 + col
            local hw_note = pads.PAD_GRID_START + pad_idx
            local color = pads.get_pad_led_color(pad_idx)
            yawn.midi_send(0x90, hw_note, color)
        end
    end
end

-- Get the resting LED color for a pad (used after note-off to restore mode color)
function pads.restore_pad_led(hw_note)
    local pad_idx = hw_note - pads.PAD_GRID_START
    if pad_idx < 0 or pad_idx > 63 then return end
    local color = pads.get_pad_led_color(pad_idx)
    yawn.midi_send(0x90, hw_note, color)
end

-- ── Scale edit display ──────────────────────────────────────────────────────

function pads.get_scale_edit_display(scales_data)
    local scale = scales_data.catalog[pads.scale_index] or {name = "?", category = "?"}
    local root_name = scales_data.note_names[(pads.root_note % 12) + 1] or "?"

    local interval_names = {"Unison", "m2", "M2", "m3", "M3", "P4", "Tri", "P5", "m6", "M6", "m7", "M7"}
    local row_name = interval_names[pads.row_interval + 1] or tostring(pads.row_interval)

    local line1 = string.format("Root:%-3s Scale: %-20s", root_name, scale.name)
    local line2 = string.format("%-12s  Row: %-6s  Oct: %d", scale.category, row_name, pads.octave)

    -- Line 3: show root note options with marker
    local line3 = ""
    for i = 1, 12 do
        local n = scales_data.note_names[i]
        if i - 1 == pads.root_note then
            line3 = line3 .. "[" .. n .. "]"
        else
            line3 = line3 .. " " .. n .. " "
        end
        if #n < 2 then line3 = line3 .. " " end
    end

    local line4 = "Enc1:Root Enc2:Scale Enc3:Row Enc4:Oct"

    return line1, line2, line3, line4
end

-- ── Mode name for display ───────────────────────────────────────────────────

function pads.get_mode_name()
    if pads.mode == pads.MODE_NOTE then
        if pads.note_submode == pads.SUBMODE_SCALE then
            local scales_data = require("scales")
            local scale = scales_data.catalog[pads.scale_index]
            local root = scales_data.note_names[(pads.root_note % 12) + 1] or "?"
            return string.format("%s %s", root, scale and scale.name or "?")
        else
            return "Chromatic"
        end
    elseif pads.mode == pads.MODE_DRUM then
        return string.format("Drum Bank %d", pads.drum_bank + 1)
    elseif pads.mode == pads.MODE_SESSION then
        return "Session"
    end
    return ""
end

return pads
