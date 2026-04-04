#pragma once

// Reverb — Freeverb-based algorithmic reverb.
// 8 parallel comb filters + 4 series allpass filters, stereo.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace yawn {
namespace effects {

class Reverb : public AudioEffect {
public:
    enum Param { kRoomSize, kDamping, kWetDry, kPreDelay, kWidth, kParamCount };

    const char* name() const override { return "Reverb"; }
    const char* id()   const override { return "reverb"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;

        double scale = sampleRate / 44100.0;
        static const int kCombTunings[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAllpassTunings[] = {556, 441, 341, 225};
        static const int kStereoSpread = 23;

        for (int i = 0; i < 8; ++i) {
            int len = static_cast<int>(kCombTunings[i] * scale);
            m_combL[i].resize(len); m_combR[i].resize(len + kStereoSpread);
        }
        for (int i = 0; i < 4; ++i) {
            int len = static_cast<int>(kAllpassTunings[i] * scale);
            m_allpassL[i].resize(len); m_allpassR[i].resize(len + kStereoSpread);
        }

        // Pre-delay buffer (up to 250ms)
        int maxPreDelay = static_cast<int>(0.25 * sampleRate);
        m_preDelayBuf.resize(maxPreDelay * 2, 0.0f);
        m_preDelayLen = maxPreDelay;
        m_preDelayPos = 0;

        reset();
        updateParams();
    }

    void reset() override {
        for (auto& c : m_combL) c.clear();
        for (auto& c : m_combR) c.clear();
        for (auto& a : m_allpassL) a.clear();
        for (auto& a : m_allpassR) a.clear();
        std::fill(m_preDelayBuf.begin(), m_preDelayBuf.end(), 0.0f);
        m_preDelayPos = 0;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        float wet1 = m_wet * (m_width / 2.0f + 0.5f);
        float wet2 = m_wet * ((1.0f - m_width) / 2.0f);
        float dry = 1.0f - m_wet;

        int pdSamples = static_cast<int>(m_params[kPreDelay] * 0.001f * m_sampleRate);
        if (pdSamples >= m_preDelayLen) pdSamples = m_preDelayLen - 1;

        for (int i = 0; i < numFrames; ++i) {
            float inL = buffer[i * numChannels];
            float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;
            float input = (inL + inR) * 0.5f;

            // Pre-delay
            int wrIdx = m_preDelayPos;
            m_preDelayBuf[wrIdx] = input;
            int rdIdx = (wrIdx - pdSamples + m_preDelayLen) % m_preDelayLen;
            float delayed = m_preDelayBuf[rdIdx];
            m_preDelayPos = (wrIdx + 1) % m_preDelayLen;

            // Parallel comb filters
            float outL = 0.0f, outR = 0.0f;
            for (int c = 0; c < 8; ++c) {
                outL += m_combL[c].process(delayed, m_roomSize, m_damp1, m_damp2);
                outR += m_combR[c].process(delayed, m_roomSize, m_damp1, m_damp2);
            }

            // Series allpass filters
            for (int a = 0; a < 4; ++a) {
                outL = m_allpassL[a].process(outL);
                outR = m_allpassR[a].process(outR);
            }

            float mixL = dry * inL + wet1 * outL + wet2 * outR;
            float mixR = dry * inR + wet1 * outR + wet2 * outL;

            buffer[i * numChannels] = mixL;
            if (numChannels > 1) buffer[i * numChannels + 1] = mixR;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Room Size", 0.0f, 1.0f, 0.7f, "",   false, false, WidgetHint::DentedKnob},
            {"Damping",   0.0f, 1.0f, 0.5f, "",   false, false, WidgetHint::DentedKnob},
            {"Wet/Dry",   0.0f, 1.0f, 0.3f, "",   false, false, WidgetHint::DentedKnob},
            {"Pre-Delay", 0.0f, 250.0f, 10.0f, "ms", false},
            {"Width",     0.0f, 1.0f, 1.0f, "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            updateParams();
        }
    }

private:
    void updateParams() {
        m_roomSize = m_params[kRoomSize] * 0.28f + 0.7f;
        float damp = m_params[kDamping];
        m_damp1 = damp;
        m_damp2 = 1.0f - damp;
        m_wet = m_params[kWetDry];
        m_width = m_params[kWidth];
    }

    // Comb filter with damped feedback
    struct CombFilter {
        void resize(int size) {
            m_buffer.assign(size, 0.0f);
            m_size = size;
            m_pos = 0;
            m_filterStore = 0.0f;
        }
        void clear() {
            std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
            m_filterStore = 0.0f;
            m_pos = 0;
        }
        float process(float input, float feedback, float damp1, float damp2) {
            if (m_size == 0) return 0.0f;
            float out = m_buffer[m_pos];
            m_filterStore = out * damp2 + m_filterStore * damp1;
            m_buffer[m_pos] = input + m_filterStore * feedback;
            m_pos = (m_pos + 1) % m_size;
            return out;
        }
        std::vector<float> m_buffer;
        int m_size = 0, m_pos = 0;
        float m_filterStore = 0.0f;
    };

    // Allpass filter
    struct AllpassFilter {
        void resize(int size) {
            m_buffer.assign(size, 0.0f);
            m_size = size;
            m_pos = 0;
        }
        void clear() {
            std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
            m_pos = 0;
        }
        float process(float input) {
            if (m_size == 0) return input;
            float buf = m_buffer[m_pos];
            float out = buf - input;
            m_buffer[m_pos] = input + buf * 0.5f;
            m_pos = (m_pos + 1) % m_size;
            return out;
        }
        std::vector<float> m_buffer;
        int m_size = 0, m_pos = 0;
    };

    CombFilter    m_combL[8], m_combR[8];
    AllpassFilter m_allpassL[4], m_allpassR[4];
    std::vector<float> m_preDelayBuf;
    int m_preDelayLen = 0, m_preDelayPos = 0;

    float m_params[kParamCount] = {0.7f, 0.5f, 0.3f, 10.0f, 1.0f};
    float m_roomSize = 0.7f, m_damp1 = 0.5f, m_damp2 = 0.5f;
    float m_wet = 0.3f, m_width = 1.0f;
};

} // namespace effects
} // namespace yawn
