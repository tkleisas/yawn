#include "presets/PresetGenerator.h"
#include "presets/PresetNameGenerator.h"
#include "presets/PresetManager.h"
#include "presets/DevicePresetHelpers.h"
#include "util/Factory.h"
#include "instruments/Instrument.h"
#include "instruments/InstrumentRack.h"
#include "effects/AudioEffect.h"
#include "midi/MidiEffect.h"
#include "midi/MidiTypes.h"
#include "WidgetHint.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace yawn {
namespace presets {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// ── RNG ──────────────────────────────────────────────────────────────────────
struct Rng {
    std::mt19937_64 e;
    explicit Rng(uint64_t s) : e(s ? s : 0x9E3779B97F4A7C15ull) {}
    float f01()  { return std::uniform_real_distribution<float>(0.0f, 1.0f)(e); }
    bool  chance(float p) { return f01() < p; }
    int   range(int lo, int hi) { return lo <= hi ? std::uniform_int_distribution<int>(lo, hi)(e) : lo; }
    // Triangular sample in [lo,hi] (biased to the middle).
    float tri(float lo, float hi) { return lo + (hi - lo) * 0.5f * (f01() + f01()); }
};

uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33; return x;
}
uint64_t seedFor(uint64_t master, const std::string& id, int idx) {
    uint64_t h = master ? master : 0x123456789ABCDEFull;
    for (char c : id) h = mix64(h ^ static_cast<uint64_t>(static_cast<unsigned char>(c)));
    return mix64(h ^ (static_cast<uint64_t>(idx) * 0x9E3779B97F4A7C15ull));
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// ── Parameter role inference (by name) ───────────────────────────────────────
enum class Role {
    AmpAtk, AmpDec, AmpSus, AmpRel,
    FiltAtk, FiltDec, FiltSus, FiltRel,
    Cutoff, Reso, FiltEnv,
    Level, Volume, Mix, Feedback, Drive,
    Rate, Depth, Time, Tone, Pan, Pitch, Phase, Generic
};

Role classifyRole(const std::string& rawName) {
    const std::string n = lower(rawName);
    const bool filt = has(n, "filt");  // "filter" / "filt"
    if (has(n, "attack"))  return filt ? Role::FiltAtk : Role::AmpAtk;
    if (has(n, "decay"))   return filt ? Role::FiltDec : Role::AmpDec;
    if (has(n, "sustain")) return filt ? Role::FiltSus : Role::AmpSus;
    if (has(n, "release")) return filt ? Role::FiltRel : Role::AmpRel;
    if (has(n, "env"))     return Role::FiltEnv;                 // "Filter Env", "Env Amount"
    if (has(n, "cutoff") || (filt && has(n, "freq"))) return Role::Cutoff;
    if (has(n, "reso"))    return Role::Reso;
    if (has(n, "feedback")) return Role::Feedback;
    if (has(n, "wet") || has(n, "dry") || has(n, "mix") || n == "amount" || has(n, "blend"))
        return Role::Mix;
    if (has(n, "drive") || has(n, "sat") || has(n, "dist") || has(n, "crush"))
        return Role::Drive;
    if (has(n, "rate") || has(n, "speed")) return Role::Rate;
    if (has(n, "depth")) return Role::Depth;
    if (has(n, "time") || has(n, "delay")) return Role::Time;
    if (has(n, "tone")) return Role::Tone;
    if (has(n, "pan") || has(n, "width") || has(n, "stereo") || has(n, "spread"))
        return Role::Pan;
    if (has(n, "volume") || has(n, "master") || has(n, "makeup") || n == "output" || n == "gain")
        return Role::Volume;
    if (has(n, "level")) return Role::Level;
    if (has(n, "tune") || has(n, "detune") || has(n, "pitch") || has(n, "octave") ||
        has(n, "root") || has(n, "transpose"))
        return Role::Pitch;
    if (has(n, "phase")) return Role::Phase;
    return Role::Generic;
}

// ── Archetype-biased normalized windows ──────────────────────────────────────
struct Win { float lo, hi; };

// Default windows when an archetype doesn't pin a role.
Win defaultWindow(Role r) {
    switch (r) {
        case Role::Volume:   return {0.55f, 0.85f};
        case Role::Level:    return {0.45f, 0.85f};
        case Role::Mix:      return {0.10f, 0.50f};
        case Role::Feedback: return {0.00f, 0.50f};
        case Role::Drive:    return {0.05f, 0.45f};
        case Role::Cutoff:   return {0.40f, 0.90f};
        case Role::Reso:     return {0.00f, 0.40f};
        case Role::FiltEnv:  return {0.10f, 0.50f};
        case Role::Rate:     return {0.05f, 0.50f};
        case Role::Depth:    return {0.10f, 0.60f};
        case Role::Time:     return {0.10f, 0.60f};
        case Role::Tone:     return {0.30f, 0.70f};
        case Role::AmpAtk:   return {0.00f, 0.20f};
        case Role::AmpDec:   return {0.20f, 0.60f};
        case Role::AmpSus:   return {0.40f, 0.90f};
        case Role::AmpRel:   return {0.15f, 0.55f};
        case Role::FiltAtk:  return {0.00f, 0.25f};
        case Role::FiltDec:  return {0.20f, 0.60f};
        case Role::FiltSus:  return {0.30f, 0.80f};
        case Role::FiltRel:  return {0.15f, 0.55f};
        default:             return {0.0f, 1.0f};
    }
}

// Instrument archetype windows. Index matches kInstrumentArch in the
// name generator: 0 Pad,1 Pluck,2 Bass,3 Lead,4 Stab,5 Drone,6 Keys,
// 7 Perc,8 Texture,9 Bell.
bool instWindow(int a, Role r, Win& w) {
    auto set = [&](float lo, float hi) { w = {lo, hi}; return true; };
    switch (a) {
        case 0: // Pad
            switch (r) { case Role::AmpAtk: return set(0.25f,0.7f); case Role::AmpDec: return set(0.3f,0.7f);
                case Role::AmpSus: return set(0.7f,1.0f); case Role::AmpRel: return set(0.4f,0.9f);
                case Role::Cutoff: return set(0.4f,0.8f); case Role::Reso: return set(0.0f,0.3f);
                case Role::FiltEnv: return set(0.1f,0.4f); case Role::Rate: return set(0.0f,0.25f);
                case Role::Depth: return set(0.1f,0.5f); case Role::Mix: return set(0.2f,0.6f); default: return false; }
        case 1: // Pluck
            switch (r) { case Role::AmpAtk: return set(0.0f,0.05f); case Role::AmpDec: return set(0.1f,0.4f);
                case Role::AmpSus: return set(0.0f,0.25f); case Role::AmpRel: return set(0.05f,0.3f);
                case Role::Cutoff: return set(0.5f,0.95f); case Role::Reso: return set(0.1f,0.5f);
                case Role::FiltEnv: return set(0.3f,0.7f); case Role::Mix: return set(0.05f,0.35f); default: return false; }
        case 2: // Bass
            switch (r) { case Role::AmpAtk: return set(0.0f,0.08f); case Role::AmpDec: return set(0.2f,0.5f);
                case Role::AmpSus: return set(0.3f,0.7f); case Role::AmpRel: return set(0.05f,0.3f);
                case Role::Cutoff: return set(0.2f,0.5f); case Role::Reso: return set(0.1f,0.5f);
                case Role::FiltEnv: return set(0.2f,0.6f); case Role::Mix: return set(0.0f,0.25f); default: return false; }
        case 3: // Lead
            switch (r) { case Role::AmpAtk: return set(0.0f,0.15f); case Role::AmpDec: return set(0.2f,0.5f);
                case Role::AmpSus: return set(0.5f,0.9f); case Role::AmpRel: return set(0.1f,0.4f);
                case Role::Cutoff: return set(0.5f,0.9f); case Role::Reso: return set(0.1f,0.6f);
                case Role::Rate: return set(0.1f,0.5f); case Role::Depth: return set(0.1f,0.6f);
                case Role::Mix: return set(0.1f,0.4f); default: return false; }
        case 4: // Stab
            switch (r) { case Role::AmpAtk: return set(0.0f,0.04f); case Role::AmpDec: return set(0.08f,0.25f);
                case Role::AmpSus: return set(0.0f,0.2f); case Role::AmpRel: return set(0.05f,0.2f);
                case Role::Cutoff: return set(0.4f,0.85f); case Role::Reso: return set(0.2f,0.6f);
                case Role::Mix: return set(0.1f,0.4f); default: return false; }
        case 5: // Drone
            switch (r) { case Role::AmpAtk: return set(0.4f,1.0f); case Role::AmpDec: return set(0.3f,0.8f);
                case Role::AmpSus: return set(0.8f,1.0f); case Role::AmpRel: return set(0.6f,1.0f);
                case Role::Cutoff: return set(0.3f,0.7f); case Role::Reso: return set(0.0f,0.4f);
                case Role::Rate: return set(0.0f,0.2f); case Role::Depth: return set(0.2f,0.7f);
                case Role::Mix: return set(0.3f,0.7f); default: return false; }
        case 6: // Keys
            switch (r) { case Role::AmpAtk: return set(0.0f,0.06f); case Role::AmpDec: return set(0.3f,0.6f);
                case Role::AmpSus: return set(0.3f,0.6f); case Role::AmpRel: return set(0.2f,0.5f);
                case Role::Cutoff: return set(0.5f,0.9f); case Role::Reso: return set(0.0f,0.3f);
                case Role::Mix: return set(0.05f,0.35f); default: return false; }
        case 7: // Perc
            switch (r) { case Role::AmpAtk: return set(0.0f,0.02f); case Role::AmpDec: return set(0.05f,0.2f);
                case Role::AmpSus: return set(0.0f,0.1f); case Role::AmpRel: return set(0.02f,0.15f);
                case Role::Cutoff: return set(0.4f,1.0f); case Role::Reso: return set(0.1f,0.6f); default: return false; }
        case 8: // Texture
            switch (r) { case Role::AmpAtk: return set(0.1f,0.7f); case Role::AmpDec: return set(0.2f,0.8f);
                case Role::AmpSus: return set(0.3f,0.9f); case Role::AmpRel: return set(0.3f,0.9f);
                case Role::Cutoff: return set(0.2f,0.9f); case Role::Reso: return set(0.1f,0.7f);
                case Role::Rate: return set(0.0f,0.6f); case Role::Depth: return set(0.2f,0.9f);
                case Role::Mix: return set(0.3f,0.8f); default: return false; }
        case 9: // Bell
            switch (r) { case Role::AmpAtk: return set(0.0f,0.03f); case Role::AmpDec: return set(0.4f,0.9f);
                case Role::AmpSus: return set(0.0f,0.2f); case Role::AmpRel: return set(0.3f,0.8f);
                case Role::Cutoff: return set(0.6f,1.0f); case Role::Reso: return set(0.0f,0.3f); default: return false; }
        default: return false;
    }
}

// Audio-effect archetype windows. Index matches kAudioArch: 0 Subtle,
// 1 Lush,2 Extreme,3 Rhythmic,4 Lofi,5 Wide,6 Warm,7 Broken.
bool audioWindow(int a, Role r, Win& w) {
    auto set = [&](float lo, float hi) { w = {lo, hi}; return true; };
    switch (a) {
        case 0: switch (r) { case Role::Mix: return set(0.1f,0.35f); case Role::Feedback: return set(0.0f,0.3f);
            case Role::Drive: return set(0.0f,0.3f); case Role::Depth: return set(0.05f,0.3f);
            case Role::Rate: return set(0.05f,0.4f); default: return false; }
        case 1: switch (r) { case Role::Mix: return set(0.35f,0.7f); case Role::Feedback: return set(0.2f,0.6f);
            case Role::Depth: return set(0.3f,0.7f); case Role::Rate: return set(0.05f,0.4f);
            case Role::Time: return set(0.2f,0.6f); default: return false; }
        case 2: switch (r) { case Role::Mix: return set(0.5f,1.0f); case Role::Feedback: return set(0.5f,0.85f);
            case Role::Drive: return set(0.5f,1.0f); case Role::Depth: return set(0.5f,1.0f);
            case Role::Reso: return set(0.4f,0.85f); default: return false; }
        case 3: switch (r) { case Role::Time: return set(0.2f,0.7f); case Role::Feedback: return set(0.3f,0.7f);
            case Role::Mix: return set(0.3f,0.7f); case Role::Rate: return set(0.3f,0.8f); default: return false; }
        case 4: switch (r) { case Role::Drive: return set(0.3f,0.8f); case Role::Mix: return set(0.4f,0.9f);
            case Role::Cutoff: return set(0.2f,0.6f); default: return false; }
        case 5: switch (r) { case Role::Depth: return set(0.3f,0.8f); case Role::Mix: return set(0.3f,0.7f);
            case Role::Pan: return set(0.6f,1.0f); default: return false; }
        case 6: switch (r) { case Role::Drive: return set(0.1f,0.4f); case Role::Mix: return set(0.2f,0.6f);
            case Role::Tone: return set(0.3f,0.6f); default: return false; }
        case 7: switch (r) { case Role::Mix: return set(0.5f,1.0f); case Role::Feedback: return set(0.4f,0.8f);
            case Role::Drive: return set(0.4f,0.9f); case Role::Reso: return set(0.4f,0.85f); default: return false; }
        default: return false;
    }
}

Win windowFor(DeviceKind kind, int arch, Role r) {
    Win w;
    if (kind == DeviceKind::Instrument && instWindow(arch, r, w)) return w;
    if (kind == DeviceKind::AudioEffect && audioWindow(arch, r, w)) return w;
    return defaultWindow(r);
}

// ── Generic parameter accessor (shared randomizer over all 3 kinds) ──────────
struct PA {
    int count = 0;
    std::function<const ParameterInfo&(int)> info;
    std::function<float(int)> get;
    std::function<void(int, float)> set;
};

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Sample a single parameter's value respecting its type + archetype bias.
float sampleParam(Rng& r, const ParameterInfo& info, DeviceKind kind, int arch, Role role) {
    const float mn = info.minValue, mx = info.maxValue;
    if (mx <= mn) return info.defaultValue;

    if (info.isBoolean) return r.chance(0.5f) ? mx : mn;

    if (info.valueLabels != nullptr && info.valueLabelCount > 0) {
        int idx = r.range(0, info.valueLabelCount - 1);
        float v = static_cast<float>(idx);
        return v < mn ? mn : (v > mx ? mx : v);
    }
    if (info.widgetHint == WidgetHint::StepSelector) {
        int lo = static_cast<int>(std::lround(mn));
        int hi = static_cast<int>(std::lround(mx));
        if (hi < lo) std::swap(lo, hi);
        return static_cast<float>(r.range(lo, hi));
    }

    // Continuous. Roles that should hug their default get a window
    // centred on the default; everything else uses the archetype window.
    Win w;
    if (role == Role::Pitch || role == Role::Generic || role == Role::Tone ||
        (role == Role::Pan && !(kind == DeviceKind::AudioEffect))) {
        float dnorm = clamp01((info.defaultValue - mn) / (mx - mn));
        float spread = (role == Role::Generic) ? 0.25f : 0.18f;
        w = {clamp01(dnorm - spread), clamp01(dnorm + spread)};
    } else {
        w = windowFor(kind, arch, role);
    }

    // A little wildness: occasionally widen the window for character.
    if (r.chance(0.10f) && role != Role::Volume && role != Role::Feedback) {
        w.lo = clamp01(w.lo - 0.2f);
        w.hi = clamp01(w.hi + 0.2f);
    }
    // Keep feedback from running away regardless of archetype.
    if (role == Role::Feedback) w.hi = std::min(w.hi, 0.85f);

    float t = r.tri(w.lo, w.hi);
    return mn + t * (mx - mn);
}

// Core randomizer + descriptor extraction, shared by all device kinds.
int randomizeCore(PA& pa, uint64_t seed, DeviceKind kind, const GenOptions& opt,
                  NameContext& ctx) {
    Rng r(seed ? seed : 1);
    const int archCount = std::max(1, archetypeCount(kind));
    const int arch = r.range(0, archCount - 1);
    ctx = NameContext{};
    ctx.kind = kind;
    ctx.archetype = arch;

    float maxEnv = 0.0f, motion = 0.0f;
    for (int i = 0; i < pa.count; ++i) {
        const ParameterInfo& info = pa.info(i);
        if (info.isPerVoice) continue;
        Role role = classifyRole(info.name ? info.name : "");
        float v = sampleParam(r, info, kind, arch, role);
        pa.set(i, v);

        const float span = info.maxValue - info.minValue;
        const float norm = span > 0.0f ? clamp01((v - info.minValue) / span) : 0.0f;
        switch (role) {
            case Role::Cutoff:  ctx.brightness = norm; break;
            case Role::Reso:    ctx.resonance  = norm; break;
            case Role::AmpRel:  maxEnv = std::max(maxEnv, norm); break;
            case Role::AmpDec:  maxEnv = std::max(maxEnv, norm * 0.7f); break;
            case Role::Depth:   motion = std::max(motion, norm); break;
            case Role::Rate:    motion = std::max(motion, norm * 0.6f); break;
            case Role::Drive:   if (norm > 0.5f) ctx.distorted = true; break;
            case Role::Pan:     if (norm > 0.7f) ctx.wide = true; break;
            default: break;
        }
    }
    ctx.length = maxEnv;
    ctx.motion = motion;
    if (kind == DeviceKind::AudioEffect && (arch == 2 || arch == 4 || arch == 7))
        ctx.distorted = true;  // Extreme / Lofi / Broken
    return arch;
}

PA makePA(instruments::Instrument& d) {
    return {d.parameterCount(),
            [&](int i) -> const ParameterInfo& { return d.parameterInfo(i); },
            [&](int i) { return d.getParameter(i); },
            [&](int i, float v) { d.setParameter(i, v); }};
}
PA makePA(effects::AudioEffect& d) {
    return {d.parameterCount(),
            [&](int i) -> const ParameterInfo& { return d.parameterInfo(i); },
            [&](int i) { return d.getParameter(i); },
            [&](int i, float v) { d.setParameter(i, v); }};
}
PA makePA(midi::MidiEffect& d) {
    return {d.parameterCount(),
            [&](int i) -> const ParameterInfo& { return d.parameterInfo(i); },
            [&](int i) { return d.getParameter(i); },
            [&](int i, float v) { d.setParameter(i, v); }};
}

// ── Offline validation helpers ───────────────────────────────────────────────
bool finiteAndBounded(const float* buf, int n, float bound, float& peakOut) {
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        float s = buf[i];
        if (!std::isfinite(s)) return false;
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    peakOut = peak;
    return peak <= bound;
}

const char* kCatalogIds[] = {
    // Instruments (self-contained, no sample/asset dependency)
    "subsynth","fmsynth","wavetable","karplus","drumsynth",
    "stringmachine","drawbarorgan","electricpiano","instrack",
    // Audio effects (exclude visualizers + IR/model-backed)
    "reverb","delay","eq","compressor","limiter","filter","chorus","phaser",
    "wah","rotary","distortion","tape","amp","bitcrusher","noisegate",
    "pingpongdelay","envfollower","splineeq",
    // MIDI effects
    "arp","chord","scale","notelength","velocity","random","pitch","lfo",
};

} // namespace

