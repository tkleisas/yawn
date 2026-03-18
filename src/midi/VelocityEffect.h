#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace midi {

// Remaps and reshapes note velocity using curves and range mapping.
class VelocityEffect : public MidiEffect {
public:
    enum Curve { Linear = 0, Exponential, Logarithmic, SCurve };
    enum Params { kMinOut = 0, kMaxOut, kCurve, kRandomAmount, kNumParams };

    void init(double sampleRate) override { m_sampleRate = sampleRate; m_rng = 42; }
    void reset() override { m_rng = 42; }

    void process(MidiBuffer& buffer, int /*numFrames*/,
                 const TransportInfo& /*transport*/) override {
        for (int i = 0; i < buffer.count(); ++i) {
            auto& msg = buffer[i];
            if (!msg.isNoteOn()) continue;

            float v = (float)msg.velocity / 65535.0f;
            v = applyCurve(v);

            float outMin = m_minOut / 127.0f;
            float outMax = m_maxOut / 127.0f;
            v = outMin + v * (outMax - outMin);

            if (m_randomAmount > 0.0f) {
                float rnd = ((float)(nextRandom() % 1000) / 500.0f - 1.0f);
                v += rnd * (m_randomAmount / 127.0f);
            }

            v = std::clamp(v, 0.0f, 1.0f);
            msg.velocity = (uint16_t)(v * 65535.0f);
            if (msg.velocity == 0) msg.velocity = 1;
        }
    }

    const char* name() const override { return "Velocity"; }
    const char* id()   const override { return "velocity"; }

    int parameterCount() const override { return kNumParams; }

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Min Out", 0.0f, 127.0f, 1.0f, "", false},
            {"Max Out", 0.0f, 127.0f, 127.0f, "", false},
            {"Curve",   0.0f, 3.0f, 0.0f, "", false},
            {"Random",  0.0f, 127.0f, 0.0f, "", false},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kMinOut:       return m_minOut;
            case kMaxOut:       return m_maxOut;
            case kCurve:        return (float)m_curve;
            case kRandomAmount: return m_randomAmount;
            default:            return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kMinOut:       m_minOut = std::clamp(value, 0.0f, 127.0f); break;
            case kMaxOut:       m_maxOut = std::clamp(value, 0.0f, 127.0f); break;
            case kCurve:        m_curve  = (Curve)std::clamp((int)value, 0, 3); break;
            case kRandomAmount: m_randomAmount = std::clamp(value, 0.0f, 127.0f); break;
        }
    }

private:
    float applyCurve(float v) const {
        switch (m_curve) {
            case Linear:      return v;
            case Exponential: return v * v;
            case Logarithmic: return std::sqrt(v);
            case SCurve:      return v * v * (3.0f - 2.0f * v);
            default:          return v;
        }
    }

    uint32_t nextRandom() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return m_rng;
    }

    float    m_minOut       = 1.0f;
    float    m_maxOut       = 127.0f;
    Curve    m_curve        = Linear;
    float    m_randomAmount = 0.0f;
    uint32_t m_rng          = 42;
};

} // namespace midi
} // namespace yawn
