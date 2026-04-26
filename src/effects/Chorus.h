#pragma once

// Chorus — modulated delay for stereo thickening.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class Chorus : public AudioEffect {
public:
    enum Param { kRate, kDepth, kWetDry, kVoices, kParamCount };

    const char* name() const override { return "Chorus"; }
    const char* id()   const override { return "chorus"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Rate",    0.1f, 5.0f, 1.0f, "Hz", false},
            {"Depth",   0.0f, 1.0f, 0.5f, "",   false, false, WidgetHint::DentedKnob},
            {"Wet/Dry", 0.0f, 1.0f, 0.5f, "",   false, false, WidgetHint::DentedKnob},
            {"Voices",  1.0f, 4.0f, 2.0f, "",   false, false, WidgetHint::StepSelector},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount)
            m_params[index] = value;
    }

private:
    float readInterp(const std::vector<float>& buf, float delaySamples) const {
        float readPos = static_cast<float>(m_writePos) - delaySamples;
        if (readPos < 0.0f) readPos += m_maxDelay;
        int idx0 = static_cast<int>(readPos) % m_maxDelay;
        int idx1 = (idx0 + 1) % m_maxDelay;
        float frac = readPos - std::floor(readPos);
        return buf[idx0] * (1.0f - frac) + buf[idx1] * frac;
    }

    std::vector<float> m_bufferL, m_bufferR;
    int m_maxDelay = 0, m_writePos = 0;
    double m_phase = 0.0;
    float m_params[kParamCount] = {1.0f, 0.5f, 0.5f, 2.0f};
};

} // namespace effects
} // namespace yawn
