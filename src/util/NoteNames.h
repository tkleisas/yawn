#pragma once
// Note-name <-> MIDI helpers.
//
// Two small functions:
//   * midiNoteName(midi, sharp=true) → "C4", "F#3", or "Bb3"
//   * parseRootFromFilename(name)    → MIDI 0..127, or -1 if no parse
//
// The filename parser scans for a token of the form `[A-G][#b]?[-]?\d`
// (e.g. C4, F#3, Bb-1). It prefers tokens at the END of the basename
// (right before the extension), since common naming conventions put
// the pitch last: `Bass_C2.wav`, `Pad_F#3_v90.wav`. If nothing matches
// at the end it falls back to the first hit anywhere in the basename.
//
// Returns -1 on no match — caller decides the fallback (typical:
// drop in at C4 / 60).

#include <cctype>
#include <cstring>
#include <string>

namespace yawn {
namespace util {

// MIDI note name for `note` ∈ [0, 127]. `useSharps` picks C# vs Db etc.
// Out-of-range returns "??".
inline std::string midiNoteName(int note, bool useSharps = true) {
    if (note < 0 || note > 127) return "??";
    static const char* sharps[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    static const char* flats[12] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };
    const char* const* names = useSharps ? sharps : flats;
    const int pc  = note % 12;
    const int oct = note / 12 - 1;     // MIDI 60 = C4 → oct = 5-1 = 4
    return std::string(names[pc]) + std::to_string(oct);
}

namespace detail {

// Try to parse `[A-G][#b]?[-]?\d+` starting at `s[i]`. On success, returns
// the consumed length and writes the MIDI note number to `*outMidi`.
// Returns 0 on no parse.
inline int tryParseNoteAt(const char* s, int len, int i, int* outMidi) {
    if (i >= len) return 0;
    const char c0 = s[i];
    if (c0 < 'A' || c0 > 'G') return 0;

    // The previous character must be a non-letter to avoid matching
    // INSIDE a word like "Bass" or "Cello". Either start-of-string,
    // a digit/symbol, or punctuation (`_`, `-`, `.`, ` `).
    if (i > 0) {
        const char p = s[i - 1];
        if (std::isalpha(static_cast<unsigned char>(p))) return 0;
    }

    // Pitch class
    static const int pcMap[7] = {
        9,  // A
        11, // B
        0,  // C
        2,  // D
        4,  // E
        5,  // F
        7   // G
    };
    int pc = pcMap[c0 - 'A'];
    int j = i + 1;

    // Optional accidental
    if (j < len && (s[j] == '#' || s[j] == 'b')) {
        if (s[j] == '#') ++pc;
        else             --pc;
        ++j;
    }

    // Optional minus sign for negative octaves (MIDI octave -1 holds C-1=0)
    int octSign = 1;
    if (j < len && s[j] == '-') {
        octSign = -1;
        ++j;
    }

    // Octave digits — at least one
    if (j >= len || !std::isdigit(static_cast<unsigned char>(s[j]))) return 0;
    int octStart = j;
    int oct = 0;
    while (j < len && std::isdigit(static_cast<unsigned char>(s[j]))) {
        oct = oct * 10 + (s[j] - '0');
        ++j;
        if (j - octStart > 2) return 0;   // 3+ digits is not an octave
    }
    oct *= octSign;

    // Trailing character must be non-alphanumeric (or end of string)
    // so `C5x` doesn't parse as a note. (`C4_` and `C4.wav` are fine.)
    if (j < len) {
        const char t = s[j];
        if (std::isalnum(static_cast<unsigned char>(t))) return 0;
    }

    // Range check: oct in [-1, 9] gives MIDI 0..127 for C; allow accidentals
    // to push slightly outside on the boundaries.
    const int midi = (oct + 1) * 12 + pc;
    if (midi < 0 || midi > 127) return 0;

    if (outMidi) *outMidi = midi;
    return j - i;
}

} // namespace detail

// Parse a note name out of a filename's basename. Strips extension first.
// Prefers a match anchored at the end of the basename (right before the
// extension); falls back to the first match anywhere. Returns -1 on no
// match.
inline int parseRootFromFilename(const std::string& name) {
    // Strip directory
    size_t slash = name.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
    // Strip extension
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.erase(dot);
    if (base.empty()) return -1;

    const char* s = base.c_str();
    const int len = static_cast<int>(base.size());

    // Pass 1: scan right-to-left for the LAST note-token. Naming
    // convention "Bass_C2", "Pad_F#3_v90" → pitch at the end.
    for (int i = len - 1; i >= 0; --i) {
        int midi = -1;
        int consumed = detail::tryParseNoteAt(s, len, i, &midi);
        if (consumed > 0) return midi;
    }
    return -1;
}

} // namespace util
} // namespace yawn
