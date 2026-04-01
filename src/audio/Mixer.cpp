// Mixer.cpp — method implementations split from Mixer.h.

#include "Mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace yawn {
namespace audio {

Mixer::Mixer() {
    // Preallocate return bus scratch buffers on heap
    m_returnBufferHeap.resize(kMaxReturnBuses * kMaxBufferSize, 0.0f);
    for (int r = 0; r < kMaxReturnBuses; ++r) {
        m_returnBufPtrs[r] = m_returnBufferHeap.data() + r * kMaxBufferSize;
    }
    reset();
}

void Mixer::initEffectChains(double sampleRate, int maxBlockSize) {
    for (auto& chain : m_trackFx)  chain.init(sampleRate, maxBlockSize);
    for (auto& chain : m_returnFx) chain.init(sampleRate, maxBlockSize);
    m_masterFx.init(sampleRate, maxBlockSize);
}

void Mixer::reset() {
    for (auto& ch : m_tracks) ch = TrackChannel{};
    for (auto& rb : m_returns) rb = ReturnBus{};
    m_master = MasterChannel{};
    m_anySoloed = false;
}

bool Mixer::isAudible(const ChannelStrip& ch) const {
    if (ch.muted) return false;
    if (m_anySoloed && !ch.soloed) return false;
    return true;
}

void Mixer::updateSoloState() {
    m_anySoloed = false;
    for (int i = 0; i < kMaxTracks; ++i) {
        if (m_tracks[i].soloed) { m_anySoloed = true; return; }
    }
}

void Mixer::process(float* const* trackBuffers, int numTracks,
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

        // --- Track insert effects (pre-fader) ---
        if (!m_trackFx[t].empty())
            m_trackFx[t].process(trackBuffers[t], numFrames, numChannels);

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
        // Return insert effects (e.g., reverb/delay on send bus)
        if (!m_returnFx[r].empty())
            m_returnFx[r].process(m_returnBufPtrs[r], numFrames, numChannels);

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

    // --- Master insert effects (pre-fader) ---
    if (!m_masterFx.empty())
        m_masterFx.process(output, numFrames, numChannels);

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

void Mixer::decayPeaks() {
    for (auto& ch : m_tracks) { ch.peakL *= kPeakDecay; ch.peakR *= kPeakDecay; }
    for (auto& rb : m_returns) { rb.peakL *= kPeakDecay; rb.peakR *= kPeakDecay; }
    m_master.peakL *= kPeakDecay;
    m_master.peakR *= kPeakDecay;
}

} // namespace audio
} // namespace yawn
