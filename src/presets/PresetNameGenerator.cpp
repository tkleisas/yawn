#include "presets/PresetNameGenerator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <random>

namespace yawn {
namespace presets {

namespace {

// ── Small seeded RNG helpers ────────────────────────────────────────────────
struct Rng {
    std::mt19937_64 e;
    explicit Rng(uint64_t s) : e(s ? s : 0x9E3779B97F4A7C15ull) {}
    float    f01()                 { return std::uniform_real_distribution<float>(0.0f, 1.0f)(e); }
    int      range(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(e); }
    bool     chance(float p)       { return f01() < p; }
    template <class Arr> const typename Arr::value_type& pick(const Arr& a) {
        return a[range(0, static_cast<int>(a.size()) - 1)];
    }
    template <class T, size_t N> const T& pick(const T (&a)[N]) {
        return a[range(0, static_cast<int>(N) - 1)];
    }
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ── Archetype tables (single source of truth, shared with the sampler) ───────
const char* kInstrumentArch[] = {
    "Pad", "Pluck", "Bass", "Lead", "Stab", "Drone", "Keys", "Perc", "Texture", "Bell"
};
const char* kAudioArch[] = {
    "Subtle", "Lush", "Extreme", "Rhythmic", "Lofi", "Wide", "Warm", "Broken"
};
const char* kMidiArch[] = {
    "Tight", "Loose", "Wild", "Hypnotic"
};

// ── Descriptive word banks ───────────────────────────────────────────────────
// Per-instrument-archetype nouns.
const std::array<std::vector<const char*>, 10> kInstNouns = {{
    {"Pad","Haze","Veil","Drift","Cloud","Bloom","Aura","Mist","Vapor","Sheet"},   // Pad
    {"Pluck","Drop","Spark","Bit","Tick","Glint","Ping","Bloom"},                  // Pluck
    {"Bass","Sub","Rumble","Grime","Wob","Throb","Root","Floor"},                  // Bass
    {"Lead","Cry","Horn","Blade","Ray","Line","Voice","Saw"},                      // Lead
    {"Stab","Hit","Jab","Chord","Punch","Slice"},                                  // Stab
    {"Drone","Choir","Swell","Wash","Field","Hum","Mass","Ohm"},                   // Drone
    {"Keys","Rhodes","Wurli","Piano","Clav","Tine"},                               // Keys
    {"Hit","Knock","Clack","Snap","Tom","Rim","Block","Bonk"},                     // Perc
    {"Texture","Grain","Dust","Static","Field","Weave","Fog","Murmur"},            // Texture
    {"Bell","Chime","Glass","Mallet","Tube","Crystal","Toll"},                     // Bell
}};

const char* kAudioNouns[] = {
    "Space","Air","Smear","Glue","Drive","Wash","Bend","Edge","Tilt","Haze","Grit","Sheen"
};
const char* kMidiNouns[] = {
    "Motion","Pattern","Flow","Pulse","Trip","Weave","Cycle","Drift","Run"
};

const char* kAdjDark[]   = {"Dark","Murky","Deep","Dim","Shadow","Night","Black","Muted","Sub"};
const char* kAdjBright[] = {"Bright","Glass","Crystal","Neon","Solar","Sharp","Lumen","Chrome"};
const char* kAdjMid[]    = {"Warm","Soft","Round","Velvet","Hazy","Pastel","Amber"};
const char* kAdjReso[]   = {"Acid","Liquid","Squelch","Vocal","Resin","Rubber"};
const char* kAdjLong[]   = {"Eternal","Endless","Slow","Distant","Vast","Cosmic","Glacial"};
const char* kAdjShort[]  = {"Tight","Quick","Blip","Micro","Snap","Crisp"};
const char* kAdjMotion[] = {"Wobble","Drifting","Swirling","Pulsing","Shifting","Restless"};
const char* kAdjDirty[]  = {"Dirty","Broken","Burnt","Crushed","Mangled","Rust","Toxic"};
const char* kAdjFlavor[] = {"Astral","Hollow","Frozen","Lunar","Ghost","Plasma","Cobalt",
                            "Violet","Velour","Ozone","Mercury","Obsidian"};

// ── Alien (IDM) grammar ──────────────────────────────────────────────────────
const char* kOnset[] = {
    "b","c","d","f","g","h","j","k","l","m","n","p","qu","r","s","t","v","w","x","z",
    "bl","br","cr","dr","fl","gl","gr","kl","kr","pl","pr","sk","sl","sp","st","str",
    "tr","vl","vr","ph","th","sh","ch","gw","tz","xy","zh","dh"
};
const char* kNucleus[] = {
    "a","e","i","o","u","y","ae","eu","oo","ou","ia","yo"
};
const char* kCoda[] = {
    "","","b","d","g","k","l","m","n","p","r","s","t","x","z",
    "sk","st","nt","rn","ng","ch","th","tz","dr","bn","gz","ff","ss","zz","ph"
};

} // namespace

int archetypeCount(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Instrument:  return static_cast<int>(std::size(kInstrumentArch));
        case DeviceKind::AudioEffect: return static_cast<int>(std::size(kAudioArch));
        case DeviceKind::MidiEffect:  return static_cast<int>(std::size(kMidiArch));
    }
    return 1;
}

const char* archetypeLabel(DeviceKind kind, int a) {
    int n = archetypeCount(kind);
    if (a < 0 || a >= n) a = 0;
    switch (kind) {
        case DeviceKind::Instrument:  return kInstrumentArch[a];
        case DeviceKind::AudioEffect: return kAudioArch[a];
        case DeviceKind::MidiEffect:  return kMidiArch[a];
    }
    return "";
}

std::string PresetNameGenerator::alienName(uint64_t seed) {
    Rng r(seed ^ 0xA5A5F00DD00Dull);
    int syllables = r.range(1, 3);
    // Bias toward 2 syllables (most Aphex-ish names sit there).
    if (syllables == 1 && r.chance(0.5f)) syllables = 2;

    std::string out;
    for (int i = 0; i < syllables; ++i) {
        out += r.pick(kOnset);
        out += r.pick(kNucleus);
        // Coda more likely on the final syllable (gives the clipped end).
        if (i == syllables - 1 || r.chance(0.45f))
            out += r.pick(kCoda);
    }
    // Trim a stray trailing space some nuclei could introduce.
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) out = "xtl";

