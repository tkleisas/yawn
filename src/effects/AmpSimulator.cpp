#include "effects/AmpSimulator.h"

namespace yawn::effects {

void AmpSimulator::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    updateFilters();
    reset();
}

void AmpSimulator::reset() {
    m_bassL.reset();   m_bassR.reset();
    m_midL.reset();    m_midR.reset();
    m_trebL.reset();   m_trebR.reset();
    m_cabLpL.reset();  m_cabLpR.reset();
    m_cabPeakL.reset();m_cabPeakR.reset();
    m_presL.reset();   m_presR.reset();
    m_inputHpL.reset();m_inputHpR.reset();
    m_osL.reset();     m_osR.reset();
}

void AmpSimulator::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;

    float driveDB = m_params[kGain];
    float drive = std::pow(10.0f, driveDB / 20.0f);
    float outputDB = m_params[kOutput];
    float outputGain = std::pow(10.0f, outputDB / 20.0f);
    int ampType = std::clamp(static_cast<int>(m_params[kAmpType]), 0, 3);
    float cabMix = std::clamp(m_params[kCabinet], 0.0f, 1.0f);
    const bool os = m_params[kOversample] >= 0.5f;

    for (int i = 0; i < numFrames; ++i) {
        float L = buffer[i * numChannels];
        float R = (numChannels > 1) ? buffer[i * numChannels + 1] : L;

        // 1. Input high-pass (remove DC / sub-bass rumble)
        L = m_inputHpL.process(L);
        R = m_inputHpR.process(R);

        // 2. Preamp drive + saturation (the only nonlinearity — oversample
        //    it when enabled so its harmonics don't alias; the tone stack
        //    and cab below are linear and stay at the host rate).
        if (os) {
            L = m_osL.process(L, [&](float v) { return ampSaturate(v * drive, ampType); });
            R = m_osR.process(R, [&](float v) { return ampSaturate(v * drive, ampType); });
        } else {
            L = ampSaturate(L * drive, ampType);
            R = ampSaturate(R * drive, ampType);
        }

        // 3. Tone stack
        L = m_bassL.process(L);
        L = m_midL.process(L);
        L = m_trebL.process(L);
        R = m_bassR.process(R);
        R = m_midR.process(R);
        R = m_trebR.process(R);

        // 4. Cabinet simulation (blend between direct and cab-filtered)
        if (cabMix > 0.001f) {
            float cabL = m_cabLpL.process(L);
            cabL = m_cabPeakL.process(cabL);
            float cabR = m_cabLpR.process(R);
            cabR = m_cabPeakR.process(cabR);
            L = L * (1.0f - cabMix) + cabL * cabMix;
            R = R * (1.0f - cabMix) + cabR * cabMix;
        }

        // 5. Presence (post-cab high shelf)
        L = m_presL.process(L);
        R = m_presR.process(R);

        // 6. Output level
        buffer[i * numChannels] = L * outputGain;
        if (numChannels > 1)
            buffer[i * numChannels + 1] = R * outputGain;
    }
}

float AmpSimulator::ampSaturate(float x, int type) {
    switch (type) {
    case Clean:
        // Gentle soft clip — mostly linear, very mild compression
        return std::tanh(x * 0.4f) * 2.5f;

    case Crunch: {
        // Asymmetric tube-like clipping
        if (x >= 0.0f)
            return std::tanh(x * 0.8f) * 1.25f;
        else
            return std::tanh(x * 0.6f) * 1.67f;
    }

    case Lead:
        // Heavy symmetric saturation with some compression
        return std::tanh(x) * 1.1f;

    case HighGain: {
        // Extreme saturation: two-stage cascade
        float s1 = std::tanh(x * 1.5f);
        return std::tanh(s1 * 2.0f) * 0.9f;
    }

    default:
        return std::tanh(x);
    }
}

void AmpSimulator::updateFilters() {
    // Tone stack
    Biquad::computeStereo(m_bassL, m_bassR, Biquad::Type::LowShelf,
                          m_sampleRate, 200.0, m_params[kBass], 0.707);
    Biquad::computeStereo(m_midL, m_midR, Biquad::Type::Peak,
                          m_sampleRate, 800.0, m_params[kMid], 1.0);
    Biquad::computeStereo(m_trebL, m_trebR, Biquad::Type::HighShelf,
                          m_sampleRate, 3200.0, m_params[kTreble], 0.707);

    // Cabinet simulation: low-pass at 5kHz + resonant peak at 2.5kHz
    Biquad::computeStereo(m_cabLpL, m_cabLpR, Biquad::Type::LowPass,
                          m_sampleRate, 5000.0, 0.0, 0.707);
    Biquad::computeStereo(m_cabPeakL, m_cabPeakR, Biquad::Type::Peak,
                          m_sampleRate, 2500.0, 3.0, 1.2);

    // Presence: post-cab high shelf at 4kHz
    Biquad::computeStereo(m_presL, m_presR, Biquad::Type::HighShelf,
                          m_sampleRate, 4000.0, m_params[kPresence], 0.707);

    // Input high-pass at 80Hz to remove rumble
    Biquad::computeStereo(m_inputHpL, m_inputHpR, Biquad::Type::HighPass,
                          m_sampleRate, 80.0, 0.0, 0.707);
}

} // namespace yawn::effects
