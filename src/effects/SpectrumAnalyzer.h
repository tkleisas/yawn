#pragma once

// SpectrumAnalyzer — pass-through audio effect that computes FFT
// for real-time frequency visualization. Does not alter the audio signal.
// Uses radix-2 Cooley-Tukey FFT.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace effects {

class SpectrumAnalyzer : public AudioEffect {
public:
    static constexpr int kFFTSize = 1024;
    static constexpr int kDisplayBins = kFFTSize / 2;

    enum Param { kSmoothing, kGain, kFreeze, kParamCount };

    const char* name() const override { return "Spectrum"; }
    const char* id()   const override { return "spectrum"; }

    void init(double sampleRate, int maxBlockSize) override {
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

    void reset() override {
        std::fill(m_inputBuffer.begin(), m_inputBuffer.end(), 0.0f);
        std::fill(m_magnitudes.begin(), m_magnitudes.end(), 0.0f);
        std::fill(m_smoothMags.begin(), m_smoothMags.end(), 0.0f);
        std::fill(m_displayBuffer.begin(), m_displayBuffer.end(), 0.0f);
        m_writePos = 0;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
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

    const float* displayData() const { return m_displayBuffer.data(); }
    int displaySize() const { return kDisplayBins; }
    bool hasNewData() const { return m_displayReady.load(std::memory_order_acquire); }
    void clearNewData() { m_displayReady.store(false, std::memory_order_release); }
    double analyzerSampleRate() const { return m_sampleRate; }

    bool isVisualizer() const { return true; }
    const char* visualizerType() const { return "spectrum"; }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo p[kParamCount] = {
            {"Smooth", 0.0f, 0.99f, 0.85f, "", false},
            {"Gain",   0.1f, 10.0f, 1.0f,  "", false},
            {"Freeze", 0.0f, 1.0f,  0.0f,  "", true},
        };
        return p[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kSmoothing: return m_smoothing;
            case kGain:      return m_gain;
            case kFreeze:    return m_freeze;
            default:         return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kSmoothing: m_smoothing = std::clamp(value, 0.0f, 0.99f); break;
            case kGain:      m_gain = std::clamp(value, 0.1f, 10.0f); break;
            case kFreeze:    m_freeze = (value > 0.5f) ? 1.0f : 0.0f; break;
        }
    }

private:
    void computeFFT() {
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

    std::vector<float> m_inputBuffer;
    std::vector<float> m_window;
    std::vector<float> m_fftReal;
    std::vector<float> m_fftImag;
    std::vector<float> m_magnitudes;
    std::vector<float> m_smoothMags;
    std::vector<float> m_displayBuffer;
    int m_writePos = 0;
    std::atomic<bool> m_displayReady{false};

    float m_smoothing = 0.85f;
    float m_gain = 1.0f;
    float m_freeze = 0.0f;
};

} // namespace effects
} // namespace yawn
