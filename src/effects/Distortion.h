#pragma once

// Distortion — waveshaper with multiple saturation modes.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class Distortion : public AudioEffect {
public:
    enum Param { kDrive, kTone, kWetDry, kType, kParamCount };
    enum DistType { SoftClip = 0, HardClip = 1, Tube = 2, Foldback = 3 };

    const char* name() const override { return "Distortion"; }
    const char* id()   const override { return "distortion"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        updateToneFilter();
        reset();
    }

    void reset() override { m_toneL.reset(); m_toneR.reset(); }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        float drive = std::pow(10.0f, m_params[kDrive] / 20.0f);
        float wet = m_params[kWetDry];
        float dry = 1.0f - wet;
        int type = static_cast<int>(m_params[kType]);

        for (int i = 0; i < numFrames; ++i) {
            float L = buffer[i * numChannels];
            float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

            float distL = shape(L * drive, type);
            float distR = shape(R * drive, type);

            // Tone filter
            distL = m_toneL.process(distL);
            distR = m_toneR.process(distR);

            buffer[i * numChannels] = dry * L + wet * distL;
            if (numChannels > 1)
                buffer[i * numChannels + 1] = dry * R + wet * distR;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Drive",   0.0f, 48.0f,  12.0f,    "dB", false},
            {"Tone",    200.0f, 20000.0f, 8000.0f, "Hz", false},
            {"Wet/Dry", 0.0f, 1.0f,   1.0f,     "",   false},
            {"Type",    0.0f, 3.0f,   0.0f,     "",   false},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index == kTone) updateToneFilter();
        }
    }

private:
    static float shape(float x, int type) {
        switch (type) {
        case SoftClip:
            return std::tanh(x);
        case HardClip:
            return std::max(-1.0f, std::min(1.0f, x));
        case Tube: {
            // Asymmetric soft saturation (tube-like)
            if (x >= 0.0f)
                return 1.0f - std::exp(-x);
            else
                return -1.0f + std::exp(x);
        }
        case Foldback: {
            // Foldback distortion
            while (x > 1.0f || x < -1.0f) {
                if (x > 1.0f) x = 2.0f - x;
                if (x < -1.0f) x = -2.0f - x;
            }
            return x;
        }
        default:
            return std::tanh(x);
        }
    }

    void updateToneFilter() {
        m_toneL.compute(Biquad::Type::LowPass, m_sampleRate, m_params[kTone], 0.0, 0.707);
        m_toneR.compute(Biquad::Type::LowPass, m_sampleRate, m_params[kTone], 0.0, 0.707);
    }

    Biquad m_toneL, m_toneR;
    float m_params[kParamCount] = {12.0f, 8000.0f, 1.0f, 0.0f};
};

} // namespace effects
} // namespace yawn
