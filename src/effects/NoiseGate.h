#pragma once
// NoiseGate — full-featured downward expander / gate.
//
// Closes when the detection signal drops below the close threshold,
// opens when it rises above the open threshold (open ≥ close → the
// gap is the user-set hysteresis). State machine:
//
//                ┌───── below close ─────┐
//                │  (start hold timer)   │
//                ▼                       │
//   Closed ──── above open ──→ Opening ──┴──→ Open ── below close ──→ Holding
//      ▲                          │                                       │
//      │                          └────── above open re-opens ────────────┘
//      └──────── release done ──── Releasing ←── hold timer expires
//
// Range is the max attenuation when fully closed (-90 dB ≈ "off",
// values closer to 0 dB give expander-style "soft" gating).
//
// Lookahead delays the audio path by N samples so the detection
// signal — which is read from the un-delayed input — gets to "see
// the future" of the audio it's gating. Without lookahead, fast
// attacks always clip the front of transients; ~3-5 ms typically
// recovers them cleanly.
//
// Sidechain (when toggled and a buffer is routed) replaces the
// internal-input detection signal with the sidechain track's audio.
// Ducking mode inverts the polarity — gain closes when the
// sidechain is hot, opens when it's quiet. Useful for the classic
// "kick ducks pad" pump.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class NoiseGate : public AudioEffect {
public:
    enum Param {
        kThreshold,     // -80..0 dB — open threshold
        kHysteresis,    // 0..24 dB — close = open - hysteresis
        kAttack,        // 0.1..100 ms
        kHold,          // 0..1000 ms
        kRelease,       // 1..2000 ms
        kLookahead,     // 0..10 ms
        kRange,         // -90..0 dB — closed-state attenuation
        kSidechain,     // 0/1 — use sidechain buffer for detection
        kDucking,       // 0/1 — invert polarity (close when hot)
        kMix,           // 0..1 — wet/dry
        kParamCount
    };

    const char* name() const override { return "Noise Gate"; }
    const char* id()   const override { return "noisegate"; }
    bool supportsSidechain() const override { return true; }

    // Lookahead is parameter-driven (kLookahead, in ms). The audio
    // path is delayed by exactly this many samples so detection can
    // see the future; reported here for the host's plugin-delay-
    // compensation calculation (Latency P2).
    int latencySamples() const override {
        const int s = static_cast<int>(
            m_params[kLookahead] * 0.001f * static_cast<float>(m_sampleRate));
        // Match the runtime clamp inside process() so reported
        // latency never exceeds what the engine actually delays by.
        const int maxLook = static_cast<int>(m_delayL.size()) - 1;
        return std::clamp(s, 0, maxLook < 0 ? 0 : maxLook);
    }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // Pre-allocate the lookahead delay line at the max possible
        // size (10 ms at 192 kHz = 1920 samples) so process() never
        // re-allocates on the audio thread.
        const int maxLook = static_cast<int>(0.010 * 192000.0) + 16;
        m_delayL.assign(maxLook, 0.0f);
        m_delayR.assign(maxLook, 0.0f);
        reset();
    }

    void reset() override {
        m_state = State::Closed;
        m_currentGain = std::pow(10.0f, m_params[kRange] / 20.0f);
        m_holdSamples = 0;
        m_attackPos = 0.0f;
        m_releasePos = 0.0f;
        m_releaseStartGain = m_currentGain;
        m_writePos = 0;
        std::fill(m_delayL.begin(), m_delayL.end(), 0.0f);
        std::fill(m_delayR.begin(), m_delayR.end(), 0.0f);
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        const float openThreshDb  = m_params[kThreshold];
        const float closeThreshDb = openThreshDb - m_params[kHysteresis];
        const float openThresh    = std::pow(10.0f, openThreshDb  / 20.0f);
        const float closeThresh   = std::pow(10.0f, closeThreshDb / 20.0f);

        const float sr = static_cast<float>(m_sampleRate);
        const int   attackSamples  = std::max(1, static_cast<int>(m_params[kAttack]  * 0.001f * sr));
        const int   releaseSamples = std::max(1, static_cast<int>(m_params[kRelease] * 0.001f * sr));
        const int   holdSamplesMax = std::max(0, static_cast<int>(m_params[kHold]    * 0.001f * sr));
        const int   lookSamples    = std::clamp(
            static_cast<int>(m_params[kLookahead] * 0.001f * sr),
            0,
            static_cast<int>(m_delayL.size()) - 1);
        const float closedGain     = std::pow(10.0f, m_params[kRange] / 20.0f);
        const bool  useSidechain   = (m_params[kSidechain] >= 0.5f) &&
                                     (m_sidechainBuffer != nullptr);
        const bool  ducking        = (m_params[kDucking]   >= 0.5f);
        const float mix            = std::clamp(m_params[kMix], 0.0f, 1.0f);

        const bool stereo = (numChannels > 1);
        const int delayLen = std::max(1, lookSamples + 1);

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            // Detection signal: peak of L+R averaged. Sidechain path
            // uses the routed buffer (interleaved, same channel layout
            // as the host buffer); internal path uses the dry input
            // before any gain change so detection stays consistent
            // even as the gate is closing.
            float detL, detR;
            if (useSidechain) {
                detL = m_sidechainBuffer[i * numChannels];
                detR = stereo ? m_sidechainBuffer[i * numChannels + 1] : detL;
            } else {
                detL = dryL;
                detR = dryR;
            }
            const float det = std::max(std::abs(detL), std::abs(detR));

            // Gate state machine. Hysteresis: open at openThresh,
            // close at closeThresh (lower). Hold timer keeps the
            // gate open for `hold` ms after detection drops below
            // closeThresh — prevents chattering on signals that
            // hover near the threshold.
            const bool openCondition  = ducking ? (det <  openThresh)
                                                : (det >= openThresh);
            const bool closeCondition = ducking ? (det >  closeThresh)
                                                : (det <= closeThresh);

            switch (m_state) {
                case State::Closed:
                    if (openCondition) {
                        m_state = State::Opening;
                        m_attackPos = 0.0f;
                    }
                    break;
                case State::Opening:
                    m_attackPos += 1.0f / static_cast<float>(attackSamples);
                    if (m_attackPos >= 1.0f) {
                        m_attackPos = 1.0f;
                        m_state = State::Open;
                    }
                    if (closeCondition) {
                        // Started closing again before fully open —
                        // skip Hold and start Releasing immediately
                        // from current gain.
                        m_state = State::Releasing;
                        m_releasePos = 0.0f;
                        m_releaseStartGain = m_currentGain;
                    }
                    break;
                case State::Open:
                    if (closeCondition) {
                        m_state = State::Holding;
                        m_holdSamples = holdSamplesMax;
                    }
                    break;
                case State::Holding:
                    if (openCondition) {
                        m_state = State::Open;
                    } else if (--m_holdSamples <= 0) {
                        m_state = State::Releasing;
                        m_releasePos = 0.0f;
                        m_releaseStartGain = m_currentGain;
                    }
                    break;
                case State::Releasing:
                    if (openCondition) {
                        m_state = State::Opening;
                        m_attackPos = m_currentGain;   // start ramp from current
                    } else {
                        m_releasePos += 1.0f / static_cast<float>(releaseSamples);
                        if (m_releasePos >= 1.0f) {
                            m_releasePos = 1.0f;
                            m_state = State::Closed;
                        }
                    }
                    break;
            }

            // Translate state to gain. Linear ramps in dB-space tend
            // to feel more natural than linear-amplitude ramps for
            // gating; we use linear-amplitude here for simplicity
            // and small CPU cost — at typical attack/release times
            // the audible difference is negligible.
            float targetGain = closedGain;
            switch (m_state) {
                case State::Closed:
                    targetGain = closedGain;
                    break;
                case State::Opening:
                    targetGain = closedGain + (1.0f - closedGain) * m_attackPos;
                    break;
                case State::Open:
                case State::Holding:
                    targetGain = 1.0f;
                    break;
                case State::Releasing:
                    targetGain = closedGain +
                        (m_releaseStartGain - closedGain) * (1.0f - m_releasePos);
                    break;
            }
            m_currentGain = targetGain;

            // Lookahead: write the dry sample into the delay line, read
            // out the sample lookSamples behind the write position. The
            // gain we just computed reflects the CURRENT detection,
            // which has effectively "seen the future" relative to the
            // delayed audio we're writing out. Lookahead = 0 → readPos
            // == writePos → no delay, no lookahead behaviour.
            m_delayL[m_writePos] = dryL;
            m_delayR[m_writePos] = dryR;
            const int readPos = (m_writePos - lookSamples + delayLen) % delayLen;
            const float dlL = m_delayL[readPos];
            const float dlR = m_delayR[readPos];
            m_writePos = (m_writePos + 1) % delayLen;

            const float wetL = dlL * m_currentGain;
            const float wetR = dlR * m_currentGain;

            const float w = mix * m_mix;
            const float d = 1.0f - w;
            buffer[i * numChannels] = dryL * d + wetL * w;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * d + wetR * w;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"Thresh",   -80.0f,  0.0f, -40.0f, "dB", false},
            {"Hyst",       0.0f, 24.0f,   6.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Atk",        0.1f,100.0f,   5.0f, "ms", false},
            {"Hold",       0.0f,1000.0f, 50.0f, "ms", false},
            {"Rel",        1.0f,2000.0f,200.0f, "ms", false},
            {"Look",       0.0f, 10.0f,   0.0f, "ms", false, false, WidgetHint::DentedKnob},
            {"Range",    -90.0f,  0.0f, -90.0f, "dB", false},
            {"SC",         0.0f,  1.0f,   0.0f, "",   false, false, WidgetHint::Toggle},
            {"Duck",       0.0f,  1.0f,   0.0f, "",   false, false, WidgetHint::Toggle},
            {"Mix",        0.0f,  1.0f,   1.0f, "",   false, false, WidgetHint::DentedKnob},
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
    }

private:
    enum class State : uint8_t { Closed, Opening, Open, Holding, Releasing };

    float m_params[kParamCount] = {
        -40.0f, 6.0f, 5.0f, 50.0f, 200.0f, 0.0f, -90.0f, 0.0f, 0.0f, 1.0f
    };

    State              m_state = State::Closed;
    float              m_currentGain = 0.0f;
    int                m_holdSamples = 0;
    float              m_attackPos = 0.0f;
    float              m_releasePos = 0.0f;
    float              m_releaseStartGain = 0.0f;

    // Lookahead delay line (per side). Sized at init() to the max
    // lookahead duration so process() never reallocates.
    std::vector<float> m_delayL;
    std::vector<float> m_delayR;
    int                m_writePos = 0;
};

} // namespace effects
} // namespace yawn
