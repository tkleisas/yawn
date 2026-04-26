#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace midi {

// Transposes notes by semitones and/or octaves.
//
// Tracks (channel, input-note → output-note) for every note currently
// held so that note-off messages always use the output-note that was
// picked at note-on time — even if the user moves the pitch knob
// between note-on and note-off. When the transpose amount changes
// while notes are still held, the next process() flushes note-offs for
// all held notes at their ORIGINAL transposed note so the downstream
// synth clears its state cleanly. Otherwise the pitch device would
// leave stuck notes — especially easy to hit in front of an
// Arpeggiator, which generates many short notes per beat.
class MidiPitch : public MidiEffect {
public:
    enum Params { kSemitones = 0, kOctave, kNumParams };

    void init(double sampleRate) override;
    void reset() override;
    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override;

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
            case kSemitones: return static_cast<float>(m_semitones);
            case kOctave:    return static_cast<float>(m_octave);
            default:         return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        int ns = m_semitones, no = m_octave;
        switch (index) {
            case kSemitones: ns = std::clamp((int)std::round(value), -48, 48); break;
            case kOctave:    no = std::clamp((int)std::round(value), -4, 4);   break;
        }
        if (ns != m_semitones || no != m_octave) {
            m_semitones = ns;
            m_octave    = no;
            m_pitchChanged = true;
        }
    }

private:
    struct ActiveNote {
        uint8_t channel;
        uint8_t inputNote;
        uint8_t outputNote;
    };

    int m_semitones = 0;
    int m_octave    = 0;
    bool m_pitchChanged = false;
    std::vector<ActiveNote> m_active;
};

} // namespace midi
} // namespace yawn
