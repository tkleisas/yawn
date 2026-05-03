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
    enum class Algorithm {
        WSOLA,             // Waveform Similarity Overlap-Add (rhythmic)
        PhaseVocoder,      // Classic Flanagan–Golden / Laroche–Dolson PV
        PhaseVocoderPGHI,  // Phase Gradient Heap Integration
                           //   (Prusa & Holighaus 2017, real-time variant)
    };

    void init(double sampleRate, int maxBlockSize, Algorithm algo = Algorithm::WSOLA);

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
                int& inputConsumed);

    void reset();

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
        return kPVFFTSize;   // both PV variants use the same FFT size
    }

    Algorithm algorithm() const { return m_algorithm; }

    // Simple radix-2 FFT (Cooley-Tukey). Public for testing.
    static void fft(float* real, float* imag, int n, bool inverse);

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

    void initWSOLA(int maxBlockSize);
    int processWSOLA(const float* input, int inputAvailable,
                     float* output, int maxOutputFrames,
                     int& inputConsumed);
    int findBestOverlap(const float* candidate, int available);

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

    void initPhaseVocoder(int maxBlockSize);
    int processPhaseVocoder(const float* input, int inputAvailable,
                            float* output, int maxOutputFrames,
                            int& inputConsumed);

    // ==================== Phase Vocoder — PGHI variant ====================
    // Phase Gradient Heap Integration (Prusa & Holighaus 2017,
    // "A Non-iterative Method for (Re)Construction of Phases";
    // real-time follow-up "Phase Vocoder Done Right" 2022).
    //
    // The classic PV (above) propagates each frequency bin's phase
    // independently along the time axis. That preserves horizontal
    // phase coherence per bin but loses VERTICAL coherence between
    // bins, producing the characteristic phasiness / smeared
    // transients on tonal material.
    //
    // PGHI computes BOTH partial derivatives of the phase
    // (∂φ/∂t = instantaneous frequency, ∂φ/∂ω = group delay) and
    // propagates phase outward from the loudest bins via integration
    // of the gradients. Bins are visited in descending magnitude
    // order via a max-heap; each pop propagates phase to its
    // unvisited time-neighbour (next frame, same bin) or
    // frequency-neighbours (current frame, ±1 bin) using
    // trapezoidal integration of the appropriate gradient.
    //
    // This implementation is the **1-step look-back** real-time
    // variant: phase propagation only crosses ONE frame boundary
    // (previous → current), which keeps streaming latency identical
    // to the classic PV (one FFT window). Quality is somewhere
    // between classic PV and offline PGHI — most of the
    // anti-phasiness benefit, none of the look-ahead latency.
    //
    // Tuning constants (kPghiMagToleranceDb, kPghiHannGamma) are
    // documented at their declarations below.

    // Hann-window-specific γ for the magnitude→group-delay relation
    // ∂φ/∂ω ≈ -γ/H_a · ∂(log|S|)/∂t. For a length-N Hann window the
    // standard value (LTFAT, Prusa 2017) is N²·0.25645... — we use
    // the closed form below.
    static constexpr float kPghiHannGammaCoef = 0.25645f;
    // Bins with magnitude below max·10^(kPghiMagToleranceDb/20) are
    // skipped during heap propagation — their phase doesn't matter
    // (they're inaudible) and including them in the heap costs CPU
    // without changing the output. -60 dB below peak is the canonical
    // PGHI default; tighter tolerances give marginally better quality
    // at higher CPU cost.
    static constexpr float kPghiMagToleranceDb = -60.0f;

    // Per-frame state propagated across calls.
    std::vector<float> m_pghiPrevMag;         // |S[m-1]|
    std::vector<float> m_pghiPrevAnaPhase;    // arg(S[m-1]) (analysis phase)
    std::vector<float> m_pghiPrevSynPhase;    // synthesised output phase from prev frame
    std::vector<float> m_pghiPrevPhaseT;      // ∂φ/∂t at prev frame (rad/sample)
    bool               m_pghiHasPrev = false;

    // Per-call scratch (kept as members so process() doesn't allocate).
    std::vector<float> m_pghiCurMag;
    std::vector<float> m_pghiCurAnaPhase;
    std::vector<float> m_pghiCurPhaseT;       // ∂φ/∂t at current frame
    std::vector<float> m_pghiCurPhaseW;       // ∂φ/∂ω at current frame
    std::vector<float> m_pghiCurSynPhase;     // output phases for current frame
    std::vector<unsigned char> m_pghiVisited; // bool per bin — has phase been assigned?

    void initPhaseVocoderPGHI(int maxBlockSize);
    int  processPhaseVocoderPGHI(const float* input, int inputAvailable,
                                  float* output, int maxOutputFrames,
                                  int& inputConsumed);

    // ==================== Common ====================
    double m_sampleRate = kDefaultSampleRate;
    double m_speedRatio = 1.0;
    Algorithm m_algorithm = Algorithm::WSOLA;
};

} // namespace audio
} // namespace yawn
