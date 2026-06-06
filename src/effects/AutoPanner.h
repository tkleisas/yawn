#pragma once
// Auto Panner — an LFO sweeps the stereo image left↔right. Amplitude
// (balance) model: the LFO complementarily attenuates the two channels,
// so it's fully transparent at Depth 0 and at the LFO centre, moves a
// mono source across the field, and preserves stereo content otherwise
// (no mono-summing). Rate is either free (Hz) or tempo-synced to a note
// division; when synced and the transport is playing the LFO phase is
// locked to the beat so the pan stays in time. Waveforms: sine,
// triangle, square, saw, and sample-&-hold (random). A short gain
// smoother declicks the hard-edged waveforms.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yawn {
namespace effects {

class AutoPanner : public AudioEffect {
public:
    enum Param { kRate, kSync, kDivision, kDepth, kWaveform, kPhase, kMix,
                 kParamCount };

    const char* name() const override { return "Auto Panner"; }
    const char* id()   const override { return "autopanner"; }

    void setTempo(double bpm, double beat, bool playing) override {
        if (bpm > 0.0) m_bpm = bpm;
        m_hostBeat = beat;
        m_playing  = playing;
    }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // ~2 ms one-pole declick on the per-channel gains.
        m_smoothCoef = std::exp(-1.0 / (0.002 * sampleRate));
        reset();
    }

