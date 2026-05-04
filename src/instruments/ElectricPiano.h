#pragma once

// Electric Piano — 2-operator FM e-piano modelled on the Rhodes /
// Wurlitzer / Suitcase Rhodes family of sounds.
//
// Architecture:
//   * Per voice: a carrier sine + a modulator sine. Output is
//     classic phase-modulation FM:
//
//         y = sin(carrier_phase + I * sin(modulator_phase))
//
//     The modulator sits at carrier_freq × ratio. Velocity drives
//     the modulation index I — harder hits → brighter, more
//     metallic "bell" / "bark"; soft hits → near-pure-sine, woody.
//     This is the same trick the DX7's E-PIANO 1 patch uses; it's
//     why a single 2-op pair can pass for a Rhodes tine OR a Wurli
//     reed depending on the ratio + envelope shape.
//
//   * Mode switch (Rhodes / Wurli / Suitcase):
//       - Rhodes:   ratio 14.0 (bell-shimmer overtone), short hammer
//                   transient, pan tremolo at 6 Hz.
//       - Wurli:    ratio  3.0 (lower, woodier overtone), pronounced
//                   "bark" on hard hits, amp tremolo at 5.5 Hz —
//                   matches the actual Wurlitzer 200A vibrato.
//       - Suitcase: like Rhodes but a hair more mod depth + richer
//                   pan tremolo (the iconic "Suitcase 73" wobble).
//                   App.cpp's menu entry also auto-inserts a Phaser
//                   into the chain — the classic Mark V / Steely
//                   Dan rig pairing.
//
//   * Per voice: short hammer-noise transient (a brief burst of
//     filtered noise at note-on, decays in ~30 ms). Adds the
//     percussive "thock" that distinguishes a tine strike from a
//     plain sine — without it the patch sounds like a synth
//     organ.
//
//   * Per voice: exponential amp envelope. Fast attack, no sustain
//     stage — sustain held at 0 so the natural decay rolls to
//     silence the way a real EP does (no infinite hold). Release
//     is short for a clean noteOff.
//
//   * Shared post-mix: tremolo. Pan-trem (Rhodes / Suitcase) sweeps
//     the stereo image L↔R; amp-trem (Wurli) modulates loudness on
//     both channels in lock-step. Off when depth = 0.
//
// 16-voice polyphony with steal-oldest, same as the other YAWN synths.

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

class ElectricPiano : public Instrument {
public:
    static constexpr int kMaxVoices = 16;

    enum Mode : int {
        Rhodes   = 0,
        Wurli    = 1,
        Suitcase = 2,
    };

