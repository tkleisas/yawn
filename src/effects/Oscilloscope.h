#pragma once

// Oscilloscope — pass-through audio effect that captures waveform data
// for real-time visualization. Does not alter the audio signal.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace yawn {
namespace effects {

class Oscilloscope : public AudioEffect {
public:
    static constexpr int kDisplaySize = 512;

    enum Param { kTimeWindow, kGain, kFreeze, kParamCount };

    const char* name() const override { return "Oscilloscope"; }
    const char* id()   const override { return "oscilloscope"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_ringBuffer.resize(kDisplaySize * 2, 0.0f);
        m_displayBuffer.resize(kDisplaySize, 0.0f);
        m_writePos = 0;
        reset();
    }

    void reset() override {
        std::fill(m_ringBuffer.begin(), m_ringBuffer.end(), 0.0f);
        std::fill(m_displayBuffer.begin(), m_displayBuffer.end(), 0.0f);
        m_writePos = 0;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_freeze > 0.5f) return; // frozen — don't update buffer

        float gain = m_gain;
        for (int i = 0; i < numFrames; ++i) {
            // Mix to mono for display
            float sample = buffer[i * numChannels];
            if (numChannels > 1)
                sample = (sample + buffer[i * numChannels + 1]) * 0.5f;

            m_ringBuffer[m_writePos] = sample * gain;
            m_writePos = (m_writePos + 1) % kDisplaySize;
        }

        // Snapshot for UI: copy from ring buffer in order
        int rp = m_writePos;
        for (int i = 0; i < kDisplaySize; ++i) {
            m_displayBuffer[i] = m_ringBuffer[(rp + i) % kDisplaySize];
        }
        m_displayReady.store(true, std::memory_order_release);
    }

    // UI thread reads display data
    const float* displayData() const { return m_displayBuffer.data(); }
    int displaySize() const { return kDisplaySize; }
    bool hasNewData() const { return m_displayReady.load(std::memory_order_acquire); }
    void clearNewData() { m_displayReady.store(false, std::memory_order_release); }

    // Visualization type identifier
    bool isVisualizer() const { return true; }
    const char* visualizerType() const { return "oscilloscope"; }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo p[kParamCount] = {
            {"Time",   1.0f, 100.0f, 20.0f, "ms", false},
            {"Gain",   0.1f, 10.0f,   1.0f, "",   false},
            {"Freeze", 0.0f,  1.0f,   0.0f, "",   true},
        };
        return p[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kTimeWindow: return m_timeWindow;
            case kGain:       return m_gain;
            case kFreeze:     return m_freeze;
            default:          return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kTimeWindow: m_timeWindow = std::clamp(value, 1.0f, 100.0f); break;
            case kGain:       m_gain = std::clamp(value, 0.1f, 10.0f); break;
            case kFreeze:     m_freeze = (value > 0.5f) ? 1.0f : 0.0f; break;
        }
    }

private:
    std::vector<float> m_ringBuffer;
    std::vector<float> m_displayBuffer;
    int m_writePos = 0;
    std::atomic<bool> m_displayReady{false};

    float m_timeWindow = 20.0f;
    float m_gain = 1.0f;
    float m_freeze = 0.0f;
};

} // namespace effects
} // namespace yawn
