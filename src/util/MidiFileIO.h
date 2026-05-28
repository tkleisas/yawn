#pragma once

// MidiFileIO — minimal Standard MIDI File (SMF) reader / writer for
// the MIDI loops library. Lives next to FileIO (audio) so the two
// asset-type loaders sit side-by-side.
//
// Scope: SMF type 0 and type 1, PPQ division only (no SMPTE). Parses
// note-on / note-off / CC, tempo and time-signature meta events; skips
// the rest. Writer emits type 0 with PPQ=480 + tempo + time-signature
// + end-of-track. This is the minimum needed to round-trip
// construction-kit drum/bass/lead loops; we deliberately don't try to
// preserve every meta event a DAW might emit.

#include "midi/MidiClip.h"
#include <filesystem>
#include <memory>

namespace yawn {
namespace util {

// Tempo + time-signature surfaced to the caller (the library scanner
// uses these to populate the loop's DB record).
struct MidiFileInfo {
    double tempoBPM   = 120.0;
    int    timeSigNum = 4;
    int    timeSigDen = 4;
};

// Read an SMF file into a MidiClip. All notes' startBeat / duration are
// in beats; channel is preserved. Returns nullptr on any parse error
// (invalid header, truncated track, etc.). `info` is optional.
std::unique_ptr<midi::MidiClip> loadMidiFile(const std::filesystem::path& path,
                                              MidiFileInfo* info = nullptr);

// Write a MidiClip as SMF type 0. Emits a single track containing a
// tempo event, a time-signature event, the clip's note-on/off pairs in
// chronological order, and end-of-track. Returns true on success.
bool saveMidiFile(const std::filesystem::path& path,
                  const midi::MidiClip& clip,
                  double tempoBPM   = 120.0,
                  int    timeSigNum = 4,
                  int    timeSigDen = 4);

} // namespace util
} // namespace yawn
