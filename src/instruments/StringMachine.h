#pragma once

// String Machine — Solina-style ensemble strings (1974 ARP reference).
//
// Architecture:
//   * Per voice: 3 detuned saw oscillators × 3 octave registers
//     (16′ / 8′ / 4′). Each register fed by its own three-saw stack,
//     pitched at the note's frequency × 0.5 / × 1 / × 2 respectively.
//     Detune staggers the three saws ±k cents so they beat against
//     each other — the bedrock of the Solina sound.
//   * Per voice: slow attack/release amp envelope (no decay/sustain
//     stage really — modelled as ADSR with sustain=1 so the value
//     hits 1.0 at end of attack and holds there). Polyphonic, but
//     paraphonic in feel — see filter note below.
//   * Shared (post-voice-mix) LP filter — single SVF with fixed Q,
//     `Brightness` knob log-maps 0..1 → 200..8000 Hz. Authentic
//     Solina behaviour: filter is paraphonic, so multiple voices
//     share its resonance state.
//   * Shared (post-filter) 3-tap BBD-style chorus: three short
//     delay lines modulated by an LFO at 120° phase offsets.
//     Output is panned tap1 → L, tap3 → R, tap2 → centre — the
//     classic ensemble stereo spread. The chorus IS the sound;
//     turning Ensemble Depth to 0 audibly makes the patch sound
//     wrong, but it's not removed because the sliders need the
//     range for very subtle settings.
//
// 16-voice polyphony with steal-oldest. ~144 saw oscillators max
// at full polyphony — totally manageable on any modern CPU.

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "instruments/Oscillator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace yawn {
namespace instruments {

class StringMachine : public Instrument {
public:
    static constexpr int kMaxVoices       = 16;
    static constexpr int kSawsPerOctave   = 3;
    static constexpr int kOctaves         = 3;   // 16′, 8′, 4′
    static constexpr int kOscsPerVoice    = kSawsPerOctave * kOctaves;
    static constexpr int kChorusTaps      = 3;
    static constexpr int kChorusBufFrames = 4096;  // ~85ms at 48k

    enum Params {
        kOct16Level = 0,    // 16' sub-octave volume
        kOct8Level,         // 8' foundation volume
        kOct4Level,         // 4' upper-octave volume
        kDetune,            // 0..1 → 0..15 cents stagger between saws
        kAttack,            // 0.005..3 s
        kRelease,           // 0.05..5 s
        kBrightness,        // 0..1 → 200..8000 Hz log
        kEnsembleDepth,     // 0..1 chorus modulation depth
        kEnsembleRate,      // 0.1..2 Hz chorus LFO
        kEnsembleOn,        // 0/1 chorus bypass switch
        kVolume,            // 0..1
        kNumParams
    };

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return "String Machine"; }
    const char* id()   const override { return "stringmachine"; }

    int parameterCount() const override { return kNumParams; }

