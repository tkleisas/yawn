#pragma once

// AmpSimulator — guitar/bass amplifier modelling.
//
// Models the key stages of a tube amplifier:
// 1. Input gain (preamp drive)
// 2. Tube-stage saturation with selectable amp type
// 3. Three-band tone stack (Bass / Mid / Treble)
// 4. Cabinet simulation (low-pass + resonant peak)
// 5. Presence control (high shelf post-cabinet)
// 6. Output level
//
// Amp types model different preamp voicings:
// - Clean: minimal saturation, wide bandwidth
// - Crunch: moderate asymmetric clipping
// - Lead: heavy saturation with mid focus
// - High Gain: extreme saturation, scooped mids
//
// All buffers are pre-allocated in init().  No allocations in process().

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class AmpSimulator : public AudioEffect {
public:
    enum Param {
        kGain,       // 0–48 dB: preamp drive
        kBass,       // -12 to +12 dB: low shelf
        kMid,        // -12 to +12 dB: mid peak
        kTreble,     // -12 to +12 dB: high shelf
        kPresence,   // -6 to +6 dB: post-cab high shelf
        kOutput,     // -24 to +6 dB: master volume
        kAmpType,    // 0–3: Clean, Crunch, Lead, HighGain
        kCabinet,    // 0–1: cabinet simulation amount
        kParamCount
    };

    enum AmpType { Clean = 0, Crunch = 1, Lead = 2, HighGain = 3 };

    const char* name() const override { return "Amp Simulator"; }
    const char* id()   const override { return "amp"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    static constexpr const char* kAmpTypeLabels[] = {"Clean", "Crunch", "Lead", "HiGain"};

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Gain",     0.0f,   48.0f, 12.0f, "dB", false},
            {"Bass",    -12.0f,  12.0f,  0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Mid",     -12.0f,  12.0f,  0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Treble",  -12.0f,  12.0f,  0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Presence", -6.0f,   6.0f,  0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Output",  -24.0f,   6.0f, -6.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Type",     0.0f,    3.0f,  1.0f, "",   false, false, WidgetHint::StepSelector, kAmpTypeLabels, 4},
            {"Cabinet",  0.0f,    1.0f,  0.8f, "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[index];
    }

    float getParameter(int index) const override { return m_params[index]; }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index >= kBass && index <= kPresence) updateFilters();
            if (index == kCabinet) updateFilters();
        }
    }

private:
    static float ampSaturate(float x, int type);
    void updateFilters();

    // Tone stack
    Biquad m_bassL, m_bassR;
    Biquad m_midL,  m_midR;
    Biquad m_trebL, m_trebR;

    // Cabinet
    Biquad m_cabLpL,   m_cabLpR;
    Biquad m_cabPeakL, m_cabPeakR;

    // Presence
    Biquad m_presL, m_presR;

    // Input HP
    Biquad m_inputHpL, m_inputHpR;

    float m_params[kParamCount] = {12.0f, 0.0f, 0.0f, 0.0f, 0.0f, -6.0f, 1.0f, 0.8f};
};

} // namespace effects
} // namespace yawn
