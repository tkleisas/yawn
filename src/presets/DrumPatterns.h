#pragma once

// DrumPatterns — factory drum-loop generator. On first run it seeds the
// YAWN-managed MIDI loops library with a construction-kit of GM drum
// patterns (rock, funk, disco, house, techno, waltz, hip-hop, breaks,
// dnb, idm). Each pattern is written as an SMF under
// <midi_loops>/drums/<name>.mid, on GM drum channel 9.
//
// Seeding is idempotent: a pattern file is only written if it does not
// already exist, so re-runs (and user deletions) are respected.

namespace yawn {

class DrumPatterns {
public:
    // Write any missing factory drum loops to the library.
    // Returns the number of new files written this call.
    static int seedFactoryLoops();
};

} // namespace yawn
