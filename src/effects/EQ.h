#pragma once

// EQ — 3-band parametric equalizer (low shelf, mid peak, high shelf).

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"

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
        static const ParameterInfo infos[] = {
            {"Low Freq",  20.0f, 2000.0f,  200.0f,  "Hz", false},
            {"Low Gain",  -24.0f, 24.0f,   0.0f,    "dB", false, false, WidgetHint::DentedKnob},
            {"Mid Freq",  100.0f, 10000.0f, 1000.0f, "Hz", false},
            {"Mid Gain",  -24.0f, 24.0f,   0.0f,    "dB", false, false, WidgetHint::DentedKnob},
            {"Mid Q",     0.1f,  10.0f,    1.0f,    "",   false, false, WidgetHint::DentedKnob},
            {"High Freq", 2000.0f, 20000.0f, 5000.0f, "Hz", false},
            {"High Gain", -24.0f, 24.0f,   0.0f,    "dB", false, false, WidgetHint::DentedKnob},
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
        m_lowL[0].compute(Biquad::Type::LowShelf,  m_sampleRate, m_params[kLowFreq],  m_params[kLowGain],  0.707);
        m_lowR[0].compute(Biquad::Type::LowShelf,  m_sampleRate, m_params[kLowFreq],  m_params[kLowGain],  0.707);
        m_midL[0].compute(Biquad::Type::Peak,       m_sampleRate, m_params[kMidFreq],  m_params[kMidGain],  m_params[kMidQ]);
        m_midR[0].compute(Biquad::Type::Peak,       m_sampleRate, m_params[kMidFreq],  m_params[kMidGain],  m_params[kMidQ]);
        m_highL[0].compute(Biquad::Type::HighShelf, m_sampleRate, m_params[kHighFreq], m_params[kHighGain], 0.707);
        m_highR[0].compute(Biquad::Type::HighShelf, m_sampleRate, m_params[kHighFreq], m_params[kHighGain], 0.707);
    }

    Biquad m_lowL[1], m_lowR[1];
    Biquad m_midL[1], m_midR[1];
    Biquad m_highL[1], m_highR[1];

    float m_params[kParamCount] = {200.0f, 0.0f, 1000.0f, 0.0f, 1.0f, 5000.0f, 0.0f};
};

} // namespace effects
} // namespace yawn
