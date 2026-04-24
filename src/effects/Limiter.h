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

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_delayBuf.assign(kMaxLookaheadSamples * 2, 0.0f);  // stereo
        updateCoeffs();
        reset();
    }

    void reset() override {
        m_envLin = 1.0f;  // no reduction at start
        m_writePos = 0;
        std::fill(m_delayBuf.begin(), m_delayBuf.end(), 0.0f);
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        const float ceilingLin = std::pow(10.0f, m_params[kCeiling] / 20.0f);
        // Lookahead in samples, clamped to the ring size. Attack
        // coefficient is derived from lookahead: one-pole smoothing with
        // τ ≈ lookahead time so the envelope reaches the target just as
        // the peak emerges from the delay line.
        const int lookSamples = std::min(
            static_cast<int>(m_params[kLookahead] * 0.001f * m_sampleRate),
            kMaxLookaheadSamples - 1);

        const int delayMask = kMaxLookaheadSamples;  // capacity per channel
        for (int i = 0; i < numFrames; ++i) {
            const float inL = buffer[i * numChannels];
            const float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;

            // Peak of the incoming (future) sample drives the envelope.
            // Target gain = min(1, ceiling / peak). Envelope follower
            // attacks fast toward lower gain, releases slowly toward 1.
            const float peak = std::max(std::abs(inL), std::abs(inR));
            const float target = (peak > ceilingLin)
                ? ceilingLin / peak : 1.0f;

            if (target < m_envLin) {
                // Attack: move quickly toward the limiting gain.
                m_envLin = m_attackCoeff * m_envLin + (1.0f - m_attackCoeff) * target;
            } else {
                m_envLin = m_releaseCoeff * m_envLin + (1.0f - m_releaseCoeff) * target;
            }

            // Apply the envelope to the DELAYED sample so the ducking
            // aligns with the peak. With zero lookahead this is just
            // instantaneous limiting.
            const int readPos = (m_writePos - lookSamples + delayMask) % delayMask;
            const float outL = m_delayBuf[readPos * 2 + 0] * m_envLin;
            const float outR = m_delayBuf[readPos * 2 + 1] * m_envLin;

            // Hard ceiling as a safety net — if the envelope hasn't
            // caught a peak, clamp here so nothing slips above the
            // ceiling. Shouldn't engage in normal operation.
            buffer[i * numChannels] = std::clamp(outL, -ceilingLin, ceilingLin);
            if (numChannels > 1)
                buffer[i * numChannels + 1] = std::clamp(outR, -ceilingLin, ceilingLin);

            // Write the incoming sample into the delay line.
            m_delayBuf[m_writePos * 2 + 0] = inL;
            m_delayBuf[m_writePos * 2 + 1] = inR;
            m_writePos = (m_writePos + 1) % delayMask;
        }
    }

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
        // Attack time is derived from lookahead: we want the envelope
        // to reach the target within the lookahead window so ducking
        // aligns with the peak. With zero lookahead, fall back to ~1ms
        // so we still react fast.
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
