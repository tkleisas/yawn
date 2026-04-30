#pragma once
// Convolution.h — uniformly-partitioned FFT block convolution engine.
//
// Used by ConvolutionReverb (and any future IR-loading effect) for
// the heavy lifting. The "uniform partitioned" scheme divides the
// IR into K equal partitions of N samples each, pre-computes each
// partition's FFT once, and per-block runs N-point FFTs of the input
// window then accumulates K complex multiplies in the frequency
// domain. Cost per block ≈ K * N complex mults + 1 forward FFT +
// 1 inverse FFT — way better than direct convolution for long IRs.
//
// Latency: zero (processes block-aligned). The FDL keeps everything
// causal — output of sample n depends only on input samples ≤ n.
//
// Design constraints chosen here:
//   * Mono engine. ConvolutionReverb instantiates two for stereo,
//     either with the same IR (mono → fake stereo) or different
//     (true stereo IR — future).
//   * Partition size locked to the host audio block size at init().
//     A larger partition would amortise FFT cost better but adds
//     latency; a smaller one needs sub-block accumulation. Lock is
//     pragmatic for v1.
//   * IR length capped at 10 seconds at the host sample rate to
//     keep partition count bounded (CPU + memory).
//
// FFT is the same radix-2 Cooley-Tukey we use elsewhere
// (SpectrumAnalyzer, SplineEQ) — no external library dependency.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace yawn {
namespace effects {

class ConvolutionEngine {
public:
    static constexpr float kMaxIRSeconds = 10.0f;

    void init(int blockSize, double sampleRate) {
        m_blockSize  = std::max(1, blockSize);
        m_fftSize    = 1;
        while (m_fftSize < 2 * m_blockSize) m_fftSize <<= 1;
        m_sampleRate = sampleRate;
        m_inputWindow.assign(m_fftSize, 0.0f);
        m_outputAcc.assign(m_fftSize, 0.0f);
        m_xRe.assign(m_fftSize, 0.0f);
        m_xIm.assign(m_fftSize, 0.0f);
        m_yRe.assign(m_fftSize, 0.0f);
        m_yIm.assign(m_fftSize, 0.0f);
        clear();
    }

    void clear() {
        m_numParts = 0;
        m_irLengthSamples = 0;
        m_partRe.clear();
        m_partIm.clear();
        m_fdlRe.clear();
        m_fdlIm.clear();
        m_fdlPos = 0;
        std::fill(m_inputWindow.begin(), m_inputWindow.end(), 0.0f);
        std::fill(m_outputAcc.begin(),   m_outputAcc.end(),   0.0f);
    }

    // Pre-compute partition FFTs from the supplied IR. The IR is
    // assumed to be at m_sampleRate (caller resamples if needed).
    // Length is clamped to 10 s so a malformed file can't blow out
    // memory.
    void setIR(const float* ir, int length) {
        if (m_blockSize <= 0 || !ir || length <= 0) {
            clear();
            return;
        }
        const int maxSamples =
            static_cast<int>(kMaxIRSeconds * m_sampleRate);
        const int N = std::min(length, maxSamples);
        m_irLengthSamples = N;
        m_numParts = (N + m_blockSize - 1) / m_blockSize;
        m_partRe.assign(m_numParts, std::vector<float>(m_fftSize, 0.0f));
        m_partIm.assign(m_numParts, std::vector<float>(m_fftSize, 0.0f));
        for (int k = 0; k < m_numParts; ++k) {
            // Copy this partition's slice of the IR into the first
            // half of the FFT buffer; second half is zero (zero-pad
            // for linear convolution via cyclic FFT).
            const int srcOff = k * m_blockSize;
            const int chunk  = std::min(m_blockSize, N - srcOff);
            std::vector<float>& re = m_partRe[k];
            std::vector<float>& im = m_partIm[k];
            std::fill(re.begin(), re.end(), 0.0f);
            std::fill(im.begin(), im.end(), 0.0f);
            for (int i = 0; i < chunk; ++i) re[i] = ir[srcOff + i];
            fft(re, im, false);
        }
        m_fdlRe.assign(m_numParts,
                        std::vector<float>(m_fftSize, 0.0f));
        m_fdlIm.assign(m_numParts,
                        std::vector<float>(m_fftSize, 0.0f));
        m_fdlPos = 0;
    }

