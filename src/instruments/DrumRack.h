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

    void setPadVolume(int note, float v)     { if (note >= 0 && note < kNumPads) m_pads[note].volume = std::clamp(v, 0.0f, 2.0f); }
    void setPadPan(int note, float p)        { if (note >= 0 && note < kNumPads) m_pads[note].pan = std::clamp(p, -1.0f, 1.0f); }
    void setPadPitch(int note, float semi)   { if (note >= 0 && note < kNumPads) m_pads[note].pitchAdjust = std::clamp(semi, -24.0f, 24.0f); }

    Pad&       pad(int note)       { return m_pads[note & 0x7F]; }
    const Pad& pad(int note) const { return m_pads[note & 0x7F]; }

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

    int parameterCount() const override { return 1; }

    const InstrumentParameterInfo& parameterInfo(int) const override {
        static const InstrumentParameterInfo p = {"Volume", 0, 1, 0.8f, "", false};
        return p;
    }

    float getParameter(int) const override { return m_volume; }
    void  setParameter(int, float v) override { m_volume = std::clamp(v, 0.0f, 1.0f); }

private:
    Pad   m_pads[kNumPads];
    float m_volume = 0.8f;
};

} // namespace instruments
} // namespace yawn
