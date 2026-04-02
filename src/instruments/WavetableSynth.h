#pragma once

// WavetableSynth — wavetable oscillator synthesizer.
//
// Reads through a set of single-cycle waveforms (frames) stored in a wavetable.
// The Position knob selects which frame(s) to read, with linear crossfade
// between adjacent frames for smooth morphing.
//
// Built-in algorithmic wavetables:
//   0 Basic     — Sine → Tri → Saw → Square → Pulse (5 frames)
//   1 PWM       — Pulse width modulation sweep (16 frames)
//   2 Formant   — Vocal-like formant sweep (16 frames)
//   3 Harmonic  — Additive harmonics buildup (16 frames)
//   4 Digital   — Bitcrushed/digital textures (8 frames)
//
// Features:
// - Wavetable position knob with LFO modulation
// - SVF filter with ADSR envelope
// - Amp ADSR envelope
// - 16-voice polyphony with voice stealing
// - Sub oscillator (one octave down, sine)
// - Unison detune (2 detuned copies per voice)
//
// All waveform data is generated in init().  No allocations in process().

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

class WavetableSynth : public Instrument {
public:
    static constexpr int kMaxVoices   = 16;
    static constexpr int kFrameSize   = 256;     // samples per waveform frame
    static constexpr int kMaxFrames   = 16;      // max frames per wavetable
    static constexpr int kNumTables   = 5;       // number of built-in wavetables

    enum Param {
        kTable,           // 0–4: wavetable selection
        kPosition,        // 0–1: position within wavetable (frame morph)
        kFilterCutoff,    // 20–20000 Hz
        kFilterResonance, // 0–1
        kFilterEnvAmount, // -1 to +1
        kAmpAttack,       // 0.001–5s
        kAmpDecay,        // 0.001–5s
        kAmpSustain,      // 0–1
        kAmpRelease,      // 0.001–5s
        kFiltAttack,      // 0.001–5s
        kFiltDecay,       // 0.001–5s
        kFiltSustain,     // 0–1
        kFiltRelease,     // 0.001–5s
        kLFORate,         // 0.1–20 Hz
        kLFOAmount,       // 0–1: LFO → position modulation
        kSubLevel,        // 0–1
        kUnisonDetune,    // 0–1: detune spread in semitones
        kVolume,          // 0–1
        kParamCount
    };

    const char* name() const override { return "Wavetable Synth"; }
    const char* id()   const override { return "wavetable"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        generateWavetables();
        for (auto& v : m_voices) {
            v.ampEnv.setSampleRate(sampleRate);
            v.filtEnv.setSampleRate(sampleRate);
        }
        applyDefaults();
    }

