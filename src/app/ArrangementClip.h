#pragma once

#include "audio/Clip.h"
#include "midi/MidiClip.h"
#include <memory>
#include <string>

namespace yawn {

// A clip placed on the arrangement timeline at a specific position.
// Holds a shared reference to audio or MIDI data (shared with session clips).
struct ArrangementClip {
    enum class Type : uint8_t { Audio, Midi };

    Type type = Type::Audio;
    double startBeat = 0.0;       // Absolute position on timeline (beats)
    double lengthBeats = 4.0;     // Duration on timeline (beats)
    double offsetBeats = 0.0;     // Trim offset into source clip (beats)

    // Shared clip data — same underlying data as session clip slots
    std::shared_ptr<audio::AudioBuffer> audioBuffer;
    std::shared_ptr<midi::MidiClip> midiClip;

    std::string name;
    int colorIndex = -1;          // -1 = use track color

    double endBeat() const { return startBeat + lengthBeats; }

    // Check if this clip overlaps a time range
    bool overlaps(double from, double to) const {
        return startBeat < to && endBeat() > from;
    }
};

} // namespace yawn
