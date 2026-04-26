#include "Scale.h"

namespace yawn {
namespace midi {

void Scale::init(double sampleRate) { m_sampleRate = sampleRate; }

void Scale::reset() {}

void Scale::process(MidiBuffer& buffer, int /*numFrames*/,
             const TransportInfo& /*transport*/) {
    for (int i = 0; i < buffer.count(); ++i) {
        auto& msg = buffer[i];
        if (msg.isNoteOn() || msg.isNoteOff())
            msg.note = quantize(msg.note);
    }
}

} // namespace midi
} // namespace yawn
