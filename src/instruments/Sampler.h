#pragma once

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace instruments {

// Simple sample playback instrument with pitch tracking, ADSR, and SVF filter.
// 16-voice polyphony. Maps a single sample across the keyboard.
class Sampler : public Instrument {
public:
    static constexpr int kMaxVoices = 16;

    enum Params {
        kRootNote = 0, kAttack, kDecay, kSustain, kRelease,
        kFilterCutoff, kFilterResonance, kVolume,
        kLoopStart, kLoopEnd, kReverse, kSampleGain,
        kNumParams
    };

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        for (auto& v : m_voices) v.env.setSampleRate(sampleRate);
        applyDefaults();
    }

    void reset() override {
        for (auto& v : m_voices) {
            v.active = false; v.env.reset();
            v.filterLow = v.filterBand = 0.0f;
        }
        m_voiceCounter = 0;
    }

    // Load sample data (copies into internal buffer).
    void loadSample(const float* data, int numFrames, int numChannels,
                    int rootNote = 60) {
        m_sampleData.assign(data, data + numFrames * numChannels);
        m_sampleFrames = numFrames;
        m_sampleChannels = numChannels;
        m_rootNote = rootNote;
        m_loopStart = 0.0f;
        m_loopEnd = 1.0f;
    }

    void clearSample() {
        m_sampleData.clear();
        m_sampleFrames = 0;
        m_sampleChannels = 1;
    }

    bool hasSample() const { return m_sampleFrames > 0; }
    const float* sampleData() const { return m_sampleData.data(); }
    int sampleFrames() const { return m_sampleFrames; }
    int sampleChannels() const { return m_sampleChannels; }

    // Normalized playhead position (0..1) of the most recently triggered voice
    float playheadPosition() const {
        int best = -1;
        int64_t bestOrder = -1;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (m_voices[i].active && m_voices[i].startOrder > bestOrder) {
                best = i; bestOrder = m_voices[i].startOrder;
            }
        }
        if (best < 0 || m_sampleFrames <= 0) return 0.0f;
        return std::clamp(static_cast<float>(m_voices[best].playPos / m_sampleFrames), 0.0f, 1.0f);
    }

    bool isPlaying() const {
        for (int i = 0; i < kMaxVoices; ++i)
            if (m_voices[i].active) return true;
        return false;
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        if (m_sampleFrames == 0) return;

        for (int i = 0; i < midi.count(); ++i) {
            const auto& msg = midi[i];
            if (msg.isNoteOn()) noteOn(msg.note, msg.velocity, msg.channel);
            else if (msg.isNoteOff()) noteOff(msg.note, msg.channel);
            else if (msg.isCC() && msg.ccNumber == 123) {
                for (auto& v : m_voices)
                    if (v.active) { v.env.gate(false); }
            }
        }

        // Compute loop boundaries in frames
        int loopStartFrame = static_cast<int>(m_loopStart * m_sampleFrames);
        int loopEndFrame   = static_cast<int>(m_loopEnd * m_sampleFrames);
        if (loopEndFrame <= loopStartFrame) loopEndFrame = m_sampleFrames;
        bool looping = (m_loopStart > 0.001f || m_loopEnd < 0.999f);

        for (int v = 0; v < kMaxVoices; ++v) {
            auto& voice = m_voices[v];
            if (!voice.active) continue;

            for (int i = 0; i < numFrames; ++i) {
                int pos0 = (int)voice.playPos;

                // Handle loop/end boundaries
                if (looping) {
                    if (!m_reverse && pos0 >= loopEndFrame) {
                        voice.playPos = loopStartFrame;
                        pos0 = loopStartFrame;
                    } else if (m_reverse && pos0 < loopStartFrame) {
                        voice.playPos = loopEndFrame - 1;
                        pos0 = loopEndFrame - 1;
                    }
                } else {
                    if (!m_reverse && pos0 >= m_sampleFrames) {
                        voice.active = false; break;
                    } else if (m_reverse && pos0 < 0) {
                        voice.active = false; break;
                    }
                }

                pos0 = std::clamp(pos0, 0, m_sampleFrames - 1);
                int pos1 = std::clamp(pos0 + (m_reverse ? -1 : 1), 0, m_sampleFrames - 1);
                float frac = (float)(voice.playPos - pos0);
                if (frac < 0) frac = -frac;

                // Linear interpolation
                float sL = m_sampleData[pos0 * m_sampleChannels] * (1.0f - frac)
                         + m_sampleData[pos1 * m_sampleChannels] * frac;
                float sR = sL;
                if (m_sampleChannels > 1) {
                    sR = m_sampleData[pos0 * m_sampleChannels + 1] * (1.0f - frac)
                       + m_sampleData[pos1 * m_sampleChannels + 1] * frac;
                }

                // Apply sample gain
                sL *= m_sampleGain;
                sR *= m_sampleGain;

                // SVF filter
                float f = std::min(2.0f * m_filterCutoff / (float)m_sampleRate, 0.99f);
                float q = 1.0f - m_filterResonance * 0.98f;
                float highL = sL - voice.filterLow - q * voice.filterBand;
                voice.filterBand += f * highL;
                voice.filterLow  += f * voice.filterBand;
                float filteredL = voice.filterLow;
                if (m_sampleChannels > 1) {
                    float filterRatio = (std::abs(sL) > 0.0001f) ? filteredL / sL : 1.0f;
                    sR *= filterRatio;
                } else {
                    sR = filteredL;
                }
                sL = filteredL;

                float env = voice.env.process();
                float gain = env * voice.velocity * m_volume;

                buffer[i * numChannels + 0] += sL * gain;
                if (numChannels > 1)
                    buffer[i * numChannels + 1] += sR * gain;

                voice.playPos += m_reverse ? -voice.playSpeed : voice.playSpeed;
            }

            if (voice.env.isIdle())
                voice.active = false;
        }
    }

    const char* name() const override { return "Sampler"; }
    const char* id()   const override { return "sampler"; }

    int parameterCount() const override { return kNumParams; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo p[kNumParams] = {
            {"Root Note",   0, 127, 60, "", false},
            {"Attack",      0.001f, 5, 0.005f, "s", false},
            {"Decay",       0.001f, 5, 0.1f, "s", false},
            {"Sustain",     0, 1, 1, "", false},
            {"Release",     0.001f, 5, 0.1f, "s", false},
            {"Filter Cut",  20, 20000, 20000, "Hz", false},
            {"Filter Reso", 0, 1, 0, "", false},
            {"Volume",      0, 1, 0.8f, "", false},
            {"Loop Start",  0, 1, 0, "", false},
            {"Loop End",    0, 1, 1, "", false},
            {"Reverse",     0, 1, 0, "", true},
            {"Sample Gain", 0, 2, 1, "x", false},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kRootNote:        return (float)m_rootNote;
            case kAttack:          return m_attack;
            case kDecay:           return m_decay;
            case kSustain:         return m_sustain;
            case kRelease:         return m_release;
            case kFilterCutoff:    return m_filterCutoff;
            case kFilterResonance: return m_filterResonance;
            case kVolume:          return m_volume;
            case kLoopStart:       return m_loopStart;
            case kLoopEnd:         return m_loopEnd;
            case kReverse:         return m_reverse ? 1.0f : 0.0f;
            case kSampleGain:      return m_sampleGain;
            default:               return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kRootNote:        m_rootNote = std::clamp((int)value, 0, 127); break;
            case kAttack:          m_attack = value; break;
            case kDecay:           m_decay = value; break;
            case kSustain:         m_sustain = std::clamp(value, 0.0f, 1.0f); break;
            case kRelease:         m_release = value; break;
            case kFilterCutoff:    m_filterCutoff = std::clamp(value, 20.0f, 20000.0f); break;
            case kFilterResonance: m_filterResonance = std::clamp(value, 0.0f, 1.0f); break;
            case kVolume:          m_volume = std::clamp(value, 0.0f, 1.0f); break;
            case kLoopStart:       m_loopStart = std::clamp(value, 0.0f, m_loopEnd - 0.001f); break;
            case kLoopEnd:         m_loopEnd = std::clamp(value, m_loopStart + 0.001f, 1.0f); break;
            case kReverse:         m_reverse = value > 0.5f; break;
            case kSampleGain:      m_sampleGain = std::clamp(value, 0.0f, 2.0f); break;
        }
    }

