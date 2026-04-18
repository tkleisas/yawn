-- scales.lua — Scale catalog for Push 1 pad modes
-- Each scale: {name, category, intervals (semitone offsets from root)}
-- Future TET24: set tones_per_octave = 24 and use quarter-tone intervals

local NOTE_NAMES = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}

local scales = {
    tones_per_octave = 12,
    note_names = NOTE_NAMES,

    catalog = {
        -- ── Western Modes ───────────────────────────────────────────────
        {name = "Major",         category = "Western", intervals = {0,2,4,5,7,9,11}},
        {name = "Nat Minor",     category = "Western", intervals = {0,2,3,5,7,8,10}},
        {name = "Harm Minor",    category = "Western", intervals = {0,2,3,5,7,8,11}},
        {name = "Mel Minor",     category = "Western", intervals = {0,2,3,5,7,9,11}},
        {name = "Dorian",        category = "Western", intervals = {0,2,3,5,7,9,10}},
        {name = "Phrygian",      category = "Western", intervals = {0,1,3,5,7,8,10}},
        {name = "Lydian",        category = "Western", intervals = {0,2,4,6,7,9,11}},
        {name = "Mixolydian",    category = "Western", intervals = {0,2,4,5,7,9,10}},
        {name = "Locrian",       category = "Western", intervals = {0,1,3,5,6,8,10}},
        {name = "WholeTone",     category = "Western", intervals = {0,2,4,6,8,10}},
        {name = "Dimin HW",      category = "Western", intervals = {0,1,3,4,6,7,9,10}},
        {name = "Augmented",     category = "Western", intervals = {0,3,4,7,8,11}},

        -- ── Pentatonic ──────────────────────────────────────────────────
        {name = "Penta Maj",     category = "Pentatonic", intervals = {0,2,4,7,9}},
        {name = "Penta Min",     category = "Pentatonic", intervals = {0,3,5,7,10}},
        {name = "Blues",          category = "Pentatonic", intervals = {0,3,5,6,7,10}},

        -- ── Maqam / Eastern (12-TET approximations) ─────────────────────
        -- Note: Bayati, Rast, Sikah etc. use quarter-tones in practice.
        -- These are closest 12-TET approximations. Proper TET24 entries
        -- will be added when quarter-tone MIDI output is implemented.
        {name = "Hijaz",         category = "Maqam", intervals = {0,1,4,5,7,8,10}},
        {name = "Bayati",        category = "Maqam", intervals = {0,2,3,5,7,8,10}},
        {name = "Rast",          category = "Maqam", intervals = {0,2,4,5,7,9,10}},
        {name = "Nahawand",      category = "Maqam", intervals = {0,2,3,5,7,8,11}},
        {name = "Kurd",          category = "Maqam", intervals = {0,1,3,5,7,8,10}},
        {name = "Saba",          category = "Maqam", intervals = {0,2,3,5,6,8,10}},
        {name = "Ajam",          category = "Maqam", intervals = {0,2,4,5,7,9,11}},
        {name = "Nikriz",        category = "Maqam", intervals = {0,2,3,6,7,9,10}},
        {name = "Sikah",         category = "Maqam", intervals = {0,1,4,5,7,9,10}},
        {name = "Husseini",      category = "Maqam", intervals = {0,2,3,5,7,9,10}},
        {name = "Phryg Dom",     category = "Maqam", intervals = {0,1,4,5,7,8,10}},
        {name = "Dbl Harm",      category = "Maqam", intervals = {0,1,4,5,7,8,11}},
        {name = "Hung Minor",    category = "Maqam", intervals = {0,2,3,6,7,8,11}},
        {name = "Hicaz Kar",     category = "Maqam", intervals = {0,1,4,5,7,9,11}},
        {name = "Ussak",         category = "Maqam", intervals = {0,2,3,5,7,9,10}},

        -- ── Messiaen Modes of Limited Transposition ────────────────────
        {name = "Mess 1",        category = "Messiaen", intervals = {0,2,4,6,8,10}},         -- = Whole Tone
        {name = "Mess 2",        category = "Messiaen", intervals = {0,1,3,4,6,7,9,10}},     -- = Dim HW
        {name = "Mess 3",        category = "Messiaen", intervals = {0,2,3,4,6,7,8,10,11}},
        {name = "Mess 4",        category = "Messiaen", intervals = {0,1,2,5,6,7,8,11}},
        {name = "Mess 5",        category = "Messiaen", intervals = {0,1,5,6,7,11}},
        {name = "Mess 6",        category = "Messiaen", intervals = {0,2,4,5,6,8,10,11}},
        {name = "Mess 7",        category = "Messiaen", intervals = {0,1,2,3,5,6,7,8,9,11}},

        -- ── Symmetric / Exotic ─────────────────────────────────────────
        {name = "Whole-Half",    category = "Symmetric", intervals = {0,2,3,5,6,8,9,11}},    -- WH diminished
        {name = "Half-Whole",    category = "Symmetric", intervals = {0,1,3,4,6,7,9,10}},    -- HW diminished
        {name = "Tritone",       category = "Symmetric", intervals = {0,1,4,6,7,10}},
        {name = "Chromatic",     category = "Symmetric", intervals = {0,1,2,3,4,5,6,7,8,9,10,11}},

        -- ── Jazz ───────────────────────────────────────────────────────
        {name = "Bebop Dom",     category = "Jazz", intervals = {0,2,4,5,7,9,10,11}},
        {name = "Bebop Maj",     category = "Jazz", intervals = {0,2,4,5,7,8,9,11}},
        {name = "Bebop Min",     category = "Jazz", intervals = {0,2,3,5,7,8,9,10}},
        {name = "Altered",       category = "Jazz", intervals = {0,1,3,4,6,8,10}},           -- Super Locrian
        {name = "Lydian Dom",    category = "Jazz", intervals = {0,2,4,6,7,9,10}},
        {name = "Lyd Augment",   category = "Jazz", intervals = {0,2,4,6,8,9,11}},

        -- ── World ──────────────────────────────────────────────────────
        {name = "Hirajoshi",     category = "World", intervals = {0,2,3,7,8}},
        {name = "In-Sen",        category = "World", intervals = {0,1,5,7,10}},
        {name = "Iwato",         category = "World", intervals = {0,1,5,6,10}},
        {name = "Kumoi",         category = "World", intervals = {0,2,3,7,9}},
        {name = "Pelog",          category = "World", intervals = {0,1,3,7,8}},
        {name = "Enigmatic",     category = "World", intervals = {0,1,4,6,8,10,11}},
        {name = "Neapolitan",    category = "World", intervals = {0,1,3,5,7,9,11}},
        {name = "Persian",       category = "World", intervals = {0,1,4,5,6,8,11}},
    }
}

return scales
