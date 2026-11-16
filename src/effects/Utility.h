#pragma once

// Utility — level / pan / mono / phase trim.
//
// The first thing in the chain when a signal is too hot or too quiet,
// or when a stereo source needs folding to mono / flipping phase.
// Unity gain at 0 dB, centre pan, mono off, phase normal.

#include "effects/AudioEffect.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class Utility : public AudioEffect {
public:
    enum Param {
        kGainDb,
        kPan,
        kMono,
        kPhaseInvert,
        kParamCount
    };

    const char* name() const override { return "Utility"; }
    const char* id()   const override { return "utility"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_gain = targetGain();
        m_panL = targetPanL();
        m_panR = targetPanR();
        reset();
    }

    void reset() override {
        m_gain = targetGain();
        m_panL = targetPanL();
        m_panR = targetPanR();
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Gain",   -60.0f, 24.0f, 0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Pan",     -1.0f,  1.0f, 0.0f, "",   false, false, WidgetHint::DentedKnob},
            {"Mono",     0.0f,  1.0f, 0.0f, "",   true,  false, WidgetHint::Toggle},
            {"Invert",   0.0f,  1.0f, 0.0f, "",   true,  false, WidgetHint::Toggle},
        };
        return infos[index];
    }

    float getParameter(int index) const override {
        return (index >= 0 && index < kParamCount) ? m_params[index] : 0.0f;
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        const auto& pi = parameterInfo(index);
        m_params[index] = std::clamp(value, pi.minValue, pi.maxValue);
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed || numFrames <= 0 || numChannels <= 0) return;

        const float targetG = targetGain();
        const float targetL = targetPanL();
        const float targetR = targetPanR();
        const bool mono = m_params[kMono] >= 0.5f;
        const float phase = m_params[kPhaseInvert] >= 0.5f ? -1.0f : 1.0f;

        const double sr = m_sampleRate > 0.0 ? m_sampleRate : 48000.0;
        const float coef = 1.0f - std::exp(-1.0f / (0.008f * float(sr)));

        for (int i = 0; i < numFrames; ++i) {
            m_gain += (targetG - m_gain) * coef;
            m_panL += (targetL - m_panL) * coef;
            m_panR += (targetR - m_panR) * coef;

            float l = buffer[i * numChannels];
            float r = (numChannels > 1) ? buffer[i * numChannels + 1] : l;
            if (mono) {
                const float m = (l + r) * 0.5f;
                l = r = m;
            }
            l *= m_gain * m_panL * phase;
            r *= m_gain * m_panR * phase;
            buffer[i * numChannels] = l;
            if (numChannels > 1) buffer[i * numChannels + 1] = r;
        }
    }

private:
    float targetGain() const {
        return std::pow(10.0f, m_params[kGainDb] / 20.0f);
    }
    float targetPanL() const {
        const float p = std::clamp(m_params[kPan], -1.0f, 1.0f);
        return p > 0.0f ? 1.0f - p : 1.0f;
    }
    float targetPanR() const {
        const float p = std::clamp(m_params[kPan], -1.0f, 1.0f);
        return p < 0.0f ? 1.0f + p : 1.0f;
    }

    float m_params[kParamCount] = {0.0f, 0.0f, 0.0f, 0.0f};
    float m_gain = 1.0f;
    float m_panL = 1.0f;
    float m_panR = 1.0f;
};

} // namespace effects
} // namespace yawn
