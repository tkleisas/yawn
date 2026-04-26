#include "effects/Oscilloscope.h"

namespace yawn::effects {

void Oscilloscope::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_ringBuffer.resize(kDisplaySize * 2, 0.0f);
    m_displayBuffer.resize(kDisplaySize, 0.0f);
    m_writePos = 0;
    reset();
}

void Oscilloscope::reset() {
    std::fill(m_ringBuffer.begin(), m_ringBuffer.end(), 0.0f);
    std::fill(m_displayBuffer.begin(), m_displayBuffer.end(), 0.0f);
    m_writePos = 0;
}

void Oscilloscope::process(float* buffer, int numFrames, int numChannels) {
    if (m_freeze > 0.5f) return; // frozen — don't update buffer

    float gain = m_gain;
    for (int i = 0; i < numFrames; ++i) {
        // Mix to mono for display
        float sample = buffer[i * numChannels];
        if (numChannels > 1)
            sample = (sample + buffer[i * numChannels + 1]) * 0.5f;

        m_ringBuffer[m_writePos] = sample * gain;
        m_writePos = (m_writePos + 1) % kDisplaySize;
    }

    // Snapshot for UI: copy from ring buffer in order
    int rp = m_writePos;
    for (int i = 0; i < kDisplaySize; ++i) {
        m_displayBuffer[i] = m_ringBuffer[(rp + i) % kDisplaySize];
    }
    m_displayReady.store(true, std::memory_order_release);
}

} // namespace yawn::effects
