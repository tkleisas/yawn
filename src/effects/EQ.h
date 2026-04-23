#pragma once

// EQ — 3-band parametric equalizer (low shelf, mid peak, high shelf).

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include "effects/FreqMap.h"

namespace yawn {
namespace effects {

class EQ : public AudioEffect {
public:
    enum Param {
        kLowFreq, kLowGain,
        kMidFreq, kMidGain, kMidQ,
        kHighFreq, kHighGain,
        kParamCount
    };

    // Each band stores its frequency normalized 0..1 and log-maps to
    // its own band range for uniform modulation + perceptual feel.
    static constexpr float kLowMinHz  = 20.0f,   kLowMaxHz  = 2000.0f;
    static constexpr float kMidMinHz  = 100.0f,  kMidMaxHz  = 10000.0f;
    static constexpr float kHighMinHz = 2000.0f, kHighMaxHz = 20000.0f;

    static float lowFreqNormToHz(float x)  { return logNormToHz(x, kLowMinHz,  kLowMaxHz);  }
    static float midFreqNormToHz(float x)  { return logNormToHz(x, kMidMinHz,  kMidMaxHz);  }
    static float highFreqNormToHz(float x) { return logNormToHz(x, kHighMinHz, kHighMaxHz); }
    static float lowFreqHzToNorm(float hz)  { return logHzToNorm(hz, kLowMinHz,  kLowMaxHz);  }
    static float midFreqHzToNorm(float hz)  { return logHzToNorm(hz, kMidMinHz,  kMidMaxHz);  }
    static float highFreqHzToNorm(float hz) { return logHzToNorm(hz, kHighMinHz, kHighMaxHz); }
    static void formatLowFreqHz(float v, char* b, int n)  { formatHz(lowFreqNormToHz(v),  b, n); }
    static void formatMidFreqHz(float v, char* b, int n)  { formatHz(midFreqNormToHz(v),  b, n); }
    static void formatHighFreqHz(float v, char* b, int n) { formatHz(highFreqNormToHz(v), b, n); }

    const char* name() const override { return "EQ"; }
    const char* id()   const override { return "eq"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        updateFilters();
        reset();
    }

    void reset() override {
        for (auto& f : m_lowL)  f.reset(); for (auto& f : m_lowR)  f.reset();
        for (auto& f : m_midL)  f.reset(); for (auto& f : m_midR)  f.reset();
        for (auto& f : m_highL) f.reset(); for (auto& f : m_highR) f.reset();
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        for (int i = 0; i < numFrames; ++i) {
            float L = buffer[i * numChannels];
            float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

            L = m_lowL[0].process(L);  R = m_lowR[0].process(R);
            L = m_midL[0].process(L);  R = m_midR[0].process(R);
            L = m_highL[0].process(L); R = m_highR[0].process(R);

            buffer[i * numChannels] = L;
            if (numChannels > 1) buffer[i * numChannels + 1] = R;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        // Frequency defaults chosen so the normalized 0..1 values map to
        // the same Hz as before: Low 200 → 0.5, Mid 1000 → 0.5, High 5000 → 0.398.
        static const ParameterInfo infos[] = {
            {"Low Freq",  0.0f, 1.0f,     0.5f,    "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatLowFreqHz},
            {"Low Gain",  -24.0f, 24.0f,  0.0f,    "dB", false, false, WidgetHint::DentedKnob},
            {"Mid Freq",  0.0f, 1.0f,     0.5f,    "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatMidFreqHz},
            {"Mid Gain",  -24.0f, 24.0f,  0.0f,    "dB", false, false, WidgetHint::DentedKnob},
            {"Mid Q",     0.1f,  10.0f,   1.0f,    "",   false, false, WidgetHint::DentedKnob},
            {"High Freq", 0.0f, 1.0f,     0.398f,  "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatHighFreqHz},
            {"High Gain", -24.0f, 24.0f,  0.0f,    "dB", false, false, WidgetHint::DentedKnob},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            updateFilters();
        }
    }

private:
    void updateFilters() {
        const float lowHz  = lowFreqNormToHz (m_params[kLowFreq]);
        const float midHz  = midFreqNormToHz (m_params[kMidFreq]);
        const float highHz = highFreqNormToHz(m_params[kHighFreq]);
        m_lowL[0].compute(Biquad::Type::LowShelf,  m_sampleRate, lowHz,  m_params[kLowGain],  0.707);
        m_lowR[0].compute(Biquad::Type::LowShelf,  m_sampleRate, lowHz,  m_params[kLowGain],  0.707);
        m_midL[0].compute(Biquad::Type::Peak,       m_sampleRate, midHz,  m_params[kMidGain],  m_params[kMidQ]);
        m_midR[0].compute(Biquad::Type::Peak,       m_sampleRate, midHz,  m_params[kMidGain],  m_params[kMidQ]);
        m_highL[0].compute(Biquad::Type::HighShelf, m_sampleRate, highHz, m_params[kHighGain], 0.707);
        m_highR[0].compute(Biquad::Type::HighShelf, m_sampleRate, highHz, m_params[kHighGain], 0.707);
    }

    Biquad m_lowL[1], m_lowR[1];
    Biquad m_midL[1], m_midR[1];
    Biquad m_highL[1], m_highR[1];

    // Initial values match the defaults above.
    float m_params[kParamCount] = {0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.398f, 0.0f};
};

} // namespace effects
} // namespace yawn
