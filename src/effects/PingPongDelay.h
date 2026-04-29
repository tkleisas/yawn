#pragma once
// PingPongDelay — dedicated stereo bouncing delay.
//
// Different from the regular Delay's "Ping-Pong" toggle in three
// meaningful ways:
//   1. Independent L/R times: classic ping-pong is hosted on
//      different beat divisions per side (e.g. L=1/4 dotted, R=1/8)
//      so the bounce isn't a pure mono echo. Two Time knobs make
//      that one-click.
//   2. Explicit Cross-Feedback knob: how much of L's tail feeds R
//      (and vice-versa). 1.0 = pure ping-pong, 0.0 = two
//      independent stereo delays with no cross-coupling.
//   3. Width control over the input split: 0 → both delays receive
//      mono (L+R)/2, 1 → each delay receives only its own side.
//      Lets you decide how wide the dry-input distribution is
//      independently of the bounce character.
//
// Feedback path runs through a one-pole lowpass per side so each
// repeat darkens — the standard "tape echo" behaviour that makes
// long feedback usable without ear-piercing buildup.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class PingPongDelay : public AudioEffect {
public:
    enum Param {
        kTimeL,        // 1..4000 ms
        kTimeR,        // 1..4000 ms
        kFeedback,     // 0..0.95
        kCrossFB,      // 0..1 — amount of L→R / R→L coupling
        kWidth,        // 0..1 — stereo spread of dry input into delays
        kFilter,       // 200..20000 Hz feedback-path lowpass
        kSync,         // 0/1 — tempo sync (snaps L/R times to beat divs)
        kMix,          // 0..1
        kParamCount
    };

    const char* name() const override { return "Ping-Pong Delay"; }
    const char* id()   const override { return "pingpongdelay"; }

    void setBPM(double bpm) { m_bpm = bpm; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // 4 seconds per side. Same headroom as Delay; matches the max
        // Time knob (4000 ms) so the user can never wrap around.
        const int maxSamples = static_cast<int>(4.0 * sampleRate);
        m_bufferL.assign(maxSamples, 0.0f);
        m_bufferR.assign(maxSamples, 0.0f);
        m_maxDelay = maxSamples;
        m_writePos = 0;
        updateFilters();
        reset();
    }

    void reset() override {
        std::fill(m_bufferL.begin(), m_bufferL.end(), 0.0f);
        std::fill(m_bufferR.begin(), m_bufferR.end(), 0.0f);
        m_writePos = 0;
        m_filterL.reset();
        m_filterR.reset();
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        int delayLSamp = computeDelaySamples(m_params[kTimeL]);
        int delayRSamp = computeDelaySamples(m_params[kTimeR]);
        delayLSamp = std::clamp(delayLSamp, 1, m_maxDelay - 1);
        delayRSamp = std::clamp(delayRSamp, 1, m_maxDelay - 1);

        const float feedback = std::clamp(m_params[kFeedback], 0.0f, 0.95f);
        const float crossFB  = std::clamp(m_params[kCrossFB],  0.0f, 1.0f);
        const float width    = std::clamp(m_params[kWidth],    0.0f, 1.0f);
        const float mix      = std::clamp(m_params[kMix],      0.0f, 1.0f);
        const bool stereo    = (numChannels > 1);

        // Width = 0 → both delays receive (L+R)/2; width = 1 → L
        // delay receives only L, R delay receives only R. Linear
        // interpolation between the two extremes — the user
        // perceives the spread continuously.
        const float dirGain = width;
        const float xtraGain = 1.0f - width;

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;
            const float mid  = (dryL + dryR) * 0.5f;
            const float inL  = dryL * dirGain + mid * xtraGain;
            const float inR  = dryR * dirGain + mid * xtraGain;

            // Read taps with per-side delay.
            const int readL = (m_writePos - delayLSamp + m_maxDelay) % m_maxDelay;
            const int readR = (m_writePos - delayRSamp + m_maxDelay) % m_maxDelay;
            const float delL = m_bufferL[readL];
            const float delR = m_bufferR[readR];

            // Feedback through the per-side lowpass — each repeat
            // gets darker, audibly emulating tape echo. crossFB
            // mixes "bounced" feedback (other side) with "self"
            // feedback (same side):
            //   crossFB = 1.0 → write[L] gets R's feedback (pure
            //                   ping-pong, every echo lands on the
            //                   opposite side from the previous)
            //   crossFB = 0.0 → write[L] gets L's feedback (two
            //                   independent stereo delays)
            //   crossFB = 0.5 → 50/50 mix
            const float fbL = m_filterL.process(delL) * feedback;
            const float fbR = m_filterR.process(delR) * feedback;
            const float writeL = inL + fbR * crossFB + fbL * (1.0f - crossFB);
            const float writeR = inR + fbL * crossFB + fbR * (1.0f - crossFB);
            m_bufferL[m_writePos] = writeL;
            m_bufferR[m_writePos] = writeR;

            const float w = mix * m_mix;
            const float d = 1.0f - w;
            buffer[i * numChannels] = dryL * d + delL * w;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * d + delR * w;

            m_writePos = (m_writePos + 1) % m_maxDelay;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"Time L",    1.0f, 4000.0f,  375.0f,  "ms", false},
            {"Time R",    1.0f, 4000.0f,  500.0f,  "ms", false},
            {"FB",        0.0f, 0.95f,    0.45f,   "",   false, false, WidgetHint::DentedKnob},
            {"Cross",     0.0f, 1.0f,     1.0f,    "",   false, false, WidgetHint::DentedKnob},
            {"Width",     0.0f, 1.0f,     1.0f,    "",   false, false, WidgetHint::DentedKnob},
            {"Filter",    200.0f, 20000.0f, 8000.0f, "Hz", false},
            {"Sync",      0.0f, 1.0f,     0.0f,    "",   false, false, WidgetHint::Toggle},
            {"Mix",       0.0f, 1.0f,     0.35f,   "",   false, false, WidgetHint::DentedKnob},
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
        if (index == kFilter) updateFilters();
    }

