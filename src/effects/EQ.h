#pragma once

// EQ — 3-band parametric equalizer (low shelf, mid peak, high shelf).

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include "effects/FreqMap.h"

namespace yawn {
namespace effects {

class EQ : public AudioEffect {
public:
    enum Param {
        kLowFreq, kLowGain,
        kMidFreq, kMidGain, kMidQ,
        kHighFreq, kHighGain,
        kParamCount
    };

    // Each band stores its frequency normalized 0..1 and log-maps to
    // its own band range for uniform modulation + perceptual feel.
    static constexpr float kLowMinHz  = 20.0f,   kLowMaxHz  = 2000.0f;
    static constexpr float kMidMinHz  = 100.0f,  kMidMaxHz  = 10000.0f;
    static constexpr float kHighMinHz = 2000.0f, kHighMaxHz = 20000.0f;

    static float lowFreqNormToHz(float x)  { return logNormToHz(x, kLowMinHz,  kLowMaxHz);  }
    static float midFreqNormToHz(float x)  { return logNormToHz(x, kMidMinHz,  kMidMaxHz);  }
    static float highFreqNormToHz(float x) { return logNormToHz(x, kHighMinHz, kHighMaxHz); }
    static float lowFreqHzToNorm(float hz)  { return logHzToNorm(hz, kLowMinHz,  kLowMaxHz);  }
    static float midFreqHzToNorm(float hz)  { return logHzToNorm(hz, kMidMinHz,  kMidMaxHz);  }
    static float highFreqHzToNorm(float hz) { return logHzToNorm(hz, kHighMinHz, kHighMaxHz); }
    static void formatLowFreqHz(float v, char* b, int n)  { formatHz(lowFreqNormToHz(v),  b, n); }
    static void formatMidFreqHz(float v, char* b, int n)  { formatHz(midFreqNormToHz(v),  b, n); }
    static void formatHighFreqHz(float v, char* b, int n) { formatHz(highFreqNormToHz(v), b, n); }

    const char* name() const override { return "EQ"; }
    const char* id()   const override { return "eq"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Low Freq",  0.0f, 1.0f,     0.5f,    "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatLowFreqHz},
            {"Low Gain",  -24.0f, 24.0f,  0.0f,    "dB", false, false, WidgetHint::DentedKnob},
            {"Mid Freq",  0.0f, 1.0f,     0.5f,    "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatMidFreqHz},
            {"Mid Gain",  -24.0f, 24.0f,  0.0f,    "dB", false, false, WidgetHint::DentedKnob},
            {"Mid Q",     0.1f,  10.0f,   1.0f,    "",   false, false, WidgetHint::DentedKnob},
            {"High Freq", 0.0f, 1.0f,     0.398f,  "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatHighFreqHz},
            {"High Gain", -24.0f, 24.0f,  0.0f,    "dB", false, false, WidgetHint::DentedKnob},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            updateFilters();
        }
    }

private:
    void updateFilters();

    Biquad m_lowL[1], m_lowR[1];
    Biquad m_midL[1], m_midR[1];
    Biquad m_highL[1], m_highR[1];

    // Initial values match the defaults above.
    float m_params[kParamCount] = {0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.398f, 0.0f};
};

} // namespace effects
} // namespace yawn