// ── Public low-level randomizers ─────────────────────────────────────────────
int randomizeInstrument(instruments::Instrument& inst, uint64_t seed,
                        const GenOptions& opt, NameContext& outCtx) {
    PA pa = makePA(inst);
    return randomizeCore(pa, seed, DeviceKind::Instrument, opt, outCtx);
}
int randomizeAudioEffect(effects::AudioEffect& fx, uint64_t seed,
                         const GenOptions& opt, NameContext& outCtx) {
    PA pa = makePA(fx);
    return randomizeCore(pa, seed, DeviceKind::AudioEffect, opt, outCtx);
}
int randomizeMidiEffect(midi::MidiEffect& fx, uint64_t seed,
                        const GenOptions& opt, NameContext& outCtx) {
    PA pa = makePA(fx);
    return randomizeCore(pa, seed, DeviceKind::MidiEffect, opt, outCtx);
}

// ── Public validators ────────────────────────────────────────────────────────
bool validateInstrument(instruments::Instrument& inst, double sampleRate) {
    const int blockSize = 256;
    const int channels = 2;
    inst.reset();
    std::vector<float> block(static_cast<size_t>(blockSize) * channels, 0.0f);

    midi::MidiBuffer noteOnBuf, empty;
    noteOnBuf.addMessage(midi::MidiMessage::noteOn(0, 57, 100, 0)); // A3

    const int totalBlocks = static_cast<int>(std::ceil(sampleRate * 0.7 / blockSize));
    const int releaseBlock = totalBlocks / 2;
    double energy = 0.0; float globalPeak = 0.0f; long samples = 0;
    bool released = false;

    for (int b = 0; b < totalBlocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0f);
        const midi::MidiBuffer* mb = &empty;
        if (b == 0) mb = &noteOnBuf;
        midi::MidiBuffer relBuf;
        if (b == releaseBlock && !released) {
            relBuf.addMessage(midi::MidiMessage::noteOff(0, 57, 0, 0));
            mb = &relBuf; released = true;
        }
        inst.process(block.data(), blockSize, channels, *mb);
        float peak;
        if (!finiteAndBounded(block.data(), static_cast<int>(block.size()), 8.0f, peak))
            return false;
        globalPeak = std::max(globalPeak, peak);
        // Accumulate energy after the first few ms (skip the very onset).
        if (b > 0) {
            for (float s : block) { energy += static_cast<double>(s) * s; ++samples; }
        }
    }
    if (samples == 0) return false;
    double rms = std::sqrt(energy / static_cast<double>(samples));
    // Audible: some sustained energy AND a non-trivial peak.
    return rms > 1.0e-4 && globalPeak > 1.0e-3;
}