    bool hasIR() const { return m_numParts > 0; }
    int  irLengthSamples() const { return m_irLengthSamples; }

    // Process numFrames samples. Output is wet only (no dry mix —
    // caller does dry/wet). When no IR is loaded, output is silence
    // (caller should fall back to the dry path entirely).
    //
    // Sub-block handling: if numFrames < m_blockSize we accumulate
    // into m_subInBuf and only call processBlock when full. If
    // numFrames > m_blockSize we loop, processing m_blockSize at a
    // time. If numFrames == m_blockSize (the common case) we go
    // straight through.
    void process(const float* in, float* out, int numFrames) {
        if (m_numParts == 0) {
            std::memset(out, 0, numFrames * sizeof(float));
            return;
        }
        // Single buffered path. Output is delayed by one partition
        // block (~5 ms at 256 samples / 48 kHz) — inaudible for
        // reverb where pre-delay is already standard. Avoids the
        // bug surface of a fast-path / slow-path split:
        //   1. Drain any pending output left from previous calls
        //      into the front of this call's output buffer.
        //   2. For samples we couldn't drain (no output ready yet),
        //      write silence and start accumulating input.
        //   3. When the input accumulator hits m_blockSize, fire
        //      processBlock; the result lands in m_subOutBuf, ready
        //      to be drained next iteration / call.
        int processed = 0;
        while (processed < numFrames) {
            const int outAvail = m_subOutFill - m_subOutRead;
            if (outAvail > 0) {
                const int copy = std::min(outAvail, numFrames - processed);
                std::memcpy(out + processed,
                             m_subOutBuf.data() + m_subOutRead,
                             copy * sizeof(float));
                m_subOutRead += copy;
                processed    += copy;
                continue;
            }
            // No output ready — buffer input, emit silence, possibly
            // fire a processBlock when the accumulator fills.
            const int needed = m_blockSize - m_subInPos;
            const int avail  = numFrames - processed;
            const int copy   = std::min(needed, avail);
            std::memcpy(m_subInBuf.data() + m_subInPos,
                         in + processed,
                         copy * sizeof(float));
            std::memset(out + processed, 0, copy * sizeof(float));
            m_subInPos += copy;
            processed  += copy;
            if (m_subInPos >= m_blockSize) {
                processBlock(m_subInBuf.data(), m_subOutBuf.data());
                m_subInPos   = 0;
                m_subOutFill = m_blockSize;
                m_subOutRead = 0;
            }
        }
    }

private:
    void processBlock(const float* in, float* out) {
        // Slide input window: discard oldest blockSize samples,
        // append new ones. Window is held in time order (latest at
        // the back) so the FFT consumes a contiguous span.
        std::memmove(m_inputWindow.data(),
                      m_inputWindow.data() + m_blockSize,
                      (m_fftSize - m_blockSize) * sizeof(float));
        std::memcpy(m_inputWindow.data() + (m_fftSize - m_blockSize),
                     in,
                     m_blockSize * sizeof(float));

        // FFT of the windowed input.
        for (int i = 0; i < m_fftSize; ++i) {
            m_xRe[i] = m_inputWindow[i];
            m_xIm[i] = 0.0f;
        }
        fft(m_xRe, m_xIm, false);

        // Store the input FFT in the FDL slot at m_fdlPos.
        std::memcpy(m_fdlRe[m_fdlPos].data(), m_xRe.data(),
                     m_fftSize * sizeof(float));
        std::memcpy(m_fdlIm[m_fdlPos].data(), m_xIm.data(),
                     m_fftSize * sizeof(float));

        // Convolution sum in the frequency domain. Y = Σ_k H_k · FDL[(pos-k) mod N].
        std::fill(m_yRe.begin(), m_yRe.end(), 0.0f);
        std::fill(m_yIm.begin(), m_yIm.end(), 0.0f);
        for (int k = 0; k < m_numParts; ++k) {
            const int fdlIdx = (m_fdlPos - k + m_numParts) % m_numParts;
            const float* fr = m_fdlRe[fdlIdx].data();
            const float* fi = m_fdlIm[fdlIdx].data();
            const float* hr = m_partRe[k].data();
            const float* hi = m_partIm[k].data();
            float* yr = m_yRe.data();
            float* yi = m_yIm.data();
            for (int b = 0; b < m_fftSize; ++b) {
                // Complex multiply (a+bi)(c+di) = (ac-bd) + (ad+bc)i
                const float ar = fr[b], ai = fi[b];
                const float br = hr[b], bi = hi[b];
                yr[b] += ar * br - ai * bi;
                yi[b] += ar * bi + ai * br;
            }
        }

        // IFFT and output the last m_blockSize samples (overlap-save:
        // the first half is wraparound garbage from the cyclic FFT).
        fft(m_yRe, m_yIm, true);
        std::memcpy(out, m_yRe.data() + (m_fftSize - m_blockSize),
                     m_blockSize * sizeof(float));

        // Advance FDL pointer.
        m_fdlPos = (m_fdlPos + 1) % m_numParts;
    }

