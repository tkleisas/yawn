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

    void init(double sampleRate) override {
        m_sampleRate = sampleRate;
        reset();
    }

    void reset() override {
        std::fill(std::begin(m_heldVelocity), std::end(m_heldVelocity), uint16_t(0));
        std::fill(std::begin(m_noteOrder), std::end(m_noteOrder), 0);
        m_orderCounter = 0;
        m_patternLen = 0;
        m_stepIndex = 0;
        m_lastStepBeat = -1000.0;
        m_activeNote = -1;
        m_gateOffBeat = -1000.0;
        m_heldChannel = 0;
        m_freeRunSamples = -1;
        m_wasPlaying = false;
    }

    void process(MidiBuffer& buffer, int numFrames,
                 const TransportInfo& transport) override {
        MidiBuffer output;

        // Compute beat position and buffer duration in beats
        double spb = transport.samplesPerBeat;
        if (spb <= 0) spb = transport.sampleRate * 60.0 / transport.bpm;
        double beatsPerBuf = (double)numFrames / spb;
        double beatPos;
        if (transport.playing) {
            beatPos = transport.positionInBeats;
            if (!m_wasPlaying) {
                // Transport just started — re-anchor arp timing to new position
                m_lastStepBeat = std::floor(beatPos / m_rate) * m_rate - m_rate;
                m_gateOffBeat = -1000.0;
            }
            m_freeRunSamples = -1;  // reset free-run when transport plays
        } else {
            // Free-running clock based on sample count
            if (m_freeRunSamples < 0) m_freeRunSamples = 0;
            if (m_wasPlaying) {
                // Transport just stopped — re-anchor to free-run start
                m_freeRunSamples = 0;
                m_lastStepBeat = -m_rate;
                m_gateOffBeat = -1000.0;
            }
            beatPos = (double)m_freeRunSamples / spb;
            m_freeRunSamples += numFrames;
        }
        m_wasPlaying = transport.playing;

        // Phase 1: Consume input notes, pass through non-note messages
        for (int i = 0; i < buffer.count(); ++i) {
            const auto& msg = buffer[i];

            if (msg.isNoteOn()) {
                m_heldVelocity[msg.note] = msg.velocity;
                m_heldChannel = msg.channel;
                m_noteOrder[msg.note] = ++m_orderCounter;
                rebuildPattern();
                if (countHeldNotes() == 1) {
                    // Reset timing so first arp step fires immediately
                    if (!transport.playing) {
                        m_freeRunSamples = 0;
                        beatPos = 0;
                        beatsPerBuf = (double)numFrames / spb;
                    }
                    m_lastStepBeat =
                        std::floor(beatPos / m_rate) * m_rate - m_rate;
                    m_stepIndex = 0;
                }
            } else if (msg.isNoteOff()) {
                m_heldVelocity[msg.note] = 0;
                m_noteOrder[msg.note] = 0;
                rebuildPattern();
                if (m_patternLen == 0 && m_activeNote >= 0) {
                    output.addMessage(MidiMessage::noteOff16(
                        m_heldChannel, (uint8_t)m_activeNote, 0, msg.frameOffset));
                    m_activeNote = -1;
                }
            } else {
                output.addMessage(msg);
            }
        }

        // Phase 2: Generate arp steps
        if (m_patternLen > 0) {
            double bufStart = beatPos;
            double bufEnd   = bufStart + beatsPerBuf;

            // Gate-off for previous note (only if no new step supersedes)
            if (m_activeNote >= 0 &&
                m_gateOffBeat >= bufStart && m_gateOffBeat < bufEnd) {
                double nextStep = m_lastStepBeat + m_rate;
                if (m_gateOffBeat < nextStep || nextStep >= bufEnd) {
                    int f = beatToFrame(m_gateOffBeat, bufStart, spb, numFrames);
                    output.addMessage(MidiMessage::noteOff16(
                        m_heldChannel, (uint8_t)m_activeNote, 0, f));
                    m_activeNote = -1;
                }
            }

            // Fire new steps
            double nextStep = m_lastStepBeat + m_rate;
            while (nextStep < bufEnd) {
                if (nextStep >= bufStart) {
                    int f = beatToFrame(nextStep, bufStart, spb, numFrames);
                    if (m_activeNote >= 0)
                        output.addMessage(MidiMessage::noteOff16(
                            m_heldChannel, (uint8_t)m_activeNote, 0, f));

                    int idx = m_stepIndex % m_patternLen;
                    if (m_direction == Random)
                        idx = (int)(nextRandom() % (uint32_t)m_patternLen);

                    auto& step = m_pattern[idx];
                    output.addMessage(MidiMessage::noteOn16(
                        m_heldChannel, step.pitch, step.velocity, f));
                    m_activeNote = step.pitch;
                    m_gateOffBeat = nextStep + m_rate * m_gate;
                    m_stepIndex = (idx + 1) % m_patternLen;
                    m_lastStepBeat = nextStep;
                }
                nextStep = m_lastStepBeat + m_rate;
            }

            // Gate-off for newly triggered notes
            if (m_activeNote >= 0 &&
                m_gateOffBeat >= bufStart && m_gateOffBeat < bufEnd) {
                int f = beatToFrame(m_gateOffBeat, bufStart, spb, numFrames);
                output.addMessage(MidiMessage::noteOff16(
                    m_heldChannel, (uint8_t)m_activeNote, 0, f));
                m_activeNote = -1;
            }
        }

        buffer = output;
        buffer.sortByFrame();
    }

    const char* name() const override { return "Arpeggiator"; }
    const char* id()   const override { return "arp"; }

    int parameterCount() const override { return kNumParams; }

    const MidiEffectParameterInfo& parameterInfo(int index) const override {
        static const MidiEffectParameterInfo p[kNumParams] = {
            {"Direction", 0.0f, 5.0f, 0.0f, "", false},
            {"Rate",      0.0625f, 2.0f, 0.25f, "beats", false},
            {"Gate",      0.1f, 1.0f, 0.8f, "", false},
            {"Octaves",   1.0f, 4.0f, 1.0f, "", false},
            {"Swing",     0.0f, 1.0f, 0.0f, "", false},
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

    int countHeldNotes() const {
        int n = 0;
        for (int i = 0; i < 128; ++i)
            if (m_heldVelocity[i] > 0) ++n;
        return n;
    }

    int beatToFrame(double beat, double bufStart,
                    double samplesPerBeat, int maxFrames) {
        int f = (int)((beat - bufStart) * samplesPerBeat);
        return std::clamp(f, 0, maxFrames - 1);
    }

    void rebuildPattern() {
        struct Held { uint8_t pitch; uint16_t vel; int order; };
        Held held[128];
        int nh = 0;
        for (int i = 0; i < 128; ++i)
            if (m_heldVelocity[i] > 0)
                held[nh++] = {(uint8_t)i, m_heldVelocity[i], m_noteOrder[i]};

        if (nh == 0) { m_patternLen = 0; return; }

        switch (m_direction) {
            case Up: case UpDown:
                std::sort(held, held + nh, [](auto& a, auto& b){ return a.pitch < b.pitch; });
                break;
            case Down: case DownUp:
                std::sort(held, held + nh, [](auto& a, auto& b){ return a.pitch > b.pitch; });
                break;
            case AsPlayed:
                std::sort(held, held + nh, [](auto& a, auto& b){ return a.order < b.order; });
                break;
            case Random:
                break;
        }

        m_patternLen = 0;

        auto addOctaves = [&](bool ascending) {
            if (ascending) {
                for (int oct = 0; oct < m_octaves; ++oct)
                    for (int i = 0; i < nh; ++i) {
                        int p = held[i].pitch + oct * 12;
                        if (p > 127) break;
                        if (m_patternLen < kMaxPattern)
                            m_pattern[m_patternLen++] = {(uint8_t)p, held[i].vel};
                    }
            } else {
                for (int oct = m_octaves - 1; oct >= 0; --oct)
                    for (int i = 0; i < nh; ++i) {
                        int p = held[i].pitch + oct * 12;
                        if (p > 127) continue;
                        if (m_patternLen < kMaxPattern)
                            m_pattern[m_patternLen++] = {(uint8_t)p, held[i].vel};
                    }
            }
        };

        switch (m_direction) {
            case Up: case AsPlayed: case Random:
                addOctaves(true); break;
            case Down:
                addOctaves(false); break;
            case UpDown: {
                addOctaves(true);
                int upLen = m_patternLen;
                if (upLen > 2)
                    for (int i = upLen - 2; i >= 1; --i)
                        if (m_patternLen < kMaxPattern)
                            m_pattern[m_patternLen++] = m_pattern[i];
                break;
            }
            case DownUp: {
                addOctaves(false);
                int downLen = m_patternLen;
                if (downLen > 2)
                    for (int i = downLen - 2; i >= 1; --i)
                        if (m_patternLen < kMaxPattern)
                            m_pattern[m_patternLen++] = m_pattern[i];
                break;
            }
        }
        if (m_stepIndex >= m_patternLen) m_stepIndex = 0;
    }

    uint32_t nextRandom() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return m_rng;
    }

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
