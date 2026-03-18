#pragma once

#include "core/Constants.h"
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
    Mixer() {
        // Preallocate return bus scratch buffers on heap
        m_returnBufferHeap.resize(kMaxReturnBuses * kMaxBufferSize, 0.0f);
        for (int r = 0; r < kMaxReturnBuses; ++r) {
            m_returnBufPtrs[r] = m_returnBufferHeap.data() + r * kMaxBufferSize;
        }
        reset();
    }

    void reset() {
        for (auto& ch : m_tracks) ch = TrackChannel{};
        for (auto& rb : m_returns) rb = ReturnBus{};
        m_master = MasterChannel{};
        m_anySoloed = false;
    }

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

    // --- Core processing ---
    //
    // trackBuffers: array of pointers to per-track interleaved stereo buffers
    //               (pre-zeroed, filled by ClipEngine::processTrackToBuffer)
    // output:       final interleaved stereo output buffer (will be cleared first)
    // numTracks:    how many tracks are active
    // numFrames:    frames per buffer
    // numChannels:  output channels (typically 2)

    void process(float* const* trackBuffers, int numTracks,
                 float* output, int numFrames, int numChannels) {
        const int nc = numChannels;
        const int bufSize = numFrames * nc;

        // Clear output and return bus accumulators
        std::memset(output, 0, bufSize * sizeof(float));
        for (int r = 0; r < kMaxReturnBuses; ++r) {
            std::memset(m_returnBufPtrs[r], 0, bufSize * sizeof(float));
        }

        // Process each track
        for (int t = 0; t < numTracks && t < kMaxTracks; ++t) {
            if (!trackBuffers[t]) continue;

            auto& ch = m_tracks[t];
            bool audible = isAudible(ch);

            // --- Pre-fader sends ---
            for (int s = 0; s < kMaxSendsPerTrack; ++s) {
                auto& send = ch.sends[s];
                if (!send.enabled || send.level <= 0.0f) continue;
                if (send.mode != SendMode::PreFader) continue;

                float* dst = m_returnBufPtrs[s];
                const float* src = trackBuffers[t];
                for (int i = 0; i < bufSize; ++i) {
                    dst[i] += src[i] * send.level;
                }
            }

            // --- Track fader + pan ---
            if (audible) {
                float panAngle = (ch.pan + 1.0f) * 0.25f * kPi;
                float gainL = ch.volume * std::cos(panAngle);
                float gainR = ch.volume * std::sin(panAngle);

                float trackPeakL = 0.0f, trackPeakR = 0.0f;

                const float* src = trackBuffers[t];
                for (int i = 0; i < numFrames; ++i) {
                    float sL = src[i * nc + 0];
                    float sR = (nc > 1) ? src[i * nc + 1] : sL;

                    float outL = sL * gainL;
                    float outR = sR * gainR;

                    // --- Post-fader sends ---
                    for (int s = 0; s < kMaxSendsPerTrack; ++s) {
                        auto& send = ch.sends[s];
                        if (!send.enabled || send.level <= 0.0f) continue;
                        if (send.mode != SendMode::PostFader) continue;

                        m_returnBufPtrs[s][i * nc + 0] += outL * send.level;
                        if (nc > 1)
                            m_returnBufPtrs[s][i * nc + 1] += outR * send.level;
                    }

                    // Sum into master
                    output[i * nc + 0] += outL;
                    if (nc > 1) output[i * nc + 1] += outR;

                    trackPeakL = std::max(trackPeakL, std::abs(outL));
                    trackPeakR = std::max(trackPeakR, std::abs(outR));
                }

                ch.peakL = std::max(trackPeakL, ch.peakL * kPeakDecay);
                ch.peakR = std::max(trackPeakR, ch.peakR * kPeakDecay);
            } else {
                ch.peakL *= kPeakDecay;
                ch.peakR *= kPeakDecay;
            }
        }

        // --- Process return buses ---
        for (int r = 0; r < kMaxReturnBuses; ++r) {
            auto& rb = m_returns[r];
            if (rb.muted) {
                rb.peakL *= kPeakDecay;
                rb.peakR *= kPeakDecay;
                continue;
            }

            float panAngle = (rb.pan + 1.0f) * 0.25f * kPi;
            float gainL = rb.volume * std::cos(panAngle);
            float gainR = rb.volume * std::sin(panAngle);

            float retPeakL = 0.0f, retPeakR = 0.0f;
            const float* src = m_returnBufPtrs[r];

            for (int i = 0; i < numFrames; ++i) {
                float sL = src[i * nc + 0];
                float sR = (nc > 1) ? src[i * nc + 1] : sL;

                float outL = sL * gainL;
                float outR = sR * gainR;

                output[i * nc + 0] += outL;
                if (nc > 1) output[i * nc + 1] += outR;

                retPeakL = std::max(retPeakL, std::abs(outL));
                retPeakR = std::max(retPeakR, std::abs(outR));
            }

            rb.peakL = std::max(retPeakL, rb.peakL * kPeakDecay);
            rb.peakR = std::max(retPeakR, rb.peakR * kPeakDecay);
        }

        // --- Master volume + metering ---
        float mPeakL = 0.0f, mPeakR = 0.0f;
        for (int i = 0; i < numFrames; ++i) {
            output[i * nc + 0] *= m_master.volume;
            mPeakL = std::max(mPeakL, std::abs(output[i * nc + 0]));
            if (nc > 1) {
                output[i * nc + 1] *= m_master.volume;
                mPeakR = std::max(mPeakR, std::abs(output[i * nc + 1]));
            }
        }

        m_master.peakL = std::max(mPeakL, m_master.peakL * kPeakDecay);
        m_master.peakR = std::max(mPeakR, m_master.peakR * kPeakDecay);
    }

    void decayPeaks() {
        for (auto& ch : m_tracks) { ch.peakL *= kPeakDecay; ch.peakR *= kPeakDecay; }
        for (auto& rb : m_returns) { rb.peakL *= kPeakDecay; rb.peakR *= kPeakDecay; }
        m_master.peakL *= kPeakDecay;
        m_master.peakR *= kPeakDecay;
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kPeakDecay = 0.95f;

    static bool inTrackRange(int t) { return t >= 0 && t < kMaxTracks; }
    static bool inSendRange(int s) { return s >= 0 && s < kMaxSendsPerTrack; }
    static bool inReturnRange(int r) { return r >= 0 && r < kMaxReturnBuses; }
    static float clampVol(float v) { return std::max(0.0f, std::min(v, 2.0f)); }

    bool isAudible(const ChannelStrip& ch) const {
        if (ch.muted) return false;
        if (m_anySoloed && !ch.soloed) return false;
        return true;
    }

    void updateSoloState() {
        m_anySoloed = false;
        for (int i = 0; i < kMaxTracks; ++i) {
            if (m_tracks[i].soloed) { m_anySoloed = true; return; }
        }
    }

    std::array<TrackChannel, kMaxTracks> m_tracks;
    std::array<ReturnBus, kMaxReturnBuses> m_returns;
    MasterChannel m_master;
    bool m_anySoloed = false;

    // Scratch buffers for return bus accumulation (heap-allocated, preallocated in ctor)
    static constexpr int kMaxBufferSize = 4096 * 2; // max frames * max channels
    std::vector<float> m_returnBufferHeap;  // kMaxReturnBuses * kMaxBufferSize
    float* m_returnBufPtrs[kMaxReturnBuses] = {};
};

} // namespace audio
} // namespace yawn
