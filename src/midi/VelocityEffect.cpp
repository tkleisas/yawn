#include "VelocityEffect.h"

namespace yawn {
namespace midi {

void VelocityEffect::init(double sampleRate) { m_sampleRate = sampleRate; m_rng = 42; }

void VelocityEffect::reset() { m_rng = 42; }

void VelocityEffect::process(MidiBuffer& buffer, int /*numFrames*/,
             const TransportInfo& /*transport*/) {
    for (int i = 0; i < buffer.count(); ++i) {
        auto& msg = buffer[i];
        if (!msg.isNoteOn()) continue;

        float v = (float)msg.velocity / 65535.0f;
        v = applyCurve(v);

        float outMin = m_minOut / 127.0f;
        float outMax = m_maxOut / 127.0f;
        v = outMin + v * (outMax - outMin);

        if (m_randomAmount > 0.0f) {
            float rnd = ((float)(nextRandom() % 1000) / 500.0f - 1.0f);
            v += rnd * (m_randomAmount / 127.0f);
        }

        v = std::clamp(v, 0.0f, 1.0f);
        msg.velocity = (uint16_t)(v * 65535.0f);
        if (msg.velocity == 0) msg.velocity = 1;
    }
}

} // namespace midi
} // namespace yawn