private:
    int computeDelaySamples(float timeMs) const {
        if (m_params[kSync] >= 0.5f && m_bpm > 0.0) {
            // Same beat-snap logic as Delay — tempo-sync mode
            // quantizes the requested ms to the nearest power-of-2
            // beat division. Per-side independence means you can
            // dial L=125ms (→ 1/32) and R=375ms (→ 1/8 dotted)
            // without fighting one global Time knob.
            const double beatsPerSecond = m_bpm / 60.0;
            const double secondsPerBeat = 1.0 / beatsPerSecond;
            const double timeSec = timeMs * 0.001;
            const double beats   = timeSec * beatsPerSecond;
            const double quantized = std::pow(2.0, std::round(std::log2(beats)));
            return static_cast<int>(quantized * secondsPerBeat * m_sampleRate);
        }
        return static_cast<int>(timeMs * 0.001 * m_sampleRate);
    }

    void updateFilters() {
        const float fc = std::clamp(m_params[kFilter], 200.0f, 20000.0f);
        m_filterL.compute(Biquad::Type::LowPass, m_sampleRate, fc, 0.0, 0.707);
        m_filterR.compute(Biquad::Type::LowPass, m_sampleRate, fc, 0.0, 0.707);
    }

    std::vector<float> m_bufferL, m_bufferR;
    int    m_maxDelay = 0;
    int    m_writePos = 0;
    Biquad m_filterL, m_filterR;
    double m_bpm = 120.0;
    float  m_params[kParamCount] = {
        375.0f, 500.0f, 0.45f, 1.0f, 1.0f, 8000.0f, 0.0f, 0.35f
    };
};

} // namespace effects
} // namespace yawn
