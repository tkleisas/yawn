#include "audio/OfflineRenderer.h"
#include "audio/AudioEngine.h"
#include "util/Logger.h"
#include <vector>
#include <cstring>

namespace yawn {
namespace audio {

std::shared_ptr<AudioBuffer> OfflineRenderer::render(
    AudioEngine& engine,
    const RenderConfig& config,
    RenderProgress& progress)
{
    progress.fraction.store(0.0f);
    progress.done.store(false);
    progress.failed.store(false);

    const double sampleRate = engine.sampleRate();
    const int channels = config.channels;
    const int framesPerBuffer = static_cast<int>(engine.config().framesPerBuffer);

    // Calculate total frames to render
    double samplesPerBeat = sampleRate * 60.0 / engine.transport().bpm();
    int64_t totalFrames = static_cast<int64_t>(
        (config.endBeat - config.startBeat) * samplesPerBeat);

    if (totalFrames <= 0) {
        LOG_ERROR("Render", "Nothing to render (%.1f - %.1f beats)",
                  config.startBeat, config.endBeat);
        progress.failed.store(true);
        progress.done.store(true);
        return nullptr;
    }

    LOG_INFO("Render", "Offline render: %.1f-%.1f beats, %lld frames at %.0f Hz",
             config.startBeat, config.endBeat,
             static_cast<long long>(totalFrames), sampleRate);

    // Stop PortAudio stream
    bool wasRunning = engine.isRunning();
    if (wasRunning) {
        engine.stop();
    }

    // Save transport state
    int64_t savedPosition = engine.transport().positionInSamples();
    bool savedPlaying = engine.transport().isPlaying();
    bool savedLoop = engine.transport().isLoopEnabled();

    // Configure transport for rendering
    engine.transport().setLoopEnabled(false);
    int64_t startSample = static_cast<int64_t>(config.startBeat * samplesPerBeat);
    engine.transport().setPositionInSamples(startSample);
    engine.transport().play();

    // Allocate output buffer
    auto result = std::make_shared<AudioBuffer>(channels, static_cast<int>(totalFrames));

    // Temp buffer for each processAudio call (interleaved)
    std::vector<float> tempBuf(static_cast<size_t>(framesPerBuffer) * channels, 0.0f);

    int64_t framesRendered = 0;
    while (framesRendered < totalFrames) {
        if (progress.cancelled.load()) {
            LOG_INFO("Render", "Render cancelled at %.1f%%",
                     progress.fraction.load() * 100.0f);
            break;
        }

        int framesToRender = static_cast<int>(
            (std::min)(static_cast<int64_t>(framesPerBuffer),
                       totalFrames - framesRendered));

        std::memset(tempBuf.data(), 0,
                    static_cast<size_t>(framesToRender) * channels * sizeof(float));

        engine.renderBuffer(tempBuf.data(),
                            static_cast<unsigned long>(framesToRender));

        // De-interleave into output AudioBuffer
        for (int ch = 0; ch < channels; ++ch) {
            float* dst = result->channelData(ch);
            for (int i = 0; i < framesToRender; ++i) {
                dst[framesRendered + i] =
                    tempBuf[static_cast<size_t>(i) * channels + ch];
            }
        }

        framesRendered += framesToRender;
        progress.fraction.store(
            static_cast<float>(framesRendered) / static_cast<float>(totalFrames));
    }

    // Restore transport state
    engine.transport().stop();
    engine.transport().setPositionInSamples(savedPosition);
    engine.transport().setLoopEnabled(savedLoop);
    if (savedPlaying) {
        engine.transport().play();
    }

    // Restart PortAudio stream
    if (wasRunning) {
        engine.start();
    }

    if (progress.cancelled.load()) {
        progress.done.store(true);
        return nullptr;
    }

    LOG_INFO("Render", "Offline render complete: %lld frames",
             static_cast<long long>(framesRendered));

    progress.fraction.store(1.0f);
    progress.done.store(true);
    return result;
}

} // namespace audio
} // namespace yawn
