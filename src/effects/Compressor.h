#pragma once

// Compressor — dynamics compressor with peak envelope follower.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class Compressor : public AudioEffect {
public:
    enum Param { kThreshold, kRatio, kAttack, kRelease, kMakeupGain, kKnee, kParamCount };

    const char* name() const override { return "Compressor"; }
    const char* id()   const override { return "compressor"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Threshold",   -60.0f, 0.0f,    -20.0f, "dB", false},
            {"Ratio",       1.0f,   20.0f,   4.0f,   ":1", false, false, WidgetHint::DentedKnob},
            {"Attack",      0.1f,   100.0f,  10.0f,  "ms", false},
            {"Release",     10.0f,  1000.0f, 100.0f, "ms", false},
            {"Makeup Gain", 0.0f,   24.0f,   0.0f,   "dB", false},
            {"Knee",        0.0f,   12.0f,   6.0f,   "dB", false, false, WidgetHint::DentedKnob},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index == kAttack || index == kRelease) updateCoeffs();
        }
    }

private:
    void updateCoeffs() {
        float attackMs  = m_params[kAttack];
        float releaseMs = m_params[kRelease];
        m_attackCoeff  = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * attackMs * 0.001f));
        m_releaseCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * releaseMs * 0.001f));
    }

    float m_envDB = -96.0f;
    float m_attackCoeff = 0.0f, m_releaseCoeff = 0.0f;
    float m_params[kParamCount] = {-20.0f, 4.0f, 10.0f, 100.0f, 0.0f, 6.0f};
};

} // namespace effects
} // namespace yawn
