#pragma once

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "effects/FreqMap.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace yawn {
namespace instruments {

// Simple sample playback instrument with pitch tracking, ADSR, and SVF filter.
// 16-voice polyphony. Maps a single sample across the keyboard.
class Sampler : public Instrument {
public:
    static constexpr int kMaxVoices = 16;

    enum Params {
        kRootNote = 0, kAttack, kDecay, kSustain, kRelease,
        kFilterCutoff, kFilterResonance, kVolume,
        kLoopStart, kLoopEnd, kReverse, kSampleGain,
        kNumParams
    };

    // Cutoff stored normalized 0..1, log-mapped to 20..20000 Hz.
    static float cutoffNormToHz(float x) { return effects::logNormToHz(x, 20.0f, 20000.0f); }
    static float cutoffHzToNorm(float hz) { return effects::logHzToNorm(hz, 20.0f, 20000.0f); }
    static void  formatCutoffHz(float v, char* buf, int n) {
        effects::formatHz(cutoffNormToHz(v), buf, n);
    }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    // Load sample data (copies into internal buffer).
    void loadSample(const float* data, int numFrames, int numChannels,
                    int rootNote = 60);
    void clearSample();

    bool hasSample() const { return m_sampleFrames > 0; }
    const float* sampleData() const { return m_sampleData.data(); }
    int sampleFrames() const { return m_sampleFrames; }
    int sampleChannels() const { return m_sampleChannels; }

    // Normalized playhead position (0..1) of the most recently triggered voice
    float playheadPosition() const {
        int best = -1;
        int64_t bestOrder = -1;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (m_voices[i].active && m_voices[i].startOrder > bestOrder) {
                best = i; bestOrder = m_voices[i].startOrder;
            }
        }
        if (best < 0 || m_sampleFrames <= 0) return 0.0f;
        return std::clamp(static_cast<float>(m_voices[best].playPos / m_sampleFrames), 0.0f, 1.0f);
    }

    bool isPlaying() const {
        for (int i = 0; i < kMaxVoices; ++i)
            if (m_voices[i].active) return true;
        return false;
    }

    const char* name() const override { return "Sampler"; }
    const char* id()   const override { return "sampler"; }

    int parameterCount() const override { return kNumParams; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo p[kNumParams] = {
            {"Root Note",   0, 127, 60, "", false, false, WidgetHint::StepSelector},
            {"Attack",      0.001f, 5, 0.005f, "s", false},
            {"Decay",       0.001f, 5, 0.1f, "s", false},
            {"Sustain",     0, 1, 1, "", false, false, WidgetHint::DentedKnob},
            {"Release",     0.001f, 5, 0.1f, "s", false},
            {"Filter Cut",  0.0f, 1.0f, 1.0f, "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Filter Reso", 0, 1, 0, "", false},
            {"Volume",      0, 1, 0.8f, "", false, false, WidgetHint::DentedKnob},
            {"Loop Start",  0, 1, 0, "", false},
            {"Loop End",    0, 1, 1, "", false},
            {"Reverse",     0, 1, 0, "", true},
            {"Sample Gain", 0, 2, 1, "x", false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kRootNote:        return (float)m_rootNote;
            case kAttack:          return m_attack;
            case kDecay:           return m_decay;
            case kSustain:         return m_sustain;
            case kRelease:         return m_release;
            case kFilterCutoff:    return m_filterCutoff;
            case kFilterResonance: return m_filterResonance;
            case kVolume:          return m_volume;
            case kLoopStart:       return m_loopStart;
            case kLoopEnd:         return m_loopEnd;
            case kReverse:         return m_reverse ? 1.0f : 0.0f;
            case kSampleGain:      return m_sampleGain;
            default:               return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kRootNote:        m_rootNote = std::clamp((int)value, 0, 127); break;
            case kAttack:          m_attack = value; break;
            case kDecay:           m_decay = value; break;
            case kSustain:         m_sustain = std::clamp(value, 0.0f, 1.0f); break;
            case kRelease:         m_release = value; break;
            case kFilterCutoff:    m_filterCutoff = std::clamp(value, 0.0f, 1.0f); break;
            case kFilterResonance: m_filterResonance = std::clamp(value, 0.0f, 1.0f); break;
            case kVolume:          m_volume = std::clamp(value, 0.0f, 1.0f); break;
            case kLoopStart:       m_loopStart = std::clamp(value, 0.0f, m_loopEnd - 0.001f); break;
            case kLoopEnd:         m_loopEnd = std::clamp(value, m_loopStart + 0.001f, 1.0f); break;
            case kReverse:         m_reverse = value > 0.5f; break;
            case kSampleGain:      m_sampleGain = std::clamp(value, 0.0f, 2.0f); break;
        }
    }

private:
    struct Voice {
        bool active = false;
        uint8_t note = 0, channel = 0;
        float velocity = 0.0f;
        double playPos = 0.0, playSpeed = 1.0;
        int64_t startOrder = 0;
        Envelope env;
        float filterLow = 0.0f, filterBand = 0.0f;
    };

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int findFreeVoice();
    void applyDefaults();

    Voice m_voices[kMaxVoices];
    int64_t m_voiceCounter = 0;

    std::vector<float> m_sampleData;
    int m_sampleFrames    = 0;
    int m_sampleChannels  = 1;
    int m_rootNote        = 60;

    float m_attack = 0.005f, m_decay = 0.1f, m_sustain = 1.0f, m_release = 0.1f;
    float m_filterCutoff = 1.0f, m_filterResonance = 0.0f;
    float m_volume = 0.8f;
    float m_loopStart = 0.0f, m_loopEnd = 1.0f;
    bool  m_reverse = false;
    float m_sampleGain = 1.0f;
};

} // namespace instruments
} // namespace yawn
