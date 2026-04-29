#pragma once
// EnvelopeFollower — audio level → control signal, optionally driving
// a filter on the audio path. Doubles as a routable modulation
// source via AudioEffect::hasModulationOutput / modulationValue, so
// other devices (visual engine parameters, modulation depths on
// other effects, etc.) can subscribe to the envelope value via the
// modulation-routing layer.
//
// Two stages:
//
//   1. Detection: rectify (peak or RMS) the source signal, smooth
//      with separate attack / release time constants. Source is
//      either the audio passing through the device (when used as a
//      classic auto-wah on the same track) or a sidechain track
//      (when used as a "duck this track when the kick fires" tool).
//      Sensitivity is a pre-detector gain in dB so quiet sources
//      can drive the envelope to its full range.
//
//   2. Optional filter modulation: the envelope value [0..1] is
//      mapped through a depth-and-range curve to a cutoff offset
//      in semitones from the Base Cutoff knob. Positive depth →
//      louder source pushes cutoff UP (classic envelope filter /
//      auto-wah). Negative depth → louder source pushes cutoff
//      DOWN (rare but useful for "ducking filter" effects). Filter
//      type 0..3 = LP / HP / BP / Off. Off = pure modulation
//      source (audio passes through unchanged); the user just
//      cares about the envelope value being routable to other
//      params.
//
// The modulation output (modulationValue()) and the optional
// per-frame atomic accessor (consumeEnvelope) both publish the
// SAME value — the routing layer reads modulationValue at audio-
// block rate, the UI / visual engine can read consumeEnvelope at
// UI rate.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace yawn {
namespace effects {

class EnvelopeFollower : public AudioEffect {
public:
    enum Param {
        kSource,        // 0=Input, 1=Sidechain
        kMode,          // 0=Peak, 1=RMS
        kSensitivity,   // -30..+30 dB pre-detector gain
        kAttack,        // 0.1..500 ms
        kRelease,       // 1..2000 ms
        kFilterType,    // 0=LP, 1=HP, 2=BP, 3=Off
        kBaseCutoff,    // 0..1 normalised → 20..20000 Hz (log)
        kDepth,         // -1..+1 (positive = env pushes cutoff up)
        kRange,         // 0..1 → 0..6 octaves of mod range
        kResonance,     // 0.5..10 Q
        kMix,           // 0..1 wet/dry on the FILTER stage (when on)
        kParamCount
    };

    const char* name() const override { return "Envelope Follower"; }
    const char* id()   const override { return "envfollower"; }
    bool supportsSidechain() const override { return true; }
    bool hasModulationOutput() const override { return true; }
    float modulationValue() const override { return m_lastEnvelope; }

    // UI-thread accessor (atomic — current envelope value, not a
    // peak-since-poll). The visual engine / panel readouts can poll
    // this at UI rate; doesn't disturb the audio thread's writes.
    float consumeEnvelope() const {
        return m_envAtomic.load(std::memory_order_acquire);
    }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        reset();
        updateFilter();
    }

    void reset() override {
        m_envelope = 0.0f;
        m_lastEnvelope = 0.0f;
        m_envAtomic.store(0.0f, std::memory_order_release);
        m_rmsAccum = 0.0f;
        m_filterL.reset();
        m_filterR.reset();
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        const int   src   = static_cast<int>(m_params[kSource]);
        const int   mode  = static_cast<int>(m_params[kMode]);
        const float sensDb = m_params[kSensitivity];
        const float sensGain = std::pow(10.0f, sensDb / 20.0f);
        const float atkMs = m_params[kAttack];
        const float relMs = m_params[kRelease];
        const int   ftype = std::clamp(static_cast<int>(m_params[kFilterType]),
                                        0, 3);
        const float depth = std::clamp(m_params[kDepth], -1.0f, 1.0f);
        const float rangeOct = m_params[kRange] * 6.0f;
        const float mix   = std::clamp(m_params[kMix], 0.0f, 1.0f);

        const float sr = static_cast<float>(m_sampleRate);
        // One-pole envelope coefficients. atkCoeff governs how fast
        // the envelope rises toward the new peak; relCoeff how fast
        // it falls toward zero. Standard analog-EF behaviour.
        const float atkCoeff = std::exp(-1.0f / std::max(0.001f, atkMs * 0.001f * sr));
        const float relCoeff = std::exp(-1.0f / std::max(0.001f, relMs * 0.001f * sr));

        // RMS smoothing window — a slow integrator that tracks the
        // power of the signal. Time constant ~10 ms gives a stable
        // RMS estimate without sluggish lag.
        constexpr float kRmsTau = 0.010f;
        const float rmsCoeff = std::exp(-1.0f / std::max(1.0f, kRmsTau * sr));

        const bool useSC = (src == 1) && (m_sidechainBuffer != nullptr);
        const bool stereo = (numChannels > 1);

        // Pre-compute target base cutoff in Hz from the normalised
        // knob (log map 20..20000 Hz). Per-sample we add the modulation
        // offset in semitones.
        const float baseHz = 20.0f * std::pow(1000.0f,
            std::clamp(m_params[kBaseCutoff], 0.0f, 1.0f));

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            // ── Detection signal ──
            float detL, detR;
            if (useSC) {
                detL = m_sidechainBuffer[i * numChannels];
                detR = stereo ? m_sidechainBuffer[i * numChannels + 1] : detL;
            } else {
                detL = dryL;
                detR = dryR;
            }
            const float det = std::max(std::abs(detL), std::abs(detR)) * sensGain;

            // ── Envelope follower ──
            float target;
            if (mode == 1) {
                // RMS mode: integrate the squared signal then sqrt.
                // Adds ~10 ms of lag vs peak mode but tracks
                // perceived loudness more faithfully on dynamic
                // material.
                const float sq = det * det;
                m_rmsAccum = rmsCoeff * m_rmsAccum + (1.0f - rmsCoeff) * sq;
                target = std::sqrt(std::max(0.0f, m_rmsAccum));
            } else {
                target = det;
            }

            // Asymmetric smoothing: rise fast (attack), fall slow
            // (release). Standard envelope-follower shape.
            if (target > m_envelope)
                m_envelope = atkCoeff * m_envelope + (1.0f - atkCoeff) * target;
            else
                m_envelope = relCoeff * m_envelope + (1.0f - relCoeff) * target;

            // ── Audio path (optional filter) ──
            float wetL = dryL, wetR = dryR;
            if (ftype != 3) {
                // Map envelope [0..1] through depth × range to a
                // semitone offset. Positive depth: louder → higher
                // cutoff. Negative depth: louder → lower cutoff.
                const float clampedEnv = std::clamp(m_envelope, 0.0f, 1.0f);
                const float semitones = clampedEnv * depth * rangeOct * 12.0f;
                const float fc = std::clamp(
                    baseHz * std::pow(2.0f, semitones / 12.0f),
                    20.0f,
                    static_cast<float>(m_sampleRate) * 0.49f);

                // Re-design filters per sample is wasteful; design
                // once per block from the BLOCK-MEAN envelope would
                // be cheaper but loses the immediacy of fast attacks.
                // Per-sample design here costs ~30 ops; acceptable
                // for a single biquad pair on a single track. If
                // this lands on every track of a heavy session,
                // we'll move to per-block redesign.
                Biquad::Type bt = (ftype == 0) ? Biquad::Type::LowPass
                                : (ftype == 1) ? Biquad::Type::HighPass
                                               : Biquad::Type::BandPass;
                m_filterL.compute(bt, m_sampleRate, fc, 0.0,
                                   m_params[kResonance]);
                m_filterR.compute(bt, m_sampleRate, fc, 0.0,
                                   m_params[kResonance]);
                wetL = m_filterL.process(dryL);
                wetR = m_filterR.process(dryR);
            }

            const float w = mix * m_mix;
            const float d = 1.0f - w;
            buffer[i * numChannels] = dryL * d + wetL * w;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * d + wetR * w;
        }