bool validateAudioEffect(effects::AudioEffect& fx, double sampleRate) {
    const int blockSize = 256;
    const int channels = 2;
    fx.reset();
    std::vector<float> block(static_cast<size_t>(blockSize) * channels, 0.0f);

    const int totalBlocks = static_cast<int>(std::ceil(sampleRate * 0.5 / blockSize));
    double phase = 0.0;
    const double inc = 2.0 * 3.14159265358979 * 220.0 / sampleRate;
    Rng nz(0xFEEDFACEull);
    float firstRms = -1.0f, lastRms = 0.0f;

    for (int b = 0; b < totalBlocks; ++b) {
        // Test signal: 220 Hz sine + light noise, both channels.
        for (int f = 0; f < blockSize; ++f) {
            float s = 0.3f * static_cast<float>(std::sin(phase)) + 0.02f * (nz.f01() * 2.0f - 1.0f);
            phase += inc; if (phase > 6.283185307) phase -= 6.283185307;
            block[f * channels + 0] = s;
            block[f * channels + 1] = s;
        }
        fx.process(block.data(), blockSize, channels);
        float peak;
        if (!finiteAndBounded(block.data(), static_cast<int>(block.size()), 16.0f, peak))
            return false;
        double e = 0.0;
        for (float s : block) e += static_cast<double>(s) * s;
        float rms = static_cast<float>(std::sqrt(e / block.size()));
        if (firstRms < 0.0f) firstRms = rms;
        lastRms = rms;
    }
    // Reject obvious instability (energy ballooning out of control).
    if (firstRms > 1.0e-6f && lastRms > firstRms * 50.0f) return false;
    return true;
}

