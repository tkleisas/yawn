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

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;

        // Delay buffer: enough for max wow/flutter excursion (~50ms)
        int maxDelay = static_cast<int>(0.05 * sampleRate) + 256;
        m_delayL.assign(maxDelay, 0.0f);
        m_delayR.assign(maxDelay, 0.0f);
        m_delaySize = maxDelay;

        updateToneFilter();
        reset();
    }

    void reset() override {
        std::fill(m_delayL.begin(), m_delayL.end(), 0.0f);
        std::fill(m_delayR.begin(), m_delayR.end(), 0.0f);
        m_writePos = 0;
        m_wowPhase = 0.0;
        m_flutterPhase = 0.0;
        m_hissState = 12345;
        m_toneL.reset();
        m_toneR.reset();
        m_hissFilterL.reset();
        m_hissFilterR.reset();
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        float sat   = m_params[kSaturation];
        float wow   = m_params[kWow];
        float flut  = m_params[kFlutter];
        float hiss  = m_params[kHiss];
        float wet   = m_params[kWetDry];
        float dry   = 1.0f - wet;

        // Saturation drive: map 0–1 to 1–8x gain
        float drive = 1.0f + sat * 7.0f;
        float makeupGain = 1.0f / (1.0f + sat * 1.5f);

        // Wow: slow LFO (~0.5–6 Hz), depth up to 3ms
        double wowRate = 0.5 + wow * 5.5;
        double wowInc  = wowRate / m_sampleRate;
        float  wowDepth = wow * static_cast<float>(0.003 * m_sampleRate);

        // Flutter: fast LFO (~6–20 Hz), depth up to 0.5ms
        double flutRate = 6.0 + flut * 14.0;
        double flutInc  = flutRate / m_sampleRate;
        float  flutDepth = flut * static_cast<float>(0.0005 * m_sampleRate);

        // Base delay: 10ms (tape transport latency)
        float baseDelay = static_cast<float>(0.01 * m_sampleRate);

        // Hiss level: -60dB to -20dB
        float hissGain = hiss * 0.01f;

        for (int i = 0; i < numFrames; ++i) {
            float inL = buffer[i * numChannels];
            float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;

            // 1. Tape saturation (asymmetric soft clipping)
            float satL = tapeSaturate(inL * drive) * makeupGain;
            float satR = tapeSaturate(inR * drive) * makeupGain;

            // 2. Write saturated signal into delay line
            m_delayL[m_writePos] = satL;
            m_delayR[m_writePos] = satR;

            // 3. Wow + flutter modulation
            float wowMod = static_cast<float>(std::sin(m_wowPhase * 6.283185307));
            float flutMod = static_cast<float>(std::sin(m_flutterPhase * 6.283185307));
            float modDelay = baseDelay + wowMod * wowDepth + flutMod * flutDepth;

            float outL = readInterp(m_delayL, modDelay);
            float outR = readInterp(m_delayR, modDelay);

            // 4. Tone filter (high-frequency rolloff)
            outL = m_toneL.process(outL);
            outR = m_toneR.process(outR);

            // 5. Tape hiss (filtered noise)
            if (hissGain > 0.0f) {
                float noiseL = nextNoise() * hissGain;
                float noiseR = nextNoise() * hissGain;
                outL += m_hissFilterL.process(noiseL);
                outR += m_hissFilterR.process(noiseR);
            }

            // 6. Wet/dry mix
            buffer[i * numChannels] = dry * inL + wet * outL;
            if (numChannels > 1)
                buffer[i * numChannels + 1] = dry * inR + wet * outR;

            m_writePos = (m_writePos + 1) % m_delaySize;
            m_wowPhase += wowInc;
            if (m_wowPhase >= 1.0) m_wowPhase -= 1.0;
            m_flutterPhase += flutInc;
            if (m_flutterPhase >= 1.0) m_flutterPhase -= 1.0;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Saturation", 0.0f, 1.0f,     0.3f,    "",   false},
            {"Wow",        0.0f, 1.0f,     0.15f,   "",   false},
            {"Flutter",    0.0f, 1.0f,     0.1f,    "",   false},
            {"Hiss",       0.0f, 1.0f,     0.05f,   "",   false},
            {"Tone",       200.0f, 20000.0f, 8000.0f, "Hz", false},
            {"Wet/Dry",    0.0f, 1.0f,     1.0f,    "",   false},
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
