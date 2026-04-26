#include "instruments/DrumRack.h"

namespace yawn {
namespace instruments {

void DrumRack::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
}

void DrumRack::reset() {
    for (auto& p : m_pads)
        p.playing = false;
}

void DrumRack::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    // Handle MIDI triggers
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn()) {
            auto& p = m_pads[msg.note & 0x7F];
            if (p.sampleFrames > 0) {
                p.playing = true;
                p.playPos = 0.0;
                p.velocity = velocityToGain(msg.velocity);
                p.playSpeed = std::pow(2.0, p.pitchAdjust / 12.0);
            }
        }
        // NoteOff: optionally stop pad (choke). For drums, we let them ring.
    }

    // Render all playing pads
    for (int n = 0; n < kNumPads; ++n) {
        auto& p = m_pads[n];
        if (!p.playing) continue;

        float angle = (p.pan + 1.0f) * 0.25f * (float)M_PI;
        float gL = p.volume * p.velocity * m_volume * std::cos(angle);
        float gR = p.volume * p.velocity * m_volume * std::sin(angle);

        for (int i = 0; i < numFrames; ++i) {
            int pos = (int)p.playPos;
            if (pos >= p.sampleFrames) { p.playing = false; break; }

            float sL = p.sampleData[pos * p.sampleChannels];
            float sR = (p.sampleChannels > 1)
                ? p.sampleData[pos * p.sampleChannels + 1] : sL;

            buffer[i * numChannels + 0] += sL * gL;
            if (numChannels > 1)
                buffer[i * numChannels + 1] += sR * gR;

            p.playPos += p.playSpeed;
        }
    }
}

} // namespace instruments
} // namespace yawn
