#pragma once

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

// 4-operator FM synthesizer with 8 algorithm presets.
// Each operator: sine oscillator + level + ratio + ADSR envelope.
// 16-voice polyphony with voice stealing.
class FMSynth : public Instrument {
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kNumOps    = 4;
    static constexpr int kNumAlgos  = 8;

    enum Params {
        kAlgorithm = 0, kFeedback,
        kOp1Level, kOp1Ratio, kOp1Attack, kOp1Release,
        kOp2Level, kOp2Ratio, kOp2Attack, kOp2Release,
        kOp3Level, kOp3Ratio, kOp3Attack, kOp3Release,
        kOp4Level, kOp4Ratio, kOp4Attack, kOp4Release,
        kVolume,
        kNumParams
    };

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return "FM Synth"; }
    const char* id()   const override { return "fmsynth"; }

    int parameterCount() const override { return kNumParams; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kAlgoLabels[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
        static const InstrumentParameterInfo p[kNumParams] = {
            {"Algorithm",  0, 7, 0,"",false, false, WidgetHint::StepSelector, kAlgoLabels, 8},
            {"Feedback",   0, 1, 0.2f,"",false},
            {"Op1 Level",  0, 1, 1,"",false, false, WidgetHint::DentedKnob},
            {"Op1 Ratio", 0.5f, 16, 1,"x",false, false, WidgetHint::DentedKnob},
            {"Op1 Attack", 0.001f, 5, 0.01f,"s",false},
            {"Op1 Release", 0.001f, 5, 0.3f,"s",false},
            {"Op2 Level",  0, 1, 0.8f,"",false, false, WidgetHint::DentedKnob},
            {"Op2 Ratio", 0.5f, 16, 2,"x",false, false, WidgetHint::DentedKnob},
            {"Op2 Attack", 0.001f, 5, 0.01f,"s",false},
            {"Op2 Release", 0.001f, 5, 0.3f,"s",false},
            {"Op3 Level",  0, 1, 0.6f,"",false, false, WidgetHint::DentedKnob},
            {"Op3 Ratio", 0.5f, 16, 3,"x",false, false, WidgetHint::DentedKnob},
            {"Op3 Attack", 0.001f, 5, 0.01f,"s",false},
            {"Op3 Release", 0.001f, 5, 0.5f,"s",false},
            {"Op4 Level",  0, 1, 0.4f,"",false, false, WidgetHint::DentedKnob},
            {"Op4 Ratio", 0.5f, 16, 4,"x",false, false, WidgetHint::DentedKnob},
            {"Op4 Attack", 0.001f, 5, 0.01f,"s",false},
            {"Op4 Release", 0.001f, 5, 0.5f,"s",false},
            {"Volume",     0, 1, 0.5f,"",false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        if (index == kAlgorithm) return (float)m_algorithm;
        if (index == kFeedback)  return m_feedback;
        if (index == kVolume)    return m_volume;
        int opBase = index - kOp1Level;
        if (opBase >= 0 && opBase < kNumOps * 4) {
            int op = opBase / 4, field = opBase % 4;
            switch (field) {
                case 0: return m_opLevel[op];
                case 1: return m_opRatio[op];
                case 2: return m_opAttack[op];
                case 3: return m_opRelease[op];
            }
        }
        return 0.0f;
    }

    void setParameter(int index, float value) override {
        if (index == kAlgorithm) { m_algorithm = std::clamp((int)value, 0, kNumAlgos - 1); return; }
        if (index == kFeedback)  { m_feedback = std::clamp(value, 0.0f, 1.0f); return; }
        if (index == kVolume)    { m_volume = std::clamp(value, 0.0f, 1.0f); return; }
        int opBase = index - kOp1Level;
        if (opBase >= 0 && opBase < kNumOps * 4) {
            int op = opBase / 4, field = opBase % 4;
            switch (field) {
                case 0: m_opLevel[op] = std::clamp(value, 0.0f, 1.0f); break;
                case 1: m_opRatio[op] = std::clamp(value, 0.5f, 16.0f); break;
                case 2: m_opAttack[op] = std::max(0.001f, value); break;
                case 3: m_opRelease[op] = std::max(0.001f, value); break;
            }
        }
    }

private:
    struct Algorithm {
        bool modMatrix[kNumOps][kNumOps]; // [dest][src] = src modulates dest
        bool isCarrier[kNumOps];
    };

    // 8 algorithm presets (operators indexed 0-3)
    static constexpr Algorithm kAlgorithms[kNumAlgos] = {
        // 0: Serial 3→2→1→0(C)
        {{{0,1,0,0},{0,0,1,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},
        // 1: (2+3)→1→0(C)
        {{{0,1,0,0},{0,0,1,1},{0,0,0,0},{0,0,0,0}}, {1,0,0,0}},
        // 2: 1→0(C), 3→2(C) — two 2-op stacks
        {{{0,1,0,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,1,0}},
        // 3: (1+2+3)→0(C) — 3 parallel modulators
        {{{0,1,1,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,0,0,0}},
        // 4: All carriers (additive)
        {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,1,1,1}},
        // 5: 3→2→0(C), 1→0(C) — merge at carrier
        {{{0,1,1,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},
        // 6: 3→(0+1+2)(C) — one mod, three carriers
        {{{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,0}}, {1,1,1,0}},
        // 7: 2→0(C), 3→1(C) — two independent pairs
        {{{0,0,1,0},{0,0,0,1},{0,0,0,0},{0,0,0,0}}, {1,1,0,0}},
    };

    struct Voice {
        bool active = false;
        uint8_t note = 0, channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        double phase[kNumOps] = {};
        float prevOut[kNumOps] = {};
        Envelope env[kNumOps];
    };

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int findFreeVoice();
    void applyDefaults();

    Voice m_voices[kMaxVoices];
    int64_t m_voiceCounter = 0;
    float m_pitchBend = 0.0f;

    int   m_algorithm = 0;
    float m_feedback  = 0.2f;
    float m_opLevel[kNumOps]   = {1.0f, 0.8f, 0.6f, 0.4f};
    float m_opRatio[kNumOps]   = {1.0f, 2.0f, 3.0f, 4.0f};
    float m_opAttack[kNumOps]  = {0.01f, 0.01f, 0.01f, 0.01f};
    float m_opRelease[kNumOps] = {0.3f, 0.3f, 0.5f, 0.5f};
    float m_volume = 0.5f;
};

} // namespace instruments
} // namespace yawn