bool validateMidiEffect(midi::MidiEffect& fx, double sampleRate) {
    fx.init(sampleRate);
    fx.reset();
    midi::TransportInfo tr;
    tr.bpm = 120.0; tr.sampleRate = sampleRate; tr.playing = true;
    tr.samplesPerBeat = sampleRate * 60.0 / tr.bpm;
    const int blockSize = 256;
    midi::MidiBuffer buf;
    buf.addMessage(midi::MidiMessage::noteOn(0, 60, 100, 0));
    buf.addMessage(midi::MidiMessage::noteOn(0, 64, 90, 8));
    buf.addMessage(midi::MidiMessage::noteOn(0, 67, 80, 16));
    for (int b = 0; b < 16; ++b) {
        tr.positionInBeats = b * 0.25;
        tr.positionInSamples = static_cast<int64_t>(b * blockSize);
        fx.process(buf, blockSize, tr);
        buf.clear();  // sustained note handled internally; just don't re-trigger
        if (b == 8) buf.addMessage(midi::MidiMessage::noteOff(0, 60, 0, 0));
    }
    return true;  // MIDI effects can't go "silent/unstable" in an audio sense
}

// ── PresetGenerator ──────────────────────────────────────────────────────────
PresetGenerator::PresetGenerator(GenOptions opts) : m_opts(opts) {
    if (m_opts.masterSeed == 0) {
        std::random_device rd;
        m_opts.masterSeed = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ 0xA11CE5ull;
    }
}

