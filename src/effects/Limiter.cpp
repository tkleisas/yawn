#include "effects/Limiter.h"

namespace yawn::effects {

void Limiter::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_delayBuf.assign(kMaxLookaheadSamples * 2, 0.0f);  // stereo
    updateCoeffs();
    reset();
}

void Limiter::reset() {
    m_envLin = 1.0f;  // no reduction at start
    m_writePos = 0;
    std::fill(m_delayBuf.begin(), m_delayBuf.end(), 0.0f);
}

void Limiter::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;
    const float ceilingLin = std::pow(10.0f, m_params[kCeiling] / 20.0f);
    const int lookSamples = std::min(
        static_cast<int>(m_params[kLookahead] * 0.001f * m_sampleRate),
        kMaxLookaheadSamples - 1);

    const int delayMask = kMaxLookaheadSamples;  // capacity per channel
    for (int i = 0; i < numFrames; ++i) {
        const float inL = buffer[i * numChannels];
        const float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;

        // Peak of the incoming (future) sample drives the envelope.
        // Target gain = min(1, ceiling / peak). Envelope follower
        // attacks fast toward lower gain, releases slowly toward 1.
        const float peak = std::max(std::abs(inL), std::abs(inR));
        const float target = (peak > ceilingLin)
            ? ceilingLin / peak : 1.0f;

        if (target < m_envLin) {
            // Attack: move quickly toward the limiting gain.
            m_envLin = m_attackCoeff * m_envLin + (1.0f - m_attackCoeff) * target;
        } else {
            m_envLin = m_releaseCoeff * m_envLin + (1.0f - m_releaseCoeff) * target;
        }

        // Apply the envelope to the DELAYED sample so the ducking
        // aligns with the peak. With zero lookahead this is just
        // instantaneous limiting.
        const int readPos = (m_writePos - lookSamples + delayMask) % delayMask;
        const float outL = m_delayBuf[readPos * 2 + 0] * m_envLin;
        const float outR = m_delayBuf[readPos * 2 + 1] * m_envLin;

        // Hard ceiling as a safety net — if the envelope hasn't
        // caught a peak, clamp here so nothing slips above the
        // ceiling. Shouldn't engage in normal operation.
        buffer[i * numChannels] = std::clamp(outL, -ceilingLin, ceilingLin);
        if (numChannels > 1)
            buffer[i * numChannels + 1] = std::clamp(outR, -ceilingLin, ceilingLin);

        // Write the incoming sample into the delay line.
        m_delayBuf[m_writePos * 2 + 0] = inL;
        m_delayBuf[m_writePos * 2 + 1] = inR;
        m_writePos = (m_writePos + 1) % delayMask;
    }
}

} // namespace yawn::effects
