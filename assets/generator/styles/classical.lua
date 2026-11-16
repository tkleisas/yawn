return {
    id = "classical",
    name = "Classical",
    tempo = { 70, 100 },
    scales = { "major", "harmonic_minor", "dorian" },
    roots = { "C", "G", "D", "F", "A" },
    progressions = {
        { "I", "IV", "V", "I" },
        { "i", "iv", "V", "i" },
        { "I", "vi", "ii", "V" },
    },
    sections = { "intro", "verse", "bridge", "verse", "outro" },
    lanes = { "bass", "chords", "lead" },
    density = { 0.3, 0.5 },
    noteLength = { 0.8, 1.6 },
    swing = 0.0,
    chordRhythm = "whole",
    melody = { contour = "arch", ornament = 0.35 },
}
