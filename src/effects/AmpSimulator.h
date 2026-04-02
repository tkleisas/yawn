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

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        updateFilters();
        reset();
    }

    void reset() override {
        m_bassL.reset();   m_bassR.reset();
        m_midL.reset();    m_midR.reset();
        m_trebL.reset();   m_trebR.reset();
        m_cabLpL.reset();  m_cabLpR.reset();
        m_cabPeakL.reset();m_cabPeakR.reset();
        m_presL.reset();   m_presR.reset();
        m_inputHpL.reset();m_inputHpR.reset();
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        float driveDB = m_params[kGain];
        float drive = std::pow(10.0f, driveDB / 20.0f);
        float outputDB = m_params[kOutput];
        float outputGain = std::pow(10.0f, outputDB / 20.0f);
        int ampType = std::clamp(static_cast<int>(m_params[kAmpType]), 0, 3);
        float cabMix = std::clamp(m_params[kCabinet], 0.0f, 1.0f);

        for (int i = 0; i < numFrames; ++i) {
            float L = buffer[i * numChannels];
            float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

            // 1. Input high-pass (remove DC / sub-bass rumble)
            L = m_inputHpL.process(L);
            R = m_inputHpR.process(R);

            // 2. Preamp drive + saturation
            L = ampSaturate(L * drive, ampType);
            R = ampSaturate(R * drive, ampType);

            // 3. Tone stack
            L = m_bassL.process(L);
            L = m_midL.process(L);
            L = m_trebL.process(L);
            R = m_bassR.process(R);
            R = m_midR.process(R);
            R = m_trebR.process(R);

            // 4. Cabinet simulation (blend between direct and cab-filtered)
            if (cabMix > 0.001f) {
                float cabL = m_cabLpL.process(L);
                cabL = m_cabPeakL.process(cabL);
                float cabR = m_cabLpR.process(R);
                cabR = m_cabPeakR.process(cabR);
                L = L * (1.0f - cabMix) + cabL * cabMix;
                R = R * (1.0f - cabMix) + cabR * cabMix;
            }

            // 5. Presence (post-cab high shelf)
            L = m_presL.process(L);
            R = m_presR.process(R);

            // 6. Output level
            buffer[i * numChannels] = L * outputGain;
            if (numChannels > 1)
                buffer[i * numChannels + 1] = R * outputGain;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Gain",     0.0f,   48.0f, 12.0f, "dB", false},
            {"Bass",    -12.0f,  12.0f,  0.0f, "dB", false},
            {"Mid",     -12.0f,  12.0f,  0.0f, "dB", false},
            {"Treble",  -12.0f,  12.0f,  0.0f, "dB", false},
            {"Presence", -6.0f,   6.0f,  0.0f, "dB", false},
            {"Output",  -24.0f,   6.0f, -6.0f, "dB", false},
            {"Type",     0.0f,    3.0f,  1.0f, "",   false},
            {"Cabinet",  0.0f,    1.0f,  0.8f, "",   false},
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
    // Per-amp-type saturation curves
    static float ampSaturate(float x, int type) {
        switch (type) {
        case Clean:
            // Gentle soft clip — mostly linear, very mild compression
            return std::tanh(x * 0.4f) * 2.5f;

        case Crunch: {
            // Asymmetric tube-like clipping
            if (x >= 0.0f)
                return std::tanh(x * 0.8f) * 1.25f;
            else
                return std::tanh(x * 0.6f) * 1.67f;
        }

        case Lead:
            // Heavy symmetric saturation with some compression
            return std::tanh(x) * 1.1f;

        case HighGain: {
            // Extreme saturation: two-stage cascade
            float s1 = std::tanh(x * 1.5f);
            return std::tanh(s1 * 2.0f) * 0.9f;
        }

        default:
            return std::tanh(x);
        }
    }

    void updateFilters() {
        // Tone stack
        m_bassL.compute(Biquad::Type::LowShelf,  m_sampleRate, 200.0, m_params[kBass], 0.707);
        m_bassR.compute(Biquad::Type::LowShelf,  m_sampleRate, 200.0, m_params[kBass], 0.707);
        m_midL.compute(Biquad::Type::Peak,        m_sampleRate, 800.0, m_params[kMid],  1.0);
        m_midR.compute(Biquad::Type::Peak,        m_sampleRate, 800.0, m_params[kMid],  1.0);
        m_trebL.compute(Biquad::Type::HighShelf,  m_sampleRate, 3200.0, m_params[kTreble], 0.707);
        m_trebR.compute(Biquad::Type::HighShelf,  m_sampleRate, 3200.0, m_params[kTreble], 0.707);

        // Cabinet simulation: low-pass at 5kHz + resonant peak at 2.5kHz
        m_cabLpL.compute(Biquad::Type::LowPass, m_sampleRate, 5000.0, 0.0, 0.707);
        m_cabLpR.compute(Biquad::Type::LowPass, m_sampleRate, 5000.0, 0.0, 0.707);
        m_cabPeakL.compute(Biquad::Type::Peak,  m_sampleRate, 2500.0, 3.0, 1.2);
        m_cabPeakR.compute(Biquad::Type::Peak,  m_sampleRate, 2500.0, 3.0, 1.2);

        // Presence: post-cab high shelf at 4kHz
        m_presL.compute(Biquad::Type::HighShelf, m_sampleRate, 4000.0, m_params[kPresence], 0.707);
        m_presR.compute(Biquad::Type::HighShelf, m_sampleRate, 4000.0, m_params[kPresence], 0.707);

        // Input high-pass at 80Hz to remove rumble
        m_inputHpL.compute(Biquad::Type::HighPass, m_sampleRate, 80.0, 0.0, 0.707);
        m_inputHpR.compute(Biquad::Type::HighPass, m_sampleRate, 80.0, 0.0, 0.707);
    }

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
