#pragma once
// CD Error — emulates a scratched/dying CD: most of the time the signal
// passes through, but at a Rate-controlled density it drops into a random
// glitch event — a chunk STUTTER (loop a slice), a STUCK micro-loop (the
// classic skip), or a MUTE (dropout). Severity scales how long/violent
// the events are. Free-running (no tempo).

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yawn {
namespace effects {

class CDError : public AudioEffect {
public:
    enum Param { kRate, kSize, kSeverity, kMix, kParamCount };

    const char* name() const override { return "CD Error"; }
    const char* id()   const override { return "cderror"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_bufLen = std::max(4, static_cast<int>(sampleRate * 0.5));  // 500 ms
        m_ringL.assign(m_bufLen, 0.0f);
        m_ringR.assign(m_bufLen, 0.0f);
        m_grainL.assign(m_bufLen, 0.0f);
        m_grainR.assign(m_bufLen, 0.0f);
        reset();
    }

    void reset() override {
        std::fill(m_ringL.begin(), m_ringL.end(), 0.0f);
        std::fill(m_ringR.begin(), m_ringR.end(), 0.0f);
        m_writePos = 0;
        m_state = Idle;
        m_grainLen = 0;
        m_grainPos = 0;
        m_eventRemaining = 0;
        m_rng = 0xB16B00B5u;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed || m_bufLen <= 0) return;
        const bool  stereo  = numChannels > 1;
        const float rate    = std::clamp(m_params[kRate], 0.0f, 20.0f);   // events/sec
        const float sizeMs  = std::clamp(m_params[kSize], 5.0f, 300.0f);
        const float severity= std::clamp(m_params[kSeverity], 0.0f, 1.0f);
        const float mix     = std::clamp(m_params[kMix], 0.0f, 1.0f) * m_mix;

        // Per-sample probability of starting an event.
        const float pStart = rate / static_cast<float>(m_sampleRate);
        const int   sizeSamp = std::clamp(
            static_cast<int>(sizeMs * 0.001f * m_sampleRate), 1, m_bufLen);

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            m_ringL[m_writePos] = dryL;
            m_ringR[m_writePos] = dryR;

            if (m_state == Idle && randf() < pStart)
                startEvent(sizeSamp, severity);

            float wetL = dryL, wetR = dryR;
            switch (m_state) {
                case Idle:
                    break;
                case Mute:
                    wetL = 0.0f; wetR = 0.0f;
                    if (--m_eventRemaining <= 0) m_state = Idle;
                    break;
                case Stutter:
                case Stuck:
                    if (m_grainLen > 0) {
                        wetL = m_grainL[m_grainPos];
                        wetR = m_grainR[m_grainPos];
                        if (++m_grainPos >= m_grainLen) m_grainPos = 0;
                    }
                    if (--m_eventRemaining <= 0) m_state = Idle;
                    break;
            }

            buffer[i * numChannels] = dryL * (1.0f - mix) + wetL * mix;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * (1.0f - mix) + wetR * mix;

            if (++m_writePos >= m_bufLen) m_writePos = 0;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"Rate",     0.0f, 20.0f,  3.0f, "/s", false, false, WidgetHint::Knob},
            {"Size",     5.0f, 300.0f, 60.0f, "ms", false, false, WidgetHint::Knob},
            {"Severity", 0.0f,  1.0f,  0.5f, "",   false, false, WidgetHint::Knob},
            {"Mix",      0.0f,  1.0f,  1.0f, "",   false, false, WidgetHint::DentedKnob},
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
    enum State { Idle, Stutter, Stuck, Mute };

    void startEvent(int sizeSamp, float severity) {
        const float roll = randf();
        // More severity → fewer harmless mutes, more stutter/stuck glitch,
        // and longer events.
        const int durMul = 1 + static_cast<int>(severity * 6.0f + 0.5f);
        if (roll < 0.34f) {
            // Stutter: loop a Size slice durMul times.
            m_state = Stutter;
            m_grainLen = sizeSamp;
            m_eventRemaining = m_grainLen * durMul;
        } else if (roll < 0.74f) {
            // Stuck: loop a tiny slice many times (CD "skip").
            m_state = Stuck;
            m_grainLen = std::max(1, sizeSamp / 8);
            m_eventRemaining = sizeSamp * durMul;
        } else {
            // Mute / dropout.
            m_state = Mute;
            m_eventRemaining = std::max(1, (sizeSamp / 2) * durMul);
            return;  // mute uses no grain
        }
        // Capture the most-recent grainLen samples to loop.
        for (int k = 0; k < m_grainLen; ++k) {
            int idx = (m_writePos - m_grainLen + 1 + k) % m_bufLen;
            if (idx < 0) idx += m_bufLen;
            m_grainL[k] = m_ringL[idx];
            m_grainR[k] = m_ringR[idx];
        }
        m_grainPos = 0;
    }

    float randf() {
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        return static_cast<float>(m_rng & 0xFFFFFF) / static_cast<float>(0x1000000);
    }

    float m_params[kParamCount] = {3.0f, 60.0f, 0.5f, 1.0f};
    std::vector<float> m_ringL, m_ringR, m_grainL, m_grainR;
    int      m_bufLen = 0, m_writePos = 0;
    State    m_state = Idle;
    int      m_grainLen = 0, m_grainPos = 0, m_eventRemaining = 0;
    uint32_t m_rng = 0xB16B00B5u;
};

} // namespace effects
} // namespace yawn
