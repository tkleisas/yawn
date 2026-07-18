#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <utility>

#include "util/RtRetireList.h"

namespace yawn {

// Base for the fixed-capacity effect chains (audio + MIDI).
//
// Thread contract
// ───────────────
// The UI thread owns the object model: m_slots holds the unique_ptrs
// and every structural mutation (insert/remove/clear/move) happens on
// the UI thread and ends with publishSnapshot(). The audio thread
// reads ONLY the published snapshot (rtSnapshot()), never m_slots —
// so a UI-side remove can't destroy an effect while the audio thread
// is inside its process(). Removed effects and stale snapshots are
// parked in the retire list (util/RtRetireList.h) and destroyed once
// the audio heartbeat has passed them. Without a retire list (unit
// tests, single-threaded use) they are destroyed immediately.
template<typename T, int N>
class ChainBase {
public:
    // Immutable view of the chain published to the audio thread.
    struct Snapshot {
        std::array<T*, N> ptrs{};
        int count = 0;
    };

    ChainBase() { publishSnapshot(); }
    ~ChainBase() { retireSnapshot(nullptr); }

    ChainBase(const ChainBase&) = delete;
    ChainBase& operator=(const ChainBase&) = delete;

    ChainBase(ChainBase&& o) noexcept
        : m_slots(std::move(o.m_slots))
        , m_count(o.m_count)
        , m_retire(o.m_retire) {
        m_snapshot.store(o.m_snapshot.exchange(nullptr, std::memory_order_acq_rel),
                         std::memory_order_release);
        o.m_count = 0;
    }

    ChainBase& operator=(ChainBase&& o) noexcept {
        if (this == &o) return *this;
        // Retire current contents (audio thread may be mid-iteration
        // on our snapshot), then adopt the source's model + snapshot.
        retireSlots();
        retireSnapshot(nullptr);
        m_slots = std::move(o.m_slots);
        m_count = o.m_count;
        m_retire = o.m_retire;
        m_snapshot.store(o.m_snapshot.exchange(nullptr, std::memory_order_acq_rel),
                         std::memory_order_release);
        o.m_count = 0;
        return *this;
    }

    // ── UI-thread model accessors ──
    int  count() const { return m_count; }
    bool empty() const { return m_count == 0; }

    void clear() {
        retireSlots();
        m_count = 0;
        publishSnapshot();
    }

    void moveEffect(int fromIndex, int toIndex) {
        if (fromIndex == toIndex) return;
        if (fromIndex < 0 || fromIndex >= m_count) return;
        if (toIndex < 0 || toIndex >= m_count) return;
        auto elem = std::move(m_slots[fromIndex]);
        if (fromIndex < toIndex) {
            for (int i = fromIndex; i < toIndex; ++i)
                m_slots[i] = std::move(m_slots[i + 1]);
        } else {
            for (int i = fromIndex; i > toIndex; --i)
                m_slots[i] = std::move(m_slots[i - 1]);
        }
        m_slots[toIndex] = std::move(elem);
        publishSnapshot();
    }

    // Retire list for deferred destruction. Set once before the chain
    // is shared with the audio thread (engine construction).
    void setRetireList(util::RtRetireList* rl) { m_retire = rl; }

    // ── Audio-thread view ──
    // Stable snapshot of the slot pointers. Never null after
    // construction; the object it points to stays alive until the
    // retire list's grace period elapses after the next publish.
    const Snapshot* rtSnapshot() const {
        return m_snapshot.load(std::memory_order_acquire);
    }

    // Snapshot-based slot lookup — the only accessor the audio
    // thread (and anything called from it, e.g. AutomationEngine)
    // may use.
    T* effectAtRt(int slot) const {
        const Snapshot* s = rtSnapshot();
        if (!s || slot < 0 || slot >= s->count) return nullptr;
        return s->ptrs[slot];
    }

protected:
    // Park an effect for deferred destruction. Caller has already
    // unlinked it from m_slots; immediate when no retire list.
    void retireEffect(std::unique_ptr<T> fx) {
        if (!fx) return;
        if (m_retire) m_retire->retire(std::move(fx));
    }

    // Publish the current m_slots as the audio thread's new view.
    // UI thread only (allocates). Called by every mutator.
    void publishSnapshot() {
        auto* snap = new Snapshot();
        snap->count = m_count;
        for (int i = 0; i < N; ++i)
            snap->ptrs[i] = m_slots[i].get();
        retireSnapshot(snap);
    }

    util::RtRetireList* retireList() const { return m_retire; }

    std::array<std::unique_ptr<T>, N> m_slots;
    int m_count = 0;

private:
    void retireSlots() {
        for (int i = 0; i < N; ++i)
            retireEffect(std::move(m_slots[i]));
    }

    // Swap the published snapshot for `snap` and park the old one.
    void retireSnapshot(const Snapshot* snap) {
        const Snapshot* old = m_snapshot.exchange(snap, std::memory_order_acq_rel);
        if (!old) return;
        if (m_retire) m_retire->retire(std::unique_ptr<const Snapshot>(old));
        else delete old;
    }

    std::atomic<const Snapshot*> m_snapshot{nullptr};
    util::RtRetireList* m_retire = nullptr;
};

} // namespace yawn
