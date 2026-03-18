#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace midi {

// Quantizes note pitches to a musical scale.
class Scale : public MidiEffect {
public:
    enum ScaleType {
        Chromatic = 0, Major, NaturalMinor, HarmonicMinor,
        Dorian, Mixolydian, PentatonicMajor, PentatonicMinor, Blues,
        kNumScaleTypes
    };

    enum Params { kRoot = 0, kScaleType, kNumParams };

    void init(double sampleRate) override { m_sampleRate = sampleRate; }
    void reset() override {}

    void process(MidiBuffer& buffer, int /*numFrames*/,
                 const TransportInfo& /*transport*/) override {
        for (int i = 0; i < buffer.count(); ++i) {
            auto& msg = buffer[i];
            if (msg.isNoteOn() || msg.isNoteOff())
                msg.note = quantize(msg.note);
        }
    }

    const char* name() const override { return "Scale"; }
    const char* id()   const override { return "scale"; }

    int parameterCount() const override { return kNumParams; }

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Root",  0.0f, 11.0f, 0.0f, "", false},
            {"Scale", 0.0f, (float)(kNumScaleTypes - 1), 1.0f, "", false},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kRoot:      return (float)m_root;
            case kScaleType: return (float)m_scaleType;
            default:         return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kRoot:      m_root = std::clamp((int)value, 0, 11); break;
            case kScaleType: m_scaleType = (ScaleType)std::clamp((int)value, 0, kNumScaleTypes - 1); break;
        }
    }

    // Public for testing
    uint8_t quantize(uint8_t note) const {
        const bool* scale = kScales[m_scaleType];
        int relative = ((int)note - m_root + 120) % 12; // +120 avoids negative mod
        if (scale[relative]) return note;
        for (int off = 1; off <= 6; ++off) {
            if (scale[(relative - off + 12) % 12])
                return (uint8_t)std::clamp((int)note - off, 0, 127);
            if (scale[(relative + off) % 12])
                return (uint8_t)std::clamp((int)note + off, 0, 127);
        }
        return note;
    }

private:
    int       m_root      = 0;
    ScaleType m_scaleType = Major;

    static constexpr bool kScales[kNumScaleTypes][12] = {
        {1,1,1,1,1,1,1,1,1,1,1,1}, // Chromatic
        {1,0,1,0,1,1,0,1,0,1,0,1}, // Major
        {1,0,1,1,0,1,0,1,1,0,1,0}, // Natural Minor
        {1,0,1,1,0,1,0,1,1,0,0,1}, // Harmonic Minor
        {1,0,1,1,0,1,0,1,0,1,1,0}, // Dorian
        {1,0,1,0,1,1,0,1,0,1,1,0}, // Mixolydian
        {1,0,1,0,1,0,0,1,0,1,0,0}, // Pentatonic Major
        {1,0,0,1,0,1,0,1,0,0,1,0}, // Pentatonic Minor
        {1,0,0,1,0,1,1,1,0,0,1,0}, // Blues
    };
};

constexpr bool Scale::kScales[Scale::kNumScaleTypes][12];

} // namespace midi
} // namespace yawn
