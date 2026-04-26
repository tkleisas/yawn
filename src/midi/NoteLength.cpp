#include "NoteLength.h"

namespace yawn {
namespace midi {

void NoteLength::init(double sampleRate) { m_sampleRate = sampleRate; reset(); }

void NoteLength::reset() { m_numActive = 0; }

void NoteLength::process(MidiBuffer& buffer, int numFrames,
             const TransportInfo& transport) {
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

} // namespace midi
} // namespace yawn
