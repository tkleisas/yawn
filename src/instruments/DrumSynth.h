#pragma once
// DrumSynth.h — fully-synthesised 8-piece drum kit.
//
// Drums are ordered by GM MIDI note number (bottom → top in PianoRoll's
// "drum mode"):
//
//   Slot  Drum          GM note
//   ───────────────────────────
//    0    Kick          36
//    1    Snare         38
//    2    Clap          39
//    3    Tom1 (low)    41
//    4    Closed HH     42
//    5    Open HH       46
//    6    Tom2 (high)   50
//    7    Tambourine    54
//
// Each drum is a small monophonic voice — re-triggering on a held key
// resets the envelope (standard drum-machine behaviour). Output is a
// stereo sum with auto-pan (classic kit positioning); ClosedHH triggers
// auto-choke OpenHH the way real hi-hats work.

#include "instruments/Instrument.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yawn {
namespace instruments {

class DrumSynth : public Instrument {
public:
    static constexpr int kNumDrums = 8;

    // Slot indices — also the row order PianoRoll's drum mode renders
    // (bottom = 0). Values are GM-ordered so the natural MIDI-value
    // sort matches the visual order.
    enum DrumSlot {
        Kick = 0,
        Snare,
        Clap,
        Tom1,
        ClosedHH,
        OpenHH,
        Tom2,
        Tambourine,
    };

    // GM MIDI note numbers, indexed by DrumSlot.
    static constexpr int kDrumNotes[kNumDrums] = {
        36, 38, 39, 41, 42, 46, 50, 54
    };

    // Which slot, if any, this MIDI note triggers. -1 if no match.
    // Linear search is fine — 8 entries.
    static int slotForNote(int note) {
        for (int i = 0; i < kNumDrums; ++i)
            if (kDrumNotes[i] == note) return i;
        return -1;
    }

    enum Param {
        // Kick — 7 params (the only drum with sine/noise mix)
        pKickTune = 0, pKickAttack, pKickDecay,
        pKickSine, pKickWhite, pKickPink, pKickDrive,
        // Snare — 4 params
        pSnareTune, pSnareAttack, pSnareDecay, pSnareDrive,
        // Clap
        pClapTune,  pClapAttack,  pClapDecay,  pClapDrive,
        // Tom1
        pTom1Tune,  pTom1Attack,  pTom1Decay,  pTom1Drive,
        // ClosedHH
        pCHHTune,   pCHHAttack,   pCHHDecay,   pCHHDrive,
        // OpenHH
        pOHHTune,   pOHHAttack,   pOHHDecay,   pOHHDrive,
        // Tom2
        pTom2Tune,  pTom2Attack,  pTom2Decay,  pTom2Drive,
        // Tambourine
        pTambTune,  pTambAttack,  pTambDecay,  pTambDrive,
        kParamCount  // 7 + 7×4 = 35
    };

