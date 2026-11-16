return {
    id = "rock",
    name = "Rock",
    tempo = { 105, 150 },
    scales = { "natural_minor", "mixolydian", "major" },
    roots = { "E", "A", "D", "G" },
    progressions = {
        { "i", "VI", "III", "VII" },
        { "i", "iv", "VI", "V" },
        { "I", "IV", "V", "I" },
    },
    sections = {
        "intro", "verse", "prechorus", "chorus",
        "verse", "chorus", "bridge", "chorus", "outro",
    },
    density = { 0.55, 0.75 },
    noteLength = { 0.35, 0.7 },
    swing = 0.0,
    chordRhythm = "quarter",
    drums = {
        kick = "x-x-x-x-x-x-x-x-",
        snare = "----x-------x---",
        hat = "x-x-x-x-x-x-x-x-",
        openhat = "--------x-------",
    },
    melody = { contour = "arch", ornament = 0.1 },
}
