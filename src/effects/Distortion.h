#pragma once

// Distortion — waveshaper with multiple saturation modes.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include "audio/Oversampling.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class Distortion : public AudioEffect {
public:
    enum Param { kDrive, kTone, kWetDry, kType, kOversample, kParamCount };
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
            {"OS 2x",   0.0f, 1.0f,   0.0f,     "",   true,  false, WidgetHint::Toggle},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
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
        Biquad::computeStereo(m_toneL, m_toneR, Biquad::Type::LowPass,
                              m_sampleRate, m_params[kTone], 0.0, 0.707);
    }

    Biquad m_toneL, m_toneR;
    float m_params[kParamCount] = {12.0f, 8000.0f, 1.0f, 0.0f, 0.0f};

    // 2× oversamplers for the waveshaper (one per channel). Used only when
    // the OS 2x toggle is on; keeps the shaper's harmonics below Nyquist so
    // hard-clip / foldback don't fold back as aliasing.
    ::yawn::audio::dsp::Oversampler2x m_osL, m_osR;
};

} // namespace effects
} // namespace yawn
