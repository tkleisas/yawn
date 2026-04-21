-- pads.lua — 4×8 pad layout for Ableton Move (MVP)
-- Chromatic and in-scale modes; row offset is configurable. No drum mode yet.

local pads = {}

-- ── Pad grid constants ──────────────────────────────────────────────────────
-- Move sends pad notes from 68 (bottom-left) to 99 (top-right), not 36..67
-- like Push. Verified from hardware: velocity-sensitive hits in range 68..99
-- (variable velocity), buttons outside the grid send fixed-velocity 127.
-- 4 rows × 8 cols = 32 pads → notes 68..99.
pads.PAD_GRID_START = 68
pads.PAD_GRID_END   = 99
pads.PAD_COLS       = 8
pads.PAD_ROWS       = 4

-- ── Submodes ────────────────────────────────────────────────────────────────

pads.SUBMODE_CHROMATIC = 1
pads.SUBMODE_SCALE     = 2

-- ── State ───────────────────────────────────────────────────────────────────

pads.submode      = pads.SUBMODE_SCALE
pads.root_note    = 0     -- 0=C, 1=C#, ... 11=B
pads.scale_index  = 1     -- index into scales.catalog (1 = Major)
pads.octave       = 3     -- base octave (C3 = MIDI 60)
pads.row_interval = 5     -- semitones between rows (5 = perfect 4th, Push-style)

-- Computed pad → MIDI note map. nil = pad inactive.
pads.pad_notes = {}

-- Pads the DAW should release when the mode/scale/octave changes.
pads.active_notes = {}

-- ── Layout computation ─────────────────────────────────────────────────────

-- Ableton's Push-style scale layout:
--   • rows offset by row_interval semitones (default 5 = 4th)
--   • column advance = next scale degree (in-scale) or next semitone (chromatic)
--   • pad lights up only if the pitch is in the scale (scale mode)
local function compute(scales)
    local root   = pads.root_note
    local scale  = scales.catalog[pads.scale_index]
    local base   = 12 * (pads.octave + 1)   -- MIDI for C<octave>
    local step_s = pads.row_interval        -- semitones between rows

    for i = 0, (pads.PAD_COLS * pads.PAD_ROWS) - 1 do
        pads.pad_notes[i] = nil
    end

    if pads.submode == pads.SUBMODE_CHROMATIC or scale == nil then
        -- Simple chromatic: each pad = base + row * row_interval + col
        for r = 0, pads.PAD_ROWS - 1 do
            for c = 0, pads.PAD_COLS - 1 do
                local note = base + r * step_s + c
                if note >= 0 and note <= 127 then
                    local idx = r * pads.PAD_COLS + c
                    pads.pad_notes[idx] = note
                end
            end
        end
        return
    end

    -- Scale mode: walk scale degrees per row
    local intervals = scale.intervals
    local n = #intervals
    if n == 0 then return end

    for r = 0, pads.PAD_ROWS - 1 do
        -- Row base: shift by step_s semitones per row, then snap to scale degree
        local row_base_semi = r * step_s
        local row_oct = math.floor(row_base_semi / 12)
        local row_rem = row_base_semi % 12
        -- Find scale-aligned starting degree for this row
        local start_deg = 0
        for i = 1, n do
            if intervals[i] <= row_rem then start_deg = i - 1 end
        end
        for c = 0, pads.PAD_COLS - 1 do
            local deg = start_deg + c
            local oct = row_oct + math.floor(deg / n)
            local step = intervals[(deg % n) + 1]
            local note = base + root + oct * 12 + step
            if note >= 0 and note <= 127 then
                local idx = r * pads.PAD_COLS + c
                pads.pad_notes[idx] = note
            end
        end
    end
end

function pads.recompute(scales)
    compute(scales)
end

-- ── Input helpers ───────────────────────────────────────────────────────────

-- Map a raw pad note from Move into a pad index (0..31). Returns nil if the
-- note is outside the pad grid.
function pads.hw_note_to_index(hw_note)
    if hw_note < pads.PAD_GRID_START or hw_note > pads.PAD_GRID_END then
        return nil
    end
    return hw_note - pads.PAD_GRID_START
end

-- Map a pad index (0..31) to its current MIDI note, or nil if inactive.
function pads.index_to_note(idx)
    return pads.pad_notes[idx]
end

-- ── Mode switching ──────────────────────────────────────────────────────────

function pads.toggle_submode(scales)
    if pads.submode == pads.SUBMODE_SCALE then
        pads.submode = pads.SUBMODE_CHROMATIC
    else
        pads.submode = pads.SUBMODE_SCALE
    end
    compute(scales)
end

function pads.octave_up(scales)
    if pads.octave < 8 then
        pads.octave = pads.octave + 1
        compute(scales)
    end
end

function pads.octave_down(scales)
    if pads.octave > -1 then
        pads.octave = pads.octave - 1
        compute(scales)
    end
end

return pads
