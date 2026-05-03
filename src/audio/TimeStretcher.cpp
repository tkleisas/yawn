#include "audio/TimeStretcher.h"

#include <queue>

namespace yawn {
namespace audio {

void TimeStretcher::init(double sampleRate, int maxBlockSize, Algorithm algo) {
    m_sampleRate = sampleRate;
    m_algorithm = algo;

    switch (algo) {
        case Algorithm::WSOLA:           initWSOLA(maxBlockSize); break;
        case Algorithm::PhaseVocoder:    initPhaseVocoder(maxBlockSize); break;
        case Algorithm::PhaseVocoderPGHI:
            // Shares the FFT buffers / window / output accumulator
            // with the classic PV; layered init.
            initPhaseVocoder(maxBlockSize);
            initPhaseVocoderPGHI(maxBlockSize);
            break;
    }
}

void TimeStretcher::reset() {
    m_wsolaInputPos = 0.0;
    m_wsolaOutputWritten = 0;
    if (!m_overlapBuffer.empty())
        std::fill(m_overlapBuffer.begin(), m_overlapBuffer.end(), 0.0f);
    if (!m_phaseAccum.empty())
        std::fill(m_phaseAccum.begin(), m_phaseAccum.end(), 0.0f);
    m_pvInputPos = 0.0;
    // PGHI per-frame history — clearing forces the next first frame
    // to skip propagation (no previous frame to integrate from).
    m_pghiHasPrev = false;
    if (!m_pghiPrevSynPhase.empty())
        std::fill(m_pghiPrevSynPhase.begin(), m_pghiPrevSynPhase.end(), 0.0f);
}

int TimeStretcher::process(const float* input, int inputAvailable,
            float* output, int maxOutputFrames,
            int& inputConsumed) {
    switch (m_algorithm) {
        case Algorithm::WSOLA:
            return processWSOLA(input, inputAvailable, output, maxOutputFrames, inputConsumed);
        case Algorithm::PhaseVocoder:
            return processPhaseVocoder(input, inputAvailable, output, maxOutputFrames, inputConsumed);
        case Algorithm::PhaseVocoderPGHI:
            return processPhaseVocoderPGHI(input, inputAvailable, output, maxOutputFrames, inputConsumed);
    }
    return 0;
}

// ==================== FFT ====================

void TimeStretcher::fft(float* real, float* imag, int n, bool inverse) {
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

// ==================== WSOLA ====================

void TimeStretcher::initWSOLA(int /*maxBlockSize*/) {
    m_wsolaWindowSize = static_cast<int>(m_sampleRate * kWsolaWindowMs / 1000.0);
    m_wsolaOverlapSize = static_cast<int>(m_sampleRate * kWsolaOverlapMs / 1000.0);
    m_wsolaSearchRange = static_cast<int>(m_sampleRate * kWsolaSearchMs / 1000.0);
    m_overlapBuffer.resize(m_wsolaOverlapSize, 0.0f);
    m_wsolaInputPos = 0.0;
    m_wsolaOutputWritten = 0;
}

int TimeStretcher::processWSOLA(const float* input, int inputAvailable,
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

int TimeStretcher::findBestOverlap(const float* candidate, int available) {
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

void TimeStretcher::initPhaseVocoder(int maxBlockSize) {
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

int TimeStretcher::processPhaseVocoder(const float* input, int inputAvailable,
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

// ==================== PGHI ====================
//
// Phase Gradient Heap Integration. See the long comment block at
// processPhaseVocoderPGHI() declaration in TimeStretcher.h for the
// motivation / paper references.
//
// State buffers are sized at half-spectrum length (kPVFFTSize/2 + 1).
// We piggyback on the classic PV's m_pvWindow / m_fftReal / m_fftImag /
// m_pvOutputAccum, set up by initPhaseVocoder().

void TimeStretcher::initPhaseVocoderPGHI(int /*maxBlockSize*/) {
    const int half = kPVFFTSize / 2 + 1;
    m_pghiPrevMag      .assign(half, 0.0f);
    m_pghiPrevAnaPhase .assign(half, 0.0f);
    m_pghiPrevSynPhase .assign(half, 0.0f);
    m_pghiPrevPhaseT   .assign(half, 0.0f);
    m_pghiCurMag       .assign(half, 0.0f);
    m_pghiCurAnaPhase  .assign(half, 0.0f);
    m_pghiCurPhaseT    .assign(half, 0.0f);
    m_pghiCurPhaseW    .assign(half, 0.0f);
    m_pghiCurSynPhase  .assign(half, 0.0f);
    m_pghiVisited      .assign(half, 0);
    m_pghiHasPrev = false;
}

namespace {
// Wrap to [-π, π] — same trick as the classic PV uses for its
// phase-difference unwrapping.
inline float wrapPi(float x) {
    return x - 2.0f * static_cast<float>(M_PI) *
        std::round(x / (2.0f * static_cast<float>(M_PI)));
}

// Heap entry: a current-frame bin awaiting phase assignment,
// ordered by its own magnitude (max-heap).
struct PghiHeapEntry {
    float mag;
    int   bin;
};
struct PghiHeapCmp {
    bool operator()(const PghiHeapEntry& a, const PghiHeapEntry& b) const {
        return a.mag < b.mag;   // priority_queue is max-heap by default
    }
};

enum class PghiSource { None, TimeFromPrev, FreqFromLeft, FreqFromRight };
} // anon

int TimeStretcher::processPhaseVocoderPGHI(const float* input, int inputAvailable,
                        float* output, int maxOutputFrames,
                        int& inputConsumed) {
    int outputWritten = 0;
    int hopOut = static_cast<int>(kPVHopSize / m_speedRatio);
    if (hopOut < 1) hopOut = 1;
    inputConsumed = 0;

    const int N    = kPVFFTSize;
    const int Ha   = kPVHopSize;
    const int half = N / 2 + 1;
    const float twoPi = 2.0f * static_cast<float>(M_PI);

    // (The Hann γ window-shape constant from the PGHI paper is no
    // longer used — we measure the freq gradient directly from
    // analysis phases now. See the long comment in the freq-grad
    // computation block below for why the structure-tensor formula
    // doesn't work for our use case.)

    while (outputWritten + hopOut <= maxOutputFrames) {
        int inputStart = static_cast<int>(m_pvInputPos);
        if (inputStart + N > inputAvailable) break;

        // ── 1. Window + FFT ──
        for (int i = 0; i < N; ++i) {
            m_fftReal[i] = input[inputStart + i] * m_pvWindow[i];
            m_fftImag[i] = 0.0f;
        }
        fft(m_fftReal.data(), m_fftImag.data(), N, false);

        // ── 2. Magnitude / phase / time-derivative / freq-derivative ──
        for (int k = 0; k < half; ++k) {
            float re = m_fftReal[k];
            float im = m_fftImag[k];
            m_pghiCurMag[k]      = std::sqrt(re * re + im * im);
            m_pghiCurAnaPhase[k] = std::atan2(im, re);
        }

        // Time derivative (instantaneous frequency in rad/sample).
        // Same unwrap-by-expected-advance trick as the classic PV.
        for (int k = 0; k < half; ++k) {
            const float omega_k = twoPi * k / N;            // bin-centre freq
            float dphi = m_pghiCurAnaPhase[k] - m_pghiPrevAnaPhase[k];
            dphi -= omega_k * Ha;
            dphi = wrapPi(dphi);
            m_pghiCurPhaseT[k] = omega_k + dphi / Ha;
        }

        // Frequency derivative of phase — measured as FORWARD diff
        // between bin k and k+1 of the analysis phase, unwrapped.
        // m_pghiCurPhaseW[k] = unwrap(φ[k+1] - φ[k]).
        //
        // Centered diff `(φ[k+1] - φ[k-1])/2` was the obvious choice
        // but fails on stationary signals: the Hann window's FFT
        // skirt makes adjacent analysis-phase bins of a steady tone
        // alternate by ≈±π, so centered diff = 2π → wraps to 0,
        // telling PGHI's freq-prop "adjacent bins have the same
        // phase". They don't, and forcing that destroys the
        // reconstruction (PGHI RMS dropped to ~40% of classic PV
        // with centered diff). Forward diff captures the actual
        // bin-to-bin relationship.
        //
        // Last bin uses backward diff (no k+1 to look at).
        for (int k = 0; k < half - 1; ++k) {
            float dphi = m_pghiCurAnaPhase[k + 1] - m_pghiCurAnaPhase[k];
            m_pghiCurPhaseW[k] = wrapPi(dphi);
        }
        m_pghiCurPhaseW[half - 1] = m_pghiCurPhaseW[half - 2];   // copy last

        // ── 3. Phase propagation via heap ──
        //
        // PGHI must reconstruct phase in a single consistent
        // cross-bin reference frame each step — earlier drafts
        // tried to overlay PGHI on top of classic PV's per-bin
        // accumulator, but that breaks because classic PV's per-bin
        // independent accumulators drift apart by ω_k·Hs per frame,
        // making any freq-propagated bin's phase incompatible with
        // its accumulated time-evolved neighbours.
        //
        // First frame: no previous to integrate from → seed every
        // bin with its analysis phase (consistent reference frame
        // since they all came from the same FFT). PGHI quality
        // kicks in from frame 2.
        if (!m_pghiHasPrev) {
            for (int k = 0; k < half; ++k)
                m_pghiCurSynPhase[k] = m_pghiCurAnaPhase[k];
        } else {
            std::fill(m_pghiVisited.begin(), m_pghiVisited.end(), 0);

            // Magnitude tolerance below the loudest bin (current
            // frame — that's what's being propagated TO).
            float maxMag = 0.0f;
            for (int k = 0; k < half; ++k) {
                if (m_pghiCurMag[k] > maxMag) maxMag = m_pghiCurMag[k];
            }
            const float tolLin = maxMag * std::pow(10.0f, kPghiMagToleranceDb / 20.0f);

            // Seed the heap with all CURRENT-frame bins above tol,
            // sorted by THEIR OWN magnitude. Each pop = "next bin to
            // assign phase to". The previous frame's bins are
            // implicitly always-processed (their synth phases are
            // m_pghiPrevSynPhase from the last call).
            //
            // This is the structural fix vs the v0.54 code, which
            // seeded with prev-frame magnitudes and drove time
            // propagation first for every cur bin — leaving
            // frequency propagation as a no-op because every cur
            // bin was already visited by the time freq prop tried
            // to fire. Now each cur bin chooses ITS best processed
            // neighbour (time vs freq) based on neighbour magnitude.
            std::priority_queue<PghiHeapEntry,
                                std::vector<PghiHeapEntry>,
                                PghiHeapCmp> heap;
            for (int k = 0; k < half; ++k) {
                if (m_pghiCurMag[k] >= tolLin) {
                    heap.push({m_pghiCurMag[k], k});
                }
            }

            // Synthesis hop in samples — used by trapezoidal time
            // integration of the instantaneous frequency.
            const float Hs = static_cast<float>(Ha) /
                             static_cast<float>(m_speedRatio);

            while (!heap.empty()) {
                PghiHeapEntry e = heap.top();
                heap.pop();
                if (m_pghiVisited[e.bin]) continue;

                // Find the highest-magnitude already-PROCESSED
                // neighbour. Three candidates:
                //   1. prev[bin]            — time source (always processed if hasPrev)
                //   2. cur[bin-1] visited   — freq source from left
                //   3. cur[bin+1] visited   — freq source from right
                // Whichever has the highest magnitude wins — that's
                // the bin whose phase is most reliably known and
                // most strongly correlated with the bin we're
                // assigning.
                PghiSource src = PghiSource::None;
                float      srcMag = 0.0f;

                // Time source — always available with prev frame.
                // (No-op on the syn_phase: the baseline accumulator
                // step in 3a already wrote classic-PV's time-integrated
                // phase. We just record this as the "winner" so the
                // bin is marked visited and won't be revisited via
                // freq propagation.)
                if (m_pghiPrevMag[e.bin] > srcMag) {
                    src    = PghiSource::TimeFromPrev;
                    srcMag = m_pghiPrevMag[e.bin];
                }
                // Freq from left.
                if (e.bin > 0 && m_pghiVisited[e.bin - 1] &&
                    m_pghiCurMag[e.bin - 1] > srcMag) {
                    src    = PghiSource::FreqFromLeft;
                    srcMag = m_pghiCurMag[e.bin - 1];
                }
                // Freq from right.
                if (e.bin + 1 < half && m_pghiVisited[e.bin + 1] &&
                    m_pghiCurMag[e.bin + 1] > srcMag) {
                    src    = PghiSource::FreqFromRight;
                    srcMag = m_pghiCurMag[e.bin + 1];
                }

                switch (src) {
                    case PghiSource::TimeFromPrev: {
                        // Rectangular integration of inst freq.
                        // Trapezoidal would use prev_phase_t too,
                        // but at frame 2 prev_phase_t is garbage
                        // (frame 1's "phase diff vs frame 0=zero"
                        // isn't a meaningful inst freq), and that
                        // garbage propagates as a constant offset
                        // forever. Rectangular avoids it.
                        m_pghiCurSynPhase[e.bin] =
                            m_pghiPrevSynPhase[e.bin] + Hs *
                            m_pghiCurPhaseT[e.bin];
                        break;
                    }
                    case PghiSource::FreqFromLeft: {
                        // Propagate from bin-1 to bin using forward
                        // diff stored at bin-1 (= unwrap(φ[bin] - φ[bin-1])).
                        //   syn[k] = syn[k-1] + fgrad[k-1]
                        m_pghiCurSynPhase[e.bin] =
                            m_pghiCurSynPhase[e.bin - 1] +
                            m_pghiCurPhaseW[e.bin - 1];
                        break;
                    }
                    case PghiSource::FreqFromRight: {
                        // Propagate from bin+1 to bin going backward.
                        // The forward diff stored at bin tells us
                        // φ[bin+1] - φ[bin], so to go from bin+1 to
                        // bin we subtract.
                        m_pghiCurSynPhase[e.bin] =
                            m_pghiCurSynPhase[e.bin + 1] -
                            m_pghiCurPhaseW[e.bin];
                        break;
                    }
                    case PghiSource::None:
                        // No processed neighbour at all — fall back
                        // to analysis phase (rarely hits in practice).
                        m_pghiCurSynPhase[e.bin] = m_pghiCurAnaPhase[e.bin];
                        break;
                }
                m_pghiVisited[e.bin] = 1;
            }

            // Bins below tolerance got no propagation — keep
            // analysis phase. Random would also work but adds
            // jitter; analysis-as-is is least surprising.
            for (int k = 0; k < half; ++k) {
                if (!m_pghiVisited[k])
                    m_pghiCurSynPhase[k] = m_pghiCurAnaPhase[k];
            }
        }

        // ── 4. Reconstruct complex spectrum ──
        for (int k = 0; k < half; ++k) {
            m_fftReal[k] = m_pghiCurMag[k] * std::cos(m_pghiCurSynPhase[k]);
            m_fftImag[k] = m_pghiCurMag[k] * std::sin(m_pghiCurSynPhase[k]);
        }
        // Mirror for inverse FFT.
        for (int k = half; k < N; ++k) {
            m_fftReal[k] =  m_fftReal[N - k];
            m_fftImag[k] = -m_fftImag[N - k];
        }

        // ── 5. Inverse FFT + window + overlap-add ──
        fft(m_fftReal.data(), m_fftImag.data(), N, true);
        for (int i = 0; i < N; ++i) {
            int outIdx = outputWritten + i;
            if (outIdx < maxOutputFrames) {
                output[outIdx] += m_fftReal[i] * m_pvWindow[i] *
                                   (2.0f / kPVOversampling);
            }
        }

        // ── 6. Roll current frame into "previous" for next call ──
        std::copy(m_pghiCurMag.begin(),       m_pghiCurMag.end(),       m_pghiPrevMag.begin());
        std::copy(m_pghiCurAnaPhase.begin(),  m_pghiCurAnaPhase.end(),  m_pghiPrevAnaPhase.begin());
        std::copy(m_pghiCurSynPhase.begin(),  m_pghiCurSynPhase.end(),  m_pghiPrevSynPhase.begin());
        std::copy(m_pghiCurPhaseT.begin(),    m_pghiCurPhaseT.end(),    m_pghiPrevPhaseT.begin());
        m_pghiHasPrev = true;

        outputWritten += hopOut;
        m_pvInputPos  += Ha;
    }

    inputConsumed = static_cast<int>(m_pvInputPos);
    return std::min(outputWritten, maxOutputFrames);
}

} // namespace audio
} // namespace yawn