    void reset() override {
        for (auto& v : m_voices) {
            v.active = false;
            v.phase = v.phase2 = v.subPhase = 0.0;
            v.filterLow = v.filterBand = 0.0f;
            v.ampEnv.reset();
            v.filtEnv.reset();
        }
        m_lfoPhase = 0.0;
        m_voiceCounter = 0;
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        // Handle MIDI
        for (int i = 0; i < midi.count(); ++i) {
            const auto& msg = midi[i];
            if (msg.isNoteOn())
                noteOn(msg.note, msg.velocity, msg.channel);
            else if (msg.isNoteOff())
                noteOff(msg.note, msg.channel);
            else if (msg.isCC() && msg.ccNumber == 123) {
                for (auto& v : m_voices)
                    if (v.active) { v.ampEnv.gate(false); v.filtEnv.gate(false); }
            }
            else if (msg.type == midi::MidiMessage::Type::PitchBend)
                m_pitchBend = midi::Convert::pb32toFloat(msg.value);
        }

        int tableIdx = std::clamp(static_cast<int>(m_params[kTable]), 0, kNumTables - 1);
        float position = m_params[kPosition];
        float cutoff = m_params[kFilterCutoff];
        float reso = m_params[kFilterResonance];
        float filtEnvAmt = m_params[kFilterEnvAmount];
        float subLevel = m_params[kSubLevel];
        float detuneST = m_params[kUnisonDetune];
        float volume = m_params[kVolume];
        float lfoAmount = m_params[kLFOAmount];

        double lfoInc = m_params[kLFORate] / m_sampleRate;
        int numFramesInTable = m_tableFrameCounts[tableIdx];

        for (int vi = 0; vi < kMaxVoices; ++vi) {
            auto& v = m_voices[vi];
            if (!v.active) continue;

            float baseFreq = noteToFreq(v.note) *
                std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);
            double phaseInc = baseFreq / m_sampleRate;

            // Unison: second oscillator detuned
            float detuneRatio = std::pow(2.0f, detuneST / 12.0f);
            double phaseInc2 = phaseInc * detuneRatio;
            double subInc = phaseInc * 0.5;

            for (int i = 0; i < numFrames; ++i) {
                // LFO for position modulation
                float lfo = static_cast<float>(std::sin(m_lfoPhase * 2.0 * M_PI));
                float modPos = position + lfo * lfoAmount * 0.5f;
                modPos = std::clamp(modPos, 0.0f, 1.0f);

                // Read wavetable with frame morphing
                float osc1 = readWavetable(tableIdx, numFramesInTable, v.phase, modPos);
                float osc2 = (detuneST > 0.001f)
                    ? readWavetable(tableIdx, numFramesInTable, v.phase2, modPos)
                    : 0.0f;

                // Sub oscillator (sine, one octave down)
                float sub = static_cast<float>(std::sin(v.subPhase * 2.0 * M_PI))
                          * subLevel;

                float mix = osc1 * 0.5f + osc2 * 0.25f + sub;

                // SVF filter with envelope modulation
                float filtEnvVal = v.filtEnv.process();
                float modCutoff = cutoff *
                    std::pow(2.0f, filtEnvAmt * filtEnvVal * 4.0f);
                modCutoff = std::clamp(modCutoff, 20.0f, 20000.0f);

                float w = static_cast<float>(M_PI * modCutoff / m_sampleRate);
                float f = 2.0f * std::sin(std::min(w, 1.5f));
                float q = 1.0f - reso * 0.98f;

                for (int os = 0; os < 2; ++os) {
                    float high = mix - v.filterLow - q * v.filterBand;
                    v.filterBand += 0.5f * f * high;
                    v.filterLow  += 0.5f * f * v.filterBand;
                }
                v.filterBand = std::clamp(v.filterBand, -10.0f, 10.0f);
                v.filterLow  = std::clamp(v.filterLow,  -10.0f, 10.0f);

                float filtered = v.filterLow;

                // Amp envelope
                float ampEnvVal = v.ampEnv.process();
                float sample = filtered * ampEnvVal * v.velocity * volume;

                buffer[i * numChannels + 0] += sample;
                if (numChannels > 1)
                    buffer[i * numChannels + 1] += sample;

                // Advance phases
                v.phase += phaseInc;
                while (v.phase >= 1.0) v.phase -= 1.0;
                v.phase2 += phaseInc2;
                while (v.phase2 >= 1.0) v.phase2 -= 1.0;
                v.subPhase += subInc;
                while (v.subPhase >= 1.0) v.subPhase -= 1.0;

                m_lfoPhase += lfoInc;
                while (m_lfoPhase >= 1.0) m_lfoPhase -= 1.0;
            }

            if (v.ampEnv.isIdle())
                v.active = false;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo infos[] = {
            {"Table",       0.0f,  4.0f,   0.0f,  "",   false},
            {"Position",    0.0f,  1.0f,   0.0f,  "",   false},
            {"Filter Cut",  20.0f, 20000.0f, 5000.0f, "Hz", false},
            {"Filter Res",  0.0f,  1.0f,   0.0f,  "",   false},
            {"Filter Env", -1.0f,  1.0f,   0.3f,  "",   false},
            {"Amp Atk",     0.001f, 5.0f,  0.01f, "s",  false},
            {"Amp Dec",     0.001f, 5.0f,  0.1f,  "s",  false},
            {"Amp Sus",     0.0f,  1.0f,   0.7f,  "",   false},
            {"Amp Rel",     0.001f, 5.0f,  0.3f,  "s",  false},
            {"Filt Atk",    0.001f, 5.0f,  0.01f, "s",  false},
            {"Filt Dec",    0.001f, 5.0f,  0.3f,  "s",  false},
            {"Filt Sus",    0.0f,  1.0f,   0.3f,  "",   false},
            {"Filt Rel",    0.001f, 5.0f,  0.3f,  "s",  false},
            {"LFO Rate",    0.1f,  20.0f,  2.0f,  "Hz", false},
            {"LFO Amt",     0.0f,  1.0f,   0.0f,  "",   false},
            {"Sub Level",   0.0f,  1.0f,   0.0f,  "",   false},
            {"Unison Det",  0.0f,  1.0f,   0.0f,  "st", false},
            {"Volume",      0.0f,  1.0f,   0.7f,  "",   false},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index >= 0 && index < kParamCount) return m_params[index];
        return 0.0f;
    }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = value;
            if (index >= kAmpAttack && index <= kFiltRelease)
                updateEnvelopes();
        }
    }

