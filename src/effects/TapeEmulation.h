#pragma once

// TapeEmulation — analog tape machine simulation.
//
// Models the key characteristics of analog tape recording:
// - Soft saturation (asymmetric, tape-like compression)
// - Wow (slow speed variations, ~0.5–6 Hz)
// - Flutter (fast speed variations, ~6–20 Hz)
// - Tape hiss (filtered noise floor)
// - High-frequency rolloff (tape head loss)
//
// Uses a modulated delay line for wow/flutter and Biquad filters for
// tone shaping.  All buffers are pre-allocated in init().

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yawn {
namespace effects {

class TapeEmulation : public AudioEffect {
public:
    enum Param {
        kSaturation,   // 0–1: drive amount
        kWow,          // 0–1: slow pitch modulation depth
        kFlutter,      // 0–1: fast pitch modulation depth
        kHiss,         // 0–1: tape noise level
        kTone,         // 200–20000 Hz: high-frequency rolloff
        kWetDry,       // 0–1: wet/dry mix
        kParamCount
    };

    const char* name() const override { return "Tape Emulation"; }
    const char* id()   const override { return "tape"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Saturation", 0.0f, 1.0f,     0.3f,    "",   false},
            {"Wow",        0.0f, 1.0f,     0.15f,   "",   false},
            {"Flutter",    0.0f, 1.0f,     0.1f,    "",   false},
            {"Hiss",       0.0f, 1.0f,     0.05f,   "",   false},
            {"Tone",       200.0f, 20000.0f, 8000.0f, "Hz", false},
            {"Wet/Dry",    0.0f, 1.0f,     1.0f,    "",   false, false, WidgetHint::DentedKnob},
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
    // Asymmetric tape saturation: softer clipping on positive, harder on negative
    static float tapeSaturate(float x) {
        if (x >= 0.0f)
            return std::tanh(x);
        else
            return std::tanh(x * 0.8f) * 1.25f;
    }

    // Linear interpolation read from circular buffer
    float readInterp(const std::vector<float>& buf, float delaySamples) const {
        float readPos = static_cast<float>(m_writePos) - delaySamples;
        if (readPos < 0.0f) readPos += m_delaySize;
        int idx0 = static_cast<int>(readPos) % m_delaySize;
        int idx1 = (idx0 + 1) % m_delaySize;
        float frac = readPos - std::floor(readPos);
        return buf[idx0] * (1.0f - frac) + buf[idx1] * frac;
    }

    // Simple noise generator (fast xorshift32)
    float nextNoise() {
        m_hissState ^= m_hissState << 13;
        m_hissState ^= m_hissState >> 17;
        m_hissState ^= m_hissState << 5;
        return static_cast<float>(static_cast<int32_t>(m_hissState)) / 2147483648.0f;
    }

    void updateToneFilter() {
        m_toneL.compute(Biquad::Type::LowPass, m_sampleRate, m_params[kTone], 0.0, 0.707);
        m_toneR.compute(Biquad::Type::LowPass, m_sampleRate, m_params[kTone], 0.0, 0.707);
        // Hiss is bandpass-filtered to sound like tape hiss (~1–8 kHz)
        m_hissFilterL.compute(Biquad::Type::BandPass, m_sampleRate, 3000.0, 0.0, 0.8);
        m_hissFilterR.compute(Biquad::Type::BandPass, m_sampleRate, 3000.0, 0.0, 0.8);
    }

    // Delay line for wow/flutter
    std::vector<float> m_delayL, m_delayR;
    int m_delaySize = 0, m_writePos = 0;

    // LFO phases
    double m_wowPhase = 0.0, m_flutterPhase = 0.0;

    // Filters
    Biquad m_toneL, m_toneR;
    Biquad m_hissFilterL, m_hissFilterR;

    // Noise generator state
    uint32_t m_hissState = 12345;

    float m_params[kParamCount] = {0.3f, 0.15f, 0.1f, 0.05f, 8000.0f, 1.0f};
};

} // namespace effects
} // namespace yawn
