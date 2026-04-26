#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>

namespace yawn {
namespace midi {

// Adds controlled randomness to pitch, velocity, timing, and note probability.
class MidiRandom : public MidiEffect {
public:
    enum Params {
        kPitchRange = 0, kVelocityRange, kTimingJitter, kProbability,
        kNumParams
    };

    void init(double sampleRate) override;
    void reset() override;
    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override;

    const char* name() const override { return "Random"; }
    const char* id()   const override { return "random"; }

    int parameterCount() const override { return kNumParams; }

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Pitch Range",    0.0f, 24.0f, 0.0f, "st",     false},
            {"Velocity Range", 0.0f, 127.0f, 0.0f, "",      false},
            {"Timing Jitter",  0.0f, 500.0f, 0.0f, "frames", false},
            {"Probability",    0.0f, 1.0f, 1.0f, "",        false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kPitchRange:    return (float)m_pitchRange;
            case kVelocityRange: return (float)m_velocityRange;
            case kTimingJitter:  return (float)m_timingJitter;
            case kProbability:   return m_probability;
            default:             return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kPitchRange:    m_pitchRange = std::clamp((int)value, 0, 24); break;
            case kVelocityRange: m_velocityRange = std::clamp((int)value, 0, 127); break;
            case kTimingJitter:  m_timingJitter = std::clamp((int)value, 0, 500); break;
            case kProbability:   m_probability = std::clamp(value, 0.0f, 1.0f); break;
        }
    }

private:
    uint32_t nextRandom() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return m_rng;
    }
    float randomFloat() { return (float)(nextRandom() & 0xFFFF) / 65536.0f; }
    int randomInt(int lo, int hi) {
        if (lo >= hi) return lo;
        return lo + (int)(nextRandom() % (uint32_t)(hi - lo + 1));
    }

    int      m_pitchRange    = 0;
    int      m_velocityRange = 0;
    int      m_timingJitter  = 0;
    float    m_probability   = 1.0f;
    uint32_t m_rng           = 54321;
};

} // namespace midi
} // namespace yawn
