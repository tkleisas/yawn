#include "presets/MelodicPatterns.h"

#include "presets/DrumPatterns.h"   // drumkit:: shared rhythm engine
#include "presets/MidiLoopManager.h"
#include "midi/MidiClip.h"
#include "util/MidiFileIO.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace yawn {

namespace fs = std::filesystem;
namespace dk = drumkit;

namespace {

// ── Music theory ────────────────────────────────────────────────────────────
struct Scale { int deg[7]; };
const Scale kMajor    = {{0, 2, 4, 5, 7, 9, 11}};
const Scale kMinor    = {{0, 2, 3, 5, 7, 8, 10}};
const Scale kDorian   = {{0, 2, 3, 5, 7, 9, 10}};
const Scale kPhrygian = {{0, 1, 3, 5, 7, 8, 10}};
const Scale kHijaz    = {{0, 1, 4, 5, 7, 8, 10}};   // eastern / maqam flavour
const Scale kRaga     = {{0, 1, 4, 5, 7, 8, 11}};   // Bhairav-ish

// Semitone offset of a (possibly out-of-octave) scale degree from the tonic.
int scaleSemi(const Scale& s, int deg) {
    int oct = deg / 7;
    int idx = deg % 7;
    if (idx < 0) { idx += 7; --oct; }
    return oct * 12 + s.deg[idx];
}

enum class Voice { Bass, Lead, Chord, Misc };

struct Melody {
    Scale            scale;
    int              tonic;       // MIDI note of the tonic in the bass octave
    std::vector<int> prog;        // chord-root scale degrees, one per bar (cycles)
    bool             seventh;     // add the 7th to chord voicings
    bool             octBass;     // alternate root / octave on the bass
};

// Per-genre musical character, keyed by the drumkit genre name. Falls back to
// a neutral minor vamp for any genre without an explicit entry.
const Melody& melodyFor(const std::string& name) {
    struct Entry { const char* name; Melody m; };
    static const std::vector<Entry> kTable = {
        { "Rock",         { kMinor,    40, {0, 5, 2, 6}, false, false } },
        { "Funk",         { kDorian,   40, {0, 0, 3, 0}, true,  false } },
        { "Disco",        { kMajor,    36, {0, 5, 1, 4}, true,  true  } },
        { "House",        { kMinor,    45, {0, 5, 6, 0}, false, false } },
        { "Techno",       { kPhrygian, 33, {0, 0, 1, 0}, false, false } },
        { "HipHop",       { kDorian,   41, {0, 0, 3, 3}, true,  false } },
        { "Breakbeat",    { kMinor,    40, {0, 5, 6, 5}, false, false } },
        { "DnB",          { kMinor,    38, {0, 0, 5, 6}, false, false } },
        { "IDM",          { kPhrygian, 36, {0, 1, 5, 6}, false, false } },
        { "Waltz",        { kMajor,    43, {0, 4, 5, 3}, false, false } },
        { "Latin",        { kDorian,   38, {0, 3, 4, 0}, true,  false } },
        { "Salsa",        { kMajor,    36, {0, 4, 1, 4}, true,  false } },
        { "Balkan",       { kPhrygian, 38, {0, 1, 0, 6}, false, false } },
        { "Greek",        { kHijaz,    38, {0, 6, 0, 1}, false, false } },
        { "Arabic",       { kHijaz,    40, {0, 0, 1, 0}, false, false } },
        { "Indian",       { kRaga,     38, {0, 0, 0, 0}, false, false } },
        { "Odd 5-4",      { kMinor,    40, {0, 5, 2, 6}, false, false } },
        { "Odd 7-8",      { kPhrygian, 38, {0, 6, 0, 1}, false, false } },
        { "Odd 9-8",      { kDorian,   40, {0, 3, 0, 5}, false, false } },
        { "Odd 11-8",     { kMinor,    38, {0, 5, 6, 0}, false, false } },
        { "Odd 13-8",     { kPhrygian, 36, {0, 1, 5, 6}, false, false } },
        { "Glitch 15-16", { kPhrygian, 36, {0, 1, 5, 6}, false, false } },
        { "Glitch 5-16",  { kMinor,    40, {0, 0, 5, 6}, false, false } },
        { "Poly 7-4",     { kDorian,   38, {0, 3, 4, 0}, false, false } },
    };
    for (const auto& e : kTable)
        if (name == e.name) return e.m;
    static const Melody kDefault = { kMinor, 40, {0, 5, 2, 6}, false, false };
    return kDefault;
}

bool hasHit(const std::string& s) {
    for (char c : s) if (dk::isHit(c)) return true;
    return false;
}

// Derive a voice's rhythm from the genre's composed drum rows: bass off the
// kick, chords off the snare/clap, lead off the hats, misc off everything.
// Tries fallbacks so genres without a given drum (e.g. clave-driven Latin)
// still produce a sensible part.
std::string voiceRhythm(const dk::Genre& g, const std::string& arr, Voice v) {
    using namespace dk;
    std::vector<std::vector<uint8_t>> cands;
    switch (v) {
        case Voice::Bass:
            cands = {{Kick}, {CongaLo}, {Snare}}; break;
        case Voice::Chord:
            cands = {{Snare, Clap}, {Rim}, {Kick}}; break;
        case Voice::Lead:
            cands = {{HatClosed}, {HatOpen, Ride, Rim}, {Snare}, {Kick}}; break;
        case Voice::Misc:
            cands = {{Kick, Snare, Clap, HatClosed, Rim, CongaLo, CongaHi}}; break;
    }
    std::string r;
    for (const auto& c : cands) {
        r = composedUnion(g, arr, c);
        if (hasHit(r)) return r;
    }
    return r;   // last attempt (possibly empty → caller skips)
}

int voiceBase(int tonic, Voice v) {
    switch (v) {
        case Voice::Bass:  return tonic;
        case Voice::Chord: return tonic + 12;
        case Voice::Lead:  return tonic + 24;
        case Voice::Misc:  return tonic + 12;
    }
    return tonic;
}
const char* voiceFolder(Voice v) {
    switch (v) {
        case Voice::Bass:  return "bass";
        case Voice::Chord: return "chord";
        case Voice::Lead:  return "lead";
        case Voice::Misc:  return "misc";
    }
    return "misc";
}
const char* voiceLabel(Voice v) {
    switch (v) {
        case Voice::Bass:  return "Bass";
        case Voice::Chord: return "Chords";
        case Voice::Lead:  return "Lead";
        case Voice::Misc:  return "Misc";
    }
    return "Misc";
}

// Lay in-key pitches on a composed drum rhythm. The chord follows the
// progression per bar; lead/misc cycle a small chord-relative figure.
midi::MidiClip buildVoiceClip(const dk::Genre& g, const Melody& mel,
                              Voice voice, const std::string& rhythm) {
    const int S     = dk::stepsPerBar(g.tsNum, g.tsDen);
    const int nbars = S > 0 ? static_cast<int>(rhythm.size()) / S : 0;
    const int base  = voiceBase(mel.tonic, voice);
    const double gate = 0.9;

    std::vector<int> hits;
    for (int i = 0; i < static_cast<int>(rhythm.size()); ++i)
        if (dk::isHit(rhythm[i])) hits.push_back(i);

    midi::MidiClip clip;
    clip.setLoop(true);
    if (hits.empty() || nbars == 0) {
        clip.setLengthBeats(std::max(1, nbars) * S * dk::kStepBeats);
        return clip;
    }

    static const int kMotif[] = {0, 2, 4, 2};   // lead  — chord-relative degrees
    static const int kArp[]   = {0, 2, 4, 6};   // misc  — ascending arpeggio
    const int progN = static_cast<int>(mel.prog.size());

    for (size_t h = 0; h < hits.size(); ++h) {
        const int    step    = hits[h];
        const int    bar     = step / S;
        const int    deg     = mel.prog[(progN > 0) ? (bar % progN) : 0];
        const uint16_t vel   = dk::velFor(rhythm[step]);
        const double start   = step * dk::kStepBeats;
        const int    nextStep = (h + 1 < hits.size())
                                  ? hits[h + 1]
                                  : nbars * S;
        const double dur = std::max(1, nextStep - step) * dk::kStepBeats * gate;

        auto add = [&](int semi) {
            int pitch = base + semi;
            if (pitch < 0)   pitch = 0;
            if (pitch > 127) pitch = 127;
            midi::MidiNote n;
            n.startBeat = start;
            n.duration  = dur;
            n.pitch     = static_cast<uint8_t>(pitch);
            n.channel   = 0;
            n.velocity  = vel;
            clip.addNote(n);
        };

        switch (voice) {
            case Voice::Bass: {
                int semi = scaleSemi(mel.scale, deg);
                if (mel.octBass && (h % 2 == 1)) semi += 12;
                add(semi);
                break;
            }
            case Voice::Chord:
                add(scaleSemi(mel.scale, deg));
                add(scaleSemi(mel.scale, deg + 2));
                add(scaleSemi(mel.scale, deg + 4));
                if (mel.seventh) add(scaleSemi(mel.scale, deg + 6));
                break;
            case Voice::Lead:
                add(scaleSemi(mel.scale, deg + kMotif[h % 4]));
                break;
            case Voice::Misc:
                add(scaleSemi(mel.scale, deg + kArp[h % 4]));
                break;
        }
    }

    // The SMF reader rounds loop length up to whole beats — extend the final
    // note to the boundary so the loop length stays exact.
    const double totalBeats = nbars * S * dk::kStepBeats;
    int    lastIdx = -1;
    double maxEnd  = 0.0;
    for (int i = 0; i < clip.noteCount(); ++i) {
        const double end = clip.note(i).startBeat + clip.note(i).duration;
        if (end > maxEnd) { maxEnd = end; lastIdx = i; }
    }
    if (lastIdx >= 0 && maxEnd < totalBeats - 1e-9)
        clip.note(lastIdx).duration += (totalBeats - maxEnd);

    clip.setLengthBeats(totalBeats);
    return clip;
}

} // namespace

