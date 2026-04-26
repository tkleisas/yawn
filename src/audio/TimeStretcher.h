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
        return kPVFFTSize;
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

    // ==================== Common ====================
    double m_sampleRate = kDefaultSampleRate;
    double m_speedRatio = 1.0;
    Algorithm m_algorithm = Algorithm::WSOLA;
};

} // namespace audio
} // namespace yawn
