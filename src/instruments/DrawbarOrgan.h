#pragma once

// Drawbar Organ — Hammond B-3 style additive tonewheel-less organ.
//
// Architecture: pure additive. Each voice holds 9 sine oscillators
// matching the 9 standard B-3 drawbar footages:
//
//   16′    sub-octave    f × 0.5
//   5⅓′    quint         f × 1.5
//   8′     foundation    f × 1.0
//   4′     octave        f × 2.0
//   2⅔′    octave + 5th  f × 3.0
//   2′     2 octaves     f × 4.0
//   1⅗′    2oct + 3rd    f × 5.0
//   1⅓′    2oct + 5th    f × 6.0
//   1′     3 octaves     f × 8.0
//
// Each drawbar is a 0..1 weight (the 0..8 stop scale on the real
// instrument, normalized). Sum of weighted sines per voice → fast
// AR amp envelope → mixed into the shared output bus.
//
// Shared (one instance, not per-voice):
//   * Percussion — single oscillator at 2nd or 3rd harmonic of the
//     played note, AR envelope (~150ms or ~400ms decay), monophonic
//     and SINGLE-TRIGGER (only fires when no other notes are held —
//     the famous "Hammond percussion drains" behaviour). Volume
//     soft/normal.
//   * Key click — short noise burst on every note-on attack
//     (modelling the busbar contact transient).
//   * Vibrato / Chorus — short modulated delay line driven by a
//     ~6.7 Hz LFO (B-3 scanner rate). "Vibrato" replaces dry with
//     the modulated signal; "Chorus" mixes them. Modernised UX:
//     one continuous depth knob + one chorus on/off toggle, in
//     place of the authentic 6-position V1/V2/V3/C1/C2/C3 selector.
//
// Note: rotary speaker is intentionally NOT built in here — it
// lives as a separate `Rotary` audio effect that the default
// preset chain auto-inserts. This keeps the Leslie reusable on
// guitar / vocals / anything (which is how every B-3 player uses
// one in real life).
//
// 16-voice polyphony with steal-oldest. ~144 sine oscillators
// max — trivial CPU cost.

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace yawn {
namespace instruments {

class DrawbarOrgan : public Instrument {
public:
    static constexpr int kMaxVoices       = 16;
    static constexpr int kDrawbars        = 9;
    static constexpr int kVibBufFrames    = 1024;   // ~21ms at 48k

    enum Params {
        kD16 = 0,    // 16' drawbar (sub octave)
        kD513,       // 5 1/3' drawbar (quint)
        kD8,         // 8' drawbar (foundation)
        kD4,         // 4' drawbar (octave)
        kD223,       // 2 2/3' drawbar (octave + 5th)
        kD2,         // 2' drawbar (2 octaves)
        kD135,       // 1 3/5' drawbar (2oct + maj 3rd)
        kD113,       // 1 1/3' drawbar (2oct + 5th)
        kD1,         // 1' drawbar (3 octaves)

        kPercOn,         // Percussion on/off (toggle)
        kPercHarmonic,   // 0 = 2nd harmonic, 1 = 3rd harmonic
        kPercDecayLong,  // 0 = short (~150ms), 1 = long (~400ms)
        kPercVolNorm,    // 0 = soft (~70%), 1 = normal (100%)

        kKeyClick,       // 0..1 click amplitude

        kVibratoDepth,   // 0..1 scanner vibrato depth
        kChorusOn,       // Chorus on/off — when off, vibrato is pure;
                         // when on, dry is mixed with modulated signal.

        kVolume,         // 0..1
        kNumParams
    };

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return "Drawbar Organ"; }
    const char* id()   const override { return "drawbarorgan"; }

    int parameterCount() const override { return kNumParams; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        // Drawbar labels: ASCII fractions only. The Unicode vulgar
        // fraction glyphs (⅓ ⅔ ⅗) aren't in our font's pre-baked
        // atlas, so they render as empty space — collapsing "5⅓'"
        // into "5'" and giving us THREE indistinguishable "1'"
        // cells. ASCII space-then-fraction stays unambiguous and
        // fits the narrow 9-cell column width.
        //
        // Row 2 labels: ≤ 7 chars so the 8-cell row doesn't bleed
        // into adjacent columns. The percussion toggles drop their
        // "Perc " prefix and rely on adjacency (the 4 toggles sit
        // together, the user reads them as "the perc cluster").
        static const InstrumentParameterInfo p[kNumParams] = {
            {"16'",    0,1,0.0f, "", false, false, WidgetHint::DentedKnob},
            {"5 1/3",  0,1,0.0f, "", false, false, WidgetHint::DentedKnob},
            {"8'",     0,1,1.0f, "", false, false, WidgetHint::DentedKnob},
            {"4'",     0,1,0.5f, "", false, false, WidgetHint::DentedKnob},
            {"2 2/3",  0,1,0.0f, "", false, false, WidgetHint::DentedKnob},
            {"2'",     0,1,0.0f, "", false, false, WidgetHint::DentedKnob},
            {"1 3/5",  0,1,0.0f, "", false, false, WidgetHint::DentedKnob},
            {"1 1/3",  0,1,0.0f, "", false, false, WidgetHint::DentedKnob},
            {"1'",     0,1,0.0f, "", false, false, WidgetHint::DentedKnob},

            {"Perc",   0,1,1.0f, "", true,  false, WidgetHint::Toggle},
            {"Harm",   0,1,0.0f, "", true,  false, WidgetHint::Toggle},
            {"Decay",  0,1,0.0f, "", true,  false, WidgetHint::Toggle},
            {"Loud",   0,1,0.0f, "", true,  false, WidgetHint::Toggle},

            {"Click",  0,1,0.2f, "", false, false, WidgetHint::Knob},

            {"Vibrato",0,1,0.0f, "", false, false, WidgetHint::DentedKnob},
            {"Chorus", 0,1,0.0f, "", true,  false, WidgetHint::Toggle},

            {"Volume", 0,1,0.7f, "", false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kD16:   return m_drawbar[0];
            case kD513:  return m_drawbar[1];
            case kD8:    return m_drawbar[2];
            case kD4:    return m_drawbar[3];
            case kD223:  return m_drawbar[4];
            case kD2:    return m_drawbar[5];
            case kD135:  return m_drawbar[6];
            case kD113:  return m_drawbar[7];
            case kD1:    return m_drawbar[8];
            case kPercOn:        return m_percOn        ? 1.0f : 0.0f;
            case kPercHarmonic:  return m_percThirdHarm ? 1.0f : 0.0f;
            case kPercDecayLong: return m_percLong      ? 1.0f : 0.0f;
            case kPercVolNorm:   return m_percNormal    ? 1.0f : 0.0f;
            case kKeyClick:      return m_keyClickLevel;
            case kVibratoDepth:  return m_vibDepth;
            case kChorusOn:      return m_chorusOn      ? 1.0f : 0.0f;
            case kVolume:        return m_volume;
            default:             return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        const float v = std::clamp(value, 0.0f, 1.0f);
        switch (index) {
            case kD16:   m_drawbar[0] = v; break;
            case kD513:  m_drawbar[1] = v; break;
            case kD8:    m_drawbar[2] = v; break;
            case kD4:    m_drawbar[3] = v; break;
            case kD223:  m_drawbar[4] = v; break;
            case kD2:    m_drawbar[5] = v; break;
            case kD135:  m_drawbar[6] = v; break;
            case kD113:  m_drawbar[7] = v; break;
            case kD1:    m_drawbar[8] = v; break;
            case kPercOn:        m_percOn        = v >= 0.5f;
                                 updatePercEnv(); break;
            case kPercHarmonic:  m_percThirdHarm = v >= 0.5f; break;
            case kPercDecayLong: m_percLong      = v >= 0.5f;
                                 updatePercEnv(); break;
            case kPercVolNorm:   m_percNormal    = v >= 0.5f; break;
            case kKeyClick:      m_keyClickLevel = v; break;
            case kVibratoDepth:  m_vibDepth      = v; break;
            case kChorusOn:      m_chorusOn      = v >= 0.5f; break;
            case kVolume:        m_volume        = v; break;
        }
    }

    // ── Public for tests ────────────────────────────────────────────
    // Drawbar harmonic ratios (16' = 0.5, 5⅓' = 1.5, …, 1' = 8.0).
    static constexpr float kHarmonicRatio[kDrawbars] = {
        0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f
    };

private:
    struct Voice {
        bool      active = false;
        uint8_t   note = 0;
        uint8_t   channel = 0;
        float     velocity = 0.0f;
        int64_t   startOrder = 0;
        // Per-drawbar oscillator phases (cycles 0..1). Sine is
        // computed from std::sin(2π·phase) — simple and clean for
        // additive synthesis (no aliasing concerns since sine is
        // band-limited by definition).
        double    phase[kDrawbars] = {};
        Envelope  ampEnv;
    };

    // Public helpers used by both process() and tests.
    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int  findFreeVoice();
    int  activeVoiceCount() const;
    void updatePercEnv();
    void applyDefaults();

    // ── Per-voice state ─────────────────────────────────────────────
    Voice    m_voices[kMaxVoices];
    int64_t  m_voiceCounter = 0;
    float    m_pitchBend    = 0.0f;

    // ── Shared monophonic percussion ────────────────────────────────
    bool     m_percActive    = false;
    double   m_percPhase     = 0.0;
    float    m_percFreq      = 0.0f;   // set on each fresh trigger
    Envelope m_percEnv;

    // ── Shared key-click noise burst ────────────────────────────────
    // Triggered on every note-on. Single short envelope shared
    // across voices — the click is impulsive and indistinguishable
    // when stacked anyway.
    float    m_clickLevel = 0.0f;      // current burst amplitude
    float    m_clickDecay = 0.0f;      // per-sample exp decay
    uint32_t m_noiseRng   = 12345;

    // ── Shared scanner-style vibrato/chorus delay line ──────────────
    float    m_vibBuf[kVibBufFrames] = {};
    int      m_vibWrite = 0;
    double   m_vibPhase = 0.0;         // 0..1 LFO phase
    static constexpr float kVibRateHz = 6.7f;   // classic B-3 scanner

    // ── Parameters ──────────────────────────────────────────────────
    float m_drawbar[kDrawbars] = {0,0,1,0.5f,0,0,0,0,0};
    bool  m_percOn         = true;
    bool  m_percThirdHarm  = false;    // false=2nd, true=3rd
    bool  m_percLong       = false;    // false=short, true=long
    bool  m_percNormal     = false;    // false=soft, true=normal
    float m_keyClickLevel  = 0.2f;
    float m_vibDepth       = 0.0f;
    bool  m_chorusOn       = false;
    float m_volume         = 0.7f;
};

} // namespace instruments
} // namespace yawn