bool PresetGenerator::isSupported(const std::string& deviceId) {
    for (const char* id : kCatalogIds) if (deviceId == id) return true;
    return false;
}

DeviceKind PresetGenerator::kindOf(const std::string& deviceId) {
    if (createInstrument(deviceId))  return DeviceKind::Instrument;
    if (createAudioEffect(deviceId)) return DeviceKind::AudioEffect;
    return DeviceKind::MidiEffect;
}

std::vector<GenSpec> PresetGenerator::defaultCatalog() {
    struct Row { const char* id; DeviceKind k; int n; };
    static const Row rows[] = {
        {"subsynth",     DeviceKind::Instrument, 40},
        {"fmsynth",      DeviceKind::Instrument, 40},
        {"wavetable",    DeviceKind::Instrument, 36},
        {"karplus",      DeviceKind::Instrument, 28},
        {"drumsynth",    DeviceKind::Instrument, 30},
        {"stringmachine",DeviceKind::Instrument, 28},
        {"drawbarorgan", DeviceKind::Instrument, 24},
        {"electricpiano",DeviceKind::Instrument, 28},
        {"instrack",     DeviceKind::Instrument, 50},  // composite layers/splits
        {"reverb",       DeviceKind::AudioEffect, 16},
        {"delay",        DeviceKind::AudioEffect, 14},
        {"eq",           DeviceKind::AudioEffect, 12},
        {"compressor",   DeviceKind::AudioEffect, 12},
        {"limiter",      DeviceKind::AudioEffect, 10},
        {"filter",       DeviceKind::AudioEffect, 12},
        {"chorus",       DeviceKind::AudioEffect, 14},
        {"phaser",       DeviceKind::AudioEffect, 16},
        {"wah",          DeviceKind::AudioEffect, 12},
        {"rotary",       DeviceKind::AudioEffect, 12},
        {"distortion",   DeviceKind::AudioEffect, 16},
        {"tape",         DeviceKind::AudioEffect, 12},
        {"amp",          DeviceKind::AudioEffect, 12},
        {"bitcrusher",   DeviceKind::AudioEffect, 14},
        {"noisegate",    DeviceKind::AudioEffect, 10},
        {"pingpongdelay",DeviceKind::AudioEffect, 14},
        {"envfollower",  DeviceKind::AudioEffect, 10},
        {"splineeq",     DeviceKind::AudioEffect, 14},
        {"arp",          DeviceKind::MidiEffect, 10},
        {"chord",        DeviceKind::MidiEffect, 10},
        {"scale",        DeviceKind::MidiEffect, 10},
        {"notelength",   DeviceKind::MidiEffect, 10},
        {"velocity",     DeviceKind::MidiEffect, 10},
        {"random",       DeviceKind::MidiEffect, 10},
        {"pitch",        DeviceKind::MidiEffect, 10},
        {"lfo",          DeviceKind::MidiEffect, 10},
    };
    std::vector<GenSpec> out;
    for (const auto& r : rows) out.push_back({r.id, r.k, r.n});
    return out;
}

