#pragma once

#include "audio/AudioBuffer.h"
#include "audio/WarpMarker.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace yawn {
namespace audio {

// Clip: a reference to audio data with playback parameters.
// The AudioBuffer is shared (loaded once, referenced by multiple clips).
struct Clip {
    std::string name;
    std::shared_ptr<AudioBuffer> buffer;

    // Loop region (in frames, relative to buffer start)
    int64_t loopStart = 0;
    int64_t loopEnd = -1;   // -1 means end of buffer
    bool looping = true;

    float gain = 1.0f;

    // Audio warping
    WarpMode warpMode = WarpMode::Off;
    double originalBPM = 0.0;   // 0 = unknown/not detected
    std::vector<WarpMarker> warpMarkers;
    std::vector<int64_t> transients;

    int64_t effectiveLoopEnd() const {
        if (!buffer) return 0;
        return (loopEnd < 0) ? buffer->numFrames() : loopEnd;
    }

    int64_t lengthInFrames() const {
        return effectiveLoopEnd() - loopStart;
    }

    // Compute speed ratio to play this clip at targetBPM.
    // Returns 1.0 if warping is off or originalBPM is unknown.
    double warpSpeedRatio(double targetBPM) const {
        if (warpMode == WarpMode::Off || originalBPM <= 0.0 || targetBPM <= 0.0)
            return 1.0;
        return originalBPM / targetBPM;
    }

    // Shallow copy (shares audio buffer, copies warp data)
    std::unique_ptr<Clip> clone() const {
        auto c = std::make_unique<Clip>();
        c->name = name;
        c->buffer = buffer;
        c->loopStart = loopStart;
        c->loopEnd = loopEnd;
        c->looping = looping;
        c->gain = gain;
        c->warpMode = warpMode;
        c->originalBPM = originalBPM;
        c->warpMarkers = warpMarkers;
        c->transients = transients;
        return c;
    }
};

// Runtime state of a clip being played (audio-thread owned)
struct ClipPlayState {
    const Clip* clip = nullptr;
    int64_t playPosition = 0;     // current position within clip buffer
    double fractionalPosition = 0.0; // sub-sample position for warped playback
    bool active = false;
    bool stopping = false;        // fade-out before stop

    float fadeGain = 1.0f;        // for fade-in/fade-out
    static constexpr float kFadeIncrement = 0.002f; // ~5ms at 44.1kHz per sample
};

} // namespace audio
} // namespace yawn