    void reset() override {
        m_phase    = 0.0;
        m_gainL    = 1.0f;
        m_gainR    = 1.0f;
        m_shVal    = 0.0f;
        m_shArmed  = true;
        m_rng      = 0x2545F4914F6CDD1Dull;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        const bool stereo = numChannels > 1;

        const float  depth    = std::clamp(m_params[kDepth], 0.0f, 1.0f);
        const bool   sync     = m_params[kSync] > 0.5f;
        const int    wave     = static_cast<int>(m_params[kWaveform]);
        const float  mix      = std::clamp(m_params[kMix], 0.0f, 1.0f) * m_mix;
        const double phaseOff = m_params[kPhase] / 360.0;

        // Cycles per sample for the LFO.
        double inc;
        if (sync) {
            const double beatsPerCycle = divisionBeats(
                static_cast<int>(m_params[kDivision]));
            const double cyclesPerSec = (m_bpm / 60.0) / beatsPerCycle;
            inc = cyclesPerSec / m_sampleRate;
            // Phase-lock to the transport while playing so the pan stays
            // in time; free-run (using the same rate) when stopped.
            if (m_playing && beatsPerCycle > 0.0)
                m_phase = std::fmod(m_hostBeat / beatsPerCycle, 1.0);
        } else {
            const double rateHz = std::clamp(
                static_cast<double>(m_params[kRate]), 0.01, 40.0);
            inc = rateHz / m_sampleRate;
        }

        for (int i = 0; i < numFrames; ++i) {
            double p = m_phase + phaseOff;
            p -= std::floor(p);                       // wrap into [0,1)
            const float lfo = waveform(wave, p);      // [-1, +1]

            // Amplitude (balance) pan: cut the channel opposite the
            // direction of travel. Transparent at lfo=0 and depth=0.
            const float lfoPos = (lfo > 0.0f) ? lfo : 0.0f;   // panning right
            const float lfoNeg = (lfo < 0.0f) ? -lfo : 0.0f;  // panning left
            const float targetL = 1.0f - depth * lfoPos;
            const float targetR = 1.0f - depth * lfoNeg;

            // One-pole declick (mainly for square / sample-&-hold edges).
            m_gainL = static_cast<float>(targetL + (m_gainL - targetL) * m_smoothCoef);
            m_gainR = static_cast<float>(targetR + (m_gainR - targetR) * m_smoothCoef);

            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;
            const float wetL = dryL * m_gainL;
            const float wetR = dryR * m_gainR;

            buffer[i * numChannels] = dryL * (1.0f - mix) + wetL * mix;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * (1.0f - mix) + wetR * mix;

            m_phase += inc;
            if (m_phase >= 1.0) {
                m_phase -= std::floor(m_phase);
                m_shArmed = true;                     // allow next S&H sample
            }
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const char* kDivLabels[] =
            {"2 bar", "1 bar", "1/2", "1/4", "1/8", "1/16"};
        static const char* kWaveLabels[] =
            {"Sine", "Triangle", "Square", "Saw", "Random"};
        static const ParameterInfo infos[kParamCount] = {
            {"Rate",      0.01f, 40.0f, 1.0f, "Hz", false, false, WidgetHint::Knob},
            {"Sync",      0.0f,  1.0f,  0.0f, "",   true,  false, WidgetHint::Toggle},
            {"Division",  0.0f,  5.0f,  3.0f, "",   false, false,
                WidgetHint::StepSelector, kDivLabels, 6},
            {"Depth",     0.0f,  1.0f,  1.0f, "",   false, false, WidgetHint::Knob},
            {"Waveform",  0.0f,  4.0f,  0.0f, "",   false, false,
                WidgetHint::StepSelector, kWaveLabels, 5},
            {"Phase",     0.0f,  360.0f, 0.0f, "\xC2\xB0", false, false, WidgetHint::Knob360},
            {"Mix",       0.0f,  1.0f,  1.0f, "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int i) const override {
        return (i >= 0 && i < kParamCount) ? m_params[i] : 0.0f;
    }
    void setParameter(int i, float v) override {
        if (i < 0 || i >= kParamCount) return;
        const auto& pi = parameterInfo(i);
        m_params[i] = std::clamp(v, pi.minValue, pi.maxValue);
    }

private:
    // Division index → beats per LFO cycle.
    static double divisionBeats(int step) {
        static const double t[6] = {8.0, 4.0, 2.0, 1.0, 0.5, 0.25};
        return t[std::clamp(step, 0, 5)];
    }

    // Bipolar waveform from phase p ∈ [0,1). All but square/saw start at
    // the centre (0) so the pan sweep begins from centre by default.
    float waveform(int wave, double p) {
        switch (wave) {
            case 1: // Triangle: 0 → +1 → 0 → -1 → 0
                if (p < 0.25) return static_cast<float>(4.0 * p);
                if (p < 0.75) return static_cast<float>(2.0 - 4.0 * p);
                return static_cast<float>(-4.0 + 4.0 * p);
            case 2: // Square
                return (p < 0.5) ? 1.0f : -1.0f;
            case 3: // Saw (rising)
                return static_cast<float>(2.0 * p - 1.0);
            case 4: // Sample & hold (one new random value per cycle)
                if (m_shArmed) { m_shVal = randBipolar(); m_shArmed = false; }
                return m_shVal;
            case 0:
            default: // Sine
                return std::sin(static_cast<float>(2.0 * M_PI * p));
        }
    }

    float randBipolar() {
        // xorshift64* → [-1, +1)
        m_rng ^= m_rng >> 12; m_rng ^= m_rng << 25; m_rng ^= m_rng >> 27;
        const uint64_t x = m_rng * 0x2545F4914F6CDD1Dull;
        const uint32_t hi = static_cast<uint32_t>(x >> 40); // 24 bits
        return static_cast<float>(hi) / static_cast<float>(0x800000) - 1.0f;
    }

    float m_params[kParamCount] = {1.0f, 0.0f, 3.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    double m_bpm = 120.0, m_hostBeat = 0.0, m_phase = 0.0;
    bool   m_playing = false, m_shArmed = true;
    double m_smoothCoef = 0.0;
    float  m_gainL = 1.0f, m_gainR = 1.0f, m_shVal = 0.0f;
    uint64_t m_rng = 0x2545F4914F6CDD1Dull;
};

} // namespace effects
} // namespace yawn
