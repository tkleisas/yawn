#pragma once

#include "audio/AudioBuffer.h"
#include <memory>
#include <string>
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

    int64_t effectiveLoopEnd() const {
        if (!buffer) return 0;
        return (loopEnd < 0) ? buffer->numFrames() : loopEnd;
    }

    int64_t lengthInFrames() const {
        return effectiveLoopEnd() - loopStart;
    }

    // Shallow copy (shares audio buffer)
    std::unique_ptr<Clip> clone() const {
        auto c = std::make_unique<Clip>();
        c->name = name;
        c->buffer = buffer;
        c->loopStart = loopStart;
        c->loopEnd = loopEnd;
        c->looping = looping;
        c->gain = gain;
        return c;
    }
};

// Runtime state of a clip being played (audio-thread owned)
struct ClipPlayState {
    const Clip* clip = nullptr;
    int64_t playPosition = 0;     // current position within clip buffer
    bool active = false;
    bool stopping = false;        // fade-out before stop

    float fadeGain = 1.0f;        // for fade-in/fade-out
    static constexpr float kFadeIncrement = 0.002f; // ~5ms at 44.1kHz per sample
};

} // namespace audio
} // namespace yawn
