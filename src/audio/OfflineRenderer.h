#pragma once

#include "audio/AudioBuffer.h"
#include <atomic>
#include <functional>
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
    //
    // onBlock, if set, is invoked on this thread just before each render block
    // with the transport beat at that block — used to drive non-audio-thread
    // modulation (e.g. macro/LFO param pushes) so the bounce matches live.
    static std::shared_ptr<AudioBuffer> render(
        AudioEngine& engine,
        const RenderConfig& config,
        RenderProgress& progress,
        const std::function<void(double beat)>& onBlock = {});
};

} // namespace audio
} // namespace yawn
