#pragma once
// Bitcrusher — amplitude quantization + sample-rate decimation, with
// optional triangular dither and an anti-alias pre-filter.
//
// Two largely-independent stages:
//   1. Sample-rate reduction: zero-order hold every N samples, where
//      N = sampleRate / targetSr. Controls the "lo-fi"/aliasing
//      character — low values produce harsh metallic aliases.
//   2. Bit-depth quantization: round each held sample to the nearest
//      step on a 2/2^bits grid. With dither on, a small triangular
//      noise term is added before quantization so the steps don't
//      lock with low-amplitude signals (audible "graininess").
//
// Pre-filter (lowpass before the crusher) is mostly stylistic — at
// high pre-filter cutoff it has no effect; lowering it gives a
// "warmer" / less brittle sound by filtering away the highs that
// would otherwise produce ugly aliases through the SR-reduction.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yawn {
namespace effects {

class Bitcrusher : public AudioEffect {
public:
    enum Param {
        kBitDepth,    // 1..16
        kSampleRate,  // 100..48000 Hz (log)
        kPreFilter,   // 200..20000 Hz (log) — lowpass before crusher
        kDither,      // 0/1 toggle
        kMix,         // 0..1
        kParamCount
    };

    const char* name() const override { return "Bitcrusher"; }
    const char* id()   const override { return "bitcrusher"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        reset();
        updatePreFilter();
    }

    void reset() override {
        m_phase = 0.0;
        m_heldL = m_heldR = 0.0f;
        m_preL.reset();
        m_preR.reset();
        m_rngState = 0xC0FFEEu;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        const int   bits     = std::clamp(static_cast<int>(m_params[kBitDepth]),
                                          1, 16);
        const float targetSr = std::clamp(m_params[kSampleRate], 100.0f, 48000.0f);
        const bool  ditherOn = m_params[kDither] >= 0.5f;
        const float mix      = std::clamp(m_params[kMix], 0.0f, 1.0f);

        // Sample-rate reduction step. Phase accumulates by
        // (targetSr / sourceSr); when it crosses 1.0 we sample the
        // current input and hold for subsequent output samples.
        // Equivalent to a zero-order-hold downsampler followed by the
        // host-rate output (no upsampling — the held value is what
        // the user hears, with aliasing baked in by design).
        const double phaseInc = static_cast<double>(targetSr)
                              / static_cast<double>(m_sampleRate);

        // Quantization step size on a [-1, 1] signal, with `bits`
        // bits available across that range. 1-bit = 2 levels, 16-bit
        // = 65536. Dither amplitude = 1 LSB triangular (uniform in
        // [-step/2, +step/2] sum of two; that's TPDF).
        const float levels = static_cast<float>(1 << bits);
        const float step   = 2.0f / levels;
        const float halfStep = step * 0.5f;

        const bool stereo = (numChannels > 1);

        for (int i = 0; i < numFrames; ++i) {
            float dryL = buffer[i * numChannels];
            float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            // Pre-filter: lowpass before SR reduction. With cutoff at
            // 20 kHz the filter is effectively transparent at the
            // host rate, so users who don't want this can leave the
            // knob maxed.
            float inL = m_preL.process(dryL);
            float inR = m_preR.process(dryR);

            // SR-reduction: advance phase, sample-and-hold on wrap.
            m_phase += phaseInc;
            if (m_phase >= 1.0) {
                m_phase -= 1.0;
                m_heldL = inL;
                m_heldR = inR;
            }
            float crL = m_heldL;
            float crR = m_heldR;

            // Optional TPDF dither (sum of two uniform RVs → triangular
            // distribution). Adds at most 1 LSB of noise before the
            // round, which de-correlates quantization error from the
            // signal and audibly smooths low-amplitude content.
            if (ditherOn) {
                const float dL = (randFloat() - randFloat()) * halfStep;
                const float dR = (randFloat() - randFloat()) * halfStep;
                crL += dL;
                crR += dR;
            }

            // Bit-depth quantization. Round to nearest step. Mid-tread
            // (signal-symmetric) — avoids the DC offset that a mid-rise
            // quantizer would introduce at very low bit depths.
            crL = std::floor(crL / step + 0.5f) * step;
            crR = std::floor(crR / step + 0.5f) * step;

            // Wet/dry mix. Use the device's m_mix as a global override
            // multiplier so the chain-level mix knob still works in
            // addition to our per-effect kMix.
            const float w = mix * m_mix;
            const float d = 1.0f - w;
            buffer[i * numChannels] = dryL * d + crL * w;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * d + crR * w;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"Bits",       1.0f,    16.0f,    12.0f,   "",  false,
                false, WidgetHint::StepSelector},
            {"Sample Rate", 100.0f, 48000.0f, 24000.0f, "Hz", false},
            {"Pre Filter", 200.0f, 20000.0f, 20000.0f, "Hz", false},
            {"Dither",     0.0f,    1.0f,     0.0f,    "",  false,
                false, WidgetHint::Toggle},
            {"Mix",        0.0f,    1.0f,     1.0f,    "",  false,
                false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        const auto& pi = parameterInfo(index);
        m_params[index] = std::clamp(value, pi.minValue, pi.maxValue);
        if (index == kPreFilter) updatePreFilter();
    }

private:
    void updatePreFilter() {
        const float fc = std::clamp(m_params[kPreFilter], 200.0f, 20000.0f);
        // 1-pole LP per side. The shaping job here doesn't need
        // sharper roll-off; users dial the cutoff for a "feel" tilt
        // rather than a brick-wall anti-alias filter.
        m_preL.compute(Biquad::Type::LowPass, m_sampleRate, fc, 0.0, 0.707);
        m_preR.compute(Biquad::Type::LowPass, m_sampleRate, fc, 0.0, 0.707);
    }

    float randFloat() {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        return static_cast<float>(m_rngState & 0x7FFFFF) / 8388607.0f;
    }

    float    m_params[kParamCount] = {12.0f, 24000.0f, 20000.0f, 0.0f, 1.0f};
    Biquad   m_preL, m_preR;
    double   m_phase = 0.0;
    float    m_heldL = 0.0f, m_heldR = 0.0f;
    uint32_t m_rngState = 0xC0FFEEu;
};

} // namespace effects
} // namespace yawn
