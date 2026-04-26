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

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

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
            {"Smooth", 0.0f, 0.99f, 0.85f, "", false, false, WidgetHint::DentedKnob},
            {"Gain",   0.1f, 10.0f, 1.0f,  "", false, false, WidgetHint::DentedKnob},
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
    void computeFFT();

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
