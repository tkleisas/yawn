#pragma once

#include "audio/Clip.h"
#include "audio/Transport.h"
#include <array>
#include <cstdint>

namespace yawn {
namespace audio {

// Maximum number of tracks that can play clips simultaneously
static constexpr int kMaxTracks = 32;

// Quantize mode for clip launching
enum class QuantizeMode {
    None,       // Launch immediately
    NextBeat,   // Quantize to next beat
    NextBar     // Quantize to next bar (4 beats in 4/4)
};

// A pending clip launch waiting for quantization
struct PendingLaunch {
    int trackIndex = -1;
    const Clip* clip = nullptr;   // nullptr means stop
    bool valid = false;
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
    void scheduleClip(int trackIndex, const Clip* clip);

    // Schedule a track to stop
    void scheduleStop(int trackIndex);

    // Process one audio buffer worth of frames. Writes into output (interleaved stereo).
    void process(float* output, int numFrames, int numChannels);

    // Query state
    bool isTrackPlaying(int trackIndex) const;
    const ClipPlayState& trackState(int trackIndex) const { return m_tracks[trackIndex]; }

private:
    void checkPendingLaunches();
    void processTrack(int trackIndex, float* output, int numFrames, int numChannels);

    Transport* m_transport = nullptr;
    double m_sampleRate = 44100.0;
    QuantizeMode m_quantizeMode = QuantizeMode::NextBar;

    std::array<ClipPlayState, kMaxTracks> m_tracks{};
    std::array<PendingLaunch, kMaxTracks> m_pending{};

    int64_t m_lastQuantizeCheck = -1;
};

} // namespace audio
} // namespace yawn
