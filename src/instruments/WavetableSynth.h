#pragma once

// WavetableSynth — wavetable oscillator synthesizer.
//
// Reads through a set of single-cycle waveforms (frames) stored in a wavetable.
// The Position knob selects which frame(s) to read, with linear crossfade
// between adjacent frames for smooth morphing.
//
// Built-in algorithmic wavetables:
//   0 Basic     — Sine → Tri → Saw → Square → Pulse (5 frames)
//   1 PWM       — Pulse width modulation sweep (16 frames)
//   2 Formant   — Vocal-like formant sweep (16 frames)
//   3 Harmonic  — Additive harmonics buildup (16 frames)
//   4 Digital   — Bitcrushed/digital textures (8 frames)
//
// Features:
// - Wavetable position knob with LFO modulation
// - SVF filter with ADSR envelope
// - Amp ADSR envelope
// - 16-voice polyphony with voice stealing
// - Sub oscillator (one octave down, sine)
// - Unison detune (2 detuned copies per voice)
//
// All waveform data is generated in init().  No allocations in process().

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "effects/FreqMap.h"
#include <algorithm>
#include <memory>

namespace yawn {
namespace instruments {

class WavetableSynth : public Instrument {
public:
    static constexpr int kMaxVoices   = 16;
    static constexpr int kFrameSize   = 256;     // samples per waveform frame
    static constexpr int kMaxFrames   = 16;      // max frames per wavetable
    static constexpr int kNumTables   = 5;       // number of built-in wavetables

    // Cutoff stored normalized 0..1, log-mapped to 20..20000 Hz.
    static float cutoffNormToHz(float x) { return effects::logNormToHz(x, 20.0f, 20000.0f); }
    static float cutoffHzToNorm(float hz) { return effects::logHzToNorm(hz, 20.0f, 20000.0f); }
    static void  formatCutoffHz(float v, char* buf, int n) {
        effects::formatHz(cutoffNormToHz(v), buf, n);
    }

    enum Param {
        kTable,           // 0–4: wavetable selection
        kPosition,        // 0–1: position within wavetable (frame morph)
        kFilterCutoff,    // 0..1 normalized → 20..20000 Hz (log)
        kFilterResonance, // 0–1
        kFilterEnvAmount, // -1 to +1
        kAmpAttack,       // 0.001–5s
        kAmpDecay,        // 0.001–5s
        kAmpSustain,      // 0–1
        kAmpRelease,      // 0.001–5s
        kFiltAttack,      // 0.001–5s
        kFiltDecay,       // 0.001–5s
        kFiltSustain,     // 0–1
        kFiltRelease,     // 0.001–5s
        kLFORate,         // 0.1–20 Hz
        kLFOAmount,       // 0–1: LFO → position modulation
        kSubLevel,        // 0–1
        kUnisonDetune,    // 0–1: detune spread in semitones
        kVolume,          // 0–1
        kParamCount
    };

    const char* name() const override { return "Wavetable Synth"; }
    const char* id()   const override { return "wavetable"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    // Wavetable data accessors for UI display
    int currentTable() const { return std::clamp(static_cast<int>(m_params[kTable]), 0, kNumTables - 1); }
    int frameCount(int table) const {
        if (table < 0 || table >= kNumTables) return 0;
        return m_tableFrameCounts[table];
    }
    int frameSize() const { return kFrameSize; }
    const float* frameData(int table, int frame) const {
        if (!m_tableData || table < 0 || table >= kNumTables) return nullptr;
        if (frame < 0 || frame >= m_tableFrameCounts[table]) return nullptr;
        return m_tableData->tables[table][frame];
    }

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kTableLabels[] = {"Basic", "Saw", "Sqr", "Pulse", "Noise"};
        static const InstrumentParameterInfo infos[] = {
            {"Table",       0.0f,  4.0f,   0.0f,  "",   false, false, WidgetHint::StepSelector, kTableLabels, 5},
            {"Position",    0.0f,  1.0f,   0.0f,  "",   false},
            // Cutoff default 0.799 ≈ 5 kHz in 20..20000
            {"Filter Cut",  0.0f, 1.0f, 0.799f, "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Filter Res",  0.0f,  1.0f,   0.0f,  "",   false},
            {"Filter Env", -1.0f,  1.0f,   0.3f,  "",   false, false, WidgetHint::DentedKnob},
            {"Amp Atk",     0.001f, 5.0f,  0.01f, "s",  false},
            {"Amp Dec",     0.001f, 5.0f,  0.1f,  "s",  false},
            {"Amp Sus",     0.0f,  1.0f,   0.7f,  "",   false, false, WidgetHint::DentedKnob},
            {"Amp Rel",     0.001f, 5.0f,  0.3f,  "s",  false},
            {"Filt Atk",    0.001f, 5.0f,  0.01f, "s",  false},
            {"Filt Dec",    0.001f, 5.0f,  0.3f,  "s",  false},
            {"Filt Sus",    0.0f,  1.0f,   0.3f,  "",   false, false, WidgetHint::DentedKnob},
            {"Filt Rel",    0.001f, 5.0f,  0.3f,  "s",  false},
            {"LFO Rate",    0.1f,  20.0f,  2.0f,  "Hz", false},
            {"LFO Amt",     0.0f,  1.0f,   0.0f,  "",   false},
            {"Sub Level",   0.0f,  1.0f,   0.0f,  "",   false},
            {"Unison Det",  0.0f,  1.0f,   0.0f,  "st", false},
            {"Volume",      0.0f,  1.0f,   0.7f,  "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index >= 0 && index < kParamCount) return m_params[index];
        return 0.0f;
    }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index >= kAmpAttack && index <= kFiltRelease)
                updateEnvelopes();
        }
    }

private:
    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        double phase = 0.0, phase2 = 0.0, subPhase = 0.0;
        float filterLow = 0.0f, filterBand = 0.0f;
        Envelope ampEnv, filtEnv;
    };

    // Wavetable storage (heap-allocated to avoid stack overflow)
    struct TableData {
        float tables[kNumTables][kMaxFrames][kFrameSize] = {};
    };

    void generateWavetables();
    float readWavetable(int table, int numFrames, double phase, float position) const;
    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int findFreeVoice();
    void updateEnvelopes();
    void applyDefaults();

    std::unique_ptr<TableData> m_tableData;
    int   m_tableFrameCounts[kNumTables] = {};

    Voice m_voices[kMaxVoices];
    int64_t m_voiceCounter = 0;
    float m_pitchBend = 0.0f;
    double m_lfoPhase = 0.0;

    float m_params[kParamCount] = {};
};

} // namespace instruments
} // namespace yawn