    // Capitalisation: mostly Title-case, sometimes all lowercase, and
    // occasionally a doubled leading consonant ("Bbreflection").
    float capRoll = r.f01();
    if (capRoll < 0.18f) {
        out = lower(out);                                  // all-lowercase
    } else {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
        if (r.chance(0.12f)) {  // doubled leading consonant
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
            if (!strchr("aeiou", c)) out.insert(out.begin() + 1, c);
        }
    }

    // Optional numeric / version suffix.
    float sfx = r.f01();
    if (sfx < 0.10f)      { char b[8]; std::snprintf(b, sizeof(b), " %d", r.range(2, 99)); out += b; }
    else if (sfx < 0.16f) { char b[8]; std::snprintf(b, sizeof(b), "v%d", r.range(2, 9));  out += b; }
    else if (sfx < 0.20f) { char b[8]; std::snprintf(b, sizeof(b), "%d.%d", r.range(1,4), r.range(1,9)); out += b; }
    return out;
}

std::string PresetNameGenerator::descriptiveName(uint64_t seed, const NameContext& ctx) {
    Rng r(seed ^ 0xC0FFEEull);

    // Choose an adjective biased by the salient descriptors.
    std::vector<const char*> adjPool;
    auto add = [&](const char* const* arr, size_t n) {
        for (size_t i = 0; i < n; ++i) adjPool.push_back(arr[i]);
    };
    if (ctx.brightness < 0.34f)      add(kAdjDark,   std::size(kAdjDark));
    else if (ctx.brightness > 0.66f) add(kAdjBright, std::size(kAdjBright));
    else                             add(kAdjMid,    std::size(kAdjMid));
    if (ctx.resonance > 0.6f)        add(kAdjReso,   std::size(kAdjReso));
    if (ctx.length    > 0.66f)       add(kAdjLong,   std::size(kAdjLong));
    else if (ctx.length < 0.25f)     add(kAdjShort,  std::size(kAdjShort));
    if (ctx.motion    > 0.55f)       add(kAdjMotion, std::size(kAdjMotion));
    if (ctx.distorted)               add(kAdjDirty,  std::size(kAdjDirty));
    add(kAdjFlavor, std::size(kAdjFlavor));  // always some flavour options

    const char* adj = adjPool[r.range(0, static_cast<int>(adjPool.size()) - 1)];

    // Noun from the archetype's bank (instruments) or a kind-generic bank.
    std::string noun;
    if (ctx.kind == DeviceKind::Instrument) {
        int a = ctx.archetype;
        if (a < 0 || a >= static_cast<int>(kInstNouns.size())) a = 0;
        const auto& bank = kInstNouns[a];
        noun = bank[r.range(0, static_cast<int>(bank.size()) - 1)];
    } else if (ctx.kind == DeviceKind::AudioEffect) {
        noun = r.pick(kAudioNouns);
    } else {
        noun = r.pick(kMidiNouns);
    }

    // Compose. Usually "Adj Noun"; sometimes prepend the archetype label
    // for effects/MIDI so the function reads, sometimes a bare noun.
    std::string name;
    float form = r.f01();
    if (form < 0.70f) {
        name = std::string(adj) + " " + noun;
    } else if (form < 0.88f && ctx.kind != DeviceKind::Instrument) {
        name = std::string(archetypeLabel(ctx.kind, ctx.archetype)) + " " + noun;
    } else {
        name = noun;
    }
    return name;
}

std::string PresetNameGenerator::generate(uint64_t seed, const NameContext& ctx,
                                          const std::vector<std::string>& used) const {
    auto taken = [&](const std::string& n) {
        std::string ln = lower(n);
        for (const auto& u : used) if (lower(u) == ln) return true;
        return false;
    };

    // Up to a few attempts to dodge collisions, then disambiguate with a
    // numeric suffix.
    for (int attempt = 0; attempt < 6; ++attempt) {
        uint64_t s = seed + static_cast<uint64_t>(attempt) * 0x100000001B3ull;
        Rng pickr(s ^ 0xBADC0DEull);
        std::string name;
        if (pickr.chance(m_alienRatio)) {
            name = alienName(s);
        } else if (pickr.chance(0.18f)) {
            // Hybrid: descriptive adjective-ish prefix + alien token, or
            // alien token + archetype noun — gives "Acid Vordh" flavour.
            std::string a = alienName(s ^ 0x55ull);
            std::string d = descriptiveName(s, ctx);
            // take the first word of the descriptive name as the modifier
            std::string mod = d.substr(0, d.find(' '));
            name = pickr.chance(0.5f) ? (mod + " " + a) : (a + " " + d.substr(d.find(' ') + 1));
        } else {
            name = descriptiveName(s, ctx);
        }
        if (!name.empty() && !taken(name)) return name;
    }
    // Fallback: append an index.
    std::string base = descriptiveName(seed, ctx);
    for (int i = 2; i < 9999; ++i) {
        std::string n = base + " " + std::to_string(i);
        if (!taken(n)) return n;
    }
    return base;
}

} // namespace presets
} // namespace yawn
