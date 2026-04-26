#pragma once

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "instruments/Oscillator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

// Classic analog-style subtractive synthesizer.
// 2 oscillators + sub + noise → SVF filter → amp envelope → output.
// 16-voice polyphony with voice stealing.
class SubtractiveSynth : public Instrument {
public:
    static constexpr int kMaxVoices = 16;

    enum Params {
        kOsc1Wave = 0, kOsc1Level,
        kOsc2Wave, kOsc2Level, kOsc2Detune, kOsc2Octave,
        kSubLevel, kNoiseLevel,
        kFilterCutoff, kFilterResonance, kFilterType, kFilterEnvAmount,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kFiltAttack, kFiltDecay, kFiltSustain, kFiltRelease,
        kLFORate, kLFODepth,
        kVolume,
        kNumParams
    };

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return "Subtractive Synth"; }
    const char* id()   const override { return "subsynth"; }

    int parameterCount() const override { return kNumParams; }

    // Log map: normalized 0..1 → 20..20000 Hz (3 decades, 20×1000^x).
    static float cutoffNormToHz(float x) {
        return 20.0f * std::pow(1000.0f, std::clamp(x, 0.0f, 1.0f));
    }
    static float cutoffHzToNorm(float hz) {
        return std::log(std::max(hz, 20.0f) / 20.0f) / std::log(1000.0f);
    }
    static void formatCutoffHz(float v, char* buf, int n) {
        const float hz = cutoffNormToHz(v);
        if (hz >= 1000.0f) std::snprintf(buf, n, "%.1fk", hz / 1000.0f);
        else                std::snprintf(buf, n, "%.0f",  hz);
    }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kOscWaveLabels[] = {"Sine", "Tri", "Saw", "Sq", "Noise"};
        static constexpr const char* kFilterTypeLabels[] = {"LP", "BP", "HP"};
        static const InstrumentParameterInfo p[kNumParams] = {
            {"Osc1 Wave",     0,4,1,"",false, false, WidgetHint::StepSelector, kOscWaveLabels, 5},
            {"Osc1 Level",    0,1,0.8f,"",false, false, WidgetHint::DentedKnob},
            {"Osc2 Wave",     0,4,1,"",false, false, WidgetHint::StepSelector, kOscWaveLabels, 5},
            {"Osc2 Level",    0,1,0,"",false},
            {"Osc2 Detune",  -1,1,0,"st",false, false, WidgetHint::DentedKnob},
            {"Osc2 Octave",  -2,2,0,"",false, false, WidgetHint::StepSelector},
            {"Sub Level",     0,1,0,"",false},
            {"Noise Level",   0,1,0,"",false},
            // Cutoff stored 0..1 for uniform modulation; displayed in Hz
            // via the custom formatter (log-mapped 20..20000).
            {"Filter Cutoff", 0.0f,1.0f,0.8f,"",false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Filter Reso",   0,1,0,"",false},
            {"Filter Type",   0,2,0,"",false, false, WidgetHint::StepSelector, kFilterTypeLabels, 3},
            {"Filter Env",   -1,1,0.3f,"",false, false, WidgetHint::DentedKnob},
            {"Amp Attack",    0.001f,5,0.01f,"s",false},
            {"Amp Decay",     0.001f,5,0.1f,"s",false},
            {"Amp Sustain",   0,1,0.7f,"",false, false, WidgetHint::DentedKnob},
            {"Amp Release",   0.001f,5,0.3f,"s",false},
            {"Filt Attack",   0.001f,5,0.01f,"s",false},
            {"Filt Decay",    0.001f,5,0.3f,"s",false},
            {"Filt Sustain",  0,1,0.3f,"",false, false, WidgetHint::DentedKnob},
            {"Filt Release",  0.001f,5,0.3f,"s",false},
            {"LFO Rate",      0.1f,20,2,"Hz",false},
            {"LFO Depth",     0,1,0,"",false},
            {"Volume",        0,1,0.7f,"",false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kOsc1Wave:       return m_osc1Wave;
            case kOsc1Level:      return m_osc1Level;
            case kOsc2Wave:       return m_osc2Wave;
            case kOsc2Level:      return m_osc2Level;
            case kOsc2Detune:     return m_osc2Detune;
            case kOsc2Octave:     return m_osc2Octave;
            case kSubLevel:       return m_subLevel;
            case kNoiseLevel:     return m_noiseLevel;
            case kFilterCutoff:   return m_filterCutoff;
            case kFilterResonance:return m_filterResonance;
            case kFilterType:     return m_filterType;
            case kFilterEnvAmount:return m_filterEnvAmount;
            case kAmpAttack:      return m_ampA;
            case kAmpDecay:       return m_ampD;
            case kAmpSustain:     return m_ampS;
            case kAmpRelease:     return m_ampR;
            case kFiltAttack:     return m_filtA;
            case kFiltDecay:      return m_filtD;
            case kFiltSustain:    return m_filtS;
            case kFiltRelease:    return m_filtR;
            case kLFORate:        return m_lfoRate;
            case kLFODepth:       return m_lfoDepth;
            case kVolume:         return m_volume;
            default:              return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kOsc1Wave:       m_osc1Wave = std::clamp(value, 0.0f, 4.0f); break;
            case kOsc1Level:      m_osc1Level = std::clamp(value, 0.0f, 1.0f); break;
            case kOsc2Wave:       m_osc2Wave = std::clamp(value, 0.0f, 4.0f); break;
            case kOsc2Level:      m_osc2Level = std::clamp(value, 0.0f, 1.0f); break;
            case kOsc2Detune:     m_osc2Detune = std::clamp(value, -1.0f, 1.0f); break;
            case kOsc2Octave:     m_osc2Octave = std::clamp(value, -2.0f, 2.0f); break;
            case kSubLevel:       m_subLevel = std::clamp(value, 0.0f, 1.0f); break;
            case kNoiseLevel:     m_noiseLevel = std::clamp(value, 0.0f, 1.0f); break;
            case kFilterCutoff:   m_filterCutoff = std::clamp(value, 0.0f, 1.0f); break;
            case kFilterResonance:m_filterResonance = std::clamp(value, 0.0f, 1.0f); break;
            case kFilterType:     m_filterType = std::clamp(value, 0.0f, 2.0f); break;
            case kFilterEnvAmount:m_filterEnvAmount = std::clamp(value, -1.0f, 1.0f); break;
            case kAmpAttack:      m_ampA = value; updateEnvelopes(); break;
            case kAmpDecay:       m_ampD = value; updateEnvelopes(); break;
            case kAmpSustain:     m_ampS = value; updateEnvelopes(); break;
            case kAmpRelease:     m_ampR = value; updateEnvelopes(); break;
            case kFiltAttack:     m_filtA = value; updateEnvelopes(); break;
            case kFiltDecay:      m_filtD = value; updateEnvelopes(); break;
            case kFiltSustain:    m_filtS = value; updateEnvelopes(); break;
            case kFiltRelease:    m_filtR = value; updateEnvelopes(); break;
            case kLFORate:        m_lfoRate = std::clamp(value, 0.1f, 20.0f); break;
            case kLFODepth:       m_lfoDepth = std::clamp(value, 0.0f, 1.0f); break;
            case kVolume:         m_volume = std::clamp(value, 0.0f, 1.0f); break;
        }
    }

private:
    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        Oscillator osc1, osc2, subOsc;
        Envelope ampEnv, filtEnv;
        float filterLow = 0.0f, filterBand = 0.0f;
    };

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int findFreeVoice();
    void updateEnvelopes();
    void applyDefaults();
    float noiseGen();

    Voice m_voices[kMaxVoices];
    int64_t m_voiceCounter = 0;
    float m_pitchBend = 0.0f;
    double m_lfoPhase = 0.0;
    uint32_t m_noiseRng = 54321;

    float m_osc1Wave = 1, m_osc1Level = 0.8f;
    float m_osc2Wave = 1, m_osc2Level = 0, m_osc2Detune = 0, m_osc2Octave = 0;
    float m_subLevel = 0, m_noiseLevel = 0;
    float m_filterCutoff = 0.8f;  // normalized 0..1 → 20..20000 Hz log-mapped
    float m_filterResonance = 0, m_filterType = 0;
    float m_filterEnvAmount = 0.3f;
    float m_ampA = 0.01f, m_ampD = 0.1f, m_ampS = 0.7f, m_ampR = 0.3f;
    float m_filtA = 0.01f, m_filtD = 0.3f, m_filtS = 0.3f, m_filtR = 0.3f;
    float m_lfoRate = 2.0f, m_lfoDepth = 0.0f;
    float m_volume = 0.7f;
};

} // namespace instruments
} // namespace yawn