int MelodicPatterns::seedFactoryLoops() {
    const fs::path root = MidiLoopManager::midiLoopsRootDir();
    std::error_code ec;

    const Voice voices[] = { Voice::Bass, Voice::Lead, Voice::Chord, Voice::Misc };

    int written = 0;
    for (const dk::Genre& g : dk::genres()) {
        const Melody& mel = melodyFor(g.name);
        for (const dk::ArrEntry& e : dk::validArrangements(g)) {
            char num[8];
            std::snprintf(num, sizeof num, "%02d", e.index);
            const std::string suffix = drumkit::descriptor(e);

            for (Voice v : voices) {
                std::string rhythm = voiceRhythm(g, e.arr, v);
                if (!hasHit(rhythm)) continue;

                midi::MidiClip clip = buildVoiceClip(g, mel, v, rhythm);
                if (clip.noteCount() == 0) continue;

                std::string name = std::string(g.name) + " " + voiceLabel(v) +
                                   " " + num + suffix;
                fs::path dir = root / voiceFolder(v);
                fs::create_directories(dir, ec);
                std::string safe = MidiLoopManager::sanitizeName(name);
                fs::path file = dir / (safe + ".mid");
                if (fs::exists(file, ec)) continue;   // idempotent

                clip.setName(name);
                if (util::saveMidiFile(file, clip, static_cast<double>(g.bpm),
                                       g.tsNum, g.tsDen))
                    ++written;
            }
        }
    }
    return written;
}

} // namespace yawn
