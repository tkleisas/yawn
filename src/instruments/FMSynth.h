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

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        for (auto& v : m_voices)
            for (auto& e : v.env) e.setSampleRate(sampleRate);
        applyDefaults();
    }

    void reset() override {
        for (auto& v : m_voices) {
            v.active = false;
            for (int o = 0; o < kNumOps; ++o) {
                v.phase[o] = 0.0; v.prevOut[o] = 0.0f; v.env[o].reset();
            }
        }
        m_voiceCounter = 0;
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        for (int i = 0; i < midi.count(); ++i) {
            const auto& msg = midi[i];
            if (msg.isNoteOn()) noteOn(msg.note, msg.velocity, msg.channel);
            else if (msg.isNoteOff()) noteOff(msg.note, msg.channel);
            else if (msg.isCC() && msg.ccNumber == 123) {
                for (auto& v : m_voices)
                    if (v.active) { for (auto& e : v.env) e.gate(false); }
            }
            else if (msg.type == midi::MidiMessage::Type::PitchBend)
                m_pitchBend = midi::Convert::pb32toFloat(msg.value);
        }

        const auto& algo = kAlgorithms[m_algorithm];

        for (int v = 0; v < kMaxVoices; ++v) {
            auto& voice = m_voices[v];
            if (!voice.active) continue;

            float baseFreq = noteToFreq(voice.note) *
                std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);

            for (int i = 0; i < numFrames; ++i) {
                float opOut[kNumOps] = {};

                // Process operators from highest to lowest (modulators first)
                for (int op = kNumOps - 1; op >= 0; --op) {
                    float modIn = 0.0f;
                    for (int src = 0; src < kNumOps; ++src)
                        if (algo.modMatrix[op][src])
                            modIn += opOut[src];

                    // Self-feedback on operator 3 (highest)
                    if (op == kNumOps - 1)
                        modIn += voice.prevOut[op] * m_feedback;

                    float freq = baseFreq * m_opRatio[op];
                    float envVal = voice.env[op].process();
                    float totalPhase = (float)voice.phase[op] + modIn;
                    opOut[op] = std::sin(totalPhase * 2.0f * (float)M_PI) *
                                m_opLevel[op] * envVal;

                    voice.prevOut[op] = opOut[op];
                    voice.phase[op] += freq / m_sampleRate;
                    if (voice.phase[op] >= 1.0) voice.phase[op] -= 1.0;
                }

                // Sum carrier outputs
                float sample = 0.0f;
                for (int op = 0; op < kNumOps; ++op)
                    if (algo.isCarrier[op])
                        sample += opOut[op];

                sample *= voice.velocity * m_volume;
                buffer[i * numChannels + 0] += sample;
                if (numChannels > 1)
                    buffer[i * numChannels + 1] += sample;
            }

            // Check if all envelopes idle → deactivate voice
            bool allIdle = true;
            for (int o = 0; o < kNumOps; ++o)
                if (!voice.env[o].isIdle()) { allIdle = false; break; }
            if (allIdle) voice.active = false;
        }
    }

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

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
        int slot = findFreeVoice();
        auto& v = m_voices[slot];
        v.active = true;
        v.note = note; v.channel = ch;
        v.velocity = velocityToGain(vel16);
        v.startOrder = m_voiceCounter++;
        for (int o = 0; o < kNumOps; ++o) {
            v.phase[o] = 0.0; v.prevOut[o] = 0.0f;
            v.env[o].setADSR(m_opAttack[o], 0.1f, 1.0f, m_opRelease[o]);
            v.env[o].gate(true);
        }
    }

    void noteOff(uint8_t note, uint8_t ch) {
        for (auto& v : m_voices)
            if (v.active && v.note == note && v.channel == ch)
                for (auto& e : v.env) e.gate(false);
    }

    int findFreeVoice() {
        for (int i = 0; i < kMaxVoices; ++i)
            if (!m_voices[i].active) return i;
        int oldest = 0;
        for (int i = 1; i < kMaxVoices; ++i)
            if (m_voices[i].startOrder < m_voices[oldest].startOrder)
                oldest = i;
        return oldest;
    }

    void applyDefaults() {
        for (int i = 0; i < kNumParams; ++i)
            setParameter(i, parameterInfo(i).defaultValue);
    }

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

constexpr FMSynth::Algorithm FMSynth::kAlgorithms[FMSynth::kNumAlgos];

} // namespace instruments
} // namespace yawn