private:
    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        double phase = 0.0, phase2 = 0.0, subPhase = 0.0;
        float filterLow = 0.0f, filterBand = 0.0f;
        Envelope ampEnv, filtEnv;
    };

    // Wavetable storage (heap-allocated to avoid stack overflow)
    struct TableData {
        float tables[kNumTables][kMaxFrames][kFrameSize] = {};
    };
    std::unique_ptr<TableData> m_tableData;
    int   m_tableFrameCounts[kNumTables] = {};

    void generateWavetables() {
        m_tableData = std::make_unique<TableData>();
        auto& T = m_tableData->tables;
        // Table 0: Basic — Sine → Triangle → Saw → Square → Pulse
        m_tableFrameCounts[0] = 5;
        for (int s = 0; s < kFrameSize; ++s) {
            float t = static_cast<float>(s) / kFrameSize;
            T[0][0][s] = std::sin(2.0f * static_cast<float>(M_PI) * t);
            T[0][1][s] = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
            T[0][2][s] = 2.0f * t - 1.0f;
            T[0][3][s] = (t < 0.5f) ? 1.0f : -1.0f;
            T[0][4][s] = (t < 0.1f) ? 1.0f : -1.0f;
        }
        // Table 1: PWM — pulse width sweep
        m_tableFrameCounts[1] = 16;
        for (int f = 0; f < 16; ++f) {
            float pw = 0.05f + 0.9f * static_cast<float>(f) / 15.0f;
            for (int s = 0; s < kFrameSize; ++s) {
                float t = static_cast<float>(s) / kFrameSize;
                T[1][f][s] = (t < pw) ? 1.0f : -1.0f;
            }
        }
        // Table 2: Formant — vocal formant sweep
        m_tableFrameCounts[2] = 16;
        for (int f = 0; f < 16; ++f) {
            float formantFreq = 1.0f + static_cast<float>(f) * 2.0f;
            for (int s = 0; s < kFrameSize; ++s) {
                float t = static_cast<float>(s) / kFrameSize;
                float carrier = std::sin(2.0f * static_cast<float>(M_PI) * t);
                float formant = std::sin(2.0f * static_cast<float>(M_PI) * t * formantFreq);
                float window = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                T[2][f][s] = carrier * 0.5f + formant * window * 0.5f;
            }
        }
        // Table 3: Harmonic — additive harmonics buildup
        m_tableFrameCounts[3] = 16;
        for (int f = 0; f < 16; ++f) {
            int numHarmonics = f + 1;
            for (int s = 0; s < kFrameSize; ++s) {
                float t = static_cast<float>(s) / kFrameSize;
                float val = 0.0f;
                for (int h = 1; h <= numHarmonics; ++h)
                    val += std::sin(2.0f * static_cast<float>(M_PI) * t * h) / h;
                T[3][f][s] = val * (2.0f / static_cast<float>(M_PI));
            }
        }
        // Table 4: Digital — bitcrushed textures
        m_tableFrameCounts[4] = 8;
        for (int f = 0; f < 8; ++f) {
            int bits = 8 - f;
            float levels = std::pow(2.0f, static_cast<float>(bits));
            for (int s = 0; s < kFrameSize; ++s) {
                float t = static_cast<float>(s) / kFrameSize;
                float raw = std::sin(2.0f * static_cast<float>(M_PI) * t);
                T[4][f][s] = std::round(raw * levels) / levels;
            }
        }
    }

    // Read wavetable with frame morphing and phase interpolation
    float readWavetable(int table, int numFrames, double phase, float position) const {
        float framePos = position * (numFrames - 1);
        int frame0 = static_cast<int>(framePos);
        int frame1 = std::min(frame0 + 1, numFrames - 1);
        float frameFrac = framePos - frame0;

        float samplePos = static_cast<float>(phase * kFrameSize);
        int idx0 = static_cast<int>(samplePos) % kFrameSize;
        int idx1 = (idx0 + 1) % kFrameSize;
        float sampleFrac = samplePos - std::floor(samplePos);

        auto& T = m_tableData->tables;
        float val0 = T[table][frame0][idx0] * (1.0f - sampleFrac)
                   + T[table][frame0][idx1] * sampleFrac;
        float val1 = T[table][frame1][idx0] * (1.0f - sampleFrac)
                   + T[table][frame1][idx1] * sampleFrac;

        return val0 * (1.0f - frameFrac) + val1 * frameFrac;
    }

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
        int slot = findFreeVoice();
        auto& v = m_voices[slot];
        v.active = true;
        v.note = note;
        v.channel = ch;
        v.velocity = velocityToGain(vel16);
        v.startOrder = m_voiceCounter++;
        v.phase = v.phase2 = v.subPhase = 0.0;
        v.filterLow = v.filterBand = 0.0f;

        v.ampEnv.setADSR(m_params[kAmpAttack], m_params[kAmpDecay],
                         m_params[kAmpSustain], m_params[kAmpRelease]);
        v.filtEnv.setADSR(m_params[kFiltAttack], m_params[kFiltDecay],
                          m_params[kFiltSustain], m_params[kFiltRelease]);
        v.ampEnv.gate(true);
        v.filtEnv.gate(true);
    }

    void noteOff(uint8_t note, uint8_t ch) {
        for (auto& v : m_voices)
            if (v.active && v.note == note && v.channel == ch) {
                v.ampEnv.gate(false);
                v.filtEnv.gate(false);
            }
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

    void updateEnvelopes() {
        for (auto& v : m_voices) {
            v.ampEnv.setADSR(m_params[kAmpAttack], m_params[kAmpDecay],
                             m_params[kAmpSustain], m_params[kAmpRelease]);
            v.filtEnv.setADSR(m_params[kFiltAttack], m_params[kFiltDecay],
                              m_params[kFiltSustain], m_params[kFiltRelease]);
        }
    }

    void applyDefaults() {
        for (int i = 0; i < kParamCount; ++i)
            m_params[i] = parameterInfo(i).defaultValue;
    }

    Voice m_voices[kMaxVoices];
    int64_t m_voiceCounter = 0;
    float m_pitchBend = 0.0f;
    double m_lfoPhase = 0.0;

    float m_params[kParamCount] = {};
};

} // namespace instruments
} // namespace yawn