GeneratedPreset PresetGenerator::generateOneAndSave(const std::string& deviceId,
                                                    uint64_t subSeed,
                                                    const std::vector<std::string>& usedNames) {
    if (deviceId == "instrack")
        return generateRackAndSave(subSeed, usedNames);

    GeneratedPreset gp;
    gp.deviceId = deviceId;
    gp.kind = kindOf(deviceId);
    gp.seed = subSeed;

    PresetNameGenerator namer(m_opts.alienNameRatio);
    NameContext ctx;
    const int blockSize = 512;
    const int maxAttempts = std::max(1, m_opts.maxRerollAttempts);

    if (gp.kind == DeviceKind::Instrument) {
        std::unique_ptr<instruments::Instrument> dev;
        uint64_t s = subSeed; int attempt = 0; bool ok = false;
        for (; attempt < maxAttempts; ++attempt) {
            dev = createInstrument(deviceId);
            if (!dev) return gp;
            dev->init(m_opts.sampleRate, blockSize);
            randomizeInstrument(*dev, s, m_opts, ctx);
            ok = !m_opts.validate || validateInstrument(*dev, m_opts.sampleRate);
            if (ok) break;
            s = mix64(s + 0x1000193ull);
        }
        gp.seed = s; gp.attempts = attempt + 1; gp.valid = ok;
        gp.deviceName = dev->name(); gp.archetype = ctx.archetype;
        gp.name = namer.generate(s, ctx, usedNames);
        gp.filePath = yawn::saveDevicePreset(gp.name, *dev);
    } else if (gp.kind == DeviceKind::AudioEffect) {
        std::unique_ptr<effects::AudioEffect> dev;
        uint64_t s = subSeed; int attempt = 0; bool ok = false;
        for (; attempt < maxAttempts; ++attempt) {
            dev = createAudioEffect(deviceId);
            if (!dev) return gp;
            dev->init(m_opts.sampleRate, blockSize);
            randomizeAudioEffect(*dev, s, m_opts, ctx);
            ok = !m_opts.validate || validateAudioEffect(*dev, m_opts.sampleRate);
            if (ok) break;
            s = mix64(s + 0x1000193ull);
        }
        gp.seed = s; gp.attempts = attempt + 1; gp.valid = ok;
        gp.deviceName = dev->name(); gp.archetype = ctx.archetype;
        gp.name = namer.generate(s, ctx, usedNames);
        gp.filePath = yawn::saveDevicePreset(gp.name, *dev);
    } else {
        std::unique_ptr<midi::MidiEffect> dev = createMidiEffect(deviceId);
        if (!dev) return gp;
        dev->init(m_opts.sampleRate);
        randomizeMidiEffect(*dev, subSeed, m_opts, ctx);
        if (m_opts.validate) validateMidiEffect(*dev, m_opts.sampleRate);
        gp.valid = true; gp.attempts = 1;
        gp.deviceName = dev->name(); gp.archetype = ctx.archetype;
        gp.name = namer.generate(subSeed, ctx, usedNames);
        gp.filePath = yawn::saveDevicePreset(gp.name, *dev);
    }
    return gp;
}

