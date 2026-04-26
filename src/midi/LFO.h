#pragma once

#include "midi/MidiEffect.h"
#include <cmath>
#include <algorithm>

namespace yawn {
namespace midi {

// LFO — Low Frequency Oscillator MIDI effect.
// Doesn't modify MIDI messages. Instead, computes a modulation value each buffer
// that the AudioEngine applies to a target parameter on the same track.
class LFO : public MidiEffect {
public:
    // Waveform shapes
    enum Shape : int { Sine = 0, Triangle, Saw, Square, SampleAndHold, kNumShapes };

    // Target types (mirrors automation::TargetType)
    enum TargetKind : int { TgtInstrument = 0, TgtAudioEffect, TgtMidiEffect, TgtMixer };

    // Parameters
    enum Params {
        kShape = 0,         // 0-4 (Sine, Triangle, Saw, Square, S&H)
        kRate,              // In beats: 0.0625 (1/16) to 16 (4 bars) when synced; Hz: 0.01-20 when free
        kSync,              // Boolean: 1=beat-synced, 0=free-running Hz
        kDepth,             // 0.0 to 1.0 (modulation amount)
        kPhase,             // 0.0 to 1.0 (phase offset, maps to 0-360°)
        kTargetType,        // 0=Instrument, 1=AudioEffect, 2=MidiEffect, 3=Mixer
        kTargetChainIndex,  // Effect slot index (for AudioEffect/MidiEffect targets)
        kTargetParamIndex,  // Parameter index within target device
        kBias,              // -1.0 to 1.0 DC offset added after depth scaling
        kNumParams
    };

    void init(double sampleRate) override;
    void reset() override;
    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override;

    const char* name() const override { return "LFO"; }
    const char* id()   const override { return "lfo"; }

    int parameterCount() const override { return kNumParams; }

    static constexpr const char* kShapeLabels[]  = {"Sine", "Tri", "Saw", "Sqr", "S&H"};
    static constexpr const char* kTargetLabels[] = {"Inst", "FX", "MIDI", "Mix"};

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Shape",    0.0f, static_cast<float>(kNumShapes - 1), 0.0f, "",   false, false, WidgetHint::StepSelector, kShapeLabels, 5},
            {"Rate",     0.0625f, 16.0f,  1.0f,  "beats", false, false},
            {"Sync",     0.0f,    1.0f,   1.0f,  "",      true,  false},
            {"Depth",    0.0f,    1.0f,   0.5f,  "",      false, false, WidgetHint::DentedKnob},
            {"Phase",    0.0f,    1.0f,   0.0f,  "°",     false, false, WidgetHint::Knob360},
            {"Target",   0.0f,    3.0f,   0.0f,  "",      false, false, WidgetHint::StepSelector, kTargetLabels, 4},
            {"Chain",    0.0f,    7.0f,   0.0f,  "",      false, false, WidgetHint::StepSelector},
            {"Param",    0.0f,    63.0f,  0.0f,  "",      false, false, WidgetHint::StepSelector},
            {"Bias",    -1.0f,    1.0f,   0.0f,  "",      false, false, WidgetHint::DentedKnob},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kShape:            return static_cast<float>(m_shape);
            case kRate:             return static_cast<float>(m_rate);
            case kSync:             return m_sync ? 1.0f : 0.0f;
            case kDepth:            return m_depth;
            case kPhase:            return m_phaseOffset;
            case kTargetType:       return static_cast<float>(m_targetType);
            case kTargetChainIndex: return static_cast<float>(m_targetChain);
            case kTargetParamIndex: return static_cast<float>(m_targetParam);
            case kBias:             return m_bias;
            default: return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kShape:
                m_shape = static_cast<Shape>(std::clamp(static_cast<int>(value), 0, kNumShapes - 1));
                break;
            case kRate:
                m_rate = std::clamp(static_cast<double>(value),
                    m_sync ? 0.0625 : 0.01, m_sync ? 16.0 : 20.0);
                break;
            case kSync:
                m_sync = (value >= 0.5f);
                break;
            case kDepth:
                m_depth = std::clamp(value, 0.0f, 1.0f);
                break;
            case kPhase:
                m_phaseOffset = std::clamp(value, 0.0f, 1.0f);
                break;
            case kTargetType:
                m_targetType = static_cast<TargetKind>(std::clamp(static_cast<int>(value), 0, 3));
                break;
            case kTargetChainIndex:
                m_targetChain = std::clamp(static_cast<int>(value), 0, 7);
                break;
            case kTargetParamIndex:
                m_targetParam = std::clamp(static_cast<int>(value), 0, 63);
                break;
            case kBias:
                m_bias = std::clamp(value, -1.0f, 1.0f);
                break;
        }
    }

    // --- Modulation output interface ---
    bool  hasModulationOutput() const override {
        return m_depth > 0.0f || m_bias != 0.0f;
    }
    float modulationValue()     const override { return m_outputValue; }
    int   modulationTargetType()  const override { return static_cast<int>(m_targetType); }
    int   modulationTargetChain() const override { return m_targetChain; }
    int   modulationTargetParam() const override { return m_targetParam; }

    // --- Phase linking interface ---
    bool     isLinkedToSource()  const override { return m_linkTargetId != 0; }
    uint32_t linkSourceId()      const override { return m_linkTargetId; }
    double   currentPhase()      const override { return m_basePhase; }

    void overridePhase(double leaderPhase) override {
        if (m_depth <= 0.0f && m_bias == 0.0f) { m_outputValue = 0.0f; return; }
        double phase = leaderPhase + static_cast<double>(m_phaseOffset);
        phase = phase - std::floor(phase);
        float raw = computeWaveform(phase);
        m_outputValue = raw * m_depth + m_bias;
    }

    // Link target ID accessors (0 = no link, otherwise leader's instanceId)
    uint32_t linkTargetId() const { return m_linkTargetId; }
    void setLinkTargetId(uint32_t id) { m_linkTargetId = id; }

    // Direct accessors for UI/display
    float currentValue() const { return m_outputValue; }
    Shape shape() const { return m_shape; }

private:
    float computeWaveform(double phase);

    // Parameters
    Shape      m_shape       = Sine;
    double     m_rate        = 1.0;      // beats (synced) or Hz (free)
    bool       m_sync        = true;
    float      m_depth       = 0.5f;
    float      m_phaseOffset = 0.0f;
    TargetKind m_targetType  = TgtInstrument;
    int        m_targetChain = 0;
    int        m_targetParam = 0;
    float      m_bias        = 0.0f;     // -1.0..1.0 DC offset
    uint32_t   m_linkTargetId = 0;       // 0 = no link, else leader's instanceId

    // State
    double m_freePhase    = 0.0;     // free-running phase accumulator
    double m_basePhase    = 0.0;     // last computed base phase (before offset)
    float  m_outputValue  = 0.0f;    // last computed modulation value

    // Sample & Hold state
    mutable double   m_shLastPhase = -1.0;
    mutable float    m_shValue     = 0.0f;
    mutable uint32_t m_shRng       = 42;
};

} // namespace midi
} // namespace yawn
