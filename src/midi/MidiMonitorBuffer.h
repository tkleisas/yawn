#pragma once

// MidiMonitorBuffer — lock-free ring buffer for MIDI monitor display.
// Stores timestamped MIDI events from all ports for debugging/visualization.
// ~2 MB fixed allocation. Multiple producer threads (audio + RtMidi
// callback), single UI reader.

#include "midi/MidiTypes.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <chrono>

namespace yawn {
namespace midi {

struct MidiMonitorEntry {
    enum class Type : uint8_t {
        NoteOn, NoteOff, CC, ProgramChange, PitchBend,
        ChannelPressure, PolyPressure, Clock, Start, Stop,
        Continue, SysEx, Other
    };

    Type     type      = Type::Other;
    uint8_t  channel   = 0;     // 0-15
    uint8_t  data1     = 0;     // note, cc#, program#
    uint8_t  data2     = 0;     // velocity (7-bit), cc value (7-bit)
    uint8_t  portIndex = 0;     // which input port
    uint8_t  _pad[3]   = {};
    uint16_t pitchBend = 0x2000; // 14-bit pitch bend (center = 0x2000)
    uint16_t _pad2     = 0;
    uint32_t timestamp = 0;     // milliseconds since monitor start
    // SysEx: first 8 bytes of payload (for display)
    uint8_t  sysexHead[8] = {};
    uint8_t  sysexLen  = 0;     // actual full length (capped display at 8)
    uint8_t  _pad3[3]  = {};
};
// 32 bytes per entry

static_assert(sizeof(MidiMonitorEntry) <= 32, "MidiMonitorEntry should be compact");

// ~2 MB buffer: 2097152 / 32 = 65536 entries. Use 65536 (power of 2).
static constexpr size_t kMonitorCapacity = 65536;

class MidiMonitorBuffer {
public:
    MidiMonitorBuffer() {
        m_startTime = std::chrono::steady_clock::now();
    }

    // Callable from multiple producer threads (audio thread + RtMidi
    // callback thread for controller-claimed ports). We reserve a
    // unique slot via fetch_add, then write it. Two producers never
    // race on the same slot. The reader may briefly observe a slot that
    // was just reserved but not yet fully written — acceptable tradeoff
    // for a debug monitor; entries are 32 bytes so the window is tiny.
    //
    // Never blocks. Old entries are simply overwritten when the reader
    // is more than kMonitorCapacity behind.
    void push(const MidiMonitorEntry& entry) {
        const size_t slot = m_head.fetch_add(1, std::memory_order_acq_rel);
        m_buffer[slot & kMask] = entry;
    }

    // Called from UI thread (single consumer).
    // Returns the total number of entries ever written (monotonic counter).
    size_t headIndex() const {
        return m_head.load(std::memory_order_acquire);
    }

    // Oldest valid index (head may have wrapped past old entries)
    size_t oldestValid() const {
        size_t h = headIndex();
        if (h <= kMonitorCapacity) return 0;
        return h - kMonitorCapacity;
    }

    // Read entry at absolute index. Caller must ensure index is in [oldestValid, headIndex).
    const MidiMonitorEntry& at(size_t absIndex) const {
        return m_buffer[absIndex & kMask];
    }

    // Number of entries currently available
    size_t count() const {
        size_t h = headIndex();
        size_t oldest = (h <= kMonitorCapacity) ? 0 : (h - kMonitorCapacity);
        return h - oldest;
    }

    // Milliseconds since monitor started
    uint32_t elapsedMs() const {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count());
    }

    void clear();

    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool e) { m_enabled.store(e, std::memory_order_relaxed); }

private:
    static constexpr size_t kMask = kMonitorCapacity - 1;
    std::array<MidiMonitorEntry, kMonitorCapacity> m_buffer;
    alignas(64) std::atomic<size_t> m_head{0};
    std::chrono::steady_clock::time_point m_startTime;
    std::atomic<bool> m_enabled{true};
};

// Helper: convert MidiMessage to MidiMonitorEntry
MidiMonitorEntry makeMonitorEntry(const struct MidiMessage& msg,
                                  uint8_t portIdx,
                                  uint32_t timestampMs);

// Helper: convert raw MIDI bytes to MidiMonitorEntry. Used by consumers
// that bypass MidiEngine's MidiMessage parser — controller-claimed ports
// read raw bytes from RtMidi and don't have a MidiMessage struct handy.
MidiMonitorEntry makeMonitorEntryFromRaw(const uint8_t* data, int length,
                                         uint8_t portIdx,
                                         uint32_t timestampMs);

} // namespace midi
} // namespace yawn
