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

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_inputBuffer.resize(kYINBufferSize, 0.0f);
        m_yinBuffer.resize(kYINBufferSize / 2, 0.0f);
        m_writePos = 0;
        m_samplesAccumulated = 0;
        reset();
    }

    void reset() override {
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

    void process(float* buffer, int numFrames, int numChannels) override {
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

    // ─── YIN pitch detection ──────────────────────────────────────────

    void detectPitch() {
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
};

} // namespace effects
} // namespace yawn
