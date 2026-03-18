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

    void init(double sampleRate) override { m_sampleRate = sampleRate; reset(); }

    void reset() override { m_numActive = 0; }

    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override {
        MidiBuffer output;

        double bufStart = transport.positionInBeats;
        double bufEnd   = bufStart + (double)numFrames / transport.samplesPerBeat;

        // Emit scheduled NoteOffs that fall in this buffer
        for (int i = 0; i < m_numActive; ) {
            auto& n = m_active[i];
            if (n.offBeat >= bufStart && n.offBeat < bufEnd) {
                int f = (int)((n.offBeat - bufStart) * transport.samplesPerBeat);
                f = std::clamp(f, 0, numFrames - 1);
                output.addMessage(MidiMessage::noteOff16(n.channel, n.pitch, 0, f));
                m_active[i] = m_active[--m_numActive];
            } else {
                ++i;
            }
        }

        // Process input messages
        for (int i = 0; i < buffer.count(); ++i) {
            const auto& msg = buffer[i];

            if (msg.isNoteOn()) {
                output.addMessage(msg);

                double lengthBeats;
                if (m_mode == BeatDivision) {
                    lengthBeats = m_length * m_gate;
                } else {
                    double ms = m_length * m_gate;
                    lengthBeats = (ms / 1000.0) * (transport.bpm / 60.0);
                }

                double noteStart = bufStart + (double)msg.frameOffset / transport.samplesPerBeat;
                double offBeat   = noteStart + lengthBeats;

                if (offBeat < bufEnd) {
                    int f = (int)((offBeat - bufStart) * transport.samplesPerBeat);
                    f = std::clamp(f, 0, numFrames - 1);
                    output.addMessage(MidiMessage::noteOff16(msg.channel, msg.note, 0, f));
                } else if (m_numActive < kMaxActiveNotes) {
                    m_active[m_numActive++] = {msg.note, msg.channel, offBeat};
                }
            } else if (msg.isNoteOff()) {
                // Suppress original NoteOff — we handle our own timing
            } else {
                output.addMessage(msg);
            }
        }

        buffer = output;
        buffer.sortByFrame();
    }

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
