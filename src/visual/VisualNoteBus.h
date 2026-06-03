#pragma once

// VisualNoteBus — lock-free audio-thread → visual-thread hand-off of
// MIDI note-on events, so scene scripts can react to drum hits / played
// notes (`ctx.notes`).
//
// Sibling of VisualModBus / VisualKnobBus, but an *event* stream rather
// than per-slot state, so it's a single-producer / single-consumer ring:
//   • Producer  = the audio thread (AudioEngine::processAudio), which
//     scans each track's final MIDI buffer for note-ons and push()es one
//     event per note, tagged with the originating track.
//   • Consumer  = the visual thread (VisualEngine::tick → drainNoteBus),
//     which drains every frame into an aged "recent notes" list.
//
// Events are tagged with track / channel / pitch / velocity only — no
// timestamp. The consumer stamps wall-clock time on drain (sub-frame
// latency is irrelevant for visuals), which keeps the producer path
// allocation- and clock-free. On overrun (consumer not draining, e.g.
// the output window is hidden) push() drops the event; the ring never
// grows or blocks.

#include <atomic>
#include <cstdint>

namespace yawn {
namespace visual {

struct VisualNoteEvent {
    uint8_t track   = 0;
    uint8_t channel = 0;
    uint8_t pitch   = 0;
    uint8_t vel7    = 0;   // 1..127
};

class VisualNoteBus {
public:
    static constexpr uint32_t kCapacity = 1024;   // power of two

    static VisualNoteBus& instance() {
        static VisualNoteBus inst;
        return inst;
    }

    // Audio thread (single producer): enqueue a note-on. Drops silently
    // when full.
    void push(uint8_t track, uint8_t channel, uint8_t pitch, uint8_t vel7) {
        const uint32_t head = m_head.load(std::memory_order_relaxed);
        const uint32_t tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= kCapacity) return;   // full → drop
        VisualNoteEvent& e = m_buf[head & (kCapacity - 1)];
        e.track = track; e.channel = channel; e.pitch = pitch; e.vel7 = vel7;
        m_head.store(head + 1, std::memory_order_release);
    }

    // Visual thread (single consumer): pop the next event. Returns false
    // when empty.
    bool pop(VisualNoteEvent& out) {
        const uint32_t tail = m_tail.load(std::memory_order_relaxed);
        const uint32_t head = m_head.load(std::memory_order_acquire);
        if (tail == head) return false;
        out = m_buf[tail & (kCapacity - 1)];
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    VisualNoteEvent       m_buf[kCapacity];
    std::atomic<uint32_t> m_head{0};   // written by producer
    std::atomic<uint32_t> m_tail{0};   // written by consumer
};

} // namespace visual
} // namespace yawn
