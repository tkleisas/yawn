#pragma once

// MelodicPatterns — factory bass / lead / chord / misc loop generator.
// Companion to DrumPatterns: each genre's melodic loops borrow the
// rhythmic feel of that genre's drum groove (bass follows the kick,
// chords hit the off-beats / stabs, etc.) and lay in-key pitch content
// from a per-genre scale + chord progression.
//
// Loops are written as SMFs under <midi_loops>/{bass,lead,chord,misc}/
// on channel 0. Seeding is idempotent: a file is only written if it
// does not already exist, so re-runs and user deletions are respected.

namespace yawn {

class MelodicPatterns {
public:
    // Write any missing factory melodic loops to the library.
    // Returns the number of new files written this call.
    static int seedFactoryLoops();
};

} // namespace yawn
