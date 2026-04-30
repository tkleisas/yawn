#pragma once
// Phaser — LFO-modulated cascade of first-order all-pass filters.
//
// Each all-pass has unity magnitude across the spectrum but its
// phase response sweeps smoothly through 180° around the corner
// frequency. Sum the all-pass output with the dry signal and you
// get spectral notches at frequencies where the cumulative phase
// is exactly 180° — for an N-stage cascade that's ≈N/2 notches.
// Sweep the corner frequencies with an LFO and the notches march
// through the audio, producing the classic swooshy "Small Stone /
// Phase 90" sound.
//
// First-order all-pass coefficient (matches the standard form
//     y[n] = -a*x[n] + x[n-1] + a*y[n-1]):
//
//     a = (1 - tan(π*fc/sr)) / (1 + tan(π*fc/sr))
//
// is computed once per processed sample (per LFO step), shared
// across all stages and across channels — the L/R difference is
// a phase offset on the LFO, not a different `a`. We pre-compute
// `tan(π*fc/sr)` directly using std::tan; at typical fc < 4 kHz
// the cost is fine and the alternative (a coefficient table) buys
// us less than the readability we'd lose.
//
// Latency: zero. The all-pass cascade is sample-by-sample causal
// (every output sample depends only on the same-or-earlier input
// samples), so the dry path through `process()` is sample-accurate.
//
// Stereo behaviour: same `a` per sample, but the L and R LFOs are
// run with a configurable phase offset (`Stereo` knob, 0..180°).
// Width=0 → mono phaser, width=180 → fully counter-rotating LFOs,
// produces a wide stereo image without any actual delay between
// the channels.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace yawn {
namespace effects {

class Phaser : public AudioEffect {
public:
    static constexpr int kMaxStages = 12;

    enum Param {
        kRate,        // 0.01..10 Hz — LFO frequency
        kDepth,       // 0..1 — sweep range as a fraction of an octave
        kFeedback,    // -0.95..0.95 — last-stage tap fed back to input
        kStages,      // 2..12 — number of all-pass stages (even values
                      //         give symmetric notches; odd ones still
                      //         work but the bottom-most notch shifts)
        kCenterHz,    // 100..4000 Hz — centre of the LFO sweep
        kStereo,      // 0..180° — L/R LFO phase offset
        kMix,         // 0..1 — dry/wet
        kParamCount
    };

    const char* name() const override { return "Phaser"; }
    const char* id()   const override { return "phaser"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // Defaults — pleasantly noticeable without being a parody of
        // itself. Half-octave depth, 6 stages, ~0.5 Hz sweep, 90°
        // stereo offset, 50/50 mix.
        m_params[kRate]     = 0.5f;
        m_params[kDepth]    = 0.7f;
        m_params[kFeedback] = 0.5f;
        m_params[kStages]   = 6.0f;
        m_params[kCenterHz] = 700.0f;
        m_params[kStereo]   = 90.0f;
        m_params[kMix]      = 0.5f;
        reset();
    }

