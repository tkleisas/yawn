#pragma once

#include "audio/AudioBuffer.h"
#include <atomic>
#include <memory>

namespace yawn {
namespace audio {

class AudioEngine;

struct RenderConfig {
    double startBeat = 0.0;
    double endBeat = 0.0;
    int targetSampleRate = static_cast<int>(kDefaultSampleRate);
    int channels = 2;
};

struct RenderProgress {
    std::atomic<float> fraction{0.0f};
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> failed{false};
};

class OfflineRenderer {
public:
    // Renders the arrangement/session from startBeat to endBeat.
    // Stops the PortAudio stream, renders offline, then restarts it.
    // Progress is updated atomically for UI polling.
    // Returns the rendered audio buffer (interleaved stereo), or nullptr on failure.
    static std::shared_ptr<AudioBuffer> render(
        AudioEngine& engine,
        const RenderConfig& config,
        RenderProgress& progress);
};

} // namespace audio
} // namespace yawn
