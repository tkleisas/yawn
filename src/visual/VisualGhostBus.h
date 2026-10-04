#pragma once

// VisualGhostBus — UI-thread → visual-thread hand-off of the live-coding
// ghost-note snapshot (feat: upcoming improv fires as shader uniforms).
//
// Sibling of VisualNoteBus / VisualModBus / VisualKnobBus. Unlike the
// event buses this is a *state* mailbox: the producer (App frame tick,
// UI thread) publishes the newest snapshot constructed from the
// LiveCodeManager's improv ledger; the consumer (VisualEngine render)
// reads it once per frame. Because the layout is a tiny fixed payload and
// the writer is a single thread, a plain seqlock keeps reads
// allocation-free and unsynchronized-with-note:
//
//   writer: seq(odd) → fill → seq(even)     (release ordering)
//   reader: retry until seq is even, copy, verify seq unchanged
//
// Payload up to 8 upcoming fires, newest-sorted by fire time, one vec4
// each — GLSL `uniform vec4 iGhostN` (pitch/127, vel 0..1, beats until
// fire, track/32). iGhostCount carries the valid slot count.

#include <atomic>
#include <cstdint>

namespace yawn {
namespace visual {

class VisualGhostBus {
public:
    static constexpr uint32_t kMaxGhosts = 8;

    // One vec4's worth of a pending ghost.
    struct GhostVec {
        // pitch01: 0..1, vel01: 0..1, untilFire: beats (>0 = upcoming,
        // 0..0.1 ≈ firing now), track: floor to 0..kMaxTrack.
        float pitch01, vel01, untilFire, track;
    };

    static VisualGhostBus& instance() {
        static VisualGhostBus inst;
        return inst;
    }

    int32_t bpb() const { return m_bpb.load(std::memory_order_relaxed); }

    // UI thread (single producer).
    void publish(const GhostVec* ghosts, uint32_t count, int bpbIn) {
        if (count > kMaxGhosts) count = kMaxGhosts;
        m_seq.store(m_seq.load(std::memory_order_relaxed) + 1,
                    std::memory_order_relaxed);   // odd = writing
        std::atomic_thread_fence(std::memory_order_release);
        for (uint32_t i = 0; i < count; ++i) {
            m_ghosts[i][0] = ghosts[i].pitch01;
            m_ghosts[i][1] = ghosts[i].vel01;
            m_ghosts[i][2] = ghosts[i].untilFire;
            m_ghosts[i][3] = ghosts[i].track;
        }
        m_count = count;
        m_bpb.store(bpbIn, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        m_seq.store(m_seq.load(std::memory_order_relaxed) + 1,
                    std::memory_order_relaxed);   // even = stable
        std::atomic_thread_fence(std::memory_order_release);
    }

    // Visual thread (single consumer). Returns the stable slot count and
    // fills `out` (never torn: seqlock retry).
    uint32_t read(GhostVec* out) const {
        for (int attempt = 0; attempt < 64; ++attempt) {
            const uint32_t s0 = m_seq.load(std::memory_order_acquire);
            if (s0 & 1) continue;             // mid-write → retry
            const uint32_t n = m_count;
            for (uint32_t i = 0; i < n; ++i) {
                out[i].pitch01   = m_ghosts[i][0];
                out[i].vel01     = m_ghosts[i][1];
                out[i].untilFire = m_ghosts[i][2];
                out[i].track     = m_ghosts[i][3];
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint32_t s1 = m_seq.load(std::memory_order_acquire);
            if (s0 == s1) return n;           // stable read
        }
        return 0;             // writer never quiesced this frame — skip
    }

private:
    constexpr VisualGhostBus() = default;

    alignas(64) float m_ghosts[kMaxGhosts][4] = {};
    uint32_t          m_count = 0;
    std::atomic<int32_t> m_bpb{4};   // beats per bar at publish time
    std::atomic<uint32_t> m_seq{0};
};

} // namespace visual
} // namespace yawn
