#pragma once

#include "audio/Clip.h"
#include "midi/MidiClip.h"
#include "visual/VisualClip.h"
#include <memory>
#include <string>

namespace yawn {

// A clip placed on the arrangement timeline at a specific position.
// Holds a shared reference to audio or MIDI data (shared with session
// clips). Visual clips store an owned VisualClip — configs are small
// and don't need cross-referencing with the session slot.
struct ArrangementClip {
    enum class Type : uint8_t { Audio, Midi, Visual };

    Type type = Type::Audio;
    double startBeat = 0.0;       // Absolute position on timeline (beats)
    double lengthBeats = 4.0;     // Duration on timeline (beats)
    double offsetBeats = 0.0;     // Trim offset into source clip (beats)

    // Shared clip data — same underlying data as session clip slots
    std::shared_ptr<audio::AudioBuffer> audioBuffer;
    std::shared_ptr<midi::MidiClip> midiClip;
    // Owned VisualClip — cloned from a session slot when placed, so
    // subsequent edits on either side stay independent.
    std::unique_ptr<visual::VisualClip> visualClip;

    std::string name;
    int colorIndex = -1;          // -1 = use track color

    double endBeat() const { return startBeat + lengthBeats; }

    // Check if this clip overlaps a time range
    bool overlaps(double from, double to) const {
        return startBeat < to && endBeat() > from;
    }

    // Copy ops deep-clone the VisualClip via its clone() method so
    // ArrangementClip stays copyable (existing callers rely on that).
    // audioBuffer / midiClip remain shared_ptr — copying shares the
    // underlying data, which is the intended behaviour.
    ArrangementClip() = default;
    ArrangementClip(ArrangementClip&&) = default;
    ArrangementClip& operator=(ArrangementClip&&) = default;
    ArrangementClip(const ArrangementClip& o)
        : type(o.type),
          startBeat(o.startBeat),
          lengthBeats(o.lengthBeats),
          offsetBeats(o.offsetBeats),
          audioBuffer(o.audioBuffer),
          midiClip(o.midiClip),
          visualClip(o.visualClip ? o.visualClip->clone() : nullptr),
          name(o.name),
          colorIndex(o.colorIndex) {}
    ArrangementClip& operator=(const ArrangementClip& o) {
        if (this == &o) return *this;
        type         = o.type;
        startBeat    = o.startBeat;
        lengthBeats  = o.lengthBeats;
        offsetBeats  = o.offsetBeats;
        audioBuffer  = o.audioBuffer;
        midiClip     = o.midiClip;
        visualClip   = o.visualClip ? o.visualClip->clone() : nullptr;
        name         = o.name;
        colorIndex   = o.colorIndex;
        return *this;
    }
};

} // namespace yawn
