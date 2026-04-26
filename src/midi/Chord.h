#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace midi {

// Adds parallel intervals to every incoming note, building chords.
class Chord : public MidiEffect {
public:
    enum Params {
        kInterval1 = 0, kInterval2, kInterval3,
        kInterval4, kInterval5, kInterval6,
        kVelocityScale,
        kNumParams
    };

    void init(double sampleRate) override;
    void reset() override;
    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override;

    const char* name() const override { return "Chord"; }
    const char* id()   const override { return "chord"; }

    int parameterCount() const override { return kNumParams; }

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Interval 1", -24.0f, 24.0f, 0.0f, "st", false},
            {"Interval 2", -24.0f, 24.0f, 0.0f, "st", false},
            {"Interval 3", -24.0f, 24.0f, 0.0f, "st", false},
            {"Interval 4", -24.0f, 24.0f, 0.0f, "st", false},
            {"Interval 5", -24.0f, 24.0f, 0.0f, "st", false},
            {"Interval 6", -24.0f, 24.0f, 0.0f, "st", false},
            {"Velocity Scale", 0.0f, 1.0f, 1.0f, "", false},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        if (index >= kInterval1 && index <= kInterval6)
            return (float)m_intervals[index];
        if (index == kVelocityScale) return m_velocityScale;
        return 0.0f;
    }

    void setParameter(int index, float value) override {
        if (index >= kInterval1 && index <= kInterval6)
            m_intervals[index] = std::clamp((int)std::round(value), -24, 24);
        else if (index == kVelocityScale)
            m_velocityScale = std::clamp(value, 0.0f, 1.0f);
    }

private:
    int   m_intervals[6]    = {};
    float m_velocityScale   = 1.0f;
};

} // namespace midi
} // namespace yawn
