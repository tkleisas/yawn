#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace midi {

// Transposes notes by semitones and/or octaves.
class MidiPitch : public MidiEffect {
public:
    enum Params { kSemitones = 0, kOctave, kNumParams };

    void init(double sampleRate) override { m_sampleRate = sampleRate; }
    void reset() override {}

    void process(MidiBuffer& buffer, int /*numFrames*/,
                 const TransportInfo& /*transport*/) override {
        int total = m_semitones + m_octave * 12;
        if (total == 0) return;

        for (int i = 0; i < buffer.count(); ++i) {
            auto& msg = buffer[i];
            if (msg.isNoteOn() || msg.isNoteOff()) {
                int np = (int)msg.note + total;
                if (np < 0 || np > 127) {
                    if (msg.isNoteOn()) msg.velocity = 0; // silence out-of-range
                    continue;
                }
                msg.note = (uint8_t)np;
            }
        }
    }

    const char* name() const override { return "Pitch"; }
    const char* id()   const override { return "pitch"; }

    int parameterCount() const override { return kNumParams; }

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Semitones", -48.0f, 48.0f, 0.0f, "st", false},
            {"Octave",    -4.0f, 4.0f, 0.0f, "", false},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kSemitones: return (float)m_semitones;
            case kOctave:    return (float)m_octave;
            default:         return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kSemitones: m_semitones = std::clamp((int)std::round(value), -48, 48); break;
            case kOctave:    m_octave = std::clamp((int)std::round(value), -4, 4); break;
        }
    }

private:
    int m_semitones = 0;
    int m_octave    = 0;
};

} // namespace midi
} // namespace yawn
