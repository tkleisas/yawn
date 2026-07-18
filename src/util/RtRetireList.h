#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace yawn {
namespace util {

// Retire list for cross-thread object handoff (RCU-lite).
//
// Thread contract
// ───────────────
// The UI thread swaps a live object by atomically publishing its
// replacement (e.g. std::atomic<T*>::exchange) and parking the old
// one here. The audio thread may still be holding the old pointer
// mid-callback, so the object must not be destroyed immediately.
// purge() only frees entries once the audio heartbeat has advanced
// at least kGraceCallbacks past the retire point — by then any
// in-flight callback that loaded the old pointer has finished.
//
// All methods are UI-thread only. The audio thread never touches
// this object; it only ever advances the heartbeat counter.
// This generalizes the pattern already used by NeuralAmp
// (src/effects/NeuralAmp.cpp, retiredDsps) and ConvolutionReverb —
// with the improvement that destruction is keyed to the audio
// callback counter, not wall-clock time, so a stalled audio thread
// (debugger, suspend) can never cause a premature free.
//
// If no heartbeat is set (unit tests, single-threaded use) retire()
// destroys immediately — with no audio thread there is nothing to
// wait for.
class RtRetireList {
public:
    // Callbacks that must complete after a retire before the entry is
    // freed. A callback that loaded the retired pointer was already
    // running when we retired (heartbeat == retire-time value), so
    // waiting for the heartbeat to advance past it covers exactly one
    // full callback cycle; 3 is generous.
    static constexpr uint64_t kGraceCallbacks = 3;

    // Point the list at the engine's callback counter. Must be called
    // before the first retire; the heartbeat must outlive this list.
    void setHeartbeat(const std::atomic<uint64_t>* heartbeat) {
        m_heartbeat = heartbeat;
    }

    // Park an object. With a heartbeat, destruction is deferred until
    // purge() observes kGraceCallbacks elapsed; without one the object
    // is destroyed on the spot.
    void retire(std::shared_ptr<const void> obj) {
        if (!obj) return;
        if (!m_heartbeat) return; // destroys obj here
        Entry e;
        e.obj = std::move(obj);
        e.retiredAt = m_heartbeat->load(std::memory_order_acquire);
        m_entries.push_back(std::move(e));
    }

    template <typename T>
    void retire(std::unique_ptr<T> obj) {
        if (!obj) return;
        if (!m_heartbeat) return; // destroys obj here
        retire(std::shared_ptr<const void>(std::move(obj)));
    }

    // Free entries whose grace period has elapsed. Call once per UI
    // frame. Cheap no-op when empty.
    void purge() {
        if (!m_heartbeat || m_entries.empty()) return;
        const uint64_t now = m_heartbeat->load(std::memory_order_acquire);
        eraseIf([now](const Entry& e) {
            return now - e.retiredAt >= kGraceCallbacks;
        });
    }

    // Free everything immediately. Only valid when no audio callback
    // can be running (stream stopped / engine shutdown).
    void clear() { m_entries.clear(); }

    size_t size() const { return m_entries.size(); }

private:
    struct Entry {
        std::shared_ptr<const void> obj;
        uint64_t retiredAt = 0;
    };

    template <typename Pred>
    void eraseIf(Pred pred) {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (pred(*it)) it = m_entries.erase(it);
            else ++it;
        }
    }

    const std::atomic<uint64_t>* m_heartbeat = nullptr;
    std::vector<Entry> m_entries;
};

} // namespace util
} // namespace yawn
