#pragma once
// Wah — resonant bandpass filter with two driver modes: Pedal
// (manual / MIDI-Learn / automation) and Auto (envelope-driven).
//
// The classic "wah-wah" sound comes from sweeping a high-Q
// bandpass filter's centre frequency across the audio. Real wah
// pedals (Vox V846, Dunlop Cry Baby, Mu-Tron III) are LC tank
// circuits with Q in the 4-8 range; modern envelope-followed
// "auto-wahs" replace the foot pedal with a level-driven envelope
// of the input signal.
//
// We deliberately keep this scoped:
//   * Two modes only — Pedal and Auto. No LFO mode (use the LFO
//     MIDI effect to drive the Pedal param if you want hands-free
//     sweeping; that keeps this device's parameter surface small
//     and lets the LFO be tempo-synced via the existing path).
//   * Bandpass only. Some auto-wahs ship lowpass-with-resonance
//     instead — that voicing is more easily had via the existing
//     Filter device with an Envelope Follower routed to its
//     cutoff. Keeping the wah knob count low means people pick it
//     up fast.
//
// Filter: TPT (topology-preserving transform) state-variable
// filter from Vadim Zavalishin's "The Art of VA Filter Design"
// (Cytomic implementation form). Cheap per-sample coefficient
// update (one tan() and three reciprocals), unconditionally
// stable up to Nyquist, and the BP output is exactly what we
// want for a wah voice.
//
// Latency: zero. SVF is sample-by-sample causal.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class Wah : public AudioEffect {
public:
    enum Mode {
        ModePedal = 0,
        ModeAuto  = 1,
    };

    enum Param {
        kMode,         // 0=Pedal, 1=Auto (StepSelector)
        kPedal,        // 0..1 — manual position (Pedal mode) or
                       //         baseline offset (Auto mode)
        kBottomHz,     // 200..1000 Hz — bottom of sweep
        kTopHz,        // 800..4000 Hz — top of sweep
        kQ,            // 1..16 — bandpass resonance
        kSensitivity,  // 0..1 — Auto mode envelope→pedal scaling
        kResponse,     // 1..200 ms — Auto mode envelope time-constant
                       //   (single value drives both attack + release;
                       //    two-knob attack/release exists on the
                       //    Envelope Follower for fancier auto-wahs)
        kMix,          // 0..1 — dry/wet (1.0 = full wet, classic wah)
        kParamCount
    };

    const char* name() const override { return "Wah"; }
    const char* id()   const override { return "wah"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_params[kMode]        = static_cast<float>(ModePedal);
        m_params[kPedal]       = 0.30f;
        m_params[kBottomHz]    = 350.0f;
        m_params[kTopHz]       = 2200.0f;
        m_params[kQ]           = 6.0f;
        m_params[kSensitivity] = 0.70f;
        m_params[kResponse]    = 30.0f;
        m_params[kMix]         = 1.0f;
        reset();
    }

    void reset() override {
        m_s1L = m_s2L = 0.0f;
        m_s1R = m_s2R = 0.0f;
        m_envL = m_envR = 0.0f;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        const float sr = static_cast<float>(m_sampleRate);
        const int   mode = static_cast<int>(std::round(m_params[kMode]));
        const float pedal = std::clamp(m_params[kPedal], 0.0f, 1.0f);
        const float fLo = std::clamp(m_params[kBottomHz], 50.0f, sr * 0.45f);
        const float fHi = std::clamp(m_params[kTopHz],    fLo + 1.0f, sr * 0.45f);
        const float Q   = std::clamp(m_params[kQ], 0.5f, 16.0f);
        const float sens = std::clamp(m_params[kSensitivity], 0.0f, 1.0f);
        const float responseMs = std::clamp(m_params[kResponse], 0.5f, 1000.0f);
        const float mix = std::clamp(m_params[kMix], 0.0f, 1.0f);
        const bool  stereo = (numChannels > 1);

        // Envelope time-constant — single value drives both attack
        // and release for the simpler auto-wah voicing. Mu-Tron
        // pedals had separate attack/decay on the front panel; if
        // the user wants that they can use the Envelope Follower
        // device with its dedicated A/R knobs and route its
        // modulation output to this Wah's Pedal param.
        const float coef = std::exp(-1.0f / std::max(1.0f, responseMs * 0.001f * sr));
        const float k = 1.0f / Q;   // SVF damping (R = 1/(2Q))

        // Log-frequency sweep — feels right for a filter cutoff;
        // matches how real LC wah circuits move (geometric, not
        // linear). Defined in cents-of-octave space so the same
        // Pedal range gives an even-feeling sweep regardless of
        // Bottom/Top knob settings.
        const float logLo = std::log(fLo);
        const float logHi = std::log(fHi);

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            // ── Detector (Auto mode only) ──
            // Peak follower per channel; the higher of the two
            // drives the wah for natural mono-summed behaviour.
            // In Pedal mode we still update the envelope state so
            // there's no "warm-up" glitch when switching modes.
            const float rectL = std::abs(dryL);
            const float rectR = std::abs(dryR);
            m_envL = (rectL > m_envL) ? rectL : m_envL * coef;
            m_envR = (rectR > m_envR) ? rectR : m_envR * coef;
            const float env = std::max(m_envL, m_envR);

            // ── Drive position (0..1) ──
            float drive = pedal;
            if (mode == ModeAuto) {
                // Boost the envelope by sens (1.0 → unity, 0.5 → halved
                // contribution) and ADD to the pedal baseline. Letting
                // pedal serve as a baseline in Auto mode is what makes
                // it feel "musical" — the wah floor doesn't slam shut
                // when input drops.
                drive = std::clamp(pedal + env * sens * 4.0f, 0.0f, 1.0f);
            }

            // Map drive → fc on the log scale defined above.
            const float fc = std::exp(logLo + (logHi - logLo) * drive);

            // ── TPT SVF (Cytomic form) ──
            // From Vadim Zavalishin's "The Art of VA Filter Design"
            // and the Cytomic SVF tech note. Stable for any fc <
            // Nyquist, well-behaved at high resonance (where naïve
            // Chamberlin SVFs blow up).
            //
            //     g = tan(π·fc/fs)
            //     k = 1/Q
            //     a1 = 1 / (1 + g·(g + k))
            //     a2 = g·a1
            //     a3 = g·a2
            //     v1 = a1·s1 + a2·(x − s2)
            //     v2 = s2 + a2·s1 + a3·(x − s2)
            //     s1 ← 2·v1 − s1
            //     s2 ← 2·v2 − s2
            //     bp = v1   ; lp = v2   ; hp = x − k·v1 − v2
            //
            // Computed per-sample because fc moves faster than
            // block-rate in Auto mode; the cost is one tan() and
            // ~6 multiplies per sample per channel.
            const float g  = std::tan(3.14159265358979f * fc / sr);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            // L
            {
                const float v3 = dryL - m_s2L;
                const float v1 = a1 * m_s1L + a2 * v3;
                const float v2 = m_s2L + a2 * m_s1L + a3 * v3;
                m_s1L = 2.0f * v1 - m_s1L;
                m_s2L = 2.0f * v2 - m_s2L;
                const float wet = v1;     // bandpass = wet output
                buffer[i * numChannels] = dryL * (1.0f - mix) + wet * mix;
            }
            if (stereo) {
                const float v3 = dryR - m_s2R;
                const float v1 = a1 * m_s1R + a2 * v3;
                const float v2 = m_s2R + a2 * m_s1R + a3 * v3;
                m_s1R = 2.0f * v1 - m_s1R;
                m_s2R = 2.0f * v2 - m_s2R;
                const float wet = v1;
                buffer[i * numChannels + 1] = dryR * (1.0f - mix) + wet * mix;
            }
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[] = {
            {"Mode",        0.0f,    1.0f,    0.0f,    "",   false, false, WidgetHint::StepSelector},
            {"Pedal",       0.0f,    1.0f,    0.30f,   "",   false, false, WidgetHint::DentedKnob},
            {"Bottom",      200.0f,  1000.0f, 350.0f,  "Hz", false},
            {"Top",         800.0f,  4000.0f, 2200.0f, "Hz", false},
            {"Q",           1.0f,    16.0f,   6.0f,    "",   false},
            {"Sensitivity", 0.0f,    1.0f,    0.70f,   "",   false, false, WidgetHint::DentedKnob},
            {"Response",    1.0f,    200.0f,  30.0f,   "ms", false},
            {"Mix",         0.0f,    1.0f,    1.0f,    "",   false, false, WidgetHint::DentedKnob},
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
    float m_params[kParamCount] = {};
    // SVF integrator state (per channel).
    float m_s1L = 0.0f, m_s2L = 0.0f;
    float m_s1R = 0.0f, m_s2R = 0.0f;
    // Envelope follower state (Auto mode).
    float m_envL = 0.0f, m_envR = 0.0f;
};

} // namespace effects
} // namespace yawn