    const char* name() const override { return "Drum Synth"; }
    const char* id()   const override { return "drumsynth"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        // Knob template: each drum exposes 4 knobs (tune/atk/dec/drv);
        // kick adds 3 more (sine/white/pink mix). Tune is normalised
        // 0..1 and log-mapped per-drum to a sensible Hz range inside
        // process() — different drums need very different ranges so we
        // keep the knob abstract and document at the param-info layer.
        static const InstrumentParameterInfo info[kParamCount] = {
            // ── Kick ──
            {"Tune",   0.0f, 1.0f, 0.30f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 50.0f, 1.0f, "ms",false},
            {"Dec",    20.0f, 1500.0f, 350.0f, "ms",false},
            {"Sine",   0.0f, 1.0f, 0.90f, "",  false, false, WidgetHint::DentedKnob},
            {"White",  0.0f, 1.0f, 0.25f, "",  false, false, WidgetHint::DentedKnob},
            {"Pink",   0.0f, 1.0f, 0.20f, "",  false, false, WidgetHint::DentedKnob},
            {"Drive",  0.0f, 1.0f, 0.20f, "",  false, false, WidgetHint::DentedKnob},
            // ── Snare ──
            {"Tune",   0.0f, 1.0f, 0.50f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 30.0f, 1.0f, "ms",false},
            {"Dec",    20.0f, 800.0f, 180.0f, "ms",false},
            {"Drive",  0.0f, 1.0f, 0.30f, "",  false, false, WidgetHint::DentedKnob},
            // ── Clap ──
            {"Tune",   0.0f, 1.0f, 0.50f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 30.0f, 1.0f, "ms",false},
            {"Dec",    20.0f, 800.0f, 220.0f, "ms",false},
            {"Drive",  0.0f, 1.0f, 0.10f, "",  false, false, WidgetHint::DentedKnob},
            // ── Tom1 ──
            {"Tune",   0.0f, 1.0f, 0.30f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 30.0f, 1.0f, "ms",false},
            {"Dec",    50.0f, 1500.0f, 500.0f, "ms",false},
            {"Drive",  0.0f, 1.0f, 0.10f, "",  false, false, WidgetHint::DentedKnob},
            // ── ClosedHH ──
            {"Tune",   0.0f, 1.0f, 0.50f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 30.0f, 1.0f, "ms",false},
            {"Dec",    10.0f, 200.0f, 60.0f, "ms",false},
            {"Drive",  0.0f, 1.0f, 0.10f, "",  false, false, WidgetHint::DentedKnob},
            // ── OpenHH ──
            {"Tune",   0.0f, 1.0f, 0.50f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 30.0f, 1.0f, "ms",false},
            {"Dec",    50.0f, 1500.0f, 350.0f, "ms",false},
            {"Drive",  0.0f, 1.0f, 0.10f, "",  false, false, WidgetHint::DentedKnob},
            // ── Tom2 ──
            {"Tune",   0.0f, 1.0f, 0.55f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 30.0f, 1.0f, "ms",false},
            {"Dec",    50.0f, 1500.0f, 400.0f, "ms",false},
            {"Drive",  0.0f, 1.0f, 0.10f, "",  false, false, WidgetHint::DentedKnob},
            // ── Tambourine ──
            {"Tune",   0.0f, 1.0f, 0.60f, "",  false, false, WidgetHint::DentedKnob},
            {"Atk",    1.0f, 30.0f, 1.0f, "ms",false},
            {"Dec",    20.0f, 600.0f, 150.0f, "ms",false},
            {"Drive",  0.0f, 1.0f, 0.10f, "",  false, false, WidgetHint::DentedKnob},
        };
        return info[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        const auto& pi = parameterInfo(index);
        m_params[index] = std::clamp(value, pi.minValue, pi.maxValue);
    }

private:
    // ── Per-drum state ──────────────────────────────────────────────
    //
    // Drums are monophonic per-pad; re-triggering during sustain just
    // resets the envelope and phases. That matches how real drum
    // machines respond and avoids per-pad voice stealing.
    struct DrumVoice {
        bool   active = false;
        // First-sample-since-trigger flag. Replaces the older
        // `ageSamples == 0.0f` exact-float-compare check that lit up
        // the per-sample initialisation branch in render*. The bool
        // is unambiguous and survives any future refactor that
        // would let ageSamples drift from a literal 0.
        bool   justTriggered = false;
        float  velocity = 0.0f;     // 0..1, becomes amp scale
        float  ampLevel = 0.0f;     // current envelope level
        int    stage = 0;           // 0=Attack, 1=Decay, 2=Idle
        int    ageSamples = 0;      // time since trigger — clap multi-burst, etc.
        // Oscillator phases. phase0 is the primary; phases[] indexes
        // 0..5 used for hihat / tambourine multi-square synthesis,
        // and as small per-sample envelopes for kick / snare / tom /
        // clap (overloaded role — see each render fn for details).
        double phase0 = 0.0;
        double phases[6] = {};
        // Per-voice biquad state for noise-bandpass / metallic-HP
        // shaping. Coefficients are computed each block from the
        // "Tune" param so we don't pay per-sample design ops.
        float bpZ1 = 0.0f, bpZ2 = 0.0f;
    };

    DrumVoice m_voices[kNumDrums] = {};

    // ── Parameters ──
    float m_params[kParamCount] = {};

    // ── PRNG for noise (xorshift32). Independent per-side calls
    // produce decorrelated noise so stereo widens naturally on the
    // noise-heavy drums (clap, tambourine, hi-hats).
    uint32_t m_rngState = 0xC0FFEEu;
    float randFloat() {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        return static_cast<float>(m_rngState & 0x7FFFFF) / 8388607.0f;
    }
    // Pink-ish noise via Voss-McCartney approximation. Cheap; not
    // perfect 1/f but close enough for kick-tail thump. Counter is
    // unsigned to make the increment + bit-twiddling well-defined
    // after the first ~12 hours of continuous playback at 48 kHz —
    // signed-int overflow there is UB.
    float    m_pinkRows[5] = {};
    float    m_pinkSum = 0.0f;
    uint32_t m_pinkCounter = 0;
    float pinkNoise();

    // ── Per-render scratch buffers ──
    // Sized at init() against the host's m_maxBlockSize contract;
    // process() uses these instead of a fixed-size stack array so
    // (a) we don't drop blocks larger than 4096 frames silently,
    // (b) we don't stack-allocate 32 KB on every audio callback.
    std::vector<float> m_scratchL;
    std::vector<float> m_scratchR;
    bool               m_oversizedBlockWarned = false;

    // ── MIDI handling ──
    void noteOn(int slot, float velocity);
    void noteOff(int slot);

    // ── Per-drum render helpers (operate on one voice for `numFrames`
    // samples, write the result into outL/outR with auto-pan baked in).
    void renderKick      (DrumVoice& v, float* outL, float* outR, int n);
    void renderSnare     (DrumVoice& v, float* outL, float* outR, int n);
    void renderClap      (DrumVoice& v, float* outL, float* outR, int n);
    void renderTom       (DrumVoice& v, float* outL, float* outR, int n,
                          int slot);
    void renderHiHat     (DrumVoice& v, float* outL, float* outR, int n,
                          bool open, int slot);
    void renderTambourine(DrumVoice& v, float* outL, float* outR, int n);

    // ── Helpers ──
    static float panL(float pan) { return std::cos((pan + 1.0f) * 0.7853981f); }
    static float panR(float pan) { return std::sin((pan + 1.0f) * 0.7853981f); }
    static float drive(float x, float amount) {
        // tanh saturator. amount=0 → identity, amount=1 → ~9 dB pre-gain
        // before the tanh squashes the peaks. Pre-gain matters: pure
        // tanh on a small input rounds linearly with no audible
        // distortion.
        const float pre = 1.0f + amount * 5.0f;
        return std::tanh(x * pre) / std::tanh(pre);
    }

    // Tune-knob mapping: 0..1 → Hz with per-drum range in process().
    static float tuneToHz(float t, float lo, float hi) {
        return lo * std::pow(hi / lo, std::clamp(t, 0.0f, 1.0f));
    }
};

} // namespace instruments
} // namespace yawn
