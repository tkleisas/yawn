#include "Chord.h"

namespace yawn {
namespace midi {

void Chord::init(double sampleRate) { m_sampleRate = sampleRate; }

void Chord::reset() {}

void Chord::process(MidiBuffer& buffer, int /*numFrames*/,
             const TransportInfo& /*transport*/) {
    MidiBuffer output;

    for (int i = 0; i < buffer.count(); ++i) {
        const auto& msg = buffer[i];
        output.addMessage(msg);

        if (msg.isNoteOn() || msg.isNoteOff()) {
            for (int p = 0; p < 6; ++p) {
                if (m_intervals[p] == 0) continue;
                int np = (int)msg.note + m_intervals[p];
                if (np < 0 || np > 127) continue;

                if (msg.isNoteOn()) {
                    uint16_t vel = (uint16_t)(msg.velocity * m_velocityScale);
                    if (vel == 0) vel = 1;
                    output.addMessage(MidiMessage::noteOn16(
                        msg.channel, (uint8_t)np, vel, msg.frameOffset));
                } else {
                    output.addMessage(MidiMessage::noteOff16(
                        msg.channel, (uint8_t)np, 0, msg.frameOffset));
                }
            }
        }
    }

    buffer = output;
    buffer.sortByFrame();
}

} // namespace midi
} // namespace yawn
