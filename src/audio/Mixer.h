#pragma once

#include "core/Constants.h"
#include "effects/EffectChain.h"
#include <array>
#include <atomic>
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

// 3-band energy analyzer for visual reactivity. Three 2nd-order biquads
// in parallel (LP ~200Hz, BP ~800Hz, HP ~2kHz). Fed from mono mix each
// block. Peaks are written by the audio thread, read by the UI/visual
// thread (torn-read safe at 32 bits — same pattern as peakL/peakR).
struct BandAnalyzer {
    // Coefficients — initialised once from sample rate.
    float lpB0 = 0, lpB1 = 0, lpB2 = 0, lpA1 = 0, lpA2 = 0;
    float bpB0 = 0, bpB1 = 0, bpB2 = 0, bpA1 = 0, bpA2 = 0;
    float hpB0 = 0, hpB1 = 0, hpB2 = 0, hpA1 = 0, hpA2 = 0;
    bool  coeffsReady = false;

    // Per-filter state (transposed direct form II).
    float lpZ1 = 0, lpZ2 = 0;
    float bpZ1 = 0, bpZ2 = 0;
    float hpZ1 = 0, hpZ2 = 0;

    // Latest per-band peak. Envelope-follower style — audio thread writes
    // the max of the block; UI thread reads and may smooth further.
    float peakLow  = 0.0f;
    float peakMid  = 0.0f;
    float peakHigh = 0.0f;

    // True when something visual is listening. Lets the audio thread skip
    // the biquads on unwatched channels. Plain bool — torn reads are safe
    // because the cost of one wrong block is a single frame of stale data.
    bool wired = false;

    void initCoeffs(double sr);
    void feedBlock(const float* interleaved, int nFrames, int nCh, float decay);
    void applyDecay(float decay);
    void reset();
};

struct ChannelStrip {
    float volume = 1.0f;          // 0.0 to ~2.0 (linear)
    float pan = 0.0f;             // -1.0 (L) to +1.0 (R)
    bool muted = false;
    bool soloed = false;

    // Peak metering (written by audio thread, read by UI)
    float peakL = 0.0f;
    float peakR = 0.0f;

    BandAnalyzer bands;
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

    BandAnalyzer bands;
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

    void removeTrackSlot(int index, int last) {
        for (int i = index; i < last; ++i) {
            m_tracks[i] = std::move(m_tracks[i + 1]);
            m_trackFx[i] = std::move(m_trackFx[i + 1]);
        }
        m_tracks[last] = TrackChannel{};
        m_trackFx[last] = effects::EffectChain{};
        updateSoloState();
    }

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

public:
    // ── Visual-engine sample tap ───────────────────────────────────────
    // Ring buffer of the last kVisTapSize mono samples of master output,
    // fed each block. Used by VisualEngine to compute the Shadertoy FFT
    // texture. Writes happen on the audio thread; reads on the UI thread
    // — torn reads are acceptable because an FFT window smooths them out.
    // Wire exactly the passed set of analysis sources — -1 = master,
    // 0..N-1 = track. Everything else gets wired=false so we skip the
    // biquads on it. Safe to call while the audio thread runs: reads of
    // `wired` are plain bool loads — a single torn frame of stale data
    // has no visible effect.
    void setVisualAudioSources(const std::vector<int>& sources) {
        bool masterWired = false;
        std::array<bool, kMaxTracks> trackWired{};
        for (int s : sources) {
            if (s < 0) masterWired = true;
            else if (s < kMaxTracks) trackWired[s] = true;
        }
        m_master.bands.wired = masterWired;
        for (int i = 0; i < kMaxTracks; ++i)
            m_tracks[i].bands.wired = trackWired[i];
    }

    static constexpr int kVisTapSize = 1024;
    void readVisualSamples(float* out, int n) const {
        if (n > kVisTapSize) n = kVisTapSize;
        const uint32_t wp = m_visWritePos.load(std::memory_order_acquire);
        // Copy the n most-recent samples, newest last.
        for (int i = 0; i < n; ++i) {
            int idx = static_cast<int>((wp + kVisTapSize - n + i) % kVisTapSize);
            out[i] = m_visSamples[idx];
        }
    }

private:
    std::array<float, kVisTapSize> m_visSamples{};
    std::atomic<uint32_t>          m_visWritePos{0};
};

} // namespace audio
} // namespace yawn
