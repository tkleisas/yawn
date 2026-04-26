#include "effects/EQ.h"

namespace yawn::effects {

void EQ::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    updateFilters();
    reset();
}

void EQ::reset() {
    for (auto& f : m_lowL)  { f.reset(); } for (auto& f : m_lowR)  { f.reset(); }
    for (auto& f : m_midL)  { f.reset(); } for (auto& f : m_midR)  { f.reset(); }
    for (auto& f : m_highL) { f.reset(); } for (auto& f : m_highR) { f.reset(); }
}

void EQ::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;
    for (int i = 0; i < numFrames; ++i) {
        float L = buffer[i * numChannels];
        float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

        L = m_lowL[0].process(L);  R = m_lowR[0].process(R);
        L = m_midL[0].process(L);  R = m_midR[0].process(R);
        L = m_highL[0].process(L); R = m_highR[0].process(R);

        buffer[i * numChannels] = L;
        if (numChannels > 1) buffer[i * numChannels + 1] = R;
    }
}

void EQ::updateFilters() {
    const float lowHz  = lowFreqNormToHz (m_params[kLowFreq]);
    const float midHz  = midFreqNormToHz (m_params[kMidFreq]);
    const float highHz = highFreqNormToHz(m_params[kHighFreq]);
    m_lowL[0].compute(Biquad::Type::LowShelf,  m_sampleRate, lowHz,  m_params[kLowGain],  0.707);
    m_lowR[0].compute(Biquad::Type::LowShelf,  m_sampleRate, lowHz,  m_params[kLowGain],  0.707);
    m_midL[0].compute(Biquad::Type::Peak,       m_sampleRate, midHz,  m_params[kMidGain],  m_params[kMidQ]);
    m_midR[0].compute(Biquad::Type::Peak,       m_sampleRate, midHz,  m_params[kMidGain],  m_params[kMidQ]);
    m_highL[0].compute(Biquad::Type::HighShelf, m_sampleRate, highHz, m_params[kHighGain], 0.707);
    m_highR[0].compute(Biquad::Type::HighShelf, m_sampleRate, highHz, m_params[kHighGain], 0.707);
}

} // namespace yawn::effects
