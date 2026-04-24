// Mixer.cpp — method implementations split from Mixer.h.

#include "Mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// MSVC doesn't define M_PI by default. Local definition keeps this
// translation unit self-contained without polluting a header.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace audio {

// ── BandAnalyzer ──────────────────────────────────────────────────────────
//
// RBJ cookbook biquads. Fixed crossover points chosen for musical content:
//   Low  : low-pass @ 200 Hz
//   Mid  : band-pass @ 800 Hz (Q=1)
//   High : high-pass @ 2 kHz
// The bands overlap in the crossover regions — fine for visual reactivity.

static constexpr double kLowHz  = 200.0;
static constexpr double kMidHz  = 800.0;
static constexpr double kHighHz = 2000.0;
static constexpr double kQ      = 0.707;  // Butterworth-ish

static void rbjLowpass(double sr, double f, double q,
                        float& b0, float& b1, float& b2, float& a1, float& a2) {
    const double w0 = 2.0 * M_PI * f / sr;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = static_cast<float>((1.0 - cw) / 2.0 / a0);
    b1 = static_cast<float>((1.0 - cw) / a0);
    b2 = static_cast<float>((1.0 - cw) / 2.0 / a0);
    a1 = static_cast<float>(-2.0 * cw / a0);
    a2 = static_cast<float>((1.0 - alpha) / a0);
}

static void rbjBandpass(double sr, double f, double q,
                         float& b0, float& b1, float& b2, float& a1, float& a2) {
    const double w0 = 2.0 * M_PI * f / sr;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = static_cast<float>(alpha / a0);
    b1 = 0.0f;
    b2 = static_cast<float>(-alpha / a0);
    a1 = static_cast<float>(-2.0 * cw / a0);
    a2 = static_cast<float>((1.0 - alpha) / a0);
}

static void rbjHighpass(double sr, double f, double q,
                         float& b0, float& b1, float& b2, float& a1, float& a2) {
    const double w0 = 2.0 * M_PI * f / sr;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = static_cast<float>((1.0 + cw) / 2.0 / a0);
    b1 = static_cast<float>(-(1.0 + cw) / a0);
    b2 = static_cast<float>((1.0 + cw) / 2.0 / a0);
    a1 = static_cast<float>(-2.0 * cw / a0);
    a2 = static_cast<float>((1.0 - alpha) / a0);
}

void BandAnalyzer::initCoeffs(double sr) {
    rbjLowpass (sr, kLowHz,  kQ,   lpB0, lpB1, lpB2, lpA1, lpA2);
    rbjBandpass(sr, kMidHz,  1.0,  bpB0, bpB1, bpB2, bpA1, bpA2);
    rbjHighpass(sr, kHighHz, kQ,   hpB0, hpB1, hpB2, hpA1, hpA2);
    coeffsReady = true;
}

void BandAnalyzer::reset() {
    lpZ1 = lpZ2 = bpZ1 = bpZ2 = hpZ1 = hpZ2 = 0.0f;
    peakLow = peakMid = peakHigh = 0.0f;
}

void BandAnalyzer::applyDecay(float decay) {
    peakLow  *= decay;
    peakMid  *= decay;
    peakHigh *= decay;
}