        // Publish the block's tail envelope value to:
        //   1. modulationValue() (read by routing layer at block rate)
        //   2. m_envAtomic (read by UI / visual engine at frame rate)
        // Both paths get the SAME value; one is non-atomic for the
        // hot routing path (single-writer, single-reader on the audio
        // thread), the other is atomic for cross-thread reads.
        const float clamped = std::clamp(m_envelope, 0.0f, 1.0f);
        m_lastEnvelope = clamped;
        m_envAtomic.store(clamped, std::memory_order_release);
    }

    int parameterCount() const override { return kParamCount; }

    static constexpr const char* kSourceLabels[] = {"Input", "SC"};
    static constexpr const char* kModeLabels[]   = {"Peak", "RMS"};
    static constexpr const char* kFilterTypeLabels[] = {"LP", "HP", "BP", "Off"};

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"Src",     0.0f, 1.0f,   0.0f, "", false, false,
                WidgetHint::StepSelector, kSourceLabels, 2},
            {"Mode",    0.0f, 1.0f,   0.0f, "", false, false,
                WidgetHint::StepSelector, kModeLabels, 2},
            {"Sens",  -30.0f,30.0f,   0.0f, "dB", false, false,
                WidgetHint::DentedKnob},
            {"Atk",     0.1f,500.0f, 10.0f, "ms", false},
            {"Rel",     1.0f,2000.0f,200.0f, "ms", false},
            {"Type",    0.0f, 3.0f,   0.0f, "", false, false,
                WidgetHint::StepSelector, kFilterTypeLabels, 4},
            {"Cut",     0.0f, 1.0f,   0.30f, "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Depth", -1.0f, 1.0f,   0.50f, "", false, false,
                WidgetHint::DentedKnob},
            {"Range",   0.0f, 1.0f,   0.50f, "", false, false,
                WidgetHint::DentedKnob},
            {"Q",       0.5f,10.0f,  0.707f, "", false, false,
                WidgetHint::DentedKnob},
            {"Mix",     0.0f, 1.0f,   1.0f, "", false, false,
                WidgetHint::DentedKnob},
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
    static void formatCutoffHz(float v, char* buf, int n) {
        const float hz = 20.0f * std::pow(1000.0f, std::clamp(v, 0.0f, 1.0f));
        if (hz >= 1000.0f) std::snprintf(buf, n, "%.1f kHz", hz / 1000.0f);
        else                std::snprintf(buf, n, "%.0f Hz", hz);
    }

    void updateFilter() {
        // Filter coefficients are recomputed per-sample inside process()
        // because the envelope drives them; this is just an init reset.
        m_filterL.reset();
        m_filterR.reset();
    }

    float m_params[kParamCount] = {
        0.0f, 0.0f, 0.0f, 10.0f, 200.0f, 0.0f, 0.30f, 0.50f, 0.50f, 0.707f, 1.0f
    };

    float  m_envelope = 0.0f;
    float  m_lastEnvelope = 0.0f;
    float  m_rmsAccum = 0.0f;
    Biquad m_filterL, m_filterR;

    // Atomic mirror of the most recent envelope value for cross-
    // thread UI / visual-engine reads. Audio thread writes once per
    // block, UI thread reads at frame rate. Single-writer / single-
    // reader so a relaxed atomic would suffice; we use release/
    // acquire for clarity and to match the consumeModulatorLevel
    // pattern used elsewhere.
    std::atomic<float> m_envAtomic{0.0f};
};

} // namespace effects
} // namespace yawn
