#include "presets/DrumPatterns.h"

#include "presets/MidiLoopManager.h"
#include "midi/MidiClip.h"
#include "util/MidiFileIO.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace yawn {

namespace fs = std::filesystem;

namespace drumkit {

// Grid characters → 16-bit velocity (the engine stores vel7 << 9).
// 'X' accent, 'x' normal, 'o' soft, 'g' ghost, '.' rest.
// Any other char (e.g. '|' or ' ') is a visual separator: it advances no step.
uint16_t velFor(char c) {
    switch (c) {
        case 'X': return static_cast<uint16_t>(112u << 9);
        case 'x': return static_cast<uint16_t>(96u  << 9);
        case 'o': return static_cast<uint16_t>(76u  << 9);
        case 'g': return static_cast<uint16_t>(40u  << 9);
        default:  return 0;
    }
}
bool isHit(char c)  { return c == 'X' || c == 'x' || c == 'o' || c == 'g'; }
static bool isStep(char c) { return isHit(c) || c == '.'; }

// Sixteenth-note steps in one bar of this meter.
int stepsPerBar(int tsNum, int tsDen) { return tsNum * 16 / tsDen; }

// Strip visual separators, keeping only grid characters.
static std::string gridOnly(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s)
        if (isStep(c)) o.push_back(c);
    return o;
}

// Normalize an authored bar to exactly n grid steps (pad rests / truncate).
// The clamp keeps a stray miscount from drifting alignment across bars.
static std::string fitBar(const std::string& g, int n) {
    std::string o = gridOnly(g);
    if (static_cast<int>(o.size()) < n) o.append(n - o.size(), '.');
    else if (static_cast<int>(o.size()) > n) o.resize(n);
    return o;
}

// Arrangement templates. Letters A-C pick grooves, F/G pick fills. Weighted
// toward long, evolving phrases (4- and 8-bar) with fill turnarounds. Every
// template's bar count is even, so odd meters (7/8, 9/8 …) still total a
// whole number of beats. A genre emits one loop per template whose letters
// it defines.
const std::vector<std::string>& arrangements() {
    static const std::vector<std::string> kArr = {
        "AB",
        "AAAF",
        "AABF",
        "ABAF",
        "ABCF",
        "BCBG",
        "ACAG",
        "CCCF",
        "BBBG",
        "AAAG",
        "BBBF",
        "ABCA",
        "AAAAAAAF",
        "AAAFAAAG",
        "AABFAABG",
        "ABABABAF",
        "AAABAAAF",
        "ABCABCAG",
        "AABFCCCG",
        "ACACACAF",
        "BCBCBCBG",
        "ABCABCAB",
    };
    return kArr;
}

