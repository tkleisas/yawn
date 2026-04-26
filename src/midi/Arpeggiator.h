#pragma once

#include "midi/MidiEffect.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace midi {

class Arpeggiator : public MidiEffect {
public:
    enum Direction { Up = 0, Down, UpDown, DownUp, Random, AsPlayed };

    enum Params {
        kDirection = 0,
        kRate,      // beats per step: 1.0=quarter, 0.5=eighth, 0.25=16th
        kGate,      // 0.1 to 1.0
        kOctaves,   // 1 to 4
        kSwing,     // 0 to 1
        kNumParams
    };

    void init(double sampleRate) override;
    void reset() override;
    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override;

    const char* name() const override { return "Arpeggiator"; }
    const char* id()   const override { return "arp"; }

    int parameterCount() const override { return kNumParams; }

    static constexpr const char* kDirectionLabels[] = {"Up", "Down", "UpDn", "DnUp", "Rand", "Played"};

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Direction", 0.0f, 5.0f, 0.0f, "",      false, false, WidgetHint::StepSelector, kDirectionLabels, 6},
            {"Rate",      0.0625f, 2.0f, 0.25f, "beats", false},
            {"Gate",      0.1f, 1.0f, 0.8f, "",      false},
            {"Octaves",   1.0f, 4.0f, 1.0f, "",      false, false, WidgetHint::StepSelector},
            {"Swing",     0.0f, 1.0f, 0.0f, "",      false},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kDirection: return (float)m_direction;
            case kRate:      return (float)m_rate;
            case kGate:      return m_gate;
            case kOctaves:   return (float)m_octaves;
            case kSwing:     return m_swing;
            default:         return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kDirection:
                m_direction = (Direction)std::clamp((int)value, 0, 5);
                rebuildPattern();
                break;
            case kRate:    m_rate = std::clamp((double)value, 0.0625, 2.0); break;
            case kGate:    m_gate = std::clamp(value, 0.1f, 1.0f); break;
            case kOctaves:
                m_octaves = std::clamp((int)value, 1, 4);
                rebuildPattern();
                break;
            case kSwing:   m_swing = std::clamp(value, 0.0f, 1.0f); break;
        }
    }

private:
    struct PatternNote { uint8_t pitch = 0; uint16_t velocity = 0; };

    int countHeldNotes() const;
    int beatToFrame(double beat, double bufStart,
                    double samplesPerBeat, int maxFrames);
    void rebuildPattern();
    uint32_t nextRandom();

    Direction m_direction = Up;
    double    m_rate      = 0.25;
    float     m_gate      = 0.8f;
    int       m_octaves   = 1;
    float     m_swing     = 0.0f;

    uint16_t m_heldVelocity[128] = {};
    int      m_noteOrder[128]    = {};
    int      m_orderCounter      = 0;
    uint8_t  m_heldChannel       = 0;

    static constexpr int kMaxPattern = 512;
    PatternNote m_pattern[kMaxPattern] = {};
    int    m_patternLen   = 0;
    int    m_stepIndex    = 0;
    double m_lastStepBeat = -1000.0;
    double m_gateOffBeat  = -1000.0;
    int    m_activeNote   = -1;
    uint32_t m_rng        = 12345;
    int64_t m_freeRunSamples = -1;
    bool    m_wasPlaying     = false;
};

} // namespace midi
} // namespace yawn
