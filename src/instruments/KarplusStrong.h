#pragma once

// KarplusStrong — plucked/struck string synthesizer using Karplus-Strong
// physical modelling.
//
// The basic KS algorithm: fill a delay line with an excitation burst, then
// feedback the output through a low-pass filter.  Extensions include:
//
// - Multiple exciter types (noise burst, filtered noise, sawtooth impulse, sine burst)
// - Damping control (low-pass filter in feedback loop)
// - Brightness (adjustable LP cutoff in the loop)
// - Decay control (feedback gain)
// - Body resonance (post-string bandpass filter simulating a body cavity)
// - Stretch factor (allpass in loop for inharmonicity, like a piano string)
// - 16-voice polyphony with voice stealing
//
// All buffers are pre-allocated in init().  No allocations in process().

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

class KarplusStrong : public Instrument {
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kMaxDelay  = 4096;  // max delay line length (~10 Hz at 44.1k)

    enum Param {
        kExciter,      // 0–3: Noise, Bright, Saw, Sine
        kDamping,      // 0–1: high-frequency damping amount
        kBrightness,   // 0–1: initial brightness of excitation
        kDecay,        // 0–1: string sustain (feedback gain)
        kBody,         // 0–1: body resonance amount
        kStretch,      // 0–1: inharmonicity (allpass stretch)
        kAttack,       // 0.001–0.1s: excitation attack time
        kVolume,       // 0–1: output volume
        kParamCount
    };

    enum ExciterType { Noise = 0, Bright = 1, Saw = 2, Sine = 3 };

    const char* name() const override { return "Karplus-Strong"; }
    const char* id()   const override { return "karplus"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        for (auto& v : m_voices) {
            v.ampEnv.setSampleRate(sampleRate);
        }
        applyDefaults();
    }

    void reset() override {
        for (auto& v : m_voices) {
            v.active = false;
            std::memset(v.delayLine, 0, sizeof(v.delayLine));
            v.writePos = 0;
            v.delayLen = 0;
            v.excitePos = 0;
            v.exciteLen = 0;
            v.lpState = 0.0f;
            v.apState = 0.0f;
            v.ampEnv.reset();
            v.bodyL.reset();
            v.bodyR.reset();
        }
        m_voiceCounter = 0;
        m_noiseRng = 54321;
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
                    if (v.active) v.ampEnv.gate(false);
            }
            else if (msg.type == midi::MidiMessage::Type::PitchBend)
                m_pitchBend = midi::Convert::pb32toFloat(msg.value);
        }

        float damping = m_params[kDamping];
        float decay = 0.9f + m_params[kDecay] * 0.099f;  // 0.9 to 0.999
        float stretch = m_params[kStretch];
        float body = m_params[kBody];
        float volume = m_params[kVolume];

        // Allpass coefficient for string stretch (inharmonicity)
        float apCoeff = stretch * 0.5f;

        // Damping: LP coefficient in feedback loop (higher = more damping)
        float lpCoeff = 0.5f + damping * 0.45f;  // 0.5 to 0.95

        for (int vi = 0; vi < kMaxVoices; ++vi) {
            auto& v = m_voices[vi];
            if (!v.active) continue;

            for (int i = 0; i < numFrames; ++i) {
                // Excitation: inject into delay line during attack phase
                float excite = 0.0f;
                if (v.excitePos < v.exciteLen) {
                    excite = generateExcitation(v) * v.velocity;
                    v.excitePos++;
                }

                // Read from delay line (with linear interpolation for fractional delay)
                float readPosF = static_cast<float>(v.writePos) - v.delayLenF;
                if (readPosF < 0.0f) readPosF += kMaxDelay;
                int idx0 = static_cast<int>(readPosF) % kMaxDelay;
                int idx1 = (idx0 + 1) % kMaxDelay;
                float frac = readPosF - std::floor(readPosF);
                float delayed = v.delayLine[idx0] * (1.0f - frac) + v.delayLine[idx1] * frac;

                // Low-pass filter in feedback loop (damping)
                v.lpState = v.lpState * lpCoeff + delayed * (1.0f - lpCoeff);

                // Allpass filter for inharmonicity (string stretch)
                float ap_in = v.lpState;
                float ap_out = apCoeff * ap_in + v.apState - apCoeff * v.apState;
                // Blend between pure KS and stretched
                float feedback = ap_in * (1.0f - stretch) + ap_out * stretch;
                v.apState = ap_in;

                // Write feedback + excitation into delay line
                v.delayLine[v.writePos] = (feedback * decay) + excite;
                v.writePos = (v.writePos + 1) % kMaxDelay;

                // Apply amplitude envelope
                float env = v.ampEnv.process();
                float sample = delayed * env * volume;

                // Body resonance (bandpass filter)
                if (body > 0.01f) {
                    float bodyOut = v.bodyL.process(sample);
                    sample = sample * (1.0f - body) + bodyOut * body;
                }

                buffer[i * numChannels + 0] += sample;
                if (numChannels > 1)
                    buffer[i * numChannels + 1] += sample;
            }

            if (v.ampEnv.isIdle())
                v.active = false;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo infos[] = {
            {"Exciter",    0.0f,  3.0f,  0.0f,   "",  false},
            {"Damping",    0.0f,  1.0f,  0.3f,   "",  false},
            {"Brightness", 0.0f,  1.0f,  0.7f,   "",  false},
            {"Decay",      0.0f,  1.0f,  0.8f,   "",  false},
            {"Body",       0.0f,  1.0f,  0.2f,   "",  false},
            {"Stretch",    0.0f,  1.0f,  0.0f,   "",  false},
            {"Attack",     0.001f, 0.1f, 0.005f, "s", false},
            {"Volume",     0.0f,  1.0f,  0.7f,   "",  false},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index >= 0 && index < kParamCount) return m_params[index];
        return 0.0f;
    }

    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount)
            m_params[index] = value;
    }

