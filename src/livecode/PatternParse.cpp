#include "livecode/PatternParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace yawn {
namespace livecode {

namespace {

// Whole-note divisions accepted by the `_N` duration suffix. Returns
// 0 on an unsupported division (the caller raises the error).
double divisionToBeats(int div) {
    switch (div) {
        case 1:  return 4.0;
        case 2:  return 2.0;
        case 4:  return 1.0;
        case 8:  return 0.5;
        case 16: return 0.25;
        case 32: return 0.125;
        default: return 0.0;
    }
}

// [A-Ga-g][#b]?<octave> — MIDI 60 = C4. Returns false + err on a bad
// pitch. `end` receives the index one past the pitch.
bool parsePitchAt(const std::string& s, size_t& i, int& outMidi,
                  std::string& err) {
    static const int semis[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    if (i >= s.size()) {
        err = "unexpected end of pattern (expected a note)";
        return false;
    }
    const char c = static_cast<char>(std::toupper(
        static_cast<unsigned char>(s[i])));
    if (c < 'A' || c > 'G') {
        err = std::string("expected a note (A-G), got '") + s[i] + "'";
        return false;
    }
    int pc = semis[c - 'A'];
    ++i;
    if (i < s.size() && (s[i] == '#' || s[i] == 'b')) {
        pc += (s[i] == '#') ? 1 : -1;
        ++i;
    }
    // Octave: optional '-' then digits. MIDI 0 = C-1.
    int octSign = 1;
    if (i < s.size() && s[i] == '-') { octSign = -1; ++i; }
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
        err = std::string("missing octave number after '") +
              s.substr(0, i) + "' (e.g. C4)";
        return false;
    }
    int oct = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        oct = oct * 10 + (s[i] - '0');
        if (oct > 12) {
            err = "octave number out of range";
            return false;
        }
        ++i;
    }
    oct *= octSign;
    const int midi = (oct + 1) * 12 + pc;
    if (midi < 0 || midi > 127) {
        err = "pitch out of MIDI range (0..127)";
        return false;
    }
    outMidi = midi;
    return true;
}

// '_' division ['*' mult] ['@' vel]. Returns false + err on a bad suffix.
bool parseDurationAt(const std::string& s, size_t& i, double& outBeats,
                     double& outVel, bool& velSet, std::string& err) {
    if (i >= s.size() || s[i] != '_') {
        err = std::string("missing duration after '") + s.substr(0, i) +
              "' (e.g. _4, _8, _16)";
        return false;
    }
    ++i;   // '_'
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
        err = "missing duration number after '_'";
        return false;
    }
    int div = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        div = div * 10 + (s[i] - '0');
        if (div > 100) {
            err = "unsupported duration (use _1, _2, _4, _8, _16 or _32)";
            return false;
        }
        ++i;
    }
    double beats = divisionToBeats(div);
    if (beats <= 0.0) {
        err = "unsupported duration _" + std::to_string(div) +
              " (use _1, _2, _4, _8, _16 or _32)";
        return false;
    }
    if (i < s.size() && s[i] == '*') {
        ++i;
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
            err = "missing multiplier after '*' (e.g. _16*4)";
            return false;
        }
        long mult = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            mult = mult * 10 + (s[i] - '0');
            if (mult > 1024) {
                err = "duration multiplier too large (max 1024)";
                return false;
            }
            ++i;
        }
        beats *= static_cast<double>(mult);
    }
    if (i < s.size() && s[i] == '@') {
        ++i;
        const size_t numStart = i;
        while (i < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i])) ||
                s[i] == '.')) ++i;
        if (i == numStart) {
            err = "missing velocity after '@' (0..1, e.g. @0.5)";
            return false;
        }
        outVel = std::clamp(std::atof(s.c_str() + numStart), 0.0, 1.0);
        velSet = true;
    }
    outBeats = beats;
    return true;
}

} // namespace

bool parseMelodicPhrase(const std::string& src, PatternClip& out,
                        std::string& err) {
    out = PatternClip{};
    const std::string s = src;
    size_t i = 0;
    double cursor = 0.0;
    bool any = false;

    auto skipSep = [&] {
        while (i < s.size() &&
               (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '|'))
            ++i;
    };

    while (true) {
        skipSep();
        if (i >= s.size()) break;

        std::vector<int> pitches;   // 1 note, or N for a chord
        bool isRest = false;
        if (s[i] == '[') {
            ++i;
            while (true) {
                skipSep();
                if (i >= s.size()) {
                    err = "unterminated chord '[' (missing ']')";
                    return false;
                }
                if (s[i] == ']') { ++i; break; }
                // A duration char where a pitch is expected almost always
                // means the ']' was forgotten — say that, not "bad note".
                if (s[i] == '_' || s[i] == '@') {
                    err = "unterminated chord '[' (missing ']' before '" +
                          std::string(1, s[i]) + "')";
                    return false;
                }
                int midi = 0;
                if (!parsePitchAt(s, i, midi, err)) return false;
                pitches.push_back(midi);
            }
            if (pitches.empty()) {
                err = "empty chord '[]'";
                return false;
            }
        } else if (s[i] == 'R' || s[i] == 'r') {
            // A rest — but only when not part of a pitch... 'R' is not a
            // pitch letter, safe. Consume and continue to the duration.
            ++i;
            isRest = true;
        } else {
            int midi = 0;
            if (!parsePitchAt(s, i, midi, err)) return false;
            pitches.push_back(midi);
        }

        double dur = 0.0, vel = 0.8;
        bool velSet = false;
        if (!parseDurationAt(s, i, dur, vel, velSet, err)) return false;
        if (dur <= 0.0) {
            err = "duration must be positive";
            return false;
        }

        if (!isRest) {
            for (int midi : pitches) {
                PatternNote n;
                n.start = cursor;
                n.dur   = dur;
                n.vel   = vel;
                n.pitch = midi;
                n.ch    = 0;
                out.notes.push_back(n);
            }
            any = true;
        }
        cursor += dur;
        (void)velSet;
    }

    if (!any) {
        err = "pattern contains no notes";
        return false;
    }
    out.beats = cursor;
    return true;
}

