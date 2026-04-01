#pragma once

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <complex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace audio {

// TimeStretcher: real-time audio time-stretching using WSOLA or phase vocoder.
// All buffers pre-allocated. No allocations in process().
class TimeStretcher {
public:
    enum class Algorithm { WSOLA, PhaseVocoder };

    void init(double sampleRate, int maxBlockSize, Algorithm algo = Algorithm::WSOLA) {
        m_sampleRate = sampleRate;
        m_algorithm = algo;

        if (algo == Algorithm::WSOLA) {
            initWSOLA(maxBlockSize);
        } else {
            initPhaseVocoder(maxBlockSize);
        }
    }

    void setSpeedRatio(double ratio) {
        m_speedRatio = std::max(0.25, std::min(4.0, ratio));
    }

    double speedRatio() const { return m_speedRatio; }

    // Process a block: reads from input at variable rate, writes to output.
    // Returns number of output frames actually written.
    // inputAvailable: total frames available starting at input pointer.
    // inputConsumed: output parameter - how many input frames were consumed.
    int process(const float* input, int inputAvailable,
                float* output, int maxOutputFrames,
                int& inputConsumed) {
        if (m_algorithm == Algorithm::WSOLA) {
            return processWSOLA(input, inputAvailable, output, maxOutputFrames, inputConsumed);
        } else {
            return processPhaseVocoder(input, inputAvailable, output, maxOutputFrames, inputConsumed);
        }
    }

    void reset() {
        m_wsolaInputPos = 0.0;
        m_wsolaOutputWritten = 0;
        if (!m_overlapBuffer.empty())
            std::fill(m_overlapBuffer.begin(), m_overlapBuffer.end(), 0.0f);
        if (!m_phaseAccum.empty())
            std::fill(m_phaseAccum.begin(), m_phaseAccum.end(), 0.0f);
        m_pvInputPos = 0.0;
    }

    // Reset input position tracking without clearing overlap/phase state.
    // Call before each process() when providing a new input buffer starting at offset 0.
    void resetInputPosition() {
        m_wsolaInputPos = 0.0;
        m_pvInputPos = 0.0;
    }

    // Minimum output frames needed for the stretcher to produce a single block.
    int minOutputFrames() const {
        if (m_algorithm == Algorithm::WSOLA)
            return m_wsolaWindowSize;
        return kPVFFTSize;
    }

    Algorithm algorithm() const { return m_algorithm; }

    // Simple radix-2 FFT (Cooley-Tukey). Public for testing.
    static void fft(float* real, float* imag, int n, bool inverse) {
        // Bit reversal
        int bits = 0;
        for (int i = n; i > 1; i >>= 1) ++bits;
        for (int i = 0; i < n; ++i) {
            int j = 0;
            for (int b = 0; b < bits; ++b) {
                if (i & (1 << b)) j |= (1 << (bits - 1 - b));
            }
            if (j > i) {
                std::swap(real[i], real[j]);
                std::swap(imag[i], imag[j]);
            }
        }

        // FFT butterfly
        float sign = inverse ? 1.0f : -1.0f;
        for (int size = 2; size <= n; size *= 2) {
            int halfSize = size / 2;
            float angle = sign * 2.0f * static_cast<float>(M_PI) / size;
            float wReal = std::cos(angle);
            float wImag = std::sin(angle);

            for (int i = 0; i < n; i += size) {
                float tReal = 1.0f, tImag = 0.0f;
                for (int j = 0; j < halfSize; ++j) {
                    int a = i + j;
                    int b = a + halfSize;
                    float bReal = real[b] * tReal - imag[b] * tImag;
                    float bImag = real[b] * tImag + imag[b] * tReal;
                    real[b] = real[a] - bReal;
                    imag[b] = imag[a] - bImag;
                    real[a] += bReal;
                    imag[a] += bImag;
                    float newTReal = tReal * wReal - tImag * wImag;
                    tImag = tReal * wImag + tImag * wReal;
                    tReal = newTReal;
                }
            }
        }

        // Scale for inverse
        if (inverse) {
            float scale = 1.0f / n;
            for (int i = 0; i < n; ++i) {
                real[i] *= scale;
                imag[i] *= scale;
            }
        }
    }

private:
    // ==================== WSOLA ====================
    // Waveform Similarity Overlap-Add
    // Good for transient-heavy material (drums, rhythmic content)

    static constexpr int kWsolaWindowMs = 20;     // ~20ms analysis window
    static constexpr int kWsolaOverlapMs = 10;     // 50% overlap
    static constexpr int kWsolaSearchMs = 8;       // search range for best overlap

    int m_wsolaWindowSize = 0;
    int m_wsolaOverlapSize = 0;
    int m_wsolaSearchRange = 0;
    std::vector<float> m_overlapBuffer;   // overlap-add tail from previous window
    double m_wsolaInputPos = 0.0;
    int m_wsolaOutputWritten = 0;

    void initWSOLA(int /*maxBlockSize*/) {
        m_wsolaWindowSize = static_cast<int>(m_sampleRate * kWsolaWindowMs / 1000.0);
        m_wsolaOverlapSize = static_cast<int>(m_sampleRate * kWsolaOverlapMs / 1000.0);
        m_wsolaSearchRange = static_cast<int>(m_sampleRate * kWsolaSearchMs / 1000.0);
        m_overlapBuffer.resize(m_wsolaOverlapSize, 0.0f);
        m_wsolaInputPos = 0.0;
        m_wsolaOutputWritten = 0;
    }

    int processWSOLA(const float* input, int inputAvailable,
                     float* output, int maxOutputFrames,
                     int& inputConsumed) {
        int outputWritten = 0;
        int hopOut = m_wsolaWindowSize - m_wsolaOverlapSize;
        double hopIn = hopOut * m_speedRatio;

        inputConsumed = 0;

        while (outputWritten + m_wsolaWindowSize <= maxOutputFrames) {
            int inputStart = static_cast<int>(m_wsolaInputPos);

            // Need enough input for window + search range
            if (inputStart + m_wsolaWindowSize + m_wsolaSearchRange > inputAvailable)
                break;

            // Find best overlap position using cross-correlation
            int bestOffset = 0;
            if (m_wsolaOutputWritten > 0) {
                bestOffset = findBestOverlap(
                    input + inputStart, inputAvailable - inputStart);
            }

            int readPos = inputStart + bestOffset;
            if (readPos + m_wsolaWindowSize > inputAvailable)
                break;

            // Copy window to output with overlap-add
            for (int i = 0; i < m_wsolaWindowSize; ++i) {
                float sample = input[readPos + i];

                // Apply overlap with previous window's tail
                if (i < m_wsolaOverlapSize && m_wsolaOutputWritten > 0) {
                    float fadeOut = 1.0f - static_cast<float>(i) / m_wsolaOverlapSize;
                    float fadeIn = static_cast<float>(i) / m_wsolaOverlapSize;
                    sample = m_overlapBuffer[i] * fadeOut + sample * fadeIn;
                }

                if (outputWritten + i < maxOutputFrames) {
                    output[outputWritten + i] = sample;
                }
            }

            // Save overlap tail for next iteration
            int tailStart = m_wsolaWindowSize - m_wsolaOverlapSize;
            for (int i = 0; i < m_wsolaOverlapSize; ++i) {
                m_overlapBuffer[i] = input[readPos + tailStart + i];
            }

            outputWritten += hopOut;
            m_wsolaInputPos += hopIn;
            m_wsolaOutputWritten++;
        }

        inputConsumed = static_cast<int>(m_wsolaInputPos);
        return outputWritten;
    }

    int findBestOverlap(const float* candidate, int available) {
        int bestOffset = 0;
        float bestCorr = -1e30f;
        int maxSearch = std::min(m_wsolaSearchRange, available - m_wsolaOverlapSize);

        for (int offset = 0; offset < maxSearch; ++offset) {
            float corr = 0.0f;
            float normA = 0.0f;
            float normB = 0.0f;
            for (int i = 0; i < m_wsolaOverlapSize; ++i) {
                float a = m_overlapBuffer[i];
                float b = candidate[offset + i];
                corr += a * b;
                normA += a * a;
                normB += b * b;
            }
            float denom = std::sqrt(normA * normB);
            if (denom > 1e-10f) corr /= denom;
            if (corr > bestCorr) {
                bestCorr = corr;
                bestOffset = offset;
            }
        }
        return bestOffset;
    }

    // ==================== Phase Vocoder ====================
    // Better for tonal/sustained content (pads, strings, vocals)

    static constexpr int kPVFFTSize = 2048;
    static constexpr int kPVHopSize = 512;         // analysis hop
    static constexpr int kPVOversampling = kPVFFTSize / kPVHopSize; // 4x

    std::vector<float> m_pvWindow;         // Hann window
    std::vector<float> m_phaseAccum;       // accumulated output phase
    std::vector<float> m_lastPhase;        // last analysis phase
    std::vector<float> m_pvOutputAccum;    // overlap-add output accumulator
    std::vector<float> m_fftReal;          // FFT workspace
    std::vector<float> m_fftImag;
    double m_pvInputPos = 0.0;
    int m_pvOutputAccumSize = 0;
    int m_pvOutputReadPos = 0;

    void initPhaseVocoder(int maxBlockSize) {
        m_pvWindow.resize(kPVFFTSize);
        for (int i = 0; i < kPVFFTSize; ++i) {
            m_pvWindow[i] = 0.5f * (1.0f - std::cos(2.0 * M_PI * i / kPVFFTSize));
        }
        m_phaseAccum.resize(kPVFFTSize / 2 + 1, 0.0f);
        m_lastPhase.resize(kPVFFTSize / 2 + 1, 0.0f);
        m_fftReal.resize(kPVFFTSize, 0.0f);
        m_fftImag.resize(kPVFFTSize, 0.0f);
        // Output accumulator large enough for 2x stretch of maxBlockSize
        m_pvOutputAccumSize = std::max(maxBlockSize * 4, kPVFFTSize * 8);
        m_pvOutputAccum.resize(m_pvOutputAccumSize, 0.0f);
        m_pvOutputReadPos = 0;
        m_pvInputPos = 0.0;
    }

    int processPhaseVocoder(const float* input, int inputAvailable,
                            float* output, int maxOutputFrames,
                            int& inputConsumed) {
        int outputWritten = 0;
        int hopOut = static_cast<int>(kPVHopSize / m_speedRatio);
        if (hopOut < 1) hopOut = 1;

        inputConsumed = 0;

        // Process frames through phase vocoder
        while (outputWritten + hopOut <= maxOutputFrames) {
            int inputStart = static_cast<int>(m_pvInputPos);
            if (inputStart + kPVFFTSize > inputAvailable) break;

            // Window the input
            for (int i = 0; i < kPVFFTSize; ++i) {
                m_fftReal[i] = input[inputStart + i] * m_pvWindow[i];
                m_fftImag[i] = 0.0f;
            }

            // In-place DFT (simplified radix-2 FFT)
            fft(m_fftReal.data(), m_fftImag.data(), kPVFFTSize, false);

            // Phase vocoder processing
            int halfN = kPVFFTSize / 2 + 1;
            float freqPerBin = static_cast<float>(m_sampleRate) / kPVFFTSize;
            float expectedPhaseAdvance = 2.0f * static_cast<float>(M_PI) * kPVHopSize / kPVFFTSize;

            for (int k = 0; k < halfN; ++k) {
                float re = m_fftReal[k];
                float im = m_fftImag[k];
                float magnitude = std::sqrt(re * re + im * im);
                float phase = std::atan2(im, re);

                // Phase difference
                float phaseDiff = phase - m_lastPhase[k];
                m_lastPhase[k] = phase;

                // Subtract expected phase advance
                phaseDiff -= k * expectedPhaseAdvance;

                // Wrap to [-pi, pi]
                phaseDiff = phaseDiff - 2.0f * static_cast<float>(M_PI) *
                    std::round(phaseDiff / (2.0f * static_cast<float>(M_PI)));

                // True frequency deviation
                float trueFreq = k * freqPerBin + phaseDiff * freqPerBin / expectedPhaseAdvance;
                (void)trueFreq; // used conceptually; phase accumulation handles it

                // Accumulate output phase (scaled by synthesis/analysis hop ratio)
                m_phaseAccum[k] += (phaseDiff + k * expectedPhaseAdvance) / static_cast<float>(m_speedRatio);

                // Reconstruct with new phase
                m_fftReal[k] = magnitude * std::cos(m_phaseAccum[k]);
                m_fftImag[k] = magnitude * std::sin(m_phaseAccum[k]);
            }

            // Mirror for inverse FFT
            for (int k = halfN; k < kPVFFTSize; ++k) {
                m_fftReal[k] = m_fftReal[kPVFFTSize - k];
                m_fftImag[k] = -m_fftImag[kPVFFTSize - k];
            }

            // Inverse FFT
            fft(m_fftReal.data(), m_fftImag.data(), kPVFFTSize, true);

            // Overlap-add to output
            for (int i = 0; i < kPVFFTSize; ++i) {
                int outIdx = outputWritten + i;
                if (outIdx < maxOutputFrames) {
                    output[outIdx] += m_fftReal[i] * m_pvWindow[i] * (2.0f / kPVOversampling);
                }
            }

            outputWritten += hopOut;
            m_pvInputPos += kPVHopSize;
        }

        inputConsumed = static_cast<int>(m_pvInputPos);
        return std::min(outputWritten, maxOutputFrames);
    }

    // ==================== Common ====================
    double m_sampleRate = 44100.0;
    double m_speedRatio = 1.0;
    Algorithm m_algorithm = Algorithm::WSOLA;
};

} // namespace audio
} // namespace yawn
