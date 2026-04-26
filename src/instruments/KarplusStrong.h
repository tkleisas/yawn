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

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kExciterLabels[] = {"Noise", "Bright", "Saw", "Sine"};
        static const InstrumentParameterInfo infos[] = {
            {"Exciter",    0.0f,  3.0f,  0.0f,   "",  false, false, WidgetHint::StepSelector, kExciterLabels, 4},
            {"Damping",    0.0f,  1.0f,  0.3f,   "",  false},
            {"Brightness", 0.0f,  1.0f,  0.7f,   "",  false},
            {"Decay",      0.0f,  1.0f,  0.8f,   "",  false},
            {"Body",       0.0f,  1.0f,  0.2f,   "",  false},
            {"Stretch",    0.0f,  1.0f,  0.0f,   "",  false},
            {"Attack",     0.001f, 0.1f, 0.005f, "s", false},
            {"Volume",     0.0f,  1.0f,  0.7f,   "",  false, false, WidgetHint::DentedKnob},
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

    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);
    int findFreeVoice();
    float generateExcitation(Voice& v);
    float noiseGen();
    void applyDefaults();

    Voice m_voices[kMaxVoices];
    int64_t m_voiceCounter = 0;
    float m_pitchBend = 0.0f;
    uint32_t m_noiseRng = 54321;

    float m_params[kParamCount] = {0.0f, 0.3f, 0.7f, 0.8f, 0.2f, 0.0f, 0.005f, 0.7f};
};

} // namespace instruments
} // namespace yawn
