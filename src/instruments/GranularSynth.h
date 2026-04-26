#pragma once

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "effects/FreqMap.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <climits>
#include <algorithm>

namespace yawn::instruments {

class GranularSynth : public Instrument {
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kMaxGrains = 64;
    static constexpr int kParamCount = 17;
    static constexpr int kSidechainRingSeconds = 2;

    static float cutoffNormToHz(float x) { return effects::logNormToHz(x, 20.0f, 20000.0f); }
    static float cutoffHzToNorm(float hz) { return effects::logHzToNorm(hz, 20.0f, 20000.0f); }
    static void  formatCutoffHz(float v, char* buf, int n) {
        effects::formatHz(cutoffNormToHz(v), buf, n);
    }

    enum Param {
        kPosition = 0,
        kGrainSize,
        kDensity,
        kSpread,
        kPitch,
        kSpray,
        kShape,
        kScan,
        kFilterCutoff,
        kFilterReso,
        kAttack,
        kRelease,
        kStereoWidth,
        kReverse,
        kJitter,
        kVolume,
        kSidechainInput,
    };

    const char* name() const override { return "Granular Synth"; }
    const char* id() const override { return "granular"; }
    bool supportsSidechain() const override { return true; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    void loadSample(const float* data, int numFrames, int numChannels,
                    int rootNote = 60);
    void clearSample();

    bool hasSample() const { return m_sampleFrames > 0; }
    int  sampleFrames() const { return m_sampleFrames; }
    int  sampleChannels() const { return m_sampleChannels; }
    const float* sampleData() const { return m_sampleData.empty() ? nullptr : m_sampleData.data(); }
    float scanPosition() const { return static_cast<float>(m_scanPos); }
    float currentPosition() const { return m_params[kPosition]; }
    bool  isPlaying() const {
        for (auto& v : m_voices) if (v.active) return true;
        return false;
    }

    int parameterCount() const override { return kParamCount; }
    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kShapeLabels[] = {"Hann", "Tri", "Gauss", "Tukey"};
        static const InstrumentParameterInfo info[kParamCount] = {
            {"Position",      0.0f,   1.0f,   0.5f,  "",    false, false, WidgetHint::DentedKnob},
            {"Grain Size",   10.0f, 500.0f, 100.0f,  "ms",  false},
            {"Density",       1.0f,  40.0f,   8.0f,  "g/s", false},
            {"Spread",        0.0f,   1.0f,   0.1f,  "",    false},
            {"Pitch",       -24.0f,  24.0f,   0.0f,  "st",  false, false, WidgetHint::DentedKnob},
            {"Spray",         0.0f, 100.0f,  10.0f,  "ms",  false},
            {"Shape",         0.0f,   3.0f,   0.0f,  "",    false, false, WidgetHint::StepSelector, kShapeLabels, 4},
            {"Scan",         -1.0f,   1.0f,   0.0f,  "",    false, false, WidgetHint::DentedKnob},
            {"Filter Cut",  0.0f, 1.0f, 1.0f, "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Filter Reso",   0.0f,   1.0f,   0.0f,  "",    false},
            {"Attack",        1.0f, 5000.0f,  10.0f, "ms",  false},
            {"Release",       1.0f, 5000.0f, 200.0f, "ms",  false},
            {"Stereo Width",  0.0f,   1.0f,   0.5f,  "",    false, false, WidgetHint::DentedKnob},
            {"Reverse",       0.0f,   1.0f,   0.0f,  "",    true},
            {"Jitter",        0.0f,   1.0f,   0.0f,  "",    false},
            {"Volume",        0.0f,   1.0f,   0.8f,  "",    false, false, WidgetHint::DentedKnob},
            {"Sidechain In",  0.0f,   1.0f,   0.0f,  "",    true},
        };
        return info[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        auto& pi = parameterInfo(index);
        m_params[index] = std::clamp(value, pi.minValue, pi.maxValue);
    }

private:
    struct Grain {
        bool active = false;
        double readPos = 0.0;
        double speed = 1.0;
        int pos = 0;
        int length = 0;
        int shape = 0;
        float velocity = 1.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        bool fromSidechain = false;
    };

    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        Envelope ampEnv;
    };

    float grainWindow(float t, int shape) const;
    float readSample(double pos, bool fromSidechain) const;
    float randFloat();
    void  spawnGrain(Voice& v, float position, float spread, float sprayMs,
                    float pitchSt, float jitter, int grainSizeSamples,
                    int shape, bool reverse, float stereoWidth,
                    bool fromSidechain);
    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    void resetParams();

    float m_params[kParamCount] = {};
    Voice m_voices[kMaxVoices] = {};
    Grain m_grains[kMaxGrains] = {};

    std::vector<float> m_sampleData;
    int m_sampleFrames = 0;
    int m_sampleChannels = 1;
    int m_rootNote = 60;

    double m_grainTimer = 0.0;
    double m_scanPos = 0.0;
    uint32_t m_rngState = 12345u;
    int64_t m_voiceCounter = 0;

    float m_filterIcL[2] = {};
    float m_filterIcR[2] = {};

    std::vector<float> m_scRing;
    int m_scRingSize = 0;
    int m_scWritePos = 0;
    int m_scFilled = 0;
};

} // namespace yawn::instruments