const std::vector<Genre>& genres() {
    static const std::vector<Genre> kGenres = {
        // ── Rock (4/4) ──────────────────────────────────────────────────────
        { "Rock", 4, 4, 120, {
            {'A', {{Kick,"X.......X......."},{Snare,"....X.......X..."},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.......X.X....."},{Snare,"....X.......X..."},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X..X....X..X...."},{Snare,"....X.......X..."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X......."},{Snare,"....X...x.x.X.x."},{TomHi,"............X..."},{TomLow,"..............X."}}},
            {'G', {{Snare,"XxXx............"},{TomHi,"....XxXx........"},{TomMid,"........XxXx...."},{TomLow,"............XxXx"}}},
        }},
        // ── Funk (4/4) ──────────────────────────────────────────────────────
        { "Funk", 4, 4, 100, {
            {'A', {{Kick,"X..X..X...X....."},{Snare,"....X.g.....X.g."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'B', {{Kick,"X.....X..X.X...."},{Snare,"....X..g.g..X..g"},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X..X.X..X..X.X.."},{Snare,"....X.......X..."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X......."},{Snare,"g.g.X.g.g.g.X.g."},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'G', {{Snare,"XgXgXgXg........"},{TomHi,"........XxXx...."},{TomLow,"............XxXx"}}},
        }},
        // ── Disco (4/4) ─────────────────────────────────────────────────────
        { "Disco", 4, 4, 120, {
            {'A', {{Kick,"X...X...X...X..."},{Clap,"....X.......X..."},{HatOpen,"..x...x...x...x."}}},
            {'B', {{Kick,"X...X...X...X..."},{Snare,"....X.......X..."},{HatClosed,"x.x.x.x.x.x.x.x."},{HatOpen,"..x...x...x...x."}}},
            {'C', {{Kick,"X...X...X...X..."},{Snare,"....X.......X..."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X...X...X......."},{Snare,"........X.X.X.X."},{HatOpen,"..x...x........."}}},
            {'G', {{Snare,"X.X.X.X.X.X.X.X."},{TomHi,"........X.X....."},{TomLow,"............X.X."}}},
        }},
        // ── House (4/4) ─────────────────────────────────────────────────────
        { "House", 4, 4, 124, {
            {'A', {{Kick,"X...X...X...X..."},{Clap,"....X.......X..."},{HatClosed,"x.x.x.x.x.x.x.x."},{HatOpen,"..x...x...x...x."}}},
            {'B', {{Kick,"X...X...X...X..."},{Clap,"....X.......X..."},{HatOpen,"..x...x...x...x."}}},
            {'C', {{Kick,"X...X...X...X..."},{Clap,"....X.......X..."},{HatClosed,"xxxxxxxxxxxxxxxx"},{HatOpen,"..x...x...x...x."}}},
            {'F', {{Kick,"X...X...X...X..."},{Clap,"....X...X...X.X."},{HatOpen,"..x...x...x...x."}}},
            {'G', {{Kick,"X...X...X......."},{Snare,"............XxXx"},{HatOpen,"..x...x........."}}},
        }},
        // ── Techno (4/4) ────────────────────────────────────────────────────
        { "Techno", 4, 4, 130, {
            {'A', {{Kick,"X...X...X...X..."},{Clap,"....X.......X..."},{HatClosed,"..x...x...x...x."}}},
            {'B', {{Kick,"X...X...X...X..."},{HatClosed,"x.x.x.x.x.x.x.x."},{HatOpen,"..x...x...x...x."}}},
            {'C', {{Kick,"X...X...X...X..."},{Rim,"..x.x...x.x.x..."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X...X...X...X..."},{Clap,"............X.X."},{HatOpen,"..x...x...x...x."}}},
            {'G', {{Kick,"X...X...X......."},{Snare,"XxXxXxXx........"},{HatOpen,"............x.x."}}},
        }},
        // ── Hip-Hop (4/4) ───────────────────────────────────────────────────
        { "HipHop", 4, 4, 90, {
            {'A', {{Kick,"X.....X...X....."},{Snare,"....X.......X..."},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X..X....X......."},{Snare,"....X.......X..g"},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X.....X.X.X....."},{Snare,"....X.g.....X.g."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X......."},{Snare,"....X.......x.x."},{Rim,"............X.X."}}},
            {'G', {{Snare,"X.g.X.g.X.g.X.g."},{TomLow,"............XxXx"},{HatClosed,"x.x.x.x.x.x.x.x."}}},
        }},
        // ── Breakbeat (4/4) ─────────────────────────────────────────────────
        { "Breakbeat", 4, 4, 136, {
            {'A', {{Kick,"X.......X.X....."},{Snare,"....X..g.X..X.g."},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.X.....X......."},{Snare,"....X.......X..g"},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X.....X...X..X.."},{Snare,"....X..X....X..."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X......."},{Snare,"....X...g.X.gX.x"},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'G', {{Snare,"X.g.X.g.X.gXg.X."},{TomHi,"............X.X."},{HatOpen,"..x...x........."}}},
        }},
        // ── Drum & Bass (4/4, amen-style) ───────────────────────────────────
        { "DnB", 4, 4, 174, {
            {'A', {{Kick,"X.......X.X....."},{Snare,"....X......X.X.g"},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.X.....X......."},{Snare,"....X..g...X...g"},{HatOpen,"............x..."}}},
            {'C', {{Kick,"X.......X......."},{Snare,"....X..gX.g.X.gg"},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X......."},{Snare,"....X......X.X.X"},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'G', {{Snare,"X.gXg.gX.gXg.gX."},{HatOpen,"..x...x...x...x."}}},
        }},
        // ── IDM (4/4) ───────────────────────────────────────────────────────
        { "IDM", 4, 4, 140, {
            {'A', {{Kick,"X..X...X..X....."},{Snare,"....X.....X.X..g"},{HatClosed,"x.xxx.x.xx.x.xxx"}}},
            {'B', {{Kick,"X...X..X.X......"},{Snare,"....X...g.X.X..."},{HatClosed,"xx.xx.xxx.xx.xxx"}}},
            {'C', {{Kick,"X.X..X..X...X.X."},{Snare,"..g.X..g..X.g.X."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X..X...."},{Snare,"g.gXg.gXg.gXg.gX"},{Rim,"..x...x...x...x."}}},
            {'G', {{Snare,"XgXgXgXgXgXgXgXg"},{TomHi,"X...X...X...X..."},{HatOpen,"..x...x...x...x."}}},
        }},
        // ── Waltz (3/4) ─────────────────────────────────────────────────────
        { "Waltz", 3, 4, 120, {
            {'A', {{Kick,"X..........."},{Snare,"....X...X..."},{HatClosed,"x...x...x..."}}},
            {'B', {{Kick,"X..........."},{Snare,"....X...X..."},{HatClosed,"x.x.x.x.x.x."}}},
            {'C', {{Kick,"X...X......."},{Snare,"....X...X..."},{Ride,"x.x.x.x.x.x."}}},
            {'F', {{Kick,"X..........."},{Snare,"....x.x.X.x."},{TomLow,"..........X."}}},
            {'G', {{Snare,"XxXx........"},{TomHi,"....XxXx...."},{TomLow,"........XxXx"}}},
        }},
        // ── Latin (4/4, songo / rumba) ──────────────────────────────────────
        { "Latin", 4, 4, 105, {
            {'A', {{Kick,"X.......X......."},{CongaLo,"X..X..X..X..X..X"},{CongaHi,"..x.x..x..x.x..x"},{HatClosed,"x.x.x.x.x.x.x.x."},{Rim,"..X.X.....X.X..."}}},
            {'B', {{Kick,"....X.......X..."},{Rim,"..X.X.....X.X..."},{HatClosed,"xxxxxxxxxxxxxxxx"},{CongaLo,"X..X..X..X..X..X"}}},
            {'C', {{Kick,"X...X...X...X..."},{Rim,"X..X..X.X..X..X."},{CongaHi,"..x.x..x..x.x..x"}}},
            {'F', {{Kick,"X.......X......."},{Snare,"....X...x.x.X.x."},{TomLow,"...........X.X.X"}}},
            {'G', {{Snare,"XxXx........XxXx"},{TomHi,"....XxXx........"},{TomLow,"........XxXx...."}}},
        }},
        // ── Salsa (4/4, clave + cascara) ────────────────────────────────────
        { "Salsa", 4, 4, 95, {
            {'A', {{Rim,"X..X..X...X.X..."},{HatClosed,"x.xx.xx.x.xx.xx."},{CongaLo,"X..X..X..X..X..X"},{Kick,"....X.......X..."}}},
            {'B', {{Rim,"X..X..X...X.X..."},{HatClosed,"x.x.x.x.x.x.x.x."},{CongaHi,"..x.x..x..x.x..x"},{Kick,"....X.......X..."}}},
            {'C', {{Rim,"..X.X...X..X..X."},{HatClosed,"xxxxxxxxxxxxxxxx"},{CongaLo,"X..X..X..X..X..X"}}},
            {'F', {{Snare,"....X...x.x.X.x."},{TomLow,"...........X.X.X"},{Rim,"X..X..X...X.X..."}}},
            {'G', {{Snare,"XxXxXxXx........"},{TomHi,"........XxXx...."},{TomLow,"............XxXx"}}},
        }},
        // ── Balkan (7/8, 2+2+3) ─────────────────────────────────────────────
        { "Balkan", 7, 8, 150, {
            {'A', {{Kick,"X...X...X....."},{Snare,"....X...X....."},{HatClosed,"x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X...X...X....."},{HatClosed,"x.x.x.x.x.x.x."},{Rim,"..x...x...x.x."}}},
            {'C', {{Kick,"X.X.X.X.X....."},{Snare,"....X...X....."},{HatClosed,"xxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X...X........."},{Snare,"....x.x.X.x.X."},{TomLow,"............X."}}},
            {'G', {{Snare,"XxXx.........."},{TomHi,"....XxXx......"},{TomLow,"........XxXxX."}}},
        }},
        // ── Greek (9/8, 2+2+2+3 Karsilamas) ─────────────────────────────────
        { "Greek", 9, 8, 130, {
            {'A', {{Kick,"X...X...X...X....."},{Snare,"....X.......X....."},{HatClosed,"x.x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X...X...X...X....."},{HatClosed,"x.x.x.x.x.x.x.x.x."},{Rim,"..x...x...x...x.x."}}},
            {'C', {{Kick,"X.X.X.X.X.X.X.X.X."},{Snare,"....X.......X....."},{HatClosed,"xxxxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X...X...X........."},{Snare,"....x.x.X.x.X.x.X."},{TomLow,"................X."}}},
            {'G', {{Snare,"XxXx.............."},{TomHi,"....XxXx.........."},{TomMid,"........XxXx......"},{TomLow,"............XxXxX."}}},
        }},
        // ── Arabic (4/4, Maqsum dum-tek) ────────────────────────────────────
        { "Arabic", 4, 4, 110, {
            {'A', {{Kick,"X.......X......."},{Rim,"....x.x.....x.x."},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X...X...X......."},{Rim,"....x.x.....x.x."},{HatClosed,"xxxxxxxxxxxxxxxx"}}},
            {'C', {{Kick,"X.......X...X..."},{Rim,"..x.x.x...x.x.x."},{HatClosed,"x.x.x.x.x.x.x.x."}}},
            {'F', {{Kick,"X.......X......."},{Snare,"....x.x.X.x.X.x."},{TomLow,"............X.X."}}},
            {'G', {{Snare,"XxXxXxXx........"},{TomHi,"........XxXx...."},{TomLow,"............XxXx"}}},
        }},
        // ── Indian (7/8, Rupak tal 3+2+2) ───────────────────────────────────
        { "Indian", 7, 8, 120, {
            {'A', {{Kick,"X.....X...X..."},{Rim,"x.x.x.x.x.x.x."},{HatClosed,"..x...x...x..."}}},
            {'B', {{Kick,"X.....X...X..."},{Rim,"x.xx.xx.x.xx.x"}}},
            {'C', {{Kick,"X.X...X.X.X..."},{Rim,"xxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.....X......."},{Rim,"x.xx.xx.X.xx.X"},{TomLow,"............X."}}},
            {'G', {{Rim,"XxXx.........."},{TomHi,"....XxXx......"},{TomLow,"........XxXxX."}}},
        }},
        // ── Odd 5/4 (Take Five feel) ────────────────────────────────────────
        { "Odd 5-4", 5, 4, 110, {
            {'A', {{Kick,"X.......X..........."},{Snare,"....X.......X......."},{HatClosed,"x.x.x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.......X.......X..."},{Snare,"....X.......X......."},{Ride,"x.x.x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X...X...X...X...X..."},{Snare,"....X.......X......."},{HatClosed,"xxxxxxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X..........."},{Snare,"............x.x.X.x."},{TomLow,"..................X."}}},
            {'G', {{Snare,"XxXx................"},{TomHi,"....XxXx............"},{TomMid,"........XxXx........"},{TomLow,"............XxXxXxXx"}}},
        }},
        // ── Odd 7/8 (prog, 3+2+2) ───────────────────────────────────────────
        { "Odd 7-8", 7, 8, 140, {
            {'A', {{Kick,"X.....X...X..."},{Snare,"......X......."},{HatClosed,"x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.....X.X....."},{Snare,"......X......."},{Ride,"x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X.X...X...X.X."},{Snare,"......X......."},{HatClosed,"xxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.....X......."},{Snare,"......x.X.x.X."},{TomLow,"............X."}}},
            {'G', {{Snare,"XxXx.........."},{TomHi,"....XxXx......"},{TomLow,"........XxXxX."}}},
        }},
        // ── Odd 9/8 (4+5) ───────────────────────────────────────────────────
        { "Odd 9-8", 9, 8, 120, {
            {'A', {{Kick,"X.......X........."},{Snare,"....X........X...."},{HatClosed,"x.x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.......X...X....."},{Snare,"....X........X...."},{Ride,"x.x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X...X...X...X...X."},{Snare,"....X........X...."},{HatClosed,"xxxxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X........."},{Snare,"..........x.x.X.x."},{TomLow,"................X."}}},
            {'G', {{Snare,"XxXx.............."},{TomHi,"....XxXx.........."},{TomMid,"........XxXx......"},{TomLow,"............XxXxXx"}}},
        }},
        // ── Odd 11/8 (3+3+3+2) ──────────────────────────────────────────────
        { "Odd 11-8", 11, 8, 130, {
            {'A', {{Kick,"X.....X.....X.....X..."},{Snare,"......X...........X..."},{HatClosed,"x.x.x.x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.....X.....X.....X..."},{Snare,"......X...........X..."},{Ride,"x.x.x.x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X.X...X.X...X.X...X..."},{Snare,"......X...........X..."},{HatClosed,"xxxxxxxxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.....X.....X........."},{Snare,"................x.X.x."},{TomLow,"....................X."}}},
            {'G', {{Snare,"XxXx.................."},{TomHi,"....XxXx.............."},{TomMid,"........XxXx.........."},{TomLow,"............XxXxXxXx.."}}},
        }},
        // ── Odd 13/8 (4+4+5) ────────────────────────────────────────────────
        { "Odd 13-8", 13, 8, 120, {
            {'A', {{Kick,"X.......X.......X........."},{Snare,"....X.......X............."},{HatClosed,"x.x.x.x.x.x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X.......X.......X...X....."},{Snare,"....X.......X............."},{Ride,"x.x.x.x.x.x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X...X...X...X...X...X...X."},{Snare,"....X.......X............."},{HatClosed,"xxxxxxxxxxxxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X.......X................."},{Snare,"....................x.X.x."},{TomLow,"........................X."}}},
            {'G', {{Snare,"XxXx......................"},{TomHi,"....XxXx.................."},{TomMid,"........XxXx.............."},{TomLow,"............XxXxXxXxXx...."}}},
        }},
        // ── Glitch 15/16 (4+4+4+3) ──────────────────────────────────────────
        { "Glitch 15-16", 15, 16, 160, {
            {'A', {{Kick,"X...X...X...X.."},{Snare,"....X.......X.."},{HatClosed,"x.x.x.x.x.x.x.x"}}},
            {'B', {{Kick,"X...X...X..X..."},{Snare,"....X.......X.."},{HatClosed,"xxxxxxxxxxxxxxx"}}},
            {'C', {{Kick,"X.X.X.X.X.X.X.X"},{Snare,"....X.......X.."},{HatClosed,"x.xx.xx.xx.x.xx"}}},
            {'F', {{Kick,"X...X.........."},{Snare,"........x.x.X.x"},{TomLow,".............X."}}},
            {'G', {{Snare,"XxXx..........."},{TomHi,"....XxXx......."},{TomLow,"........XxXxXxX"}}},
        }},
        // ── Glitch 5/16 (micro-loop) ────────────────────────────────────────
        { "Glitch 5-16", 5, 16, 170, {
            {'A', {{Kick,"X.x.."},{Snare,"..X.."},{HatClosed,"x.x.x"}}},
            {'B', {{Kick,"X...x"},{HatClosed,"xxxxx"},{Rim,"..x.."}}},
            {'C', {{Kick,"X.X.x"},{Snare,"..X.."},{HatClosed,"x.x.x"}}},
            {'F', {{Kick,"X...."},{Snare,"..x.X"},{TomLow,"....X"}}},
            {'G', {{Snare,"XxXxX"},{TomLow,"...XX"}}},
        }},
        // ── Poly 7/4 (wide odd meter) ───────────────────────────────────────
        { "Poly 7-4", 7, 4, 100, {
            {'A', {{Kick,"X...............X..........."},{Snare,"........X...............X..."},{HatClosed,"x.x.x.x.x.x.x.x.x.x.x.x.x.x."}}},
            {'B', {{Kick,"X...............X.......X..."},{Snare,"........X...............X..."},{Ride,"x.x.x.x.x.x.x.x.x.x.x.x.x.x."}}},
            {'C', {{Kick,"X...X...X...X...X...X...X..."},{Snare,"........X...............X..."},{HatClosed,"xxxxxxxxxxxxxxxxxxxxxxxxxxxx"}}},
            {'F', {{Kick,"X...............X..........."},{Snare,"......................x.X.x."},{TomLow,"..........................X."}}},
            {'G', {{Snare,"XxXx........................"},{TomHi,"....XxXx...................."},{TomMid,"........XxXx................"},{TomLow,"............XxXxXxXxXxXx...."}}},
        }},
    };
    return kGenres;
}

std::string composedRow(const Genre& g, const std::string& arr, uint8_t pitch) {
    const int S = stepsPerBar(g.tsNum, g.tsDen);
    std::string full;
    full.reserve(static_cast<size_t>(S) * arr.size());
    for (char l : arr) {
        const Bar& bar = g.bars.at(l);
        auto it = bar.find(pitch);
        full += (it != bar.end()) ? fitBar(it->second, S) : std::string(S, '.');
    }
    return full;
}

std::string composedUnion(const Genre& g, const std::string& arr,
                          const std::vector<uint8_t>& pitches) {
    std::string out;
    for (uint8_t p : pitches) {
        std::string r = composedRow(g, arr, p);
        if (out.empty()) out.assign(r.size(), '.');
        for (size_t i = 0; i < r.size() && i < out.size(); ++i)
            if (isHit(r[i]) && velFor(r[i]) > velFor(out[i])) out[i] = r[i];
    }
    return out;
}

std::vector<ArrEntry> validArrangements(const Genre& g) {
    std::vector<ArrEntry> out;
    const int S = stepsPerBar(g.tsNum, g.tsDen);
    int idx = 0;
    for (const std::string& arr : arrangements()) {
        bool ok = true;
        for (char l : arr)
            if (!g.bars.count(l)) { ok = false; break; }
        if (!ok) continue;

        // The SMF reader rounds loop length up to whole beats — skip
        // arrangements whose total isn't a whole number of beats for this
        // meter (keeps odd meters like 15/16 looping cleanly).
        const double totalBeats = S * static_cast<int>(arr.size()) * kStepBeats;
        const double frac = totalBeats - std::floor(totalBeats);
        if (frac > 1e-6 && frac < 1.0 - 1e-6) continue;

        ArrEntry e;
        e.arr   = arr;
        e.index = ++idx;
        e.bars  = static_cast<int>(arr.size());
        e.fill  = (arr.back() == 'F' || arr.back() == 'G' || arr.back() == 'H');
        out.push_back(std::move(e));
    }
    return out;
}

std::string descriptor(const ArrEntry& e) {
    return " (" + std::to_string(e.bars) + "-bar" + (e.fill ? " fill" : "") + ")";
}

} // namespace drumkit

namespace {

// Build the drum clip for an arrangement — every drum pitch's composed row,
// each hit a sixteenth on channel 9. Identical output to the original
// inline builder.
midi::MidiClip buildDrumClip(const drumkit::Genre& g, const std::string& arr) {
    using namespace drumkit;
    const int S     = stepsPerBar(g.tsNum, g.tsDen);
    const int nbars = static_cast<int>(arr.size());

    std::set<uint8_t> pitches;
    for (char l : arr)
        for (const auto& kv : g.bars.at(l)) pitches.insert(kv.first);

    midi::MidiClip clip;
    clip.setLoop(true);
    for (uint8_t pitch : pitches) {
        const std::string full = composedRow(g, arr, pitch);
        for (int step = 0; step < static_cast<int>(full.size()); ++step) {
            if (isHit(full[step])) {
                midi::MidiNote n;
                n.startBeat = step * kStepBeats;
                n.duration  = kStepBeats;
                n.pitch     = pitch;
                n.channel   = 9;
                n.velocity  = velFor(full[step]);
                clip.addNote(n);
            }
        }
    }

    const double totalBeats = S * nbars * kStepBeats;
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

int DrumPatterns::seedFactoryLoops() {
    fs::path dir = MidiLoopManager::midiLoopsRootDir() / "drums";
    std::error_code ec;
    fs::create_directories(dir, ec);

    int written = 0;
    for (const drumkit::Genre& g : drumkit::genres()) {
        for (const drumkit::ArrEntry& e : drumkit::validArrangements(g)) {
            char num[8];
            std::snprintf(num, sizeof num, "%02d", e.index);
            std::string name = std::string(g.name) + " " + num + drumkit::descriptor(e);

            std::string safe = MidiLoopManager::sanitizeName(name);
            fs::path file = dir / (safe + ".mid");
            if (fs::exists(file, ec)) continue;     // idempotent — respect deletions

            midi::MidiClip clip = buildDrumClip(g, e.arr);
            clip.setName(name);
            if (util::saveMidiFile(file, clip, static_cast<double>(g.bpm), g.tsNum, g.tsDen))
                ++written;
        }
    }
    return written;
}

} // namespace yawn