    // Radix-2 Cooley-Tukey FFT — same pattern as SpectrumAnalyzer /
    // SplineEQ. The inverse path divides by N at the end.
    void fft(std::vector<float>& re, std::vector<float>& im,
             bool inverse) const {
        const int n = static_cast<int>(re.size());
        // Bit-reversal permutation.
        int j = 0;
        for (int i = 1; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const float ang = (inverse ? +2.0f : -2.0f) *
                              3.14159265f / static_cast<float>(len);
            const float wRe = std::cos(ang), wIm = std::sin(ang);
            for (int i = 0; i < n; i += len) {
                float pRe = 1.0f, pIm = 0.0f;
                for (int k = 0; k < len / 2; ++k) {
                    const float uR = re[i + k];
                    const float uI = im[i + k];
                    const float vR = re[i + k + len / 2] * pRe -
                                     im[i + k + len / 2] * pIm;
                    const float vI = re[i + k + len / 2] * pIm +
                                     im[i + k + len / 2] * pRe;
                    re[i + k]                = uR + vR;
                    im[i + k]                = uI + vI;
                    re[i + k + len / 2]      = uR - vR;
                    im[i + k + len / 2]      = uI - vI;
                    const float npRe = pRe * wRe - pIm * wIm;
                    pIm = pRe * wIm + pIm * wRe;
                    pRe = npRe;
                }
            }
        }
        if (inverse) {
            const float invN = 1.0f / static_cast<float>(n);
            for (int i = 0; i < n; ++i) { re[i] *= invN; im[i] *= invN; }
        }
    }

    int    m_blockSize       = 0;
    int    m_fftSize         = 0;
    int    m_numParts        = 0;
    int    m_irLengthSamples = 0;
    double m_sampleRate      = 48000.0;

    // Pre-computed partition FFTs.
    std::vector<std::vector<float>> m_partRe, m_partIm;

    // Frequency-domain delay line of past-input FFTs.
    std::vector<std::vector<float>> m_fdlRe, m_fdlIm;
    int m_fdlPos = 0;

    // Sliding input window (fft-size samples; latest at the back).
    std::vector<float> m_inputWindow;

    // Output accumulator (currently unused; reserved for hop-sized
    // output handling in a future non-uniform partitioner).
    std::vector<float> m_outputAcc;

    // FFT scratch.
    mutable std::vector<float> m_xRe, m_xIm, m_yRe, m_yIm;

    // Sub-block buffering — handles host audio block sizes that
    // don't match m_blockSize. Common when the host runs at 64 or
    // 128 samples vs our partition size (typically 256).
    std::vector<float> m_subInBuf  = std::vector<float>(8192, 0.0f);
    std::vector<float> m_subOutBuf = std::vector<float>(8192, 0.0f);
    int m_subInPos   = 0;
    int m_subOutFill = 0;
    int m_subOutRead = 0;
};

} // namespace effects
} // namespace yawn
