#include "livecode/LiveCodeScheduler.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace livecode {

// Tolerance for "is this entry due" and backward-jump detection.
static constexpr double kEps = 1e-6;

ScheduleHandle LiveCodeScheduler::atBeat(double beat, uint32_t generation, Fn fn) {
    Entry e;
    e.id         = m_nextId++;
    e.generation = generation;
    e.axis       = Axis::Beat;
    e.kind       = Kind::OneShot;
    e.next       = beat;
    e.fn         = std::move(fn);
    m_entries.push_back(std::move(e));
    return ScheduleHandle{m_entries.back().id};
}

ScheduleHandle LiveCodeScheduler::afterBeats(double delta, uint32_t generation, Fn fn) {
    const double now = m_nowBeats ? m_nowBeats() : 0.0;
    return atBeat(now + std::max(delta, 0.0), generation, std::move(fn));
}

ScheduleHandle LiveCodeScheduler::every(double intervalBeats, uint32_t generation,
                                        Fn fn, int repetitions) {
    const double now = m_nowBeats ? m_nowBeats() : 0.0;
    Entry e;
    e.id          = m_nextId++;
    e.generation  = generation;
    e.axis        = Axis::Beat;
    e.kind        = Kind::Recurring;
    e.interval    = std::max(intervalBeats, kEps);
    e.base        = now;
    e.next        = now + e.interval;
    e.repetitions = repetitions;
    e.fn          = std::move(fn);
    m_entries.push_back(std::move(e));
    return ScheduleHandle{m_entries.back().id};
}

ScheduleHandle LiveCodeScheduler::onBar(uint32_t generation, Fn fn) {
    const double now = m_nowBeats ? m_nowBeats() : 0.0;
    const int bpb = m_beatsPerBar ? std::max(m_beatsPerBar(), 1) : 4;
    Entry e;
    e.id         = m_nextId++;
    e.generation = generation;
    e.axis       = Axis::Beat;
    e.kind       = Kind::Bar;
    e.interval   = static_cast<double>(bpb);
    e.base       = 0.0;
    // First fire at the next bar boundary strictly after now.
    e.next       = (std::floor(now / e.interval) + 1.0) * e.interval;
    e.fn         = std::move(fn);
    m_entries.push_back(std::move(e));
    return ScheduleHandle{m_entries.back().id};
}

ScheduleHandle LiveCodeScheduler::afterSeconds(double sec, uint32_t generation, Fn fn) {
    const double now = m_nowSeconds ? m_nowSeconds() : 0.0;
    Entry e;
    e.id         = m_nextId++;
    e.generation = generation;
    e.axis       = Axis::Wall;
    e.kind       = Kind::OneShot;
    e.next       = now + std::max(sec, 0.0);
    e.fn         = std::move(fn);
    m_entries.push_back(std::move(e));
    return ScheduleHandle{m_entries.back().id};
}

bool LiveCodeScheduler::cancel(uint64_t id) {
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            if (m_onRemove) m_onRemove(id);
            eraseAt(i);
            return true;
        }
    }
    return false;
}

void LiveCodeScheduler::clearGeneration(uint32_t generation) {
    for (size_t i = m_entries.size(); i-- > 0;) {
        if (m_entries[i].generation == generation) {
            if (m_onRemove) m_onRemove(m_entries[i].id);
            eraseAt(i);
        }
    }
}

void LiveCodeScheduler::clearAll() {
    if (m_onRemove) {
        for (const auto& e : m_entries) m_onRemove(e.id);
    }
    m_entries.clear();
}

void LiveCodeScheduler::eraseAt(size_t index) {
    m_entries[index] = std::move(m_entries.back());
    m_entries.pop_back();
    // Note: swap-erase invalidates the queue's ordering invariant (the
    // vector is unordered anyway — tick() scans by next-time), so nothing
    // else is needed here.
}

void LiveCodeScheduler::realignRecurring(Entry& e, double now) {
    // Smallest grid point ≥ now (grid: base + n*interval for recurring,
    // 0 + n*interval for bar entries).
    const double offset = std::max(now - e.base, 0.0);
    const double n = std::ceil(offset / e.interval - kEps);
    e.next = e.base + n * e.interval;
    if (e.kind == Kind::Recurring && e.next <= now) e.next += e.interval;
    if (e.kind == Kind::Bar && e.next <= now) e.next += e.interval;
    e.fired = 0;
}

void LiveCodeScheduler::tick() {
    const bool playing = m_isPlaying ? m_isPlaying() : false;
    const double beatNow = m_nowBeats ? m_nowBeats() : 0.0;
    const double wallNow = m_nowSeconds ? m_nowSeconds() : 0.0;

    // Backward jump on the beat axis → re-align recurring/bar entries.
    if (m_havePrev && beatNow < m_prevBeat - kEps && playing) {
        for (auto& e : m_entries) {
            if (e.axis != Axis::Beat || e.kind == Kind::OneShot) continue;
            realignRecurring(e, beatNow);
        }
    }
    m_havePrev = true;
    m_prevBeat = beatNow;

    // Scan by index (swap-erase during iteration — restart scan after
    // each removal, entry counts per frame are tiny).
    for (size_t i = 0; i < m_entries.size();) {
        Entry& e = m_entries[i];
        if (e.axis == Axis::Beat) {
            if (!playing) { ++i; continue; }
            // Schedule-ahead window: fire early enough that callbacks can
            // park notes in the audio thread's pending queue on time.
            if (beatNow + kEps < e.next - m_lookaheadBeats) { ++i; continue; }
            // Late beyond the due time (UI hitch longer than lookahead)?
            if (m_lateDrop && beatNow > e.next + kEps) {
                if (e.kind == Kind::OneShot) {
                    if (m_onRemove) m_onRemove(e.id);
                    eraseAt(i);
                } else {
                    realignRecurring(e, beatNow);   // skip without firing
                    ++i;
                }
                continue;
            }
        } else {
            if (wallNow + kEps < e.next) { ++i; continue; }
        }

        // Fire. Arg: upcoming target beat (every/at/after), 1-based bar
        // number (on_bar), 0 (wall axis).
        const double arg = (e.kind == Kind::Bar)
            ? (std::floor(e.next / e.interval) + 1.0)
            : (e.axis == Axis::Beat ? e.next : 0.0);
        Fn fn = e.fn;
        const bool done = (e.kind == Kind::OneShot) ||
                          (e.repetitions > 0 && ++e.fired >= e.repetitions);
        if (done) {
            if (m_onRemove) m_onRemove(e.id);
            eraseAt(i);
        } else {
            // Advance on the entry's axis. A long hitch (more than one
            // interval missed) → skip the missed batch, land on the grid.
            const double now = (e.axis == Axis::Beat) ? beatNow : wallNow;
            if (e.next < now - e.interval) realignRecurring(e, now);
            else                           e.next += e.interval;
            ++i;
        }
        if (fn) fn(arg);
    }
}

} // namespace livecode
} // namespace yawn
