#pragma once

// LiveCodeScheduler — beat-anchored scheduler for the live-coding layer
// (docs/live-coding.md §3). Generation-tagged: each script run bumps the
// generation id; entries created during a run belong to that generation and
// are dropped wholesale when it is replaced (improv.clear handles single
// entries). Two time axes:
//
//   beat — musical; only advances while the transport plays. All musical
//          scheduling (at/after/every/on_bar) lives here, anchored to
//          transport beats so tempo changes never invalidate pending work.
//   wall — seconds; advances regardless of playback. Used for held-note
//          releases while the transport is stopped (phase-1 moves note
//          scheduling to the audio thread's at-beat queue entirely).
//
// Lua-agnostic by design: entries own plain std::function<void(double)>
// callbacks (arg = 1-based bar number for bar entries, else 0) and the
// owner is notified via onRemove when an entry leaves the queue, so
// Lua-side resources (function refs) can be released.
//
// Jump handling: when the beat provider reports a backward jump (loop wrap,
// seek), recurring entries re-align to their next grid position ≥ now so
// they keep firing at musically-correct spots; one-shot entries already in
// the past fire immediately (late → play-now policy, §3.2).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace yawn {
namespace livecode {

struct ScheduleHandle {
    uint64_t id = 0;
    bool valid() const { return id != 0; }
};

class LiveCodeScheduler {
public:
    // Callback arg: 1-based bar number for on_bar entries; the target
    // transport beat for beat-axis entries (so callbacks can pass it to
    // yawn.note's at_beat); 0 for wall-axis entries.
    using Fn = std::function<void(double)>;

    // Providers — set once at init. nowBeats/nowSeconds must be safe to
    // call several times per tick (cheap atomic reads).
    void setBeatProvider(std::function<double()> f)      { m_nowBeats = std::move(f); }
    void setPlayingProvider(std::function<bool()> f)     { m_isPlaying = std::move(f); }
    void setSecondsProvider(std::function<double()> f)   { m_nowSeconds = std::move(f); }
    void setBeatsPerBarProvider(std::function<int()> f)  { m_beatsPerBar = std::move(f); }
    // Schedule-ahead window for beat-axis entries (§3.2): entries fire when
    // now >= next - lookahead, so callbacks run early enough to schedule
    // sample-accurate notes via the audio thread's pending queue. 0 =
    // fire-when-due (phase-0 behaviour). Keep it below the smallest
    // interval in use.
    void setLookaheadBeats(double b) { m_lookaheadBeats = std::max(b, 0.0); }
    // Late policy (§3.2): when an entry's target beat has already passed
    // at fire time (UI hitch longer than the lookahead), false = fire
    // anyway (play now), true = drop the occurrence (one-shot: remove;
    // recurring: realign without firing).
    void setLateDrop(bool d) { m_lateDrop = d; }

    // Notify when an entry leaves the queue (cancel / clearGeneration /
    // clearAll) so owners can release per-entry resources.
    void setRemoveListener(std::function<void(uint64_t)> f) {
        m_onRemove = std::move(f);
    }

    // ── Schedule APIs ────────────────────────────────────────────────

    // One-shot at an absolute transport beat.
    ScheduleHandle atBeat(double beat, uint32_t generation, Fn fn);

    // One-shot `delta` beats from the current beat position.
    ScheduleHandle afterBeats(double delta, uint32_t generation, Fn fn);

    // Recurring every `intervalBeats`, first fire one interval from now.
    // repetitions == 0 means unlimited.
    ScheduleHandle every(double intervalBeats, uint32_t generation, Fn fn,
                         int repetitions = 0);

    // Recurring on the bar grid; fn receives the 1-based bar number.
    ScheduleHandle onBar(uint32_t generation, Fn fn);

    // Wall-clock axis: fire `sec` seconds from now regardless of transport.
    ScheduleHandle afterSeconds(double sec, uint32_t generation, Fn fn);

    bool cancel(uint64_t id);
    void clearGeneration(uint32_t generation);
    void clearAll();

    size_t pending() const { return m_entries.size(); }

    // Fire due entries. Call once per UI frame. Beat-axis entries only
    // progress while the transport plays; wall-axis entries always do.
    void tick();

private:
    enum class Axis : uint8_t { Beat, Wall };
    enum class Kind : uint8_t { OneShot, Recurring, Bar };

    struct Entry {
        uint64_t id = 0;
        uint32_t generation = 0;
        Axis     axis = Axis::Beat;
        Kind     kind = Kind::OneShot;
        double   interval = 0.0;   // beats or seconds, per axis
        double   base = 0.0;       // grid origin (absolute beat / 0 for bars)
        double   next = 0.0;       // next fire time on this entry's axis
        int      repetitions = 0;  // 0 = unlimited
        int      fired = 0;
        Fn       fn;
    };

    void eraseAt(size_t index);
    // Realign a recurring/bar entry after the beat position moved backward.
    void realignRecurring(Entry& e, double now);

    std::vector<Entry>        m_entries;
    uint64_t                  m_nextId = 1;
    double                    m_prevBeat = 0.0;
    bool                      m_havePrev = false;
    double                    m_lookaheadBeats = 0.0;
    bool                      m_lateDrop = false;

    std::function<double()> m_nowBeats;
    std::function<bool()>   m_isPlaying;
    std::function<double()> m_nowSeconds;
    std::function<int()>    m_beatsPerBar;
    std::function<void(uint64_t)> m_onRemove;
};

} // namespace livecode
} // namespace yawn
