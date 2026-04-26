#include "effects/Filter.h"

namespace yawn::effects {

void Filter::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    updateFilter();
    reset();
}

void Filter::reset() { m_filterL.reset(); m_filterR.reset(); }

void Filter::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;
    for (int i = 0; i < numFrames; ++i) {
        buffer[i * numChannels] = m_filterL.process(buffer[i * numChannels]);
        if (numChannels > 1)
            buffer[i * numChannels + 1] = m_filterR.process(buffer[i * numChannels + 1]);
    }
}

void Filter::updateFilter() {
    Biquad::Type bt;
    int t = static_cast<int>(m_params[kType]);
    switch (t) {
        case HP:    bt = Biquad::Type::HighPass; break;
        case BP:    bt = Biquad::Type::BandPass; break;
        case Notch: bt = Biquad::Type::Notch;    break;
        default:    bt = Biquad::Type::LowPass;  break;
    }
    const float hz = cutoffNormToHz(m_params[kCutoff]);
    m_filterL.compute(bt, m_sampleRate, hz, 0.0, m_params[kResonance]);
    m_filterR.compute(bt, m_sampleRate, hz, 0.0, m_params[kResonance]);
}

} // namespace yawn::effects
