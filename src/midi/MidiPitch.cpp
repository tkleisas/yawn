#include "MidiPitch.h"

namespace yawn {
namespace midi {

void MidiPitch::init(double sampleRate) {
    m_sampleRate = sampleRate;
    reset();
}

void MidiPitch::reset() {
    m_activeCount = 0;
    m_pitchChanged = false;
}

void MidiPitch::process(MidiBuffer& buffer, int /*numFrames*/,
             const TransportInfo& /*transport*/) {
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
            if (m_activeCount < kMaxActiveNotes) {
                m_active[m_activeCount++] = {msg.channel, msg.note,
                                             static_cast<uint8_t>(np)};
            }
            msg.note = static_cast<uint8_t>(np);
        } else if (msg.isNoteOff()) {
            // Prefer the stored output note so releases always match
            // the note-on they came from.
            int found = -1;
            for (int i = 0; i < m_activeCount; ++i) {
                if (m_active[i].channel == msg.channel &&
                    m_active[i].inputNote == msg.note) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                msg.note = m_active[found].outputNote;
                m_active[found] = m_active[--m_activeCount];
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
        for (int i = 0; i < m_activeCount; ++i) {
            buffer.addMessage(
                MidiMessage::noteOff(m_active[i].channel,
                                     m_active[i].outputNote, 0, 0));
        }
        m_activeCount = 0;
    }
}

} // namespace midi
} // namespace yawn
