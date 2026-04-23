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

    void init(double sampleRate) override {
        m_sampleRate = sampleRate;
        reset();
    }

    void reset() override {
        m_active.clear();
        m_pitchChanged = false;
    }

    void process(MidiBuffer& buffer, int /*numFrames*/,
                 const TransportInfo& /*transport*/) override {
        const int total = m_semitones + m_octave * 12;

        // Phase 1 — process incoming messages at the current offset.
        // Capture the pre-flush count so appended flush note-offs aren't
        // re-transposed by this loop.
        const int n = buffer.count();
        for (int i = 0; i < n; ++i) {
            auto& msg = buffer[i];
            if (msg.isNoteOn()) {
                const int np = static_cast<int>(msg.note) + total;
                if (np < 0 || np > 127) {
                    msg.velocity = 0;  // silence out-of-range
                    continue;
                }
                m_active.push_back({msg.channel, msg.note,
                                     static_cast<uint8_t>(np)});
                msg.note = static_cast<uint8_t>(np);
            } else if (msg.isNoteOff()) {
                // Prefer the stored output note so releases always match
                // the note-on they came from.
                auto it = std::find_if(m_active.begin(), m_active.end(),
                    [&](const ActiveNote& an) {
                        return an.channel == msg.channel &&
                               an.inputNote == msg.note;
                    });
                if (it != m_active.end()) {
                    msg.note = it->outputNote;
                    m_active.erase(it);
                } else {
                    // Untracked: best-effort transpose with current
                    // offset. Skip out-of-range.
                    const int np = static_cast<int>(msg.note) + total;
                    if (np < 0 || np > 127) continue;
                    msg.note = static_cast<uint8_t>(np);
                }
            }
        }

        // Phase 2 — pitch changed while notes are still held: flush
        // note-offs at their ORIGINAL output so the downstream
        // arp/synth clears cleanly. Any key the user released in this
        // buffer was already handled by phase 1's m_active lookup.
        if (m_pitchChanged) {
            m_pitchChanged = false;
            for (auto& an : m_active) {
                buffer.addMessage(
                    MidiMessage::noteOff(an.channel, an.outputNote, 0, 0));
            }
            m_active.clear();
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
