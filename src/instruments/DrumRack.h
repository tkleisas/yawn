#pragma once

#include "instruments/Instrument.h"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

// Drum Rack — pad-based percussion instrument (like Ableton Drum Rack).
// 128 pads (one per MIDI note 0-127), each with a sample, volume, pan, pitch.
// Monophonic per pad (retrigger on repeated hits), all pads play simultaneously.
class DrumRack : public Instrument {
public:
    static constexpr int kNumPads = 128;

    struct Pad {
        std::vector<float> sampleData;
        int   sampleFrames    = 0;
        int   sampleChannels  = 1;
        float volume          = 1.0f;
        float pan             = 0.0f;
        float pitchAdjust     = 0.0f; // semitones

        // Playback state
        bool   playing   = false;
        double playPos   = 0.0;
        double playSpeed = 1.0;
        float  velocity  = 1.0f;
    };

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
    }

    void reset() override {
        for (auto& p : m_pads)
            p.playing = false;
    }

    // Load a sample into a pad. Note = MIDI note number (0-127).
    void loadPad(int note, const float* data, int numFrames, int numChannels) {
        if (note < 0 || note >= kNumPads || !data || numFrames <= 0) return;
        auto& pad = m_pads[note];
        pad.sampleData.assign(data, data + numFrames * numChannels);
        pad.sampleFrames = numFrames;
        pad.sampleChannels = numChannels;
    }

    void clearPad(int note) {
        if (note < 0 || note >= kNumPads) return;
        m_pads[note].sampleData.clear();
        m_pads[note].sampleFrames = 0;
        m_pads[note].sampleChannels = 1;
    }

    void setPadVolume(int note, float v)     { if (note >= 0 && note < kNumPads) m_pads[note].volume = std::clamp(v, 0.0f, 2.0f); }
    void setPadPan(int note, float p)        { if (note >= 0 && note < kNumPads) m_pads[note].pan = std::clamp(p, -1.0f, 1.0f); }
    void setPadPitch(int note, float semi)   { if (note >= 0 && note < kNumPads) m_pads[note].pitchAdjust = std::clamp(semi, -24.0f, 24.0f); }

    Pad&       pad(int note)       { return m_pads[note & 0x7F]; }
    const Pad& pad(int note) const { return m_pads[note & 0x7F]; }

    // Selected pad for UI editing
    int  selectedPad() const { return m_selectedPad; }
    void setSelectedPad(int note) { m_selectedPad = std::clamp(note, 0, kNumPads - 1); }

    bool isPadPlaying(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].playing : false;
    }

    bool hasSample(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].sampleFrames > 0 : false;
    }

    // Sample data accessors for waveform display
    const float* padSampleData(int note) const {
        if (note < 0 || note >= kNumPads || m_pads[note].sampleFrames <= 0) return nullptr;
        return m_pads[note].sampleData.data();
    }
    int padSampleFrames(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].sampleFrames : 0;
    }
    int padSampleChannels(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].sampleChannels : 1;
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        // Handle MIDI triggers
        for (int i = 0; i < midi.count(); ++i) {
            const auto& msg = midi[i];
            if (msg.isNoteOn()) {
                auto& p = m_pads[msg.note & 0x7F];
                if (p.sampleFrames > 0) {
                    p.playing = true;
                    p.playPos = 0.0;
                    p.velocity = velocityToGain(msg.velocity);
                    p.playSpeed = std::pow(2.0, p.pitchAdjust / 12.0);
                }
            }
            // NoteOff: optionally stop pad (choke). For drums, we let them ring.
        }

        // Render all playing pads
        for (int n = 0; n < kNumPads; ++n) {
            auto& p = m_pads[n];
            if (!p.playing) continue;

            float angle = (p.pan + 1.0f) * 0.25f * (float)M_PI;
            float gL = p.volume * p.velocity * m_volume * std::cos(angle);
            float gR = p.volume * p.velocity * m_volume * std::sin(angle);

            for (int i = 0; i < numFrames; ++i) {
                int pos = (int)p.playPos;
                if (pos >= p.sampleFrames) { p.playing = false; break; }

                float sL = p.sampleData[pos * p.sampleChannels];
                float sR = (p.sampleChannels > 1)
                    ? p.sampleData[pos * p.sampleChannels + 1] : sL;

                buffer[i * numChannels + 0] += sL * gL;
                if (numChannels > 1)
                    buffer[i * numChannels + 1] += sR * gR;

                p.playPos += p.playSpeed;
            }
        }
    }

    const char* name() const override { return "Drum Rack"; }
    const char* id()   const override { return "drumrack"; }

    // Parameter indices
    static constexpr int kVolume    = 0;
    static constexpr int kPadVolume = 1;
    static constexpr int kPadPan    = 2;
    static constexpr int kPadPitch  = 3;

    int parameterCount() const override { return 4; }

    const InstrumentParameterInfo& parameterInfo(int idx) const override {
        static const InstrumentParameterInfo infos[] = {
            {"Volume",     0,   1,   0.8f, "",   false},
            {"Pad Volume", 0,   2,   1.0f, "",   false},
            {"Pad Pan",   -1,   1,   0.0f, "",   false},
            {"Pad Pitch", -24,  24,  0.0f, "st", false},
        };
        return infos[std::clamp(idx, 0, 3)];
    }

    float getParameter(int idx) const override {
        switch (idx) {
        case kVolume:    return m_volume;
        case kPadVolume: return m_pads[m_selectedPad].volume;
        case kPadPan:    return m_pads[m_selectedPad].pan;
        case kPadPitch:  return m_pads[m_selectedPad].pitchAdjust;
        default:         return 0.0f;
        }
    }

    void setParameter(int idx, float v) override {
        switch (idx) {
        case kVolume:    m_volume = std::clamp(v, 0.0f, 1.0f); break;
        case kPadVolume: m_pads[m_selectedPad].volume = std::clamp(v, 0.0f, 2.0f); break;
        case kPadPan:    m_pads[m_selectedPad].pan = std::clamp(v, -1.0f, 1.0f); break;
        case kPadPitch:  m_pads[m_selectedPad].pitchAdjust = std::clamp(v, -24.0f, 24.0f); break;
        }
    }

private:
    Pad   m_pads[kNumPads];
    float m_volume = 0.8f;
    int   m_selectedPad = 36; // Default to C2 (standard GM kick drum)
};

} // namespace instruments
} // namespace yawn
