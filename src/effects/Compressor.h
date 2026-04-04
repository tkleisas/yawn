#pragma once

// Compressor — dynamics compressor with peak envelope follower.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class Compressor : public AudioEffect {
public:
    enum Param { kThreshold, kRatio, kAttack, kRelease, kMakeupGain, kKnee, kParamCount };

    const char* name() const override { return "Compressor"; }
    const char* id()   const override { return "compressor"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        updateCoeffs();
        reset();
    }

    void reset() override { m_envDB = -96.0f; }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        float thresh = m_params[kThreshold];
        float ratio  = m_params[kRatio];
        float knee   = m_params[kKnee];
        float makeup = std::pow(10.0f, m_params[kMakeupGain] / 20.0f);
        float halfKnee = knee * 0.5f;

        for (int i = 0; i < numFrames; ++i) {
            float L = buffer[i * numChannels];
            float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

            // Peak detection (take max of L/R)
            float peak = std::max(std::abs(L), std::abs(R));
            float peakDB = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -96.0f;

            // Envelope follower
            if (peakDB > m_envDB)
                m_envDB = m_attackCoeff * m_envDB + (1.0f - m_attackCoeff) * peakDB;
            else
                m_envDB = m_releaseCoeff * m_envDB + (1.0f - m_releaseCoeff) * peakDB;

            // Gain computer with soft knee
            float overDB = m_envDB - thresh;
            float gainReductionDB = 0.0f;

            if (knee > 0.0f && overDB > -halfKnee && overDB < halfKnee) {
                // Soft knee region
                float x = overDB + halfKnee;
                gainReductionDB = (x * x) / (2.0f * knee) * (1.0f / ratio - 1.0f);
            } else if (overDB >= halfKnee) {
                gainReductionDB = overDB * (1.0f / ratio - 1.0f);
            }

            float gain = std::pow(10.0f, gainReductionDB / 20.0f) * makeup;

            buffer[i * numChannels] = L * gain;
            if (numChannels > 1) buffer[i * numChannels + 1] = R * gain;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Threshold",   -60.0f, 0.0f,    -20.0f, "dB", false},
            {"Ratio",       1.0f,   20.0f,   4.0f,   ":1", false, false, WidgetHint::DentedKnob},
            {"Attack",      0.1f,   100.0f,  10.0f,  "ms", false},
            {"Release",     10.0f,  1000.0f, 100.0f, "ms", false},
            {"Makeup Gain", 0.0f,   24.0f,   0.0f,   "dB", false},
            {"Knee",        0.0f,   12.0f,   6.0f,   "dB", false, false, WidgetHint::DentedKnob},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index == kAttack || index == kRelease) updateCoeffs();
        }
    }

private:
    void updateCoeffs() {
        float attackMs  = m_params[kAttack];
        float releaseMs = m_params[kRelease];
        m_attackCoeff  = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * attackMs * 0.001f));
        m_releaseCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * releaseMs * 0.001f));
    }

    float m_envDB = -96.0f;
    float m_attackCoeff = 0.0f, m_releaseCoeff = 0.0f;
    float m_params[kParamCount] = {-20.0f, 4.0f, 10.0f, 100.0f, 0.0f, 6.0f};
};

} // namespace effects
} // namespace yawn
