#include "effects/Tuner.h"

namespace yawn::effects {

void Tuner::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_inputBuffer.resize(kYINBufferSize, 0.0f);
    m_yinBuffer.resize(kYINBufferSize / 2, 0.0f);
    m_writePos = 0;
    m_samplesAccumulated = 0;
    reset();
}

void Tuner::reset() {
    std::fill(m_inputBuffer.begin(), m_inputBuffer.end(), 0.0f);
    std::fill(m_yinBuffer.begin(), m_yinBuffer.end(), 0.0f);
    m_writePos = 0;
    m_samplesAccumulated = 0;
    m_displayBuffer[0] = 0.0f;
    m_displayBuffer[1] = 0.0f;
    m_displayBuffer[2] = 0.0f;
    m_displayBuffer[3] = 0.0f;
    m_hasSmoothed = false;
    m_smoothFreq = 0.0f;
    m_smoothCents = 0.0f;
    m_smoothConf = 0.0f;
    m_smoothNote = -1;
}

void Tuner::process(float* buffer, int numFrames, int numChannels) {
    float gain = m_gain;
    for (int i = 0; i < numFrames; ++i) {
        float sample = buffer[i * numChannels];
        if (numChannels > 1)
            sample = (sample + buffer[i * numChannels + 1]) * 0.5f;

        m_inputBuffer[m_writePos] = sample * gain;
        m_writePos = (m_writePos + 1) % kYINBufferSize;
        ++m_samplesAccumulated;
    }

    // Run YIN when we have a full buffer of new samples
    if (m_samplesAccumulated >= kYINBufferSize) {
        m_samplesAccumulated = 0;
        detectPitch();
    }
}

// ─── YIN pitch detection ──────────────────────────────────────────

void Tuner::detectPitch() {
    const int halfN = kYINBufferSize / 2;

    // Build a contiguous buffer from ring buffer
    std::vector<float> buf(kYINBufferSize);
    for (int i = 0; i < kYINBufferSize; ++i)
        buf[i] = m_inputBuffer[(m_writePos + i) % kYINBufferSize];

    // Check for silence — skip detection if RMS is very low
    float rms = 0.0f;
    for (int i = 0; i < kYINBufferSize; ++i)
        rms += buf[i] * buf[i];
    rms = std::sqrt(rms / kYINBufferSize);
    if (rms < 0.005f) {
        m_displayBuffer[0] = 0.0f;
        m_displayBuffer[1] = 0.0f;
        m_displayBuffer[2] = 0.0f;
        m_displayBuffer[3] = 0.0f;
        m_hasSmoothed = false;
        m_displayReady.store(true, std::memory_order_release);
        return;
    }

    // Step 1: Difference function
    for (int tau = 0; tau < halfN; ++tau) {
        float sum = 0.0f;
        for (int j = 0; j < halfN; ++j) {
            float d = buf[j] - buf[j + tau];
            sum += d * d;
        }
        m_yinBuffer[tau] = sum;
    }

    // Step 2: Cumulative mean normalized difference
    m_yinBuffer[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < halfN; ++tau) {
        runningSum += m_yinBuffer[tau];
        m_yinBuffer[tau] = m_yinBuffer[tau] * tau / runningSum;
    }

    // Step 3: Absolute threshold — find first dip below threshold
    constexpr float kThreshold = 0.15f;
    int tauEstimate = -1;
    for (int tau = 2; tau < halfN; ++tau) {
        if (m_yinBuffer[tau] < kThreshold) {
            // Find local minimum
            while (tau + 1 < halfN && m_yinBuffer[tau + 1] < m_yinBuffer[tau])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 0) {
        // No clear pitch detected
        m_displayBuffer[2] = 0.0f;
        m_displayReady.store(true, std::memory_order_release);
        return;
    }

    // Step 4: Parabolic interpolation for sub-sample accuracy
    float betterTau = static_cast<float>(tauEstimate);
    if (tauEstimate > 0 && tauEstimate < halfN - 1) {
        float s0 = m_yinBuffer[tauEstimate - 1];
        float s1 = m_yinBuffer[tauEstimate];
        float s2 = m_yinBuffer[tauEstimate + 1];
        float denom = 2.0f * (2.0f * s1 - s0 - s2);
        if (std::abs(denom) > 1e-8f)
            betterTau += (s0 - s2) / denom;
    }

    float frequency = static_cast<float>(m_sampleRate) / betterTau;
    float confidence = 1.0f - m_yinBuffer[tauEstimate];

    // Clamp to musical range (20 Hz - 5000 Hz)
    if (frequency < 20.0f || frequency > 5000.0f) {
        m_displayBuffer[2] = 0.0f;
        m_displayReady.store(true, std::memory_order_release);
        return;
    }

    // Convert frequency to nearest MIDI note and cents deviation
    float midiNoteF = 69.0f + 12.0f * std::log2(frequency / m_refPitch);
    int midiNote = static_cast<int>(std::round(midiNoteF));
    float cents = (midiNoteF - midiNote) * 100.0f;

    // Exponential smoothing — fast when note changes, slow for fine tuning
    if (!m_hasSmoothed) {
        m_smoothFreq = frequency;
        m_smoothCents = cents;
        m_smoothConf = confidence;
        m_smoothNote = midiNote;
        m_hasSmoothed = true;
    } else {
        bool noteChanged = (midiNote != m_smoothNote);
        float alpha = noteChanged ? kSmoothFast : kSmoothSlow;
        m_smoothFreq  = m_smoothFreq  + alpha * (frequency  - m_smoothFreq);
        m_smoothCents = m_smoothCents + alpha * (cents - m_smoothCents);
        m_smoothConf  = m_smoothConf  + alpha * (confidence - m_smoothConf);
        if (noteChanged) m_smoothNote = midiNote;
    }

    m_displayBuffer[0] = m_smoothFreq;
    m_displayBuffer[1] = m_smoothCents;
    m_displayBuffer[2] = m_smoothConf;
    m_displayBuffer[3] = static_cast<float>(m_smoothNote);
    m_displayReady.store(true, std::memory_order_release);
}

} // namespace yawn::effects
