#include "LFO.h"

namespace yawn {
namespace midi {

void LFO::init(double sampleRate) {
    m_sampleRate = sampleRate;
    m_freePhase = 0.0;
    m_shValue = 0.0f;
    m_shLastPhase = -1.0;
}

void LFO::reset() {
    m_freePhase = 0.0;
    m_shValue = 0.0f;
    m_shLastPhase = -1.0;
}

void LFO::process(MidiBuffer& /*buffer*/, int numFrames,
             const TransportInfo& transport) {
    if (m_bypassed) {
        m_outputValue = 0.0f;
        return;
    }
    // If both depth and bias are off, skip the oscillator math.
    if (m_depth <= 0.0f && m_bias == 0.0f) {
        m_outputValue = 0.0f;
        return;
    }

    double phase = 0.0;

    if (m_sync) {
        // Beat-synced: phase from transport position
        // Rate is in beats per LFO cycle (e.g., 4 = one cycle per bar in 4/4)
        double rate = std::max(m_rate, 0.0001);
        phase = transport.positionInBeats / rate;
    } else {
        // Free-running: advance by Hz
        double rate = std::max(m_rate, 0.0001);
        double samplesThisBuffer = static_cast<double>(numFrames);
        m_freePhase += (rate * samplesThisBuffer) / transport.sampleRate;
        phase = m_freePhase;
    }

    // Store base phase (before offset) for linking
    m_basePhase = phase - std::floor(phase);

    // Add phase offset
    phase += static_cast<double>(m_phaseOffset);

    // Wrap to [0, 1)
    phase = phase - std::floor(phase);

    // Compute waveform value in [-1, 1]
    float raw = computeWaveform(phase);

    // Scale by depth, then apply DC bias.
    m_outputValue = raw * m_depth + m_bias;
}

float LFO::computeWaveform(double phase) {
    float p = static_cast<float>(phase);
    switch (m_shape) {
    case Sine:
        return std::sin(p * 6.283185307f);

    case Triangle:
        // 0→0.25: 0→1, 0.25→0.75: 1→-1, 0.75→1.0: -1→0
        if (p < 0.25f) return p * 4.0f;
        if (p < 0.75f) return 2.0f - p * 4.0f;
        return p * 4.0f - 4.0f;

    case Saw:
        // Rising: 0→1 maps to -1→1
        return 2.0f * p - 1.0f;

    case Square:
        return (p < 0.5f) ? 1.0f : -1.0f;

    case SampleAndHold:
        // Change value only at the start of each cycle (phase wraps past 0)
        if (m_shLastPhase < 0.0 || phase < m_shLastPhase) {
            // Simple LCG random
            m_shRng = m_shRng * 1664525u + 1013904223u;
            m_shValue = (static_cast<float>(m_shRng & 0xFFFF) / 32767.5f) - 1.0f;
        }
        m_shLastPhase = phase;
        return m_shValue;

    default:
        return 0.0f;
    }
}

} // namespace midi
} // namespace yawn
