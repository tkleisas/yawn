#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace midi {

// Forces note durations to a specified length, ignoring the original NoteOff timing.
class NoteLength : public MidiEffect {
public:
    enum Mode { BeatDivision = 0, Milliseconds };
    enum Params { kMode = 0, kLength, kGate, kNumParams };

    void init(double sampleRate) override;
    void reset() override;
    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override;

    const char* name() const override { return "Note Length"; }
    const char* id()   const override { return "notelength"; }

    int parameterCount() const override { return kNumParams; }

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Mode",   0.0f, 1.0f, 0.0f, "", false},
            {"Length", 0.0625f, 5000.0f, 0.25f, "", false},
            {"Gate",   0.1f, 1.0f, 1.0f, "", false},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kMode:   return (float)m_mode;
            case kLength: return (float)m_length;
            case kGate:   return m_gate;
            default:      return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kMode:   m_mode = (Mode)std::clamp((int)value, 0, 1); break;
            case kLength: m_length = std::max(0.001, (double)value); break;
            case kGate:   m_gate = std::clamp(value, 0.1f, 1.0f); break;
        }
    }

private:
    Mode   m_mode   = BeatDivision;
    double m_length = 0.25;
    float  m_gate   = 1.0f;

    struct ActiveNote {
        uint8_t pitch   = 0;
        uint8_t channel = 0;
        double  offBeat = 0.0;
    };

    static constexpr int kMaxActiveNotes = 128;
    ActiveNote m_active[kMaxActiveNotes] = {};
    int m_numActive = 0;
};

} // namespace midi
} // namespace yawn
