#include "effects/TapeEmulation.h"

namespace yawn::effects {

void TapeEmulation::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    // Delay buffer: enough for max wow/flutter excursion (~50ms)
    int maxDelay = static_cast<int>(0.05 * sampleRate) + 256;
    m_delayL.assign(maxDelay, 0.0f);
    m_delayR.assign(maxDelay, 0.0f);
    m_delaySize = maxDelay;

    updateToneFilter();
    reset();
}

void TapeEmulation::reset() {
    std::fill(m_delayL.begin(), m_delayL.end(), 0.0f);
    std::fill(m_delayR.begin(), m_delayR.end(), 0.0f);
    m_writePos = 0;
    m_wowPhase = 0.0;
    m_flutterPhase = 0.0;
    m_hissState = 12345;
    m_toneL.reset();
    m_toneR.reset();
    m_hissFilterL.reset();
    m_hissFilterR.reset();
}

void TapeEmulation::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;

    float sat   = m_params[kSaturation];
    float wow   = m_params[kWow];
    float flut  = m_params[kFlutter];
    float hiss  = m_params[kHiss];
    float wet   = m_params[kWetDry];
    float dry   = 1.0f - wet;

    // Saturation drive: map 0–1 to 1–8x gain
    float drive = 1.0f + sat * 7.0f;
    float makeupGain = 1.0f / (1.0f + sat * 1.5f);

    // Wow: slow LFO (~0.5–6 Hz), depth up to 3ms
    double wowRate = 0.5 + wow * 5.5;
    double wowInc  = wowRate / m_sampleRate;
    float  wowDepth = wow * static_cast<float>(0.003 * m_sampleRate);

    // Flutter: fast LFO (~6–20 Hz), depth up to 0.5ms
    double flutRate = 6.0 + flut * 14.0;
    double flutInc  = flutRate / m_sampleRate;
    float  flutDepth = flut * static_cast<float>(0.0005 * m_sampleRate);

    // Base delay: 10ms (tape transport latency)
    float baseDelay = static_cast<float>(0.01 * m_sampleRate);

    // Hiss level: -60dB to -20dB
    float hissGain = hiss * 0.01f;

    for (int i = 0; i < numFrames; ++i) {
        float inL = buffer[i * numChannels];
        float inR = (numChannels > 1) ? buffer[i * numChannels + 1] : inL;

        // 1. Tape saturation (asymmetric soft clipping)
        float satL = tapeSaturate(inL * drive) * makeupGain;
        float satR = tapeSaturate(inR * drive) * makeupGain;

        // 2. Write saturated signal into delay line
        m_delayL[m_writePos] = satL;
        m_delayR[m_writePos] = satR;

        // 3. Wow + flutter modulation
        float wowMod = static_cast<float>(std::sin(m_wowPhase * 6.283185307));
        float flutMod = static_cast<float>(std::sin(m_flutterPhase * 6.283185307));
        float modDelay = baseDelay + wowMod * wowDepth + flutMod * flutDepth;

        float outL = readInterp(m_delayL, modDelay);
        float outR = readInterp(m_delayR, modDelay);

        // 4. Tone filter (high-frequency rolloff)
        outL = m_toneL.process(outL);
        outR = m_toneR.process(outR);

        // 5. Tape hiss (filtered noise)
        if (hissGain > 0.0f) {
            float noiseL = nextNoise() * hissGain;
            float noiseR = nextNoise() * hissGain;
            outL += m_hissFilterL.process(noiseL);
            outR += m_hissFilterR.process(noiseR);
        }

        // 6. Wet/dry mix
        buffer[i * numChannels] = dry * inL + wet * outL;
        if (numChannels > 1)
            buffer[i * numChannels + 1] = dry * inR + wet * outR;

        m_writePos = (m_writePos + 1) % m_delaySize;
        m_wowPhase += wowInc;
        if (m_wowPhase >= 1.0) m_wowPhase -= 1.0;
        m_flutterPhase += flutInc;
        if (m_flutterPhase >= 1.0) m_flutterPhase -= 1.0;
    }
}

} // namespace yawn::effects