    enum Params {
        kMode = 0,        // 0=Rhodes, 1=Wurli, 2=Suitcase
        kBellMix,         // 0..1 — base modulation index (velocity scales on top)
        kHardness,        // 0..1 — how strongly velocity drives mod index
        kHammer,          // 0..1 — note-on transient amplitude
        kAttack,          // 0.001..0.05 s
        kDecay,           // 0.5..10 s — natural EP decay length
        kRelease,         // 0.05..2 s — note-off tail
        kTremRate,        // 0.5..10 Hz
        kTremDepth,       // 0..1
        kVolume,          // 0..1
        kNumParams
    };

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return "Electric Piano"; }
    const char* id()   const override { return "electricpiano"; }

    int parameterCount() const override { return kNumParams; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kModeLabels[] = {"Rhodes", "Wurli", "Suitcase"};
        static const InstrumentParameterInfo p[kNumParams] = {
            {"Mode",       0,    2,    2.0f,  "",   false, false,
                WidgetHint::StepSelector, kModeLabels, 3},
            // Bell + Hardness defaults dropped (was 0.4 / 0.7) — at
            // the Rhodes 14:1 ratio those produced a mod index ~1.9,
            // which puts strong sidebands at the 13th + 15th harmonic
            // and starves the fundamental, so the brain perceives the
            // overtones as the pitch and the patch sounds out of tune.
            // Lower defaults keep the fundamental dominant.
            {"Bell",       0,    1,    0.3f,  "",   false, false, WidgetHint::DentedKnob},
            {"Hardness",   0,    1,    0.5f,  "",   false, false, WidgetHint::DentedKnob},
            {"Hammer",     0,    1,    0.35f, "",   false, false, WidgetHint::DentedKnob},
            {"Attack",     0.001f, 0.05f, 0.003f, "s", false},
            {"Decay",      0.5f, 10.0f, 4.5f,  "s",  false},
            {"Release",    0.05f, 2.0f, 0.25f, "s",  false},
            {"Trem Rate",  0.5f, 10.0f, 6.0f,  "Hz", false},
            {"Trem Depth", 0,    1,    0.35f, "",   false, false, WidgetHint::DentedKnob},
            {"Volume",     0,    1,    0.5f,  "",   false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kMode:       return static_cast<float>(m_mode);
            case kBellMix:    return m_bellMix;
            case kHardness:   return m_hardness;
            case kHammer:     return m_hammer;
            case kAttack:     return m_attack;
            case kDecay:      return m_decay;
            case kRelease:    return m_release;
            case kTremRate:   return m_tremRate;
            case kTremDepth:  return m_tremDepth;
            case kVolume:     return m_volume;
            default:          return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kMode:
                m_mode = static_cast<Mode>(std::clamp(
                    static_cast<int>(value + 0.5f), 0, 2));
                break;
            case kBellMix:    m_bellMix   = std::clamp(value, 0.0f, 1.0f); break;
            case kHardness:   m_hardness  = std::clamp(value, 0.0f, 1.0f); break;
            case kHammer:     m_hammer    = std::clamp(value, 0.0f, 1.0f); break;
            case kAttack:     m_attack    = std::clamp(value, 0.001f, 0.05f);
                              updateEnvelopes(); break;
            case kDecay:      m_decay     = std::clamp(value, 0.5f, 10.0f);
                              updateEnvelopes(); break;
            case kRelease:    m_release   = std::clamp(value, 0.05f, 2.0f);
                              updateEnvelopes(); break;
            case kTremRate:   m_tremRate  = std::clamp(value, 0.5f, 10.0f); break;
            case kTremDepth:  m_tremDepth = std::clamp(value, 0.0f, 1.0f);  break;
            case kVolume:     m_volume    = std::clamp(value, 0.0f, 1.0f);  break;
        }
    }

private:
    struct Voice {
        bool      active = false;
        uint8_t   note = 0;
        uint8_t   channel = 0;
        float     velocity = 0.0f;
        int64_t   startOrder = 0;
        // Phase accumulators for carrier + modulator (radians-style:
        // 0..1 fractional, multiplied by 2π in process). Kept as
        // double so long sustains don't accumulate drift.
        double    carPhase = 0.0;
        double    modPhase = 0.0;
        // Hammer transient — short noise burst with its own
        // exponential decay coefficient. amp starts at the per-note
        // hammer level and multiplies by `decay` each sample until
        // it falls below threshold.
        float     hammerAmp = 0.0f;
        float     hammerDecay = 0.0f;
        // Per-sample noise state for the hammer (one-pole LP for a
        // less harsh "thock" than raw white noise).
        float     hammerLpZ = 0.0f;
        // Pre-resolved per-voice frequencies (Hz). Updated at
        // note-on; pitch-bend scales them per-block.
        float     carFreqHz = 0.0f;
        float     modFreqHz = 0.0f;
        Envelope  ampEnv;
    };

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int  findFreeVoice();
    void updateEnvelopes();
    void applyDefaults();

    // Mode → (carrier:modulator ratio, mod-index range, hammer scalar,
    // tremolo type 0=pan / 1=amp).
    //
    // idxBase = mod index at velocity 0 (with Bell=Hardness=0).
    // idxRange = how much mod index opens up with Bell + velocity.
    //
    // Tuned so the fundamental stays dominant at default settings:
    //   * Rhodes / Suitcase (ratio 14): Bessel sidebands at fc±n·14fc
    //     are technically harmonic, but n≥2 lobes get loud past
    //     β≈1 and the perceived pitch starts wandering. Cap at ~0.9.
    //   * Wurli (ratio 3): shorter modulator → sidebands stay tight,
    //     can take a much higher β before the fundamental dies.
    struct ModeInfo {
        float ratio;
        float idxBase;
        float idxRange;
        float hammerScale;
        int   tremType;
    };
    ModeInfo modeInfo() const {
        switch (m_mode) {
            case Wurli:    return { 3.0f,  0.3f,  2.5f,  0.6f,  1 };
            case Suitcase: return {14.0f,  0.0f,  0.9f,  0.45f, 0 };
            case Rhodes:   default:
                           return {14.0f,  0.0f,  0.7f,  0.5f,  0 };
        }
    }

    // ── Per-voice ──────────────────────────────────────────────────
    Voice    m_voices[kMaxVoices];
    int64_t  m_voiceCounter = 0;
    float    m_pitchBend = 0.0f;

    // ── Shared tremolo LFO (post-mix) ──────────────────────────────
    double   m_tremPhase = 0.0;

    // Cheap pseudo-random for hammer noise. xorshift32 — one-line
    // generator, good enough for short transients (we're not after
    // cryptographic quality, just decorrelated samples).
    uint32_t m_rng = 0x6e8a3217u;
    float    nextNoise() {
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        // Map to ~[-1, 1] cheap.
        return (static_cast<int32_t>(m_rng) * (1.0f / 2147483648.0f));
    }

    // ── Parameters ─────────────────────────────────────────────────
    Mode  m_mode      = Suitcase;
    float m_bellMix   = 0.3f;
    float m_hardness  = 0.5f;
    float m_hammer    = 0.35f;
    float m_attack    = 0.003f;
    float m_decay     = 4.5f;
    float m_release   = 0.25f;
    float m_tremRate  = 6.0f;
    float m_tremDepth = 0.35f;
    float m_volume    = 0.5f;
};

} // namespace instruments
} // namespace yawn
