#include "effects/Reverb.h"

namespace yawn::effects {

void Reverb::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    double scale = sampleRate / 44100.0;
    static const int kCombTunings[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int kAllpassTunings[] = {556, 441, 341, 225};
    static const int kStereoSpread = 23;

    for (int i = 0; i < 8; ++i) {
        int len = static_cast<int>(kCombTunings[i] * scale);
        m_combL[i].resize(len); m_combR[i].resize(len + kStereoSpread);
    }
    for (int i = 0; i < 4; ++i) {
        int len = static_cast<int>(kAllpassTunings[i] * scale);
        m_allpassL[i].resize(len); m_allpassR[i].resize(len + kStereoSpread);
    }

    // Pre-delay buffer (up to 250ms)
    int maxPreDelay = static_cast<int>(0.25 * sampleRate);
    m_preDelayBuf.resize(maxPreDelay * 2, 0.0f);
    m_preDelayLen = maxPreDelay;
    m_preDelayPos = 0;

    reset();
    updateParams();
}

void Reverb::reset() {
    for (auto& c : m_combL) c.clear();
    for (auto& c : m_combR) c.clear();
    for (auto& a : m_allpassL) a.clear();
    for (auto& a : m_allpassR) a.clear();
    std::fill(m_preDelayBuf.begin(), m_preDelayBuf.end(), 0.0f);
    m_preDelayPos = 0;
}

void Reverb::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;
    float wet1 = m_wet * (m_width / 2.0f + 0.5f);
    float wet2 = m_wet * ((1.0f - m_width) / 2.0f);
    float dry = 1.0f - m_wet;

    int pdSamples = static_cast<int>(m_params[kPreDelay] * 0.001f * m_sampleRate);
    if (pdSamples >= m_preDelayLen) pdSamples = m_preDelayLen - 1;

    for (int i = 0; i < numFrames; ++i) {
        float inL = buffer[i * numChannels];
        float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;
        float input = (inL + inR) * 0.5f;

        // Pre-delay
        int wrIdx = m_preDelayPos;
        m_preDelayBuf[wrIdx] = input;
        int rdIdx = (wrIdx - pdSamples + m_preDelayLen) % m_preDelayLen;
        float delayed = m_preDelayBuf[rdIdx];
        m_preDelayPos = (wrIdx + 1) % m_preDelayLen;

        // Parallel comb filters
        float outL = 0.0f, outR = 0.0f;
        for (int c = 0; c < 8; ++c) {
            outL += m_combL[c].process(delayed, m_roomSize, m_damp1, m_damp2);
            outR += m_combR[c].process(delayed, m_roomSize, m_damp1, m_damp2);
        }

        // Series allpass filters
        for (int a = 0; a < 4; ++a) {
            outL = m_allpassL[a].process(outL);
            outR = m_allpassR[a].process(outR);
        }

        float mixL = dry * inL + wet1 * outL + wet2 * outR;
        float mixR = dry * inR + wet1 * outR + wet2 * outL;

        buffer[i * numChannels] = mixL;
        if (numChannels > 1) buffer[i * numChannels + 1] = mixR;
    }
}

} // namespace yawn::effects
