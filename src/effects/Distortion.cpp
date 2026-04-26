#include "effects/Distortion.h"

namespace yawn::effects {

void Distortion::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    updateToneFilter();
    reset();
}

void Distortion::reset() { m_toneL.reset(); m_toneR.reset(); }

void Distortion::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;

    float drive = std::pow(10.0f, m_params[kDrive] / 20.0f);
    float wet = m_params[kWetDry];
    float dry = 1.0f - wet;
    int type = static_cast<int>(m_params[kType]);

    for (int i = 0; i < numFrames; ++i) {
        float L = buffer[i * numChannels];
        float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

        float distL = shape(L * drive, type);
        float distR = shape(R * drive, type);

        // Tone filter
        distL = m_toneL.process(distL);
        distR = m_toneR.process(distR);

        buffer[i * numChannels] = dry * L + wet * distL;
        if (numChannels > 1)
            buffer[i * numChannels + 1] = dry * R + wet * distR;
    }
}

float Distortion::shape(float x, int type) {
    switch (type) {
    case SoftClip:
        return std::tanh(x);
    case HardClip:
        return std::max(-1.0f, std::min(1.0f, x));
    case Tube: {
        // Asymmetric soft saturation (tube-like)
        if (x >= 0.0f)
            return 1.0f - std::exp(-x);
        else
            return -1.0f + std::exp(x);
    }
    case Foldback: {
        // Foldback distortion
        while (x > 1.0f || x < -1.0f) {
            if (x > 1.0f) x = 2.0f - x;
            if (x < -1.0f) x = -2.0f - x;
        }
        return x;
    }
    default:
        return std::tanh(x);
    }
}

} // namespace yawn::effects
