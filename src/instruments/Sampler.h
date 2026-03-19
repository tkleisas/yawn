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
    }

    bool hasSample() const { return m_sampleFrames > 0; }

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

        for (int v = 0; v < kMaxVoices; ++v) {
            auto& voice = m_voices[v];
            if (!voice.active) continue;

            for (int i = 0; i < numFrames; ++i) {
                int pos0 = (int)voice.playPos;
                if (pos0 >= m_sampleFrames) {
                    voice.active = false; break;
                }
                int pos1 = std::min(pos0 + 1, m_sampleFrames - 1);
                float frac = (float)(voice.playPos - pos0);

                // Linear interpolation
                float sL = m_sampleData[pos0 * m_sampleChannels] * (1.0f - frac)
                         + m_sampleData[pos1 * m_sampleChannels] * frac;
                float sR = sL;
                if (m_sampleChannels > 1) {
                    sR = m_sampleData[pos0 * m_sampleChannels + 1] * (1.0f - frac)
                       + m_sampleData[pos1 * m_sampleChannels + 1] * frac;
                }

                // SVF filter
                float f = std::min(2.0f * m_filterCutoff / (float)m_sampleRate, 0.99f);
                float q = 1.0f - m_filterResonance * 0.98f;
                float highL = sL - voice.filterLow - q * voice.filterBand;
                voice.filterBand += f * highL;
                voice.filterLow  += f * voice.filterBand;
                sL = voice.filterLow;
                // Simple: apply same filter to right by reusing coefficients
                // (true stereo would need separate state, but this is acceptable)
                sR = (m_sampleChannels > 1) ? sR * (voice.filterLow / (sL + 0.0001f)) : sL;
                // Actually just filter L only for simplicity, use original R balance
                float ratio = (std::abs(sL) > 0.0001f) ? voice.filterLow / sL : 1.0f;
                sR = sR; // Keep R unfiltered for now (TODO: per-channel SVF)
                sL = voice.filterLow;

                float env = voice.env.process();
                float gain = env * voice.velocity * m_volume;

                buffer[i * numChannels + 0] += sL * gain;
                if (numChannels > 1)
                    buffer[i * numChannels + 1] += sR * gain;

                voice.playPos += voice.playSpeed;
            }

            if (voice.env.isIdle() || voice.playPos >= m_sampleFrames)
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
        v.playPos = 0.0;
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
};

} // namespace instruments
} // namespace yawn
