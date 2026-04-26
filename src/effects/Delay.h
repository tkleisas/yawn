#pragma once

// Delay — stereo delay with tempo sync, feedback filter, and ping-pong mode.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class Delay : public AudioEffect {
public:
    enum Param { kTimeMs, kFeedback, kWetDry, kPingPong, kFilterCutoff, kSync, kParamCount };

    const char* name() const override { return "Delay"; }
    const char* id()   const override { return "delay"; }

    void setBPM(double bpm) { m_bpm = bpm; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Time",       1.0f, 4000.0f, 500.0f, "ms",  false},
            {"Feedback",   0.0f, 0.95f,   0.4f,   "",    false},
            {"Wet/Dry",    0.0f, 1.0f,    0.3f,   "",    false, false, WidgetHint::DentedKnob},
            {"Ping-Pong",  0.0f, 1.0f,    0.0f,   "",    true},
            {"Filter",     200.0f, 20000.0f, 8000.0f, "Hz", false},
            {"Tempo Sync", 0.0f, 1.0f,    0.0f,   "",    true},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index == kFilterCutoff) {
                m_feedbackFilter.compute(Biquad::Type::LowPass, m_sampleRate,
                                          value, 0.0, 0.707);
            }
        }
    }

private:
    int computeDelaySamples() const;

    std::vector<float> m_bufferL, m_bufferR;
    int m_maxDelay = 0, m_writePos = 0;
    Biquad m_feedbackFilter;
    double m_bpm = 120.0;
    float m_params[kParamCount] = {500.0f, 0.4f, 0.3f, 0.0f, 8000.0f, 0.0f};
};

} // namespace effects
} // namespace yawn
