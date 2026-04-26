#pragma once

// Filter — multi-mode biquad filter (LP, HP, BP, Notch).

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include "effects/FreqMap.h"

namespace yawn {
namespace effects {

class Filter : public AudioEffect {
public:
    enum Param { kCutoff, kResonance, kType, kParamCount };
    enum FilterType { LP = 0, HP = 1, BP = 2, Notch = 3 };
    static constexpr const char* kFilterTypeLabels[] = {"LP", "HP", "BP", "Notch"};

    // Cutoff is stored normalized 0..1 for uniform LFO modulation.
    // Maps log-scaled to 20..20000 Hz for the biquad.
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static float cutoffNormToHz(float x) { return logNormToHz(x, kMinHz, kMaxHz); }
    static float cutoffHzToNorm(float hz) { return logHzToNorm(hz, kMinHz, kMaxHz); }
    static void  formatCutoffHz(float v, char* buf, int n) {
        formatHz(cutoffNormToHz(v), buf, n);
    }

    const char* name() const override { return "Filter"; }
    const char* id()   const override { return "filter"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        // Cutoff default 0.566 ≈ 1 kHz (norm of 1000 in 20..20000).
        static const ParameterInfo infos[] = {
            {"Cutoff",    0.0f,  1.0f,     0.566f,  "",   false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Resonance", 0.1f,  20.0f,    0.707f,  "",   false, false, WidgetHint::DentedKnob},
            {"Type",      0.0f,  3.0f,     0.0f,    "",   false, false, WidgetHint::StepSelector, kFilterTypeLabels, 4},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            updateFilter();
        }
    }

private:
    void updateFilter();

    Biquad m_filterL, m_filterR;
    float m_params[kParamCount] = {0.566f, 0.707f, 0.0f};
};

} // namespace effects
} // namespace yawn
