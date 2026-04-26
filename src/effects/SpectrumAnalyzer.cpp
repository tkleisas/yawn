#include "effects/SpectrumAnalyzer.h"

namespace yawn::effects {

void SpectrumAnalyzer::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_inputBuffer.resize(kFFTSize, 0.0f);
    m_window.resize(kFFTSize, 0.0f);
    m_fftReal.resize(kFFTSize, 0.0f);
    m_fftImag.resize(kFFTSize, 0.0f);
    m_magnitudes.resize(kDisplayBins, 0.0f);
    m_smoothMags.resize(kDisplayBins, 0.0f);
    m_displayBuffer.resize(kDisplayBins, 0.0f);
    m_writePos = 0;

    // Hann window
    for (int i = 0; i < kFFTSize; ++i)
        m_window[i] = 0.5f * (1.0f - std::cos(2.0 * M_PI * i / (kFFTSize - 1)));

    reset();
}

void SpectrumAnalyzer::reset() {
    std::fill(m_inputBuffer.begin(), m_inputBuffer.end(), 0.0f);
    std::fill(m_magnitudes.begin(), m_magnitudes.end(), 0.0f);
    std::fill(m_smoothMags.begin(), m_smoothMags.end(), 0.0f);
    std::fill(m_displayBuffer.begin(), m_displayBuffer.end(), 0.0f);
    m_writePos = 0;
}

void SpectrumAnalyzer::process(float* buffer, int numFrames, int numChannels) {
    if (m_freeze > 0.5f) return;

    float gain = m_gain;
    for (int i = 0; i < numFrames; ++i) {
        float sample = buffer[i * numChannels];
        if (numChannels > 1)
            sample = (sample + buffer[i * numChannels + 1]) * 0.5f;

        m_inputBuffer[m_writePos] = sample * gain;
        m_writePos = (m_writePos + 1) % kFFTSize;

        // Run FFT when buffer is full
        if (m_writePos == 0) {
            computeFFT();
        }
    }
}

void SpectrumAnalyzer::computeFFT() {
    // Apply window and copy to FFT buffers
    for (int i = 0; i < kFFTSize; ++i) {
        m_fftReal[i] = m_inputBuffer[i] * m_window[i];
        m_fftImag[i] = 0.0f;
    }

    // Radix-2 Cooley-Tukey FFT (in-place)
    int n = kFFTSize;

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(m_fftReal[i], m_fftReal[j]);
            std::swap(m_fftImag[i], m_fftImag[j]);
        }
    }

    // Butterfly stages
    for (int len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / len;
        double wRe = std::cos(angle);
        double wIm = std::sin(angle);

        for (int i = 0; i < n; i += len) {
            double curRe = 1.0, curIm = 0.0;
            for (int j = 0; j < len / 2; ++j) {
                float tRe = (float)(m_fftReal[i + j + len / 2] * curRe -
                                    m_fftImag[i + j + len / 2] * curIm);
                float tIm = (float)(m_fftReal[i + j + len / 2] * curIm +
                                    m_fftImag[i + j + len / 2] * curRe);

                m_fftReal[i + j + len / 2] = m_fftReal[i + j] - tRe;
                m_fftImag[i + j + len / 2] = m_fftImag[i + j] - tIm;
                m_fftReal[i + j] += tRe;
                m_fftImag[i + j] += tIm;

                double newRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = newRe;
            }
        }
    }

    // Compute magnitudes (dB scale) with smoothing
    float smooth = m_smoothing;
    for (int i = 0; i < kDisplayBins; ++i) {
        float re = m_fftReal[i];
        float im = m_fftImag[i];
        float mag = std::sqrt(re * re + im * im) / (float)kFFTSize;
        // Convert to dB, clamp to -96..0 range, normalize to 0..1
        float dB = 20.0f * std::log10(std::max(mag, 1e-6f));
        float norm = std::clamp((dB + 96.0f) / 96.0f, 0.0f, 1.0f);

        m_smoothMags[i] = smooth * m_smoothMags[i] + (1.0f - smooth) * norm;
        m_magnitudes[i] = m_smoothMags[i];
    }

    // Snapshot for UI
    std::copy(m_magnitudes.begin(), m_magnitudes.end(), m_displayBuffer.begin());
    m_displayReady.store(true, std::memory_order_release);
}

} // namespace yawn::effects