    // Brightness 0..1 → 200..8000 Hz log (a useful musical range
    // for an LP smoothing filter on saw-stacks).
    static float brightnessNormToHz(float x) {
        return 200.0f * std::pow(40.0f, std::clamp(x, 0.0f, 1.0f));
    }
    static void formatBrightnessHz(float v, char* buf, int n) {
        const float hz = brightnessNormToHz(v);
        if (hz >= 1000.0f) std::snprintf(buf, n, "%.1fk", hz / 1000.0f);
        else                std::snprintf(buf, n, "%.0f",  hz);
    }
    // Detune 0..1 → cents display ("3.5 ct" etc.).
    static void formatDetuneCents(float v, char* buf, int n) {
        const float cents = std::clamp(v, 0.0f, 1.0f) * 15.0f;
        std::snprintf(buf, n, "%.1f", cents);
    }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo p[kNumParams] = {
            {"16' Level",     0,1,0.0f,   "", false, false, WidgetHint::DentedKnob},
            {"8' Level",      0,1,0.8f,   "", false, false, WidgetHint::DentedKnob},
            {"4' Level",      0,1,0.5f,   "", false, false, WidgetHint::DentedKnob},
            {"Detune",        0,1,0.5f,   "ct", false, false,
                WidgetHint::DentedKnob, nullptr, 0, &formatDetuneCents},
            {"Attack",        0.005f,3.0f,0.3f, "s",  false},
            {"Release",       0.05f, 5.0f,0.8f, "s",  false},
            {"Brightness",    0,1,0.5f,   "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatBrightnessHz},
            // Ensemble = the chorus stage, named after the actual
            // Solina front-panel label. Abbreviated to "Ens *" so
            // the labels fit the knob-grid column width without
            // bleeding into Brightness on the left.
            {"Ens Depth",     0,1,0.6f,   "", false, false, WidgetHint::DentedKnob},
            {"Ens Rate",      0.1f,2.0f,0.6f, "Hz", false},
            {"Ens On",        0,1,1.0f,   "", true},
            {"Volume",        0,1,0.5f,   "", false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kOct16Level:     return m_oct16Level;
            case kOct8Level:      return m_oct8Level;
            case kOct4Level:      return m_oct4Level;
            case kDetune:         return m_detune;
            case kAttack:         return m_attack;
            case kRelease:        return m_release;
            case kBrightness:     return m_brightness;
            case kEnsembleDepth:  return m_ensembleDepth;
            case kEnsembleRate:   return m_ensembleRate;
            case kEnsembleOn:     return m_ensembleOn ? 1.0f : 0.0f;
            case kVolume:         return m_volume;
            default:              return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kOct16Level:     m_oct16Level = std::clamp(value, 0.0f, 1.0f); break;
            case kOct8Level:      m_oct8Level  = std::clamp(value, 0.0f, 1.0f); break;
            case kOct4Level:      m_oct4Level  = std::clamp(value, 0.0f, 1.0f); break;
            case kDetune:         m_detune     = std::clamp(value, 0.0f, 1.0f); break;
            case kAttack:         m_attack     = std::clamp(value, 0.005f, 3.0f);
                                  updateEnvelopes(); break;
            case kRelease:        m_release    = std::clamp(value, 0.05f, 5.0f);
                                  updateEnvelopes(); break;
            case kBrightness:     m_brightness = std::clamp(value, 0.0f, 1.0f); break;
            case kEnsembleDepth:  m_ensembleDepth = std::clamp(value, 0.0f, 1.0f); break;
            case kEnsembleRate:   m_ensembleRate  = std::clamp(value, 0.1f, 2.0f); break;
            case kEnsembleOn:     m_ensembleOn = value >= 0.5f; break;
            case kVolume:         m_volume     = std::clamp(value, 0.0f, 1.0f); break;
        }
    }

private:
    struct Voice {
        bool      active = false;
        uint8_t   note = 0;
        uint8_t   channel = 0;
        float     velocity = 0.0f;
        int64_t   startOrder = 0;
        // 9 saws: laid out as [octave * kSawsPerOctave + sawIdx],
        // octave 0 = 16′, 1 = 8′, 2 = 4′.
        Oscillator saws[kOscsPerVoice];
        Envelope   ampEnv;
    };

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int  findFreeVoice();
    void updateEnvelopes();
    void applyDefaults();

    // ── Per-voice ───────────────────────────────────────────────────
    Voice    m_voices[kMaxVoices];
    int64_t  m_voiceCounter = 0;
    float    m_pitchBend = 0.0f;

    // ── Shared post-mix DSP ─────────────────────────────────────────
    // Single-pole LP smoothing — see brightnessNormToHz().
    float    m_filterLow = 0.0f, m_filterBand = 0.0f;

    // 3-tap BBD-style chorus. Single shared write buffer; each tap
    // reads at its own LFO-modulated delay length. Stereo split
    // happens at output by panning tap1 → L, tap3 → R, tap2 centre.
    float    m_chorusBuf[kChorusBufFrames] = {};
    int      m_chorusWrite = 0;
    double   m_chorusPhase = 0.0;   // 0..1 LFO phase

    // ── Parameters ──────────────────────────────────────────────────
    float m_oct16Level = 0.0f, m_oct8Level = 0.8f, m_oct4Level = 0.5f;
    float m_detune     = 0.5f;
    float m_attack     = 0.3f, m_release = 0.8f;
    float m_brightness = 0.5f;
    float m_ensembleDepth = 0.6f, m_ensembleRate = 0.6f;
    bool  m_ensembleOn  = true;
    float m_volume     = 0.5f;
};

} // namespace instruments
} // namespace yawn
