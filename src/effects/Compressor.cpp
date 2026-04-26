#include "effects/Compressor.h"

namespace yawn::effects {

void Compressor::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    updateCoeffs();
    reset();
}

void Compressor::reset() { m_envDB = -96.0f; }

void Compressor::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;

    float thresh = m_params[kThreshold];
    float ratio  = m_params[kRatio];
    float knee   = m_params[kKnee];
    float makeup = std::pow(10.0f, m_params[kMakeupGain] / 20.0f);
    float halfKnee = knee * 0.5f;

    for (int i = 0; i < numFrames; ++i) {
        float L = buffer[i * numChannels];
        float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

        // Peak detection (take max of L/R)
        float peak = std::max(std::abs(L), std::abs(R));
        float peakDB = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -96.0f;

        // Envelope follower
        if (peakDB > m_envDB)
            m_envDB = m_attackCoeff * m_envDB + (1.0f - m_attackCoeff) * peakDB;
        else
            m_envDB = m_releaseCoeff * m_envDB + (1.0f - m_releaseCoeff) * peakDB;

        // Gain computer with soft knee
        float overDB = m_envDB - thresh;
        float gainReductionDB = 0.0f;

        if (knee > 0.0f && overDB > -halfKnee && overDB < halfKnee) {
            // Soft knee region
            float x = overDB + halfKnee;
            gainReductionDB = (x * x) / (2.0f * knee) * (1.0f / ratio - 1.0f);
        } else if (overDB >= halfKnee) {
            gainReductionDB = overDB * (1.0f / ratio - 1.0f);
        }

        float gain = std::pow(10.0f, gainReductionDB / 20.0f) * makeup;

        buffer[i * numChannels] = L * gain;
        if (numChannels > 1) buffer[i * numChannels + 1] = R * gain;
    }
}

} // namespace yawn::effects
