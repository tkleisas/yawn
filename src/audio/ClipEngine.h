#pragma once

#include "core/Constants.h"
#include "audio/Clip.h"
#include "audio/FollowAction.h"
#include "audio/Transport.h"
#include "audio/TimeStretcher.h"
#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace yawn {
namespace audio {

// Quantize mode for clip launching
enum class QuantizeMode {
    None,       // Launch immediately
    NextBeat,   // Quantize to next beat
    NextBar     // Quantize to next bar
};

// A pending clip launch waiting for quantization
struct PendingLaunch {
    int trackIndex = -1;
    int sceneIndex = -1;
    const Clip* clip = nullptr;   // nullptr means stop
    QuantizeMode quantizeMode = QuantizeMode::NextBar;
    bool valid = false;
    const std::vector<automation::AutomationLane>* clipAutomation = nullptr;
    FollowAction followAction;
};

// Per-track time stretcher state (one stretcher per channel, max 2 for stereo)
struct TrackStretcher {
    static constexpr int kMaxChannels = 2;
    static constexpr int kOutBufSize = 8192;
    TimeStretcher stretchers[kMaxChannels];
    WarpMode activeMode = WarpMode::Off;
    int numChannels = 0;
    bool initialized = false;

    // Intermediate output buffer per channel (heap-allocated on init)
    std::vector<float> outBuf[kMaxChannels];
    int outBufAvail[kMaxChannels]{};
    int outBufRead[kMaxChannels]{};

    // Fractional read position for pitch-shift resampling (post-stretch)
    double resamplePos[kMaxChannels]{};

    void init(double sampleRate, int blockSize, WarpMode mode, int channels) {
        auto algo = (mode == WarpMode::Tones || mode == WarpMode::Texture)
                    ? TimeStretcher::Algorithm::PhaseVocoder
                    : TimeStretcher::Algorithm::WSOLA;
        numChannels = std::min(channels, kMaxChannels);
        for (int ch = 0; ch < numChannels; ++ch) {
            stretchers[ch].init(sampleRate, blockSize, algo);
            outBuf[ch].resize(kOutBufSize, 0.0f);
        }
        activeMode = mode;
        initialized = true;
        for (int ch = 0; ch < kMaxChannels; ++ch) {
            outBufAvail[ch] = 0;
            outBufRead[ch] = 0;
            resamplePos[ch] = 0.0;
        }
    }

    void reset() {
        for (int ch = 0; ch < numChannels; ++ch)
            stretchers[ch].reset();
        for (int ch = 0; ch < kMaxChannels; ++ch) {
            outBufAvail[ch] = 0;
            outBufRead[ch] = 0;
            resamplePos[ch] = 0.0;
        }
    }

    void setSpeedRatio(double ratio) {
        for (int ch = 0; ch < numChannels; ++ch)
            stretchers[ch].setSpeedRatio(ratio);
    }
};

// ClipEngine: manages playback of clips across multiple tracks.
// Runs entirely on the audio thread. The UI communicates via the
// AudioEngine's command queue.
class ClipEngine {
public:
    ClipEngine() = default;

    void setTransport(Transport* transport) { m_transport = transport; }
    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }
    void setQuantizeMode(QuantizeMode mode) { m_quantizeMode = mode; }
    QuantizeMode quantizeMode() const { return m_quantizeMode; }

    // Schedule a clip to launch on a track (called from audio thread after command processing)
    void scheduleClip(int trackIndex, int sceneIndex, const Clip* clip,
                      QuantizeMode quantize = QuantizeMode::NextBar,
                      const std::vector<automation::AutomationLane>* clipAutomation = nullptr,
                      const FollowAction& followAction = FollowAction{});

    // Schedule a track to stop
    void scheduleStop(int trackIndex,
                      QuantizeMode quantize = QuantizeMode::NextBar);

    // Process one audio buffer worth of frames. Writes into output (interleaved stereo).
    void process(float* output, int numFrames, int numChannels);

    // Render a single track into its own buffer (for per-track mixer routing).
    // Buffer must be pre-zeroed. Only renders if the track is active.
    void processTrackToBuffer(int trackIndex, float* buffer, int numFrames, int numChannels);

    // Check and fire pending quantized launches. Call once per buffer before processing tracks.
    void checkAndFirePending();

    // Query state
    bool isTrackPlaying(int trackIndex) const;
    const ClipPlayState& trackState(int trackIndex) const { return m_tracks[trackIndex]; }

    // Follow action callback: called when a follow action triggers
    // Parameters: trackIndex, sceneIndex, resolvedAction
    using FollowActionCallback = std::function<void(int, int, FollowActionType)>;
    void setFollowActionCallback(FollowActionCallback cb) { m_followActionCb = std::move(cb); }

    void removeTrackSlot(int index, int last) {
        for (int i = index; i < last; ++i) {
            m_tracks[i] = std::move(m_tracks[i + 1]);
            m_pending[i] = std::move(m_pending[i + 1]);
            m_stretchers[i] = std::move(m_stretchers[i + 1]);
        }
        m_tracks[last] = {};
        m_pending[last] = {};
        m_stretchers[last] = {};
    }

private:
    void checkPendingLaunches();
    void checkFollowActions();
    void processTrack(int trackIndex, float* output, int numFrames, int numChannels);
    void processTrackStretched(int trackIndex, float* output, int numFrames, int numChannels);
    double computeLocalSpeedRatio(const Clip& clip, double samplePos, double projectBPM) const;

    Transport* m_transport = nullptr;
    double m_sampleRate = kDefaultSampleRate;
    QuantizeMode m_quantizeMode = QuantizeMode::NextBar;

    std::array<ClipPlayState, kMaxTracks> m_tracks{};
    std::array<PendingLaunch, kMaxTracks> m_pending{};
    std::array<TrackStretcher, kMaxTracks> m_stretchers{};

    int64_t m_lastQuantizeCheck = -1;
    FollowActionCallback m_followActionCb;
    std::mt19937 m_rng{std::random_device{}()};
};

} // namespace audio
} // namespace yawn
