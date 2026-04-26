#pragma once

// Tuner — pass-through audio effect with YIN pitch detection.
// Detects the fundamental frequency of monophonic audio input and
// exposes note name, cents deviation, and confidence for visualization.

#include "effects/AudioEffect.h"
#include "WidgetHint.h"
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

class Tuner : public AudioEffect {
public:
    // YIN buffer size — 2048 gives good accuracy down to ~43 Hz (low E on bass)
    static constexpr int kYINBufferSize = 2048;
    // Display data layout: [frequency, cents, confidence, midiNote]
    static constexpr int kDisplaySize   = 4;

    enum Param { kGain, kRefPitch, kParamCount };

    const char* name() const override { return "Tuner"; }
    const char* id()   const override { return "tuner"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    // UI thread reads display data
    const float* displayData() const { return m_displayBuffer; }
    int displaySize() const { return kDisplaySize; }
    bool hasNewData() const { return m_displayReady.load(std::memory_order_acquire); }
    void clearNewData() { m_displayReady.store(false, std::memory_order_release); }

    bool isVisualizer() const { return true; }
    const char* visualizerType() const { return "tuner"; }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo p[kParamCount] = {
            {"Gain",  0.1f, 10.0f, 1.0f, "",   false, false, WidgetHint::DentedKnob},
            {"A4 Ref", 420.0f, 460.0f, 440.0f, "Hz", false},
        };
        return p[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kGain:     return m_gain;
            case kRefPitch: return m_refPitch;
            default:        return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kGain:     m_gain = std::clamp(value, 0.1f, 10.0f); break;
            case kRefPitch: m_refPitch = std::clamp(value, 420.0f, 460.0f); break;
        }
    }

    // Detected pitch info — thread-safe read from UI
    float detectedFrequency() const { return m_displayBuffer[0]; }
    float detectedCents()     const { return m_displayBuffer[1]; }
    float detectedConfidence() const { return m_displayBuffer[2]; }
    int   detectedMidiNote()  const { return static_cast<int>(m_displayBuffer[3]); }

    static const char* noteName(int midiNote) {
        static const char* names[] = {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        if (midiNote < 0 || midiNote > 127) return "?";
        return names[midiNote % 12];
    }

    static int noteOctave(int midiNote) {
        if (midiNote < 0) return -1;
        return (midiNote / 12) - 1;
    }

private:
    void detectPitch();

    std::vector<float> m_inputBuffer;
    std::vector<float> m_yinBuffer;
    int m_writePos = 0;
    int m_samplesAccumulated = 0;
    std::atomic<bool> m_displayReady{false};

    float m_displayBuffer[kDisplaySize] = {};
    float m_gain = 1.0f;
    float m_refPitch = 440.0f;

    // Smoothing state
    float m_smoothFreq = 0.0f;
    float m_smoothCents = 0.0f;
    float m_smoothConf = 0.0f;
    int   m_smoothNote = -1;
    bool  m_hasSmoothed = false;
    // Smoothing factor: 0 = frozen, 1 = instant. ~0.15 feels natural for tuners.
    static constexpr float kSmoothSlow = 0.12f;
    static constexpr float kSmoothFast = 0.6f;
};

} // namespace effects
} // namespace yawn
