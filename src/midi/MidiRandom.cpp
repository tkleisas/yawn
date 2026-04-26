#include "MidiRandom.h"

namespace yawn {
namespace midi {

void MidiRandom::init(double sampleRate) { m_sampleRate = sampleRate; m_rng = 54321; }

void MidiRandom::reset() { m_rng = 54321; }

void MidiRandom::process(MidiBuffer& buffer, int numFrames,
             const TransportInfo& /*transport*/) {
    MidiBuffer output;

    for (int i = 0; i < buffer.count(); ++i) {
        const auto& msg = buffer[i];

        if (msg.isNoteOn()) {
            // Probability gate
            if (m_probability < 1.0f && randomFloat() >= m_probability)
                continue;

            MidiMessage out = msg;

            if (m_pitchRange > 0) {
                int off = randomInt(-m_pitchRange, m_pitchRange);
                out.note = (uint8_t)std::clamp((int)out.note + off, 0, 127);
            }

            if (m_velocityRange > 0) {
                int off = randomInt(-m_velocityRange, m_velocityRange);
                int nv  = std::clamp((int)out.velocity + off * 512, 1, 65535);
                out.velocity = (uint16_t)nv;
            }

            if (m_timingJitter > 0) {
                int off = randomInt(-m_timingJitter, m_timingJitter);
                out.frameOffset = std::clamp(out.frameOffset + off, (int32_t)0,
                                             (int32_t)(numFrames - 1));
            }

            output.addMessage(out);
        } else {
            output.addMessage(msg);
        }
    }

    buffer = output;
    buffer.sortByFrame();
}

} // namespace midi
} // namespace yawn
