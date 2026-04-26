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

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

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
