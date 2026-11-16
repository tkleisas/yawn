#pragma once

// DrumKits — curated factory presets for the drum modules.
//
// DrumSynth kits are small JSON presets (parameter sets). The DrumRack
// kit is rendered once from the Acoustic DrumSynth preset into one-shot
// WAVs and saved as a normal rack preset (per-pad samples + params).
// Seeding is idempotent: it only writes kit names that don't exist.

namespace yawn {

class DrumKits {
public:
    // Returns the number of presets written.
    static int seedFactoryKits();
};

} // namespace yawn
