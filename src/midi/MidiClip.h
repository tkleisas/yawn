#pragma once

// MIDI clip data model — stores note events and CC automation.
// Uses high-resolution values (MIDI 2.0 scale) internally.
// Per-note expression data supports MPE workflows.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace yawn {
namespace midi {

// ---------------------------------------------------------------------------
// MidiNote — a note event in a MIDI clip
// ---------------------------------------------------------------------------
struct MidiNote {
    double   startBeat       = 0.0;    // Start time in beats from clip start
    double   duration         = 0.25;   // Duration in beats
    uint8_t  pitch            = 60;     // MIDI note number (0-127)
    uint8_t  channel          = 0;      // MIDI channel (0-15)
    uint16_t velocity         = 32512;  // 16-bit note-on velocity (MIDI 2.0 scale)
    uint16_t releaseVelocity  = 0;      // 16-bit note-off velocity (MIDI 2.0)

    // Per-note expression initial values (MPE / MIDI 2.0 per-note controllers)
    float    pressure         = 0.0f;   // 0.0-1.0 (poly aftertouch / per-note pressure)
    float    slide            = 0.5f;   // 0.0-1.0 (CC74 / timbre dimension)
    float    pitchBendOffset  = 0.0f;   // -1.0 to 1.0 (per-note pitch bend, in PB range units)

    bool operator<(const MidiNote& o) const {
        if (startBeat != o.startBeat) return startBeat < o.startBeat;
        return pitch < o.pitch;
    }
};

// ---------------------------------------------------------------------------
// MidiCCEvent — a CC automation point in a MIDI clip
// ---------------------------------------------------------------------------
struct MidiCCEvent {
    double   beat     = 0.0;     // Time in beats from clip start
    uint16_t ccNumber = 0;       // CC number
    uint32_t value    = 0;       // 32-bit value (MIDI 2.0 scale)
    uint8_t  channel  = 0;       // MIDI channel

    bool operator<(const MidiCCEvent& o) const { return beat < o.beat; }
};

// ---------------------------------------------------------------------------
// MidiClip — a clip containing MIDI note and CC data
// ---------------------------------------------------------------------------
class MidiClip {
public:
    MidiClip() = default;
    explicit MidiClip(double lengthBeats) : m_lengthBeats(lengthBeats) {}

    // ---- Notes ----

    void addNote(const MidiNote& note) {
        auto it = std::lower_bound(m_notes.begin(), m_notes.end(), note);
        m_notes.insert(it, note);
    }

    void removeNote(int index) {
        if (index >= 0 && index < static_cast<int>(m_notes.size()))
            m_notes.erase(m_notes.begin() + index);
    }

    int noteCount() const { return static_cast<int>(m_notes.size()); }
    const MidiNote& note(int i) const { return m_notes[i]; }
    MidiNote& note(int i) { return m_notes[i]; }

    // Get indices of notes whose startBeat falls in [startBeat, endBeat)
    void getNotesInRange(double startBeat, double endBeat,
                         std::vector<int>& outIndices) const {
        outIndices.clear();
        MidiNote key;
        key.startBeat = startBeat;
        auto it = std::lower_bound(m_notes.begin(), m_notes.end(), key,
            [](const MidiNote& n, const MidiNote& k) { return n.startBeat < k.startBeat; });

        int idx = static_cast<int>(it - m_notes.begin());
        while (idx < static_cast<int>(m_notes.size()) && m_notes[idx].startBeat < endBeat) {
            outIndices.push_back(idx);
            ++idx;
        }
    }

    // Get indices of notes that are sounding at a given beat (started before, not yet ended)
    void getActiveNotesAt(double beat, std::vector<int>& outIndices) const {
        outIndices.clear();
        for (int i = 0; i < static_cast<int>(m_notes.size()); ++i) {
            const auto& n = m_notes[i];
            if (n.startBeat <= beat && (n.startBeat + n.duration) > beat)
                outIndices.push_back(i);
        }
    }

    // ---- CC Automation ----

    void addCC(const MidiCCEvent& cc) {
        auto it = std::lower_bound(m_ccEvents.begin(), m_ccEvents.end(), cc);
        m_ccEvents.insert(it, cc);
    }

    void removeCC(int index) {
        if (index >= 0 && index < static_cast<int>(m_ccEvents.size()))
            m_ccEvents.erase(m_ccEvents.begin() + index);
    }

    int ccCount() const { return static_cast<int>(m_ccEvents.size()); }
    const MidiCCEvent& ccEvent(int i) const { return m_ccEvents[i]; }

    void getCCInRange(double startBeat, double endBeat,
                      std::vector<int>& outIndices) const {
        outIndices.clear();
        MidiCCEvent key;
        key.beat = startBeat;
        auto it = std::lower_bound(m_ccEvents.begin(), m_ccEvents.end(), key,
            [](const MidiCCEvent& e, const MidiCCEvent& k) { return e.beat < k.beat; });

        int idx = static_cast<int>(it - m_ccEvents.begin());
        while (idx < static_cast<int>(m_ccEvents.size()) && m_ccEvents[idx].beat < endBeat) {
            outIndices.push_back(idx);
            ++idx;
        }
    }

    // ---- Properties ----

    double lengthBeats() const { return m_lengthBeats; }
    void setLengthBeats(double beats) { m_lengthBeats = beats > 0 ? beats : 0.25; }

    bool loop() const { return m_loop; }
    void setLoop(bool l) { m_loop = l; }

    double loopStartBeat() const { return m_loopStartBeat; }
    void setLoopStartBeat(double b) { m_loopStartBeat = std::max(0.0, b); }

    const std::string& name() const { return m_name; }
    void setName(const std::string& n) { m_name = n; }

private:
    std::vector<MidiNote>    m_notes;       // Sorted by startBeat
    std::vector<MidiCCEvent> m_ccEvents;    // Sorted by beat
    double      m_lengthBeats = 4.0;
    bool        m_loop           = true;
    double      m_loopStartBeat  = 0.0;
    std::string m_name;
};

} // namespace midi
} // namespace yawn
