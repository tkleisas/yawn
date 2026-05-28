#pragma once

// DrumPatterns — factory drum-loop generator. On first run it seeds the
// YAWN-managed MIDI loops library with a construction-kit of GM drum
// patterns (rock, funk, disco, house, techno, waltz, hip-hop, breaks,
// dnb, idm, world + odd meters). Each pattern is written as an SMF under
// <midi_loops>/drums/<name>.mid, on GM drum channel 9.
//
// Seeding is idempotent: a pattern file is only written if it does not
// already exist, so re-runs (and user deletions) are respected.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace yawn {

class DrumPatterns {
public:
    // Write any missing factory drum loops to the library.
    // Returns the number of new files written this call.
    static int seedFactoryLoops();
};

// ─────────────────────────────────────────────────────────────────────────────
// Shared rhythm kit. The genre grooves/fills + arrangement templates that
// DrumPatterns composes into loops are exposed here so MelodicPatterns can
// derive its bass/lead/chord/misc loops from the very same drum rhythms
// (bass off the kick, chords off the snare/clap, etc.).
// ─────────────────────────────────────────────────────────────────────────────
namespace drumkit {

// GM drum note numbers (channel 9). Used both as the table's pitch keys
// and as rhythm sources for the melodic generator.
enum Gm : uint8_t {
    Kick      = 36, Rim       = 37, Snare     = 38, Clap     = 39,
    HatClosed = 42, TomFloor  = 43, TomLow    = 45, HatOpen  = 46,
    TomMid    = 48, Crash     = 49, TomHi     = 50, Ride     = 51,
    CongaHi   = 63, CongaLo   = 64,
};

// One bar of one instrument's grid, keyed by GM pitch.
using Bar = std::map<uint8_t, std::string>;
struct Genre {
    const char*         name;
    int                 tsNum;   // time-signature numerator
    int                 tsDen;   // time-signature denominator (4, 8 or 16)
    int                 bpm;
    std::map<char, Bar> bars;    // 'A'..'C' grooves, 'F'/'G' fills
};

// A resolved arrangement for a genre: the letter sequence, plus the
// 1-based index and descriptor DrumPatterns uses to name the file.
struct ArrEntry {
    std::string arr;
    int         index;
    int         bars;
    bool        fill;
};

constexpr double kStepBeats = 0.25;   // one grid step = a sixteenth note

const std::vector<Genre>&       genres();
const std::vector<std::string>& arrangements();

int      stepsPerBar(int tsNum, int tsDen);
bool     isHit(char c);
uint16_t velFor(char c);

// Arrangements valid for a genre — all letters present and a whole-beat
// total (the SMF reader rounds up, so odd meters skip fractional totals).
// Indexed exactly as DrumPatterns names its files.
std::vector<ArrEntry> validArrangements(const Genre& g);
std::string           descriptor(const ArrEntry& e);   // e.g. " (4-bar fill)"

// Composed multi-bar grid (length stepsPerBar*bars) for one drum pitch
// (rests where unused), or the per-step union of several pitches (loudest
// hit wins). These are the rhythms the melodic generator lays pitches on.
std::string composedRow(const Genre& g, const std::string& arr, uint8_t pitch);
std::string composedUnion(const Genre& g, const std::string& arr,
                          const std::vector<uint8_t>& pitches);

} // namespace drumkit
} // namespace yawn