GeneratedPreset PresetGenerator::generateRackAndSave(uint64_t subSeed,
                                                     const std::vector<std::string>& used) {
    static const char* kRackInstr[] = {
        "subsynth","fmsynth","wavetable","karplus",
        "stringmachine","drawbarorgan","electricpiano"
    };
    GeneratedPreset gp;
    gp.deviceId = "instrack";
    gp.kind = DeviceKind::Instrument;
    gp.deviceName = "Instrument Rack";
    gp.seed = subSeed;

    const int blockSize = 512;
    const int maxAttempts = std::max(1, m_opts.maxRerollAttempts);
    NameContext ctx; ctx.kind = DeviceKind::Instrument;

    std::unique_ptr<instruments::InstrumentRack> rack;
    uint64_t s = subSeed; int attempt = 0; bool ok = false;

    for (; attempt < maxAttempts; ++attempt) {
        rack = std::make_unique<instruments::InstrumentRack>();
        rack->init(m_opts.sampleRate, blockSize);
        rack->clearChains();

        Rng r(s ? s : 1);
        const int topo = r.range(0, 2);  // 0 layer, 1 key-split, 2 vel-split
        int nch = (topo == 0) ? r.range(1, 3) : (topo == 1 ? r.range(2, 3) : 2);

        float aggBright = 0.0f, aggLen = 0.0f, aggMotion = 0.0f;
        int domArch = 0; bool anyDistort = false;

        for (int c = 0; c < nch; ++c) {
            const char* id = kRackInstr[r.range(0, static_cast<int>(std::size(kRackInstr)) - 1)];
            auto sub = createInstrument(id);
            if (!sub) continue;
            sub->init(m_opts.sampleRate, blockSize);
            NameContext subCtx;
            randomizeInstrument(*sub, mix64(s + static_cast<uint64_t>(c) * 0x9E3779B9ull),
                                m_opts, subCtx);

            uint8_t kl = 0, kh = 127, vl = 1, vh = 127;
            if (topo == 1) {                         // key split across 0..127
                int span = 128 / nch;
                kl = static_cast<uint8_t>(c == 0 ? 0 : c * span);
                kh = static_cast<uint8_t>(c == nch - 1 ? 127 : (c + 1) * span - 1);
            } else if (topo == 2 && nch == 2) {       // soft / hard velocity layers
                if (c == 0) { vl = 1;  vh = 70; }
                else        { vl = 71; vh = 127; }
            }
            if (rack->addChain(std::move(sub), kl, kh, vl, vh)) {
                int ci = rack->chainCount() - 1;
                // Layers sit a touch lower + spread in the stereo field;
                // splits stay centred at unity.
                rack->chain(ci).volume = (topo == 0) ? (0.65f + r.f01() * 0.3f) : 0.9f;
                rack->chain(ci).pan = (topo == 0 && nch > 1)
                    ? ((static_cast<float>(c) / static_cast<float>(nch - 1)) * 2.0f - 1.0f) * 0.5f
                    : 0.0f;
            }
            aggBright = std::max(aggBright, subCtx.brightness);
            aggLen    = std::max(aggLen,    subCtx.length);
            aggMotion = std::max(aggMotion, subCtx.motion);
            anyDistort = anyDistort || subCtx.distorted;
            if (c == 0) domArch = subCtx.archetype;
        }

        ctx.archetype = domArch; ctx.brightness = aggBright;
        ctx.length = aggLen; ctx.motion = aggMotion; ctx.distorted = anyDistort;

        if (rack->chainCount() == 0) { s = mix64(s + 1); continue; }
        ok = !m_opts.validate || validateInstrument(*rack, m_opts.sampleRate);
        if (ok) break;
        s = mix64(s + 0x1000193ull);
    }

    gp.seed = s; gp.attempts = attempt + 1; gp.valid = ok; gp.archetype = ctx.archetype;
    PresetNameGenerator namer(m_opts.alienNameRatio);
    gp.name = namer.generate(s, ctx, used);
    if (rack) gp.filePath = yawn::saveDevicePreset(gp.name, *rack);
    return gp;
}