private:
    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;

        float delayLine[kMaxDelay] = {};
        int   writePos = 0;
        float delayLenF = 0.0f;   // fractional delay length
        int   delayLen = 0;        // integer delay length

        int excitePos = 0;         // current position in excitation
        int exciteLen = 0;         // excitation duration in samples

        float lpState = 0.0f;     // feedback LP filter state
        float apState = 0.0f;     // allpass filter state

        Envelope ampEnv;
        effects::Biquad bodyL;    // body resonance filter
        effects::Biquad bodyR;
    };

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
        int slot = findFreeVoice();
        auto& v = m_voices[slot];
        v.active = true;
        v.note = note;
        v.channel = ch;
        v.velocity = velocityToGain(vel16);
        v.startOrder = m_voiceCounter++;

        // Calculate delay line length from pitch
        float freq = noteToFreq(note) *
            std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);
        v.delayLenF = static_cast<float>(m_sampleRate / freq);
        v.delayLen = std::min(static_cast<int>(v.delayLenF), kMaxDelay - 1);

        // Clear delay line
        std::memset(v.delayLine, 0, sizeof(v.delayLine));
        v.writePos = 0;
        v.lpState = 0.0f;
        v.apState = 0.0f;

        // Excitation length
        float attackTime = m_params[kAttack];
        v.exciteLen = std::max(1, static_cast<int>(attackTime * m_sampleRate));
        v.excitePos = 0;

        // Body resonance: tune to note fundamental
        v.bodyL.compute(effects::Biquad::Type::BandPass, m_sampleRate,
                        freq, 0.0, 2.0);

        // Amp envelope: short attack, long sustain, moderate release
        v.ampEnv.setADSR(0.001f, 0.01f, 1.0f, 0.5f);
        v.ampEnv.gate(true);
    }

    void noteOff(uint8_t note, uint8_t ch) {
        for (auto& v : m_voices) {
            if (v.active && v.note == note && v.channel == ch)
                v.ampEnv.gate(false);
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

    float generateExcitation(Voice& v) {
        int type = std::clamp(static_cast<int>(m_params[kExciter]), 0, 3);
        float brightness = m_params[kBrightness];
        float t = static_cast<float>(v.excitePos) / static_cast<float>(v.exciteLen);

        // Envelope shape: quick attack, fast decay within excitation window
        float excEnv = (t < 0.1f) ? t / 0.1f : (1.0f - t) / 0.9f;
        excEnv = std::max(0.0f, excEnv);

        float sample = 0.0f;
        switch (type) {
        case Noise:
            // White noise burst
            sample = noiseGen();
            break;
        case Bright:
            // Band-limited noise (brighter, more harmonics)
            sample = noiseGen() * 0.5f + noiseGen() * 0.5f;  // sum of two → more gaussian
            sample *= (1.0f + brightness);
            break;
        case Saw:
            // Sawtooth impulse
            sample = 2.0f * (static_cast<float>(v.excitePos % v.delayLen) /
                     std::max(1.0f, static_cast<float>(v.delayLen))) - 1.0f;
            break;
        case Sine:
            // Sine burst at string fundamental
            sample = std::sin(2.0f * static_cast<float>(M_PI) * v.excitePos / v.delayLenF);
            break;
        }

        // Brightness: simple LP on excitation
        sample = sample * brightness + v.lpState * (1.0f - brightness) * 0.3f;

        return sample * excEnv;
    }

    float noiseGen() {
        m_noiseRng ^= m_noiseRng << 13;
        m_noiseRng ^= m_noiseRng >> 17;
        m_noiseRng ^= m_noiseRng << 5;
        return static_cast<float>(static_cast<int32_t>(m_noiseRng)) / 2147483648.0f;
    }

    void applyDefaults() {
        for (int i = 0; i < kParamCount; ++i)
            m_params[i] = parameterInfo(i).defaultValue;
    }

    Voice m_voices[kMaxVoices];
    int64_t m_voiceCounter = 0;
    float m_pitchBend = 0.0f;
    uint32_t m_noiseRng = 54321;

    float m_params[kParamCount] = {0.0f, 0.3f, 0.7f, 0.8f, 0.2f, 0.0f, 0.005f, 0.7f};
};

} // namespace instruments
} // namespace yawn
