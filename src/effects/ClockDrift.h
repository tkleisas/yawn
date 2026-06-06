// Clock Drift — a wandering sample clock. The signal runs through a
// fractional delay whose length slowly random-walks; because pitch
// tracks the rate-of-change of delay, the output continuously drifts
// sharp/flat like a dying tape or a free-running clock losing lock.
//   Depth  — how far the delay wanders (→ max pitch deviation)
//   Rate   — how fast it picks new targets (wander speed)
//   Jitter — smooth wow (0) ↔ erratic random walk (1)
#pragma once

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yawn {
namespace effects {

class ClockDrift : public AudioEffect {
public:
    enum Param { kDepth, kRate, kJitter, kMix, kParamCount };

    const char* name() const override { return "Clock Drift"; }
    const char* id()   const override { return "clockdrift"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // Base delay 50 ms + up to ~45 ms of drift either way.
        m_baseDelay = static_cast<float>(sampleRate * 0.050);
        m_maxDrift  = static_cast<float>(sampleRate * 0.045);
        m_bufLen = std::max(4, static_cast<int>(sampleRate * 0.2));
        m_bufL.assign(m_bufLen, 0.0f);
        m_bufR.assign(m_bufLen, 0.0f);
        reset();
    }

    void reset() override {
        std::fill(m_bufL.begin(), m_bufL.end(), 0.0f);
        std::fill(m_bufR.begin(), m_bufR.end(), 0.0f);
        m_writePos = 0;
        m_drift = 0.0f;
        m_target = 0.0f;
        m_phase = 0.0f;
        m_counter = 0;
        m_rng = 0x51ED2701u;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed || m_bufLen <= 0) return;
        const bool  stereo = numChannels > 1;
        const float depth  = std::clamp(m_params[kDepth],  0.0f, 1.0f);
        const float rateHz = std::clamp(m_params[kRate],   0.01f, 10.0f);
        const float jitter = std::clamp(m_params[kJitter], 0.0f, 1.0f);
        const float mix    = std::clamp(m_params[kMix], 0.0f, 1.0f) * m_mix;

        // Re-target interval (samples) from Rate; smoothing coefficient
        // so the drift glides toward the target over roughly that span.
        const int   period = std::max(1, static_cast<int>(m_sampleRate / rateHz));
        const float smooth = 1.0f - std::exp(-1.0f / static_cast<float>(period));
        const float lfoInc = static_cast<float>(
            2.0 * 3.14159265358979 * rateHz / m_sampleRate);

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            m_bufL[m_writePos] = dryL;
            m_bufR[m_writePos] = dryR;

            // Pick a fresh random target every `period` samples (jittered)
            // and a smooth sine component; Jitter crossfades between them.
            if (++m_counter >= period) {
                m_counter = 0;
                m_target  = (randf() * 2.0f - 1.0f) * depth * m_maxDrift;
            }
            m_phase += lfoInc;
            if (m_phase > 6.2831853f) m_phase -= 6.2831853f;
            const float sine = std::sin(m_phase) * depth * m_maxDrift;
            const float goal = sine * (1.0f - jitter) + m_target * jitter;
            m_drift += (goal - m_drift) * smooth;

            // Fractional read at writePos - (base + drift).
            float rp = static_cast<float>(m_writePos) - (m_baseDelay + m_drift);
            while (rp < 0.0f) rp += m_bufLen;
            while (rp >= m_bufLen) rp -= m_bufLen;
            const int   r0 = static_cast<int>(rp);
            const float fr = rp - r0;
            int r1 = r0 + 1; if (r1 >= m_bufLen) r1 -= m_bufLen;
            const float wetL = m_bufL[r0] + (m_bufL[r1] - m_bufL[r0]) * fr;
            const float wetR = m_bufR[r0] + (m_bufR[r1] - m_bufR[r0]) * fr;

            buffer[i * numChannels] = dryL * (1.0f - mix) + wetL * mix;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * (1.0f - mix) + wetR * mix;

            if (++m_writePos >= m_bufLen) m_writePos = 0;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"Depth",  0.0f,  1.0f, 0.4f, "",   false, false, WidgetHint::Knob},
            {"Rate",   0.01f,10.0f, 0.5f, "Hz", false, false, WidgetHint::Knob},
            {"Jitter", 0.0f,  1.0f, 0.3f, "",   false, false, WidgetHint::Knob},
            {"Mix",    0.0f,  1.0f, 1.0f, "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int i) const override {
        return (i >= 0 && i < kParamCount) ? m_params[i] : 0.0f;
    }
    void setParameter(int i, float v) override {
        if (i < 0 || i >= kParamCount) return;
        const auto& pi = parameterInfo(i);
        m_params[i] = std::clamp(v, pi.minValue, pi.maxValue);
    }

private:
    float randf() {
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        return static_cast<float>(m_rng & 0xFFFFFF) / static_cast<float>(0x1000000);
    }

    float m_params[kParamCount] = {0.4f, 0.5f, 0.3f, 1.0f};
    std::vector<float> m_bufL, m_bufR;
    int    m_bufLen = 0, m_writePos = 0, m_counter = 0;
    float  m_baseDelay = 0.0f, m_maxDrift = 0.0f;
    float  m_drift = 0.0f, m_target = 0.0f, m_phase = 0.0f;
    uint32_t m_rng = 0x51ED2701u;
};

} // namespace effects
} // namespace yawn