std::vector<GeneratedPreset> PresetGenerator::generateBatch(const std::vector<GenSpec>& specs) {
    std::vector<GeneratedPreset> existing = loadManifest();

    // Index locked entries per device so we can preserve them.
    std::vector<GeneratedPreset> result;
    std::unordered_set<std::string> regenDevices;
    for (const auto& s : specs) regenDevices.insert(s.deviceId);

    // Carry over locked presets; delete the rest of the previously-
    // generated files for devices we're about to regenerate.
    std::unordered_set<std::string> lockedNamesGlobal;
    for (const auto& e : existing) {
        bool regen = regenDevices.count(e.deviceId) > 0;
        if (e.locked || !regen) {
            result.push_back(e);
            lockedNamesGlobal.insert(e.deviceId + "\x1f" + e.name);
        } else if (!e.filePath.empty()) {
            std::error_code ec;
            if (fs::exists(e.filePath, ec)) PresetManager::deletePreset(e.filePath);
        }
    }

    for (const auto& spec : specs) {
        if (!isSupported(spec.deviceId)) continue;
        std::vector<std::string> used;
        for (const auto& e : result)
            if (e.deviceId == spec.deviceId) used.push_back(e.name);
        for (int i = 0; i < spec.count; ++i) {
            uint64_t sub = seedFor(m_opts.masterSeed, spec.deviceId, i);
            GeneratedPreset gp = generateOneAndSave(spec.deviceId, sub, used);
            if (gp.name.empty()) continue;
            used.push_back(gp.name);
            result.push_back(gp);
        }
    }

    saveManifest(result);
    return result;
}

fs::path PresetGenerator::manifestPath() {
    // Kept OUTSIDE the presets root: the library scanner indexes every
    // <presetsRoot>/<device>/*.json file as a preset, so a manifest
    // stored under that root would show up as a junk entry in the
    // Browser. The parent dir (e.g. %APPDATA%/YAWN) is not scanned.
    return PresetManager::presetsRootDir().parent_path() / "preset_gen_manifest.json";
}

std::vector<GeneratedPreset> PresetGenerator::loadManifest() const {
    std::vector<GeneratedPreset> out;
    fs::path p = manifestPath();
    std::error_code ec;
    if (!fs::exists(p, ec)) return out;
    try {
        std::ifstream f(p);
        json j; f >> j;
        if (!j.contains("presets")) return out;
        for (const auto& e : j["presets"]) {
            GeneratedPreset gp;
            gp.deviceId   = e.value("deviceId", "");
            gp.deviceName = e.value("deviceName", "");
            gp.kind       = static_cast<DeviceKind>(e.value("kind", 0));
            gp.name       = e.value("name", "");
            gp.seed       = e.value("seed", static_cast<uint64_t>(0));
            gp.archetype  = e.value("archetype", 0);
            gp.attempts   = e.value("attempts", 1);
            gp.valid      = e.value("valid", true);
            gp.locked     = e.value("locked", false);
            gp.filePath   = e.value("filePath", "");
            out.push_back(gp);
        }
    } catch (...) {}
    return out;
}

void PresetGenerator::saveManifest(const std::vector<GeneratedPreset>& presets) const {
    fs::path p = manifestPath();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    json j;
    j["masterSeed"] = m_opts.masterSeed;
    json arr = json::array();
    for (const auto& gp : presets) {
        json e;
        e["deviceId"]   = gp.deviceId;
        e["deviceName"] = gp.deviceName;
        e["kind"]       = static_cast<int>(gp.kind);
        e["name"]       = gp.name;
        e["seed"]       = gp.seed;
        e["archetype"]  = gp.archetype;
        e["attempts"]   = gp.attempts;
        e["valid"]      = gp.valid;
        e["locked"]     = gp.locked;
        e["filePath"]   = gp.filePath.string();
        arr.push_back(e);
    }
    j["presets"] = arr;
    try {
        std::ofstream f(p, std::ios::trunc);
        f << j.dump(2);
    } catch (...) {}
}

} // namespace presets
} // namespace yawn
