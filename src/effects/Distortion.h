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

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    static constexpr const char* kDistTypeLabels[] = {"Soft", "Hard", "Tube", "Fold"};

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Drive",   0.0f, 48.0f,  12.0f,    "dB", false},
            {"Tone",    200.0f, 20000.0f, 8000.0f, "Hz", false},
            {"Wet/Dry", 0.0f, 1.0f,   1.0f,     "",   false, false, WidgetHint::DentedKnob},
            {"Type",    0.0f, 3.0f,   0.0f,     "",   false, false, WidgetHint::StepSelector, kDistTypeLabels, 4},
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
    static float shape(float x, int type);

    void updateToneFilter() {
        m_toneL.compute(Biquad::Type::LowPass, m_sampleRate, m_params[kTone], 0.0, 0.707);
        m_toneR.compute(Biquad::Type::LowPass, m_sampleRate, m_params[kTone], 0.0, 0.707);
    }

    Biquad m_toneL, m_toneR;
    float m_params[kParamCount] = {12.0f, 8000.0f, 1.0f, 0.0f};
};

} // namespace effects
} // namespace yawn