void BandAnalyzer::feedBlock(const float* interleaved, int nFrames, int nCh, float decay) {
    if (!coeffsReady) return;
    float blkLow = 0.0f, blkMid = 0.0f, blkHigh = 0.0f;
    for (int i = 0; i < nFrames; ++i) {
        // Mono sum.
        float s = interleaved[i * nCh];
        if (nCh > 1) s = 0.5f * (s + interleaved[i * nCh + 1]);

        // LP — transposed direct form II
        float yLp = lpB0 * s + lpZ1;
        lpZ1 = lpB1 * s - lpA1 * yLp + lpZ2;
        lpZ2 = lpB2 * s - lpA2 * yLp;

        float yBp = bpB0 * s + bpZ1;
        bpZ1 = bpB1 * s - bpA1 * yBp + bpZ2;
        bpZ2 = bpB2 * s - bpA2 * yBp;

        float yHp = hpB0 * s + hpZ1;
        hpZ1 = hpB1 * s - hpA1 * yHp + hpZ2;
        hpZ2 = hpB2 * s - hpA2 * yHp;

        blkLow  = std::max(blkLow,  std::abs(yLp));
        blkMid  = std::max(blkMid,  std::abs(yBp));
        blkHigh = std::max(blkHigh, std::abs(yHp));
    }
    peakLow  = std::max(blkLow,  peakLow  * decay);
    peakMid  = std::max(blkMid,  peakMid  * decay);
    peakHigh = std::max(blkHigh, peakHigh * decay);
}
// ──────────────────────────────────────────────────────────────────────────

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

    // Band analyzer coefficients depend on sample rate — init once here.
    for (auto& ch : m_tracks)  ch.bands.initCoeffs(sampleRate);
    for (auto& rb : m_returns) rb.bands.initCoeffs(sampleRate);
    m_master.bands.initCoeffs(sampleRate);
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
            float blkLow = 0.0f, blkMid = 0.0f, blkHigh = 0.0f;
            auto& bd = ch.bands;

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

                // Per-sample 3-band biquads on post-fader mono sum.
                // Skip entirely when no visual is listening to this track —
                // saves ~40 flops per sample per unwatched track.
                if (bd.coeffsReady && bd.wired) {
                    float mono = 0.5f * (outL + outR);
                    float yLp = bd.lpB0 * mono + bd.lpZ1;
                    bd.lpZ1 = bd.lpB1 * mono - bd.lpA1 * yLp + bd.lpZ2;
                    bd.lpZ2 = bd.lpB2 * mono - bd.lpA2 * yLp;

                    float yBp = bd.bpB0 * mono + bd.bpZ1;
                    bd.bpZ1 = bd.bpB1 * mono - bd.bpA1 * yBp + bd.bpZ2;
                    bd.bpZ2 = bd.bpB2 * mono - bd.bpA2 * yBp;

                    float yHp = bd.hpB0 * mono + bd.hpZ1;
                    bd.hpZ1 = bd.hpB1 * mono - bd.hpA1 * yHp + bd.hpZ2;
                    bd.hpZ2 = bd.hpB2 * mono - bd.hpA2 * yHp;

                    blkLow  = std::max(blkLow,  std::abs(yLp));
                    blkMid  = std::max(blkMid,  std::abs(yBp));
                    blkHigh = std::max(blkHigh, std::abs(yHp));
                }
            }

            ch.peakL = std::max(trackPeakL, ch.peakL * kPeakDecay);
            ch.peakR = std::max(trackPeakR, ch.peakR * kPeakDecay);
            bd.peakLow  = std::max(blkLow,  bd.peakLow  * kPeakDecay);
            bd.peakMid  = std::max(blkMid,  bd.peakMid  * kPeakDecay);
            bd.peakHigh = std::max(blkHigh, bd.peakHigh * kPeakDecay);
        } else {
            ch.peakL *= kPeakDecay;
            ch.peakR *= kPeakDecay;
            ch.bands.applyDecay(kPeakDecay);
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

    // --- Master volume, metering, always-on soft-clip ---
    //
    // Tanh-style soft-clipper at the very end of the chain prevents
    // harsh digital clipping when cumulative track output pushes the
    // master hot. Linear below kClipThresh (transparent for normal
    // signals); above it, smoothly curves toward ±1.0. Peak meters
    // read PRE-clip so the user still sees that they're overloading
    // instead of the clipper hiding the problem.
    auto softClip = [](float x) -> float {
        constexpr float t = 0.95f;
        constexpr float range = 1.0f - t;
        if (x >  t) return t + std::tanh((x - t) / range) * range;
        if (x < -t) return -(t + std::tanh((-x - t) / range) * range);
        return x;
    };

    float mPeakL = 0.0f, mPeakR = 0.0f;
    for (int i = 0; i < numFrames; ++i) {
        const float l = output[i * nc + 0] * m_master.volume;
        mPeakL = std::max(mPeakL, std::abs(l));
        output[i * nc + 0] = softClip(l);
        if (nc > 1) {
            const float r = output[i * nc + 1] * m_master.volume;
            mPeakR = std::max(mPeakR, std::abs(r));
            output[i * nc + 1] = softClip(r);
        }
    }

    m_master.peakL = std::max(mPeakL, m_master.peakL * kPeakDecay);
    m_master.peakR = std::max(mPeakR, m_master.peakR * kPeakDecay);

    // Master 3-band analysis — cheap to feed the contiguous output buffer.
    // Skip when nothing listens to master (e.g. the active visual clip is
    // wired to a specific track instead).
    if (m_master.bands.wired)
        m_master.bands.feedBlock(output, numFrames, nc, kPeakDecay);

    // Visual sample tap: write mono master into the circular buffer. The UI
    // thread reads this each frame to compute the Shadertoy-style FFT.
    {
        uint32_t wp = m_visWritePos.load(std::memory_order_relaxed);
        for (int i = 0; i < numFrames; ++i) {
            float s = output[i * nc];
            if (nc > 1) s = 0.5f * (s + output[i * nc + 1]);
            m_visSamples[wp] = s;
            wp = (wp + 1) % kVisTapSize;
        }
        m_visWritePos.store(wp, std::memory_order_release);
    }
}

void Mixer::decayPeaks() {
    for (auto& ch : m_tracks) {
        ch.peakL *= kPeakDecay; ch.peakR *= kPeakDecay;
        ch.bands.applyDecay(kPeakDecay);
    }
    for (auto& rb : m_returns) {
        rb.peakL *= kPeakDecay; rb.peakR *= kPeakDecay;
        rb.bands.applyDecay(kPeakDecay);
    }
    m_master.peakL *= kPeakDecay;
    m_master.peakR *= kPeakDecay;
    m_master.bands.applyDecay(kPeakDecay);
}

} // namespace audio
} // namespace yawn
