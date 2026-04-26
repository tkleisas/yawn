#include "effects/Chorus.h"

namespace yawn::effects {

void Chorus::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    // Max modulation depth of ~25ms
    int maxDelay = static_cast<int>(0.025 * sampleRate) + 256;
    m_bufferL.assign(maxDelay, 0.0f);
    m_bufferR.assign(maxDelay, 0.0f);
    m_maxDelay = maxDelay;
    reset();
}

void Chorus::reset() {
    std::fill(m_bufferL.begin(), m_bufferL.end(), 0.0f);
    std::fill(m_bufferR.begin(), m_bufferR.end(), 0.0f);
    m_writePos = 0;
    m_phase = 0.0;
}

void Chorus::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;

    float rate = m_params[kRate];
    float depth = m_params[kDepth];
    float wet = m_params[kWetDry];
    float dry = 1.0f - wet;
    int voices = std::max(1, std::min(4, static_cast<int>(m_params[kVoices])));

    double phaseInc = rate / m_sampleRate;
    float baseDelay = static_cast<float>(0.007 * m_sampleRate); // 7ms base delay
    float modDepth = depth * static_cast<float>(0.003 * m_sampleRate); // Up to 3ms modulation

    for (int i = 0; i < numFrames; ++i) {
        float inL = buffer[i * numChannels];
        float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;

        m_bufferL[m_writePos] = inL;
        m_bufferR[m_writePos] = inR;

        float chorusL = 0.0f, chorusR = 0.0f;
        for (int v = 0; v < voices; ++v) {
            double voicePhase = m_phase + static_cast<double>(v) / voices;
            float modL = static_cast<float>(std::sin(voicePhase * 6.283185307));
            float modR = static_cast<float>(std::sin(voicePhase * 6.283185307 + 1.5707963));

            float delayL = baseDelay + modL * modDepth;
            float delayR = baseDelay + modR * modDepth;

            chorusL += readInterp(m_bufferL, delayL);
            chorusR += readInterp(m_bufferR, delayR);
        }

        float voiceGain = 1.0f / static_cast<float>(voices);
        buffer[i * numChannels] = dry * inL + wet * chorusL * voiceGain;
        if (numChannels > 1)
            buffer[i * numChannels + 1] = dry * inR + wet * chorusR * voiceGain;

        m_writePos = (m_writePos + 1) % m_maxDelay;
        m_phase += phaseInc;
        if (m_phase >= 1.0) m_phase -= 1.0;
    }
}

} // namespace yawn::effects
