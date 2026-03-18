#pragma once

// Filter — multi-mode biquad filter (LP, HP, BP, Notch).

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"

namespace yawn {
namespace effects {

class Filter : public AudioEffect {
public:
    enum Param { kCutoff, kResonance, kType, kParamCount };
    enum FilterType { LP = 0, HP = 1, BP = 2, Notch = 3 };

    const char* name() const override { return "Filter"; }
    const char* id()   const override { return "filter"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        updateFilter();
        reset();
    }

    void reset() override { m_filterL.reset(); m_filterR.reset(); }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        for (int i = 0; i < numFrames; ++i) {
            buffer[i * numChannels] = m_filterL.process(buffer[i * numChannels]);
            if (numChannels > 1)
                buffer[i * numChannels + 1] = m_filterR.process(buffer[i * numChannels + 1]);
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Cutoff",    20.0f, 20000.0f, 1000.0f, "Hz", false},
            {"Resonance", 0.1f,  20.0f,    0.707f,  "",   false},
            {"Type",      0.0f,  3.0f,     0.0f,    "",   false},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            updateFilter();
        }
    }

private:
    void updateFilter() {
        Biquad::Type bt;
        int t = static_cast<int>(m_params[kType]);
        switch (t) {
            case HP:    bt = Biquad::Type::HighPass; break;
            case BP:    bt = Biquad::Type::BandPass; break;
            case Notch: bt = Biquad::Type::Notch;    break;
            default:    bt = Biquad::Type::LowPass;  break;
        }
        m_filterL.compute(bt, m_sampleRate, m_params[kCutoff], 0.0, m_params[kResonance]);
        m_filterR.compute(bt, m_sampleRate, m_params[kCutoff], 0.0, m_params[kResonance]);
    }

    Biquad m_filterL, m_filterR;
    float m_params[kParamCount] = {1000.0f, 0.707f, 0.0f};
};

} // namespace effects
} // namespace yawn
