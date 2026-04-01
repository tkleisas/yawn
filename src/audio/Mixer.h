#pragma once

#include "core/Constants.h"
#include "effects/EffectChain.h"
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

namespace yawn {
namespace audio {

enum class SendMode {
    PreFader,
    PostFader
};

struct Send {
    float level = 0.0f;           // 0.0 to 1.0
    SendMode mode = SendMode::PostFader;
    bool enabled = false;
};

struct ChannelStrip {
    float volume = 1.0f;          // 0.0 to ~2.0 (linear)
    float pan = 0.0f;             // -1.0 (L) to +1.0 (R)
    bool muted = false;
    bool soloed = false;

    // Peak metering (written by audio thread, read by UI)
    float peakL = 0.0f;
    float peakR = 0.0f;
};

struct TrackChannel : ChannelStrip {
    std::array<Send, kMaxSendsPerTrack> sends{};
};

struct ReturnBus : ChannelStrip {
    // Return buses have their own channel strip but no sends
};

struct MasterChannel {
    float volume = 1.0f;
    float peakL = 0.0f;
    float peakR = 0.0f;
};

// Mixer: full signal-flow processor.
//
// Signal flow per buffer:
//   1. ClipEngine renders each track into a separate buffer
//   2. For each track:
//      a. Tap pre-fader sends → accumulate into return bus buffers
//      b. Apply track volume + pan → track output
//      c. Tap post-fader sends → accumulate into return bus buffers
//      d. Sum track output into master bus
//   3. For each return bus:
//      a. Apply return volume + pan
//      b. Sum into master bus
//   4. Apply master volume
//
// Runs entirely on the audio thread.
class Mixer {
public:
    Mixer();

    void initEffectChains(double sampleRate, int maxBlockSize);
    void reset();

    // --- Track channel controls ---

    void setTrackVolume(int track, float vol) {
        if (inTrackRange(track))
            m_tracks[track].volume = clampVol(vol);
    }

    void setTrackPan(int track, float pan) {
        if (inTrackRange(track))
            m_tracks[track].pan = std::max(-1.0f, std::min(pan, 1.0f));
    }

    void setTrackMute(int track, bool muted) {
        if (inTrackRange(track))
            m_tracks[track].muted = muted;
    }

    void setTrackSolo(int track, bool soloed) {
        if (inTrackRange(track)) {
            m_tracks[track].soloed = soloed;
            updateSoloState();
        }
    }

    // --- Send controls ---

    void setSendLevel(int track, int sendIndex, float level) {
        if (inTrackRange(track) && inSendRange(sendIndex))
            m_tracks[track].sends[sendIndex].level = clampVol(level);
    }

    void setSendMode(int track, int sendIndex, SendMode mode) {
        if (inTrackRange(track) && inSendRange(sendIndex))
            m_tracks[track].sends[sendIndex].mode = mode;
    }

    void setSendEnabled(int track, int sendIndex, bool enabled) {
        if (inTrackRange(track) && inSendRange(sendIndex))
            m_tracks[track].sends[sendIndex].enabled = enabled;
    }

    // --- Return bus controls ---

    void setReturnVolume(int bus, float vol) {
        if (inReturnRange(bus))
            m_returns[bus].volume = clampVol(vol);
    }

    void setReturnPan(int bus, float pan) {
        if (inReturnRange(bus))
            m_returns[bus].pan = std::max(-1.0f, std::min(pan, 1.0f));
    }

    void setReturnMute(int bus, bool muted) {
        if (inReturnRange(bus))
            m_returns[bus].muted = muted;
    }

    // --- Master controls ---

    void setMasterVolume(float vol) { m_master.volume = clampVol(vol); }

    // --- Getters (with bounds safety) ---

    const TrackChannel& trackChannel(int track) const {
        return m_tracks[std::max(0, std::min(track, kMaxTracks - 1))];
    }
    const ReturnBus& returnBus(int bus) const {
        return m_returns[std::max(0, std::min(bus, kMaxReturnBuses - 1))];
    }
    const MasterChannel& master() const { return m_master; }
    bool anySoloed() const { return m_anySoloed; }

    // --- Effect chain access ---

    effects::EffectChain& trackEffects(int track) {
        return m_trackFx[std::max(0, std::min(track, kMaxTracks - 1))];
    }
    effects::EffectChain& returnEffects(int bus) {
        return m_returnFx[std::max(0, std::min(bus, kMaxReturnBuses - 1))];
    }
    effects::EffectChain& masterEffects() { return m_masterFx; }

    // --- Core processing ---
    //
    // trackBuffers: array of pointers to per-track interleaved stereo buffers
    //               (pre-zeroed, filled by ClipEngine::processTrackToBuffer)
    // output:       final interleaved stereo output buffer (will be cleared first)
    // numTracks:    how many tracks are active
    // numFrames:    frames per buffer
    // numChannels:  output channels (typically 2)

    void process(float* const* trackBuffers, int numTracks,
                 float* output, int numFrames, int numChannels);

    void decayPeaks();

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kPeakDecay = 0.95f;

    static bool inTrackRange(int t) { return t >= 0 && t < kMaxTracks; }
    static bool inSendRange(int s) { return s >= 0 && s < kMaxSendsPerTrack; }
    static bool inReturnRange(int r) { return r >= 0 && r < kMaxReturnBuses; }
    static float clampVol(float v) { return std::max(0.0f, std::min(v, 2.0f)); }

    bool isAudible(const ChannelStrip& ch) const;
    void updateSoloState();

    std::array<TrackChannel, kMaxTracks> m_tracks;
    std::array<ReturnBus, kMaxReturnBuses> m_returns;
    MasterChannel m_master;
    bool m_anySoloed = false;

    // Per-channel effect chains
    std::array<effects::EffectChain, kMaxTracks> m_trackFx;
    std::array<effects::EffectChain, kMaxReturnBuses> m_returnFx;
    effects::EffectChain m_masterFx;

    // Scratch buffers for return bus accumulation (heap-allocated, preallocated in ctor)
    static constexpr int kMaxBufferSize = 4096 * 2; // max frames * max channels
    std::vector<float> m_returnBufferHeap;  // kMaxReturnBuses * kMaxBufferSize
    float* m_returnBufPtrs[kMaxReturnBuses] = {};
};

} // namespace audio
} // namespace yawn
