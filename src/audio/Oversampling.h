#pragma once
// audio/Oversampling.h — 2× oversampling helpers for taming aliasing from
// nonlinear DSP (clippers, saturators, bitcrushers, drum drives).
//
// A memoryless nonlinearity f(x) generates harmonics; the ones above the
// host Nyquist fold back down as inharmonic "fizz". Running f() at 2× the
// host rate pushes those harmonics up so an anti-aliasing filter can remove
// them before we drop back to the host rate.
//
// Two building blocks:
//   Downsampler2x — half-band-decimate a 2× stream → host rate. Use when a
//     SOURCE already produces 2× samples (e.g. a synth rendered at 2×).
//   Oversampler2x — wrap a memoryless nonlinearity: upsample the host-rate
//     input 2×, apply f at 2×, downsample back. Use for EFFECTS/clippers.
//
// Both use a 32-tap windowed-sinc half-band FIR (cutoff = host Nyquist,
// Blackman window → ~ -58 dB stopband). One instance per channel; state
// carries across process blocks. kTaps is a power of two so the ring index
// masks instead of modulos.

#include <cmath>

namespace yawn {
namespace audio {
namespace dsp {

// Fill h[0..N-1] with a unity-DC windowed-sinc half-band lowpass, cutoff at
// 0.25 of the 2× rate (= the host Nyquist). N must be even and a power of
// two. For fc = 0.25 the even taps (bar the centre) are ~0, i.e. it really
// is a half-band filter.
inline void designHalfband(float* h, int N) {
    constexpr double kPi = 3.14159265358979323846;
    const double fc = 0.25;
    double sum = 0.0;
    for (int n = 0; n < N; ++n) {
        const double m = n - (N - 1) / 2.0;
        const double s = (std::abs(m) < 1e-9)
            ? 2.0 * fc
            : std::sin(2.0 * kPi * fc * m) / (kPi * m);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * n / (N - 1))
                              + 0.08 * std::cos(4.0 * kPi * n / (N - 1));
        h[n] = static_cast<float>(s * w);
        sum  += s * w;
    }
    const float inv = static_cast<float>(1.0 / sum);
    for (int n = 0; n < N; ++n) h[n] *= inv;
}

// Half-band decimator: push two 2×-rate samples, read one host-rate sample.
struct Downsampler2x {
    static constexpr int kTaps = 32;
    float coeffs[kTaps];
    float hist[kTaps] = {};
    int   pos = 0;

    Downsampler2x() { designHalfband(coeffs, kTaps); }
    void reset() { for (float& s : hist) s = 0.0f; pos = 0; }

    inline void push(float x) { hist[pos] = x; pos = (pos + 1) & (kTaps - 1); }
    inline float read() const {
        float acc = 0.0f;
        int p = pos;
        for (int k = 0; k < kTaps; ++k) {
            p = (p - 1) & (kTaps - 1);
            acc += coeffs[k] * hist[p];
        }
        return acc;
    }
    // Decimate two 2×-rate input samples to one host-rate output.
    inline float process(float x0, float x1) { push(x0); push(x1); return read(); }
};

// Half-band interpolator: push one host-rate sample, produce two 2×-rate
// samples via the two polyphase sub-filters of the half-band FIR (×2 to
// compensate the zero-stuffing energy loss → unity overall gain).
struct Upsampler2x {
    static constexpr int kTaps = 32;
    float coeffs[kTaps];
    float hist[kTaps] = {};   // host-rate input history (kTaps/2 used)
    int   pos = 0;

    Upsampler2x() { designHalfband(coeffs, kTaps); }
    void reset() { for (float& s : hist) s = 0.0f; pos = 0; }

    inline void process(float x, float& y0, float& y1) {
        hist[pos] = x;
        pos = (pos + 1) & (kTaps - 1);
        float even = 0.0f, odd = 0.0f;
        for (int p = 0; p < kTaps / 2; ++p) {
            const float xp = hist[(pos - 1 - p) & (kTaps - 1)];
            even += coeffs[2 * p]     * xp;
            odd  += coeffs[2 * p + 1] * xp;
        }
        y0 = 2.0f * even;
        y1 = 2.0f * odd;
    }
};

// 2× oversampler for a memoryless nonlinearity. One per channel.
struct Oversampler2x {
    Upsampler2x   up;
    Downsampler2x down;

    void reset() { up.reset(); down.reset(); }

    // Apply f at 2× the host rate. f is a callable float(float); it's
    // inlined (no std::function), called twice per host sample.
    template <class F>
    inline float process(float x, F&& f) {
        float u0, u1;
        up.process(x, u0, u1);
        return down.process(f(u0), f(u1));
    }
};

} // namespace dsp
} // namespace audio
} // namespace yawn
