#pragma once

// VisualModBus — lock-free audio-thread → UI-thread hand-off for MIDI LFO
// modulation destined for a visual layer (A..H knobs and shader @range
// uniforms).
//
// Sibling of VisualKnobBus. Where VisualKnobBus carries absolute MIDI CC
// values for the A..H knobs, this bus carries a midi::LFO's bipolar output
// (depth+bias already applied) so the UI thread can compose it on top of a
// visual param's base value each frame. The bus is keyed by the LFO's
// (track, chain-slot) — a fixed, bounded address that needs no string on
// the audio thread, so the value path stays allocation-free.
//
// A single torn read is harmless: the UI re-reads every frame and the value
// changes smoothly, so a stale float for one frame is invisible. That makes
// a plain relaxed atomic<float> per slot sufficient — no version seqlock
// needed, because (unlike VisualKnobBus) the UI does not gate on "changed";
// it unconditionally re-accumulates every active LFO's contribution.

#include "core/Constants.h"
#include "midi/MidiEffectChain.h"   // kMaxMidiEffects

#include <atomic>

namespace yawn {
namespace visual {

class VisualModBus {
public:
    static constexpr int kNumSlots = midi::kMaxMidiEffects;

    static VisualModBus& instance() {
        static VisualModBus inst;
        return inst;
    }

    // Audio thread: store the LFO at (track, slot)'s current modulation
    // value. Writing 0 (idle/bypassed) lets the UI side relax the offset.
    void write(int track, int slot, float value) {
        if (track < 0 || track >= kMaxTracks)  return;
        if (slot  < 0 || slot  >= kNumSlots)   return;
        m_slots[track][slot].store(value, std::memory_order_relaxed);
    }

    // UI thread: latest modulation value for the LFO at (track, slot).
    float read(int track, int slot) const {
        if (track < 0 || track >= kMaxTracks)  return 0.0f;
        if (slot  < 0 || slot  >= kNumSlots)   return 0.0f;
        return m_slots[track][slot].load(std::memory_order_relaxed);
    }

private:
    std::atomic<float> m_slots[kMaxTracks][kNumSlots]{};
};

} // namespace visual
} // namespace yawn