    void reset() override {
        for (auto& z : m_zL) z = 0.0f;
        for (auto& z : m_zR) z = 0.0f;
        m_phaseL = 0.0;
        m_phaseR = 0.0;
        m_fbL = 0.0f;
        m_fbR = 0.0f;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        const float sr = static_cast<float>(m_sampleRate);
        const float rate     = std::clamp(m_params[kRate],   0.01f, 10.0f);
        const float depthOct = std::clamp(m_params[kDepth],  0.0f,  1.0f) * 1.5f; // ±1.5 octaves max
        const float fb       = std::clamp(m_params[kFeedback], -0.95f, 0.95f);
        const int   stages   = std::clamp(static_cast<int>(std::round(m_params[kStages])),
                                          2, kMaxStages);
        const float centerHz = std::clamp(m_params[kCenterHz], 50.0f, 8000.0f);
        const float stereoOffsetRad = std::clamp(m_params[kStereo], 0.0f, 180.0f)
                                    * (3.14159265358979f / 180.0f);
        const float mix = std::clamp(m_params[kMix], 0.0f, 1.0f);
        const bool  stereo = (numChannels > 1);

        const double phaseInc = static_cast<double>(rate) / static_cast<double>(sr);

        // Apply the stereo offset by sliding the R LFO ahead of L's
        // current value — we don't store m_phaseR independently of
        // m_phaseL because the offset is constant per block (and the
        // user might twiddle the Stereo knob mid-playback, in which
        // case the right channel jumps to track the new offset
        // without the left channel skipping).
        for (int i = 0; i < numFrames; ++i) {
            const double tau = 6.28318530717958647692;  // 2π
            // Bipolar [-1, 1] LFO sample (sine).
            const float lfoL = std::sin(static_cast<float>(m_phaseL * tau));
            const float lfoR = std::sin(static_cast<float>(m_phaseL * tau)
                                        + stereoOffsetRad);

            // LFO → corner frequency in Hz. depthOct=1 means the
            // sweep covers ±1 octave around the centre. powf(2, x)
            // is the standard "octaves to ratio" mapping; the result
            // is clamped to a sane Nyquist-safe range so very high
            // rates / depths don't drive the all-pass filter beyond
            // its stable region.
            const float fcL = std::clamp(centerHz * std::pow(2.0f, lfoL * depthOct),
                                         20.0f, sr * 0.45f);
            const float fcR = std::clamp(centerHz * std::pow(2.0f, lfoR * depthOct),
                                         20.0f, sr * 0.45f);
            const float aL  = allPassCoeff(fcL, sr);
            const float aR  = allPassCoeff(fcR, sr);

            // Per-channel cascade. Dry input + feedback from the
            // last sample's last-stage output goes into stage 0; we
            // read the pre-input feedback into a local so a dry-spike
            // doesn't get amplified within the same sample.
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            float sigL = dryL + m_fbL * fb;
            float sigR = stereo ? (dryR + m_fbR * fb) : sigL;

            for (int s = 0; s < stages; ++s) {
                const float xL = sigL;
                sigL = -aL * xL + m_zL[s];
                m_zL[s] = xL + aL * sigL;
                if (stereo) {
                    const float xR = sigR;
                    sigR = -aR * xR + m_zR[s];
                    m_zR[s] = xR + aR * sigR;
                }
            }

            m_fbL = sigL;
            if (stereo) m_fbR = sigR;

            // Mix dry + wet. At mix=0 the output is pure dry, at
            // mix=1 it's pure wet. The classic phaser sound is the
            // sum of dry+wet (mix=0.5) — at that ratio the comb
            // filter notches are deepest.
            const float wL = mix;
            const float dM = 1.0f - mix;
            buffer[i * numChannels] = dryL * dM + sigL * wL;
            if (stereo) buffer[i * numChannels + 1] = dryR * dM + sigR * wL;

            m_phaseL += phaseInc;
            if (m_phaseL >= 1.0) m_phaseL -= 1.0;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Rate",     0.01f, 10.0f, 0.5f,   "Hz", false},
            {"Depth",    0.0f,  1.0f,  0.7f,   "",   false, false, WidgetHint::DentedKnob},
            {"Feedback", -0.95f, 0.95f, 0.5f,  "",   false, false, WidgetHint::DentedKnob},
            {"Stages",   2.0f,  12.0f, 6.0f,   "",   false, false, WidgetHint::StepSelector},
            {"Center",   100.0f, 4000.0f, 700.0f, "Hz", false},
            {"Stereo",   0.0f,  180.0f, 90.0f, "°",  false},
            {"Mix",      0.0f,  1.0f,  0.5f,   "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        m_params[index] = value;
    }

private:
    // (1 - tan(π·fc/sr)) / (1 + tan(π·fc/sr)) — the standard direct-
    // form first-order all-pass coefficient. Per-sample call inside
    // the inner loop — std::tan on x86 is ~10 ns, fine for an LFO-
    // modulated phaser. A tan-LUT would shave a bit but at the cost
    // of fc-resolution artefacts when the LFO crosses bin edges; we
    // pick correctness over micro-perf here.
    static float allPassCoeff(float fc, float sr) {
        const float t = std::tan(3.14159265358979f * fc / sr);
        return (1.0f - t) / (1.0f + t);
    }

    float m_params[kParamCount] = {};
    std::array<float, kMaxStages> m_zL{};
    std::array<float, kMaxStages> m_zR{};
    double m_phaseL = 0.0;
    double m_phaseR = 0.0;       // unused — R LFO is L+offset, but
                                  // the field is here in case we
                                  // ever want true-independent rates.
    float  m_fbL    = 0.0f;
    float  m_fbR    = 0.0f;
};

} // namespace effects
} // namespace yawn
