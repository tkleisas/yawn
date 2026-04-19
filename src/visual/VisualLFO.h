#pragma once

// VisualLFO — tiny time-varying modulator applied to a visual layer's knob
// on the UI thread. Not to be confused with midi::LFO (which lives on the
// audio thread and targets audio-engine parameters). The waveform math is
// the same; the integration point is different.
//
// Beat-synced when the transport is playing; falls back to wall-clock when
// stopped so knobs still breathe while you're shader-authoring.

#include <cmath>
#include <cstdint>

namespace yawn {
namespace visual {

struct VisualLFO {
    enum Shape : uint8_t {
        Sine = 0, Triangle = 1, Saw = 2, Square = 3, SampleAndHold = 4,
        kNumShapes
    };

    bool   enabled = false;
    Shape  shape   = Sine;
    float  rate    = 1.0f;   // beats per cycle (beat-synced) or Hz when free
    float  depth   = 0.3f;   // 0..1 — added on top of the knob base value
    bool   sync    = true;   // true: rate is in beats/cycle; false: rate is Hz
    float  phase   = 0.0f;   // 0..1 phase offset

    // Per-instance S&H state. Updated as a side effect of evaluate().
    float  shValue      = 0.0f;
    double shLastPhase  = -1.0;

    // Returns a bipolar −1..+1 waveform value. Caller multiplies by depth
    // and adds to the knob's base value, then clamps to [0,1].
    float evaluate(double transportBeats, double wallSeconds) {
        if (!enabled || depth <= 0.0f) return 0.0f;
        const double r = (rate > 0.0001f) ? static_cast<double>(rate) : 0.0001;
        double t = sync ? (transportBeats / r) : (wallSeconds * r);
        double ph = t + static_cast<double>(phase);
        double frac = ph - std::floor(ph);

        switch (shape) {
            case Sine:
                return static_cast<float>(std::sin(frac * 6.28318530718));
            case Triangle:
                return static_cast<float>(
                    frac < 0.5 ? (4.0 * frac - 1.0) : (3.0 - 4.0 * frac));
            case Saw:
                return static_cast<float>(2.0 * frac - 1.0);
            case Square:
                return frac < 0.5 ? 1.0f : -1.0f;
            case SampleAndHold: {
                // Resample at each cycle boundary. A tiny 32-bit hash of the
                // cycle index gives a deterministic pseudo-random series.
                int cycle = static_cast<int>(std::floor(ph));
                if (cycle != static_cast<int>(std::floor(shLastPhase))) {
                    uint32_t x = static_cast<uint32_t>(cycle) * 2654435761u;
                    x ^= x >> 16;
                    shValue = (static_cast<float>(x & 0xFFFFFF) /
                               static_cast<float>(0xFFFFFF)) * 2.0f - 1.0f;
                }
                shLastPhase = ph;
                return shValue;
            }
            default:
                return 0.0f;
        }
    }
};

} // namespace visual
} // namespace yawn
