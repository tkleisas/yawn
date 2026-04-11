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

-- Session mode state: 8x8 grid window into the clip matrix
pads.session_track_offset = 0   -- first visible track column
pads.session_scene_offset = 0   -- first visible scene row

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
        if velocity > 0 then
            -- Pad grid: row 7 (top) = first scene, row 0 (bottom) = last scene
            local row = math.floor(pad_idx / 8)
            local col = pad_idx % 8
            local track = pads.session_track_offset + col
            local scene = pads.session_scene_offset + (7 - row)  -- invert: top row = first scene
            yawn.launch_clip(track, scene)
        end
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
    -- Don't auto-switch when in session mode — user explicitly chose it
    if pads.mode == pads.MODE_SESSION then return false end

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
        return pads.get_session_pad_color(pad_idx)
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

-- ── Session mode helpers ────────────────────────────────────────────────────

-- Push 1 pad colors for session mode
local LED_SESSION_EMPTY     = 0     -- off
local LED_SESSION_CLIP      = 122   -- has clip, stopped (dim)
local LED_SESSION_PLAYING   = 127   -- playing (bright green)
local LED_SESSION_RECORDING = 4     -- recording (red — Push 1 color index 4)
local LED_SESSION_ARMED     = 1     -- armed empty slot (dim red)

function pads.get_session_pad_color(pad_idx)
    local row = math.floor(pad_idx / 8)
    local col = pad_idx % 8
    local track = pads.session_track_offset + col
    local scene = pads.session_scene_offset + (7 - row)

    local state = yawn.get_clip_slot_state(track, scene)
    if not state then return LED_SESSION_EMPTY end

    if state.recording then
        return LED_SESSION_RECORDING
    elseif state.playing then
        return LED_SESSION_PLAYING
    elseif state.type ~= "empty" then
        return LED_SESSION_CLIP
    elseif state.armed then
        return LED_SESSION_ARMED
    end
    return LED_SESSION_EMPTY
end

-- Navigate session grid (called from cursor key handlers)
function pads.session_navigate(dx, dy)
    local num_tracks = yawn.get_track_count()
    local num_scenes = yawn.get_num_scenes()

    pads.session_track_offset = math.max(0,
        math.min(pads.session_track_offset + dx, math.max(0, num_tracks - 8)))
    pads.session_scene_offset = math.max(0,
        math.min(pads.session_scene_offset + dy, math.max(0, num_scenes - 8)))

    pads.sync_session_focus()
    pads.needs_recompute = true
end

-- Sync the session focus rectangle to the UI
function pads.sync_session_focus()
    yawn.set_session_focus(pads.session_track_offset,
                           pads.session_scene_offset,
                           pads.mode == pads.MODE_SESSION)
end

-- Get session display info
function pads.get_session_display()
    local num_tracks = yawn.get_track_count()
    local num_scenes = yawn.get_num_scenes()

    -- Line 1: track names (8 columns)
    local line1 = ""
    for c = 0, 7 do
        local t = pads.session_track_offset + c
        local name = ""
        if t < num_tracks then
            name = yawn.get_track_name(t) or ""
        end
        if #name > 8 then name = name:sub(1, 8) end
        name = name .. string.rep(" ", 8 - #name)
        if c < 7 then
            line1 = line1 .. name .. " "
        else
            line1 = line1 .. name
        end
    end

    -- Line 2: track armed/type status
    local line2 = ""
    for c = 0, 7 do
        local t = pads.session_track_offset + c
        local status = "        "
        if t < num_tracks then
            local armed = yawn.is_track_armed(t)
            local ttype = yawn.get_track_type(t) or "?"
            if armed then
                status = string.format("%-4s ARM", ttype:sub(1,4):upper())
            else
                status = string.format("%-8s", ttype:sub(1,8):upper())
            end
        end
        if c < 7 then
            line2 = line2 .. status .. " "
        else
            line2 = line2 .. status
        end
    end

    -- Line 3: scene range
    local line3 = string.format("Scenes %d-%d of %d   Tracks %d-%d of %d",
        pads.session_scene_offset + 1,
        math.min(pads.session_scene_offset + 8, num_scenes),
        num_scenes,
        pads.session_track_offset + 1,
        math.min(pads.session_track_offset + 8, num_tracks),
        num_tracks)

    -- Line 4: transport + recording status
    local line4 = ""
    local playing = yawn.is_playing()
    local recording = yawn.is_recording()
    if recording then
        line4 = string.format("REC  %.1f BPM", yawn.get_bpm())
    elseif playing then
        line4 = string.format("PLAY  %.1f BPM", yawn.get_bpm())
    else
        line4 = string.format("STOP  %.1f BPM", yawn.get_bpm())
    end

    return line1, line2, line3, line4
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
