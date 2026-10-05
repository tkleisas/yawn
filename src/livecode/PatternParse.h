#pragma once
// PatternParse — text notations for musical patterns, exposed through the
// live-coding Lua API (yawn.midi / yawn.drums).
//
// Melodic phrases ("A1_16 B1_16 D#3_8"): sequential events; each note
// starts where the previous one ends. Pitch is [A-Ga-g][#b]?<octave>
// with MIDI 60 = C4 (matches util::midiNoteName). The duration suffix
// _16/_8/_4/_2/_1 (also _32) is a fraction of a whole note; `*N`
// multiplies the base length ("_16*8" = 8 sixteenths = 2 beats); `@vel`
// sets velocity 0..1 (default 0.8). "[C3E3G3]_2" is a chord (one
// duration, all pitches). "R_8" is a rest. `|` bar separators are
// readability-only. Clip length = the exact sum of durations.
//
// Drum grids: one lane per drum voice, one character per 16th step.
// X accent / x normal / o soft / g ghost (the same vocabulary and
// velocities as presets::drumkit::velFor), `.` or `-` rest, `|` and
// whitespace are separators. Lane names map onto the GM drum set
// (channel 9). Lanes may differ in length — each repeats over the clip,
// whose length is the longest lane (or the forced beat count).

#include <map>
#include <string>
#include <vector>

namespace yawn {
namespace livecode {

struct PatternNote {
    double start = 0.0;   // beats from clip origin
    double dur   = 0.0;   // beats
    double vel   = 0.8;   // 0..1
    int    pitch = 0;     // MIDI 0..127
    int    ch    = 0;     // MIDI channel 0..15 (drums: 9)
};

struct PatternClip {
    double beats = 0.0;    // total clip length in beats
    std::vector<PatternNote> notes;
};

// Parse a melodic phrase. On failure returns false with a human-readable
// `err` (includes the offending token).
bool parseMelodicPhrase(const std::string& src, PatternClip& out,
                        std::string& err);

// Parse a drum grid. `forceBeats` > 0 pins the clip length (lanes loop
// to fill); 0 uses the longest lane's length. On unknown lane names the
// error lists the valid ones.
bool parseDrumGrid(const std::map<std::string, std::string>& lanes,
                   double forceBeats, PatternClip& out, std::string& err);

} // namespace livecode
} // namespace yawn
