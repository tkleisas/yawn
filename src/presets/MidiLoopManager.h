#pragma once

// MidiLoopManager — paths, save helper, and category utilities for
// the MIDI loops library. Mirrors PresetManager's shape so the
// scanner / Browser / save-flow code reads the same way.
//
// Storage layout:
//   Windows:    %APPDATA%/YAWN/midi_loops/<category>/<sanitized-name>.mid
//   macOS/Linux: ~/.yawn/midi_loops/<category>/<sanitized-name>.mid
//
// The category subfolder is informational only — the library scanner
// reads the actual category from each row's DB metadata, so renaming a
// folder doesn't change classification by itself.

#include "midi/MidiClip.h"
#include <filesystem>
#include <string>
#include <vector>

namespace yawn {

class MidiLoopManager {
public:
    // Root directory for YAWN-managed loops (created on first call).
    static std::filesystem::path midiLoopsRootDir();

    // Save a MidiClip as an SMF under <root>/<category>/<sanitized-name>.mid.
    // Returns the absolute path written (empty path on error).
    static std::filesystem::path saveMidiLoop(const std::string& name,
                                              const std::string& category,
                                              const midi::MidiClip& clip,
                                              double tempoBPM   = 120.0,
                                              int    timeSigNum = 4,
                                              int    timeSigDen = 4);

    // Sanitize a name for use as a filename (strips illegal chars).
    static std::string sanitizeName(const std::string& name);

    // The supported categories. Always returns the canonical, ordered set.
    static const std::vector<std::string>& categories();

    // Is the given category one of the supported ones?
    static bool isValidCategory(const std::string& cat);

    // Heuristic auto-categorization from a clip's notes (used by the
    // scanner on import + as the default in the save dialog). Returns
    // one of the supported categories.
    static const char* autoCategory(const midi::MidiClip& clip);
};

} // namespace yawn
