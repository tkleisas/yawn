#pragma once

// VisualKnobBus — lock-free audio-thread → UI-thread hand-off for MIDI CC
// values destined for the A..H knobs of a visual layer.
//
// MidiEngine runs on the audio thread; VisualEngine state lives on the UI
// thread (GL context ownership). We can't call setLayerKnob from audio, so
// the audio thread writes into this bus and the UI thread polls.
//
// Per-slot seqlock-ish pattern: atomic value + atomic monotonic version.
// Audio writer bumps version after the value; UI reader notices when the
// version differs from its last-seen value. A single torn read is harmless
// (next frame catches it) so this pattern is sufficient.
//
// This is a singleton because MidiEngine has no natural owner-path to
// VisualEngine, and making it a global keeps the dependency direction
// clean (midi/ → visual/VisualKnobBus without pulling in VisualEngine).

#include "core/Constants.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace yawn {
namespace visual {

class VisualKnobBus {
public:
    static constexpr int kNumKnobs = 8;

    static VisualKnobBus& instance() {
        static VisualKnobBus inst;
        return inst;
    }

    // Called from the audio thread when a MIDI CC mapped to VisualKnob fires.
    void write(int track, int knob, float value) {
        if (track < 0 || track >= kMaxTracks) return;
        if (knob < 0 || knob >= kNumKnobs)    return;
        auto& s = m_slots[track][knob];
        s.value.store(value, std::memory_order_relaxed);
        // Release fence: any prior writes (value) become visible to a
        // reader that sees this incremented version.
        s.version.fetch_add(1, std::memory_order_release);
    }

    // Called from the UI thread each frame per (track, knob). Updates
    // `lastSeen` and returns the new value if a fresh one is available.
    bool readIfChanged(int track, int knob, uint32_t& lastSeen, float* outValue) {
        if (track < 0 || track >= kMaxTracks) return false;
        if (knob < 0 || knob >= kNumKnobs)    return false;
        auto& s = m_slots[track][knob];
        uint32_t v = s.version.load(std::memory_order_acquire);
        if (v == lastSeen) return false;
        lastSeen = v;
        if (outValue) *outValue = s.value.load(std::memory_order_relaxed);
        return true;
    }

private:
    struct Slot {
        std::atomic<float>    value{0.0f};
        std::atomic<uint32_t> version{0};
    };
    Slot m_slots[kMaxTracks][kNumKnobs];
};

} // namespace visual
} // namespace yawn
