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

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // Max 4 seconds delay per channel
        int maxSamples = static_cast<int>(4.0 * sampleRate);
        m_bufferL.assign(maxSamples, 0.0f);
        m_bufferR.assign(maxSamples, 0.0f);
        m_maxDelay = maxSamples;
        m_writePos = 0;
        m_feedbackFilter.reset();
        m_feedbackFilter.compute(Biquad::Type::LowPass, sampleRate,
                                  m_params[kFilterCutoff], 0.0, 0.707);
        reset();
    }

    void reset() override {
        std::fill(m_bufferL.begin(), m_bufferL.end(), 0.0f);
        std::fill(m_bufferR.begin(), m_bufferR.end(), 0.0f);
        m_writePos = 0;
        m_feedbackFilter.reset();
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        int delaySamples = computeDelaySamples();
        if (delaySamples < 1) delaySamples = 1;
        if (delaySamples >= m_maxDelay) delaySamples = m_maxDelay - 1;

        float feedback = m_params[kFeedback];
        float wet = m_params[kWetDry];
        float dry = 1.0f - wet;
        bool pingPong = m_params[kPingPong] >= 0.5f;

        for (int i = 0; i < numFrames; ++i) {
            float inL = buffer[i * numChannels];
            float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;

            int readPos = (m_writePos - delaySamples + m_maxDelay) % m_maxDelay;
            float delayL = m_bufferL[readPos];
            float delayR = m_bufferR[readPos];

            // Feedback with filter
            float fbL = m_feedbackFilter.process(delayL) * feedback;
            float fbR = delayR * feedback;

            if (pingPong) {
                m_bufferL[m_writePos] = inL + fbR; // Cross-feed
                m_bufferR[m_writePos] = inR + fbL;
            } else {
                m_bufferL[m_writePos] = inL + fbL;
                m_bufferR[m_writePos] = inR + fbR;
            }

            buffer[i * numChannels] = dry * inL + wet * delayL;
            if (numChannels > 1)
                buffer[i * numChannels + 1] = dry * inR + wet * delayR;

            m_writePos = (m_writePos + 1) % m_maxDelay;
        }
    }

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
    int computeDelaySamples() const {
        if (m_params[kSync] >= 0.5f && m_bpm > 0.0) {
            // Tempo sync: time parameter maps to beat division
            // 125ms=1/32, 250ms=1/16, 500ms=1/8, 1000ms=1/4, 2000ms=1/2
            double beatsPerSecond = m_bpm / 60.0;
            double secondsPerBeat = 1.0 / beatsPerSecond;
            // Snap to nearest beat division
            double timeMs = m_params[kTimeMs];
            double timeSec = timeMs * 0.001;
            double beats = timeSec * beatsPerSecond;
            // Quantize to nearest power-of-2 subdivision
            double quantized = std::pow(2.0, std::round(std::log2(beats)));
            return static_cast<int>(quantized * secondsPerBeat * m_sampleRate);
        }
        return static_cast<int>(m_params[kTimeMs] * 0.001 * m_sampleRate);
    }

    std::vector<float> m_bufferL, m_bufferR;
    int m_maxDelay = 0, m_writePos = 0;
    Biquad m_feedbackFilter;
    double m_bpm = 120.0;
    float m_params[kParamCount] = {500.0f, 0.4f, 0.3f, 0.0f, 8000.0f, 0.0f};
};

} // namespace effects
} // namespace yawn
