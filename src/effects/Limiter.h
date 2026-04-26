#pragma once

// Limiter — brick-wall peak limiter with small lookahead.
//
// Models the user-visible "catch cumulative overload" behavior on the
// master bus (or any chain): hard ceiling, fast attack, adjustable
// release. Uses a lookahead delay line so the envelope can pre-emptively
// duck before the peak arrives — prevents audible transient slicing
// that instantaneous limiters produce.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class Limiter : public AudioEffect {
public:
    enum Param { kCeiling, kRelease, kLookahead, kParamCount };

    // Max supported lookahead = 20 ms @ 192 kHz → 3840 samples, rounded
    // up so reset() never reallocates during processing.
    static constexpr int kMaxLookaheadSamples = 4096;

    const char* name() const override { return "Limiter"; }
    const char* id()   const override { return "limiter"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Ceiling",   -24.0f, 0.0f,   -0.3f, "dB", false},
            {"Release",   1.0f,   1000.0f, 50.0f, "ms", false},
            {"Lookahead", 0.0f,   20.0f,   5.0f,  "ms", false},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index == kRelease || index == kLookahead) updateCoeffs();
        }
    }

private:
    void updateCoeffs() {
        const float attackMs = std::max(1.0f, m_params[kLookahead]);
        const float releaseMs = std::max(1.0f, m_params[kRelease]);
        m_attackCoeff  = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * attackMs * 0.001f));
        m_releaseCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * releaseMs * 0.001f));
    }

    float              m_envLin = 1.0f;
    std::vector<float> m_delayBuf;       // interleaved stereo ring buffer
    int                m_writePos = 0;
    float              m_attackCoeff = 0.0f;
    float              m_releaseCoeff = 0.0f;
    float              m_params[kParamCount] = {-0.3f, 50.0f, 5.0f};
};

} // namespace effects
} // namespace yawn
