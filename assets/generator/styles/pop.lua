return {
    id = "pop",
    name = "Pop",
    tempo = { 100, 124 },
    scales = { "major", "natural_minor", "mixolydian" },
    roots = { "C", "G", "D", "A", "F" },
    progressions = {
        { "I", "V", "vi", "IV" },
        { "vi", "IV", "I", "V" },
        { "I", "vi", "IV", "V" },
    },
    sections = {
        "intro", "verse", "prechorus", "chorus",
        "verse", "chorus", "bridge", "chorus", "outro",
    },
    density = { 0.45, 0.65 },
    noteLength = { 0.4, 0.8 },
    swing = 0.0,
    chordRhythm = "half",
    drums = {
        kick = "x---x---x---x---",
        snare = "----x-------x---",
        hat = "x-x-x-x-x-x-x-x-",
    },
    melody = { contour = "arch", ornament = 0.15 },
}
