#pragma once
// Buffer Repeat — free-running (ms-based) stutter. A slot timer fires
// every Size ms; on each fire it rolls Chance, and on a hit it freezes
// the most-recent Size-ms chunk and loops it Repeats times before
// releasing to dry. Unlike Beat Repeat this isn't tempo-locked — it's
// the momentary "buffer freeze / glitch" stutter.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yawn {
namespace effects {

class BufferRepeat : public AudioEffect {
public:
    enum Param { kSize, kRepeats, kChance, kMix, kParamCount };

    const char* name() const override { return "Buffer Repeat"; }
    const char* id()   const override { return "bufferrepeat"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // Max captured chunk = 1 s.
        m_bufLen = std::max(1, static_cast<int>(sampleRate));
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
        m_slotCounter = 0;
        m_active = false;
        m_grainLen = 0;
        m_grainPos = 0;
        m_repeatsLeft = 0;
        m_rng = 0x9E3779B9u;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed || m_bufLen <= 0) return;
        const bool  stereo = numChannels > 1;
        const float sizeMs = std::clamp(m_params[kSize], 5.0f, 1000.0f);
        const int   slot   = std::clamp(
            static_cast<int>(sizeMs * 0.001f * m_sampleRate), 1, m_bufLen);
        const int   repeats = std::max(1, static_cast<int>(m_params[kRepeats]));
        const float chance  = std::clamp(m_params[kChance], 0.0f, 1.0f);
        const float mix     = std::clamp(m_params[kMix], 0.0f, 1.0f) * m_mix;

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            m_ringL[m_writePos] = dryL;
            m_ringR[m_writePos] = dryR;

            // Slot boundary — roll a new stutter if idle.
            if (++m_slotCounter >= slot) {
                m_slotCounter = 0;
                if (!m_active && randf() < chance) {
                    m_grainLen = slot;
                    for (int k = 0; k < m_grainLen; ++k) {
                        int idx = (m_writePos - m_grainLen + 1 + k) % m_bufLen;
                        if (idx < 0) idx += m_bufLen;
                        m_grainL[k] = m_ringL[idx];
                        m_grainR[k] = m_ringR[idx];
                    }
                    m_grainPos = 0;
                    m_repeatsLeft = repeats;
                    m_active = true;
                }
            }

            float wetL = dryL, wetR = dryR;
            if (m_active && m_grainLen > 0) {
                wetL = m_grainL[m_grainPos];
                wetR = m_grainR[m_grainPos];
                if (++m_grainPos >= m_grainLen) {
                    m_grainPos = 0;
                    if (--m_repeatsLeft <= 0) m_active = false;
                }
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
            {"Size",    5.0f, 1000.0f, 120.0f, "ms", false, false, WidgetHint::Knob},
            {"Repeats", 1.0f,   32.0f,   4.0f, "",   false, false, WidgetHint::StepSelector},
            {"Chance",  0.0f,    1.0f,   0.5f, "",   false, false, WidgetHint::Knob},
            {"Mix",     0.0f,    1.0f,   1.0f, "",   false, false, WidgetHint::DentedKnob},
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

    float m_params[kParamCount] = {120.0f, 4.0f, 0.5f, 1.0f};
    std::vector<float> m_ringL, m_ringR, m_grainL, m_grainR;
    int      m_bufLen = 0, m_writePos = 0, m_slotCounter = 0;
    bool     m_active = false;
    int      m_grainLen = 0, m_grainPos = 0, m_repeatsLeft = 0;
    uint32_t m_rng = 0x9E3779B9u;
};

} // namespace effects
} // namespace yawn
