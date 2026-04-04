#pragma once

// Chorus — modulated delay for stereo thickening.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class Chorus : public AudioEffect {
public:
    enum Param { kRate, kDepth, kWetDry, kVoices, kParamCount };

    const char* name() const override { return "Chorus"; }
    const char* id()   const override { return "chorus"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // Max modulation depth of ~25ms
        int maxDelay = static_cast<int>(0.025 * sampleRate) + 256;
        m_bufferL.assign(maxDelay, 0.0f);
        m_bufferR.assign(maxDelay, 0.0f);
        m_maxDelay = maxDelay;
        reset();
    }

    void reset() override {
        std::fill(m_bufferL.begin(), m_bufferL.end(), 0.0f);
        std::fill(m_bufferR.begin(), m_bufferR.end(), 0.0f);
        m_writePos = 0;
        m_phase = 0.0;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        float rate = m_params[kRate];
        float depth = m_params[kDepth];
        float wet = m_params[kWetDry];
        float dry = 1.0f - wet;
        int voices = std::max(1, std::min(4, static_cast<int>(m_params[kVoices])));

        double phaseInc = rate / m_sampleRate;
        float baseDelay = static_cast<float>(0.007 * m_sampleRate); // 7ms base delay
        float modDepth = depth * static_cast<float>(0.003 * m_sampleRate); // Up to 3ms modulation

        for (int i = 0; i < numFrames; ++i) {
            float inL = buffer[i * numChannels];
            float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;

            m_bufferL[m_writePos] = inL;
            m_bufferR[m_writePos] = inR;

            float chorusL = 0.0f, chorusR = 0.0f;
            for (int v = 0; v < voices; ++v) {
                double voicePhase = m_phase + static_cast<double>(v) / voices;
                float modL = static_cast<float>(std::sin(voicePhase * 6.283185307));
                float modR = static_cast<float>(std::sin(voicePhase * 6.283185307 + 1.5707963));

                float delayL = baseDelay + modL * modDepth;
                float delayR = baseDelay + modR * modDepth;

                chorusL += readInterp(m_bufferL, delayL);
                chorusR += readInterp(m_bufferR, delayR);
            }

            float voiceGain = 1.0f / static_cast<float>(voices);
            buffer[i * numChannels] = dry * inL + wet * chorusL * voiceGain;
            if (numChannels > 1)
                buffer[i * numChannels + 1] = dry * inR + wet * chorusR * voiceGain;

            m_writePos = (m_writePos + 1) % m_maxDelay;
            m_phase += phaseInc;
            if (m_phase >= 1.0) m_phase -= 1.0;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Rate",    0.1f, 5.0f, 1.0f, "Hz", false},
            {"Depth",   0.0f, 1.0f, 0.5f, "",   false, false, WidgetHint::DentedKnob},
            {"Wet/Dry", 0.0f, 1.0f, 0.5f, "",   false, false, WidgetHint::DentedKnob},
            {"Voices",  1.0f, 4.0f, 2.0f, "",   false, false, WidgetHint::StepSelector},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount)
            m_params[index] = value;
    }

private:
    float readInterp(const std::vector<float>& buf, float delaySamples) const {
        float readPos = static_cast<float>(m_writePos) - delaySamples;
        if (readPos < 0.0f) readPos += m_maxDelay;
        int idx0 = static_cast<int>(readPos) % m_maxDelay;
        int idx1 = (idx0 + 1) % m_maxDelay;
        float frac = readPos - std::floor(readPos);
        return buf[idx0] * (1.0f - frac) + buf[idx1] * frac;
    }

    std::vector<float> m_bufferL, m_bufferR;
    int m_maxDelay = 0, m_writePos = 0;
    double m_phase = 0.0;
    float m_params[kParamCount] = {1.0f, 0.5f, 0.5f, 2.0f};
};

} // namespace effects
} // namespace yawn
