#pragma once

#include <vector>
#include <cstdint>

namespace yawn {
namespace audio {

// A warp marker maps an original audio position to a target beat position.
struct WarpMarker {
    int64_t samplePosition;   // position in original audio (frames)
    double beatPosition;       // target beat position in project
};

// Warp mode for audio clips
enum class WarpMode : uint8_t {
    Off,         // No warping — play at original speed
    Auto,        // Auto-select algorithm based on content
    Beats,       // WSOLA optimized for rhythmic content
    Tones,       // Phase vocoder for tonal content
    Texture,     // Phase vocoder with reduced transient preservation
    TonesPGHI,   // Phase Vocoder Done Right (PGHI / Prusa-Holighaus 2017)
                 //   — preserves vertical phase coherence, less
                 //   "phasiness" on tonal material than the classic
                 //   PV. Slightly higher CPU cost; same latency as
                 //   classic PV. Per-clip choice — leave existing
                 //   projects on Tones / Texture untouched.
    Repitch      // Simple resampling (changes pitch with tempo)
};

} // namespace audio
} // namespace yawn