bool parseDrumGrid(const std::map<std::string, std::string>& lanes,
                   double forceBeats, PatternClip& out, std::string& err) {
    out = PatternClip{};

    // Lane name → GM pitch (channel 9). Case-insensitive aliases.
    static const std::map<std::string, int> kLanePitch = {
        {"kick", 36},    {"bd", 36},
        {"rim", 37},     {"rimshot", 37},  {"rs", 37},
        {"snare", 38},   {"sn", 38},
        {"clap", 39},    {"cp", 39},
        {"hihat", 42},   {"hh", 42},       {"ch", 42},
        {"closedhat", 42}, {"closedhh", 42},
        {"tomfloor", 43}, {"ft", 43},      {"floortom", 43},
        {"tomlow", 45},  {"lt", 45},       {"lowtom", 45},
        {"tommid", 48},  {"tm", 48},       {"midtom", 48}, {"tom", 48},
        {"openhh", 46},  {"oh", 46},       {"openhat", 46},
        {"tomhi", 50},   {"ht", 50},       {"hightom", 50},
        {"crash", 49},   {"cr", 49},       {"cy", 49},     {"cymbal", 49},
        {"ride", 51},    {"rd", 51},
    };

    if (lanes.empty()) {
        err = "drum pattern has no lanes";
        return false;
    }

    // Resolve + measure lanes.
    struct Resolved {
        int pitch;
        std::vector<char> steps;   // grid chars, separators stripped
        std::string name;          // original name (for errors)
    };
    std::vector<Resolved> resolved;
    size_t maxSteps = 0;
    for (const auto& [name, grid] : lanes) {
        std::string key;
        for (char c : name)
            key.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        const auto it = kLanePitch.find(key);
        if (it == kLanePitch.end()) {
            std::string valid;
            for (const auto& [k, p] : kLanePitch) {
                if (!valid.empty()) valid += ", ";
                valid += k;
            }
            err = "unknown drum lane '" + name + "' (valid: " + valid + ")";
            return false;
        }
        Resolved r;
        r.pitch = it->second;
        r.name  = name;
        for (char c : grid) {
            if (c == ' ' || c == '|') continue;
            r.steps.push_back(c);
        }
        if (r.steps.empty()) {
            err = "drum lane '" + name + "' is empty";
            return false;
        }
        for (size_t k = 0; k < r.steps.size(); ++k) {
            const char c = r.steps[k];
            if (c != 'X' && c != 'x' && c != 'o' && c != 'g' &&
                c != '.' && c != '-') {
                err = "unknown grid char '" + std::string(1, c) +
                      "' in lane " + name + " (step " +
                      std::to_string(k + 1) +
                      "; use X x o g for hits, . or - for rests)";
                return false;
            }
        }
        maxSteps = std::max(maxSteps, r.steps.size());
        resolved.push_back(std::move(r));
    }

    constexpr double kStepBeats = 0.25;   // one step = a 16th note
    double beats = forceBeats > 0.0
        ? forceBeats
        : static_cast<double>(maxSteps) * kStepBeats;
    if (beats <= 0.0) {
        err = "drum pattern length must be positive";
        return false;
    }
    const long totalSteps = static_cast<long>(
        std::lround(beats / kStepBeats));
    if (totalSteps <= 0) {
        err = "drum pattern length must be a multiple of a 16th note";
        return false;
    }

    for (const auto& r : resolved) {
        for (long step = 0; step < totalSteps; ++step) {
            const char c = r.steps[step % r.steps.size()];
            double vel = 0.0;
            switch (c) {
                case 'X': vel = 112.0 / 127.0; break;
                case 'x': vel = 96.0  / 127.0; break;
                case 'o': vel = 76.0  / 127.0; break;
                case 'g': vel = 40.0  / 127.0; break;
                default:  continue;            // rest
            }
            PatternNote n;
            n.start = step * kStepBeats;
            n.dur   = kStepBeats;
            n.vel   = vel;
            n.pitch = r.pitch;
            n.ch    = 9;
            out.notes.push_back(n);
        }
    }
    out.beats = beats;
    return true;
}

} // namespace livecode
} // namespace yawn