private:
    struct Voice {
        bool active = false;
        uint8_t note = 0, channel = 0;
        float velocity = 0.0f;
        double playPos = 0.0, playSpeed = 1.0;
        int64_t startOrder = 0;
        Envelope env;
        float filterLow = 0.0f, filterBand = 0.0f;
    };

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
        int slot = findFreeVoice();
        auto& v = m_voices[slot];
        v.active = true;
        v.note = note; v.channel = ch;
        v.velocity = velocityToGain(vel16);
        // Start from loop end if reversed, loop start otherwise
        if (m_reverse)
            v.playPos = static_cast<double>(m_loopEnd * m_sampleFrames) - 1.0;
        else
            v.playPos = static_cast<double>(m_loopStart * m_sampleFrames);
        v.playSpeed = std::pow(2.0, ((int)note - m_rootNote) / 12.0);
        v.startOrder = m_voiceCounter++;
        v.filterLow = v.filterBand = 0.0f;
        v.env.setADSR(m_attack, m_decay, m_sustain, m_release);
        v.env.gate(true);
    }

    void noteOff(uint8_t note, uint8_t ch) {
        for (auto& v : m_voices)
            if (v.active && v.note == note && v.channel == ch)
                v.env.gate(false);
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

    std::vector<float> m_sampleData;
    int m_sampleFrames    = 0;
    int m_sampleChannels  = 1;
    int m_rootNote        = 60;

    float m_attack = 0.005f, m_decay = 0.1f, m_sustain = 1.0f, m_release = 0.1f;
    float m_filterCutoff = 20000.0f, m_filterResonance = 0.0f;
    float m_volume = 0.8f;
    float m_loopStart = 0.0f, m_loopEnd = 1.0f;
    bool  m_reverse = false;
    float m_sampleGain = 1.0f;
};

} // namespace instruments
} // namespace yawn
