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
    Off,        // No warping — play at original speed
    Auto,       // Auto-select algorithm based on content
    Beats,      // WSOLA optimized for rhythmic content
    Tones,      // Phase vocoder for tonal content
    Texture,    // Phase vocoder with reduced transient preservation
    Repitch     // Simple resampling (changes pitch with tempo)
};

} // namespace audio
} // namespace yawn
