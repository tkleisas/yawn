#pragma once

#include "audio/AudioBuffer.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace yawn {
namespace audio {

// Transient detection using energy envelope derivative with adaptive threshold.
// Detects sudden increases in signal energy (onsets/transients).
class TransientDetector {
public:
    struct Config {
        double sampleRate = 44100.0;
        float sensitivity = 0.5f;       // 0.0 = less sensitive, 1.0 = very sensitive
        double minInterOnsetSec = 0.05;  // minimum 50ms between detected transients
        int windowSize = 512;            // analysis window size in samples
        int hopSize = 128;               // hop between windows
    };

    // Detect transients in a mono or stereo AudioBuffer.
    // Returns vector of sample positions where transients occur.
    static std::vector<int64_t> detect(const AudioBuffer& buffer, const Config& config = {}) {
        if (buffer.isEmpty() || buffer.numFrames() < config.windowSize)
            return {};

        int numFrames = buffer.numFrames();
        int numChannels = buffer.numChannels();

        // Compute energy envelope (sum across channels, windowed)
        int numWindows = (numFrames - config.windowSize) / config.hopSize + 1;
        if (numWindows <= 1) return {};

        std::vector<float> energy(numWindows, 0.0f);
        for (int w = 0; w < numWindows; ++w) {
            int start = w * config.hopSize;
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch) {
                const float* data = buffer.channelData(ch);
                for (int i = 0; i < config.windowSize; ++i) {
                    int idx = start + i;
                    if (idx < numFrames) {
                        float s = data[idx];
                        sum += s * s;
                    }
                }
            }
            energy[w] = sum / (config.windowSize * numChannels);
        }

        // Compute energy derivative (spectral flux — onset detection function)
        std::vector<float> flux(numWindows, 0.0f);
        for (int w = 1; w < numWindows; ++w) {
            float diff = energy[w] - energy[w - 1];
            flux[w] = std::max(0.0f, diff); // half-wave rectify
        }

        // Adaptive threshold: local median + sensitivity-scaled offset
        int medianWindow = std::max(3, static_cast<int>(config.sampleRate / (config.hopSize * 4)));
        float threshScale = 1.0f + (1.0f - config.sensitivity) * 4.0f;

        // Compute overall mean for base threshold
        float meanFlux = 0.0f;
        for (float f : flux) meanFlux += f;
        meanFlux /= numWindows;

        int64_t minInterOnsetFrames = static_cast<int64_t>(config.minInterOnsetSec * config.sampleRate);

        std::vector<int64_t> transients;
        int64_t lastTransient = -minInterOnsetFrames - 1;

        for (int w = 1; w < numWindows; ++w) {
            // Local median threshold
            int halfWin = medianWindow / 2;
            int lo = std::max(0, w - halfWin);
            int hi = std::min(numWindows - 1, w + halfWin);
            std::vector<float> local(flux.begin() + lo, flux.begin() + hi + 1);
            std::sort(local.begin(), local.end());
            float localMedian = local[local.size() / 2];
            float threshold = std::max(localMedian * threshScale, meanFlux * 0.5f);

            if (flux[w] > threshold) {
                int64_t pos = static_cast<int64_t>(w) * config.hopSize;
                if (pos - lastTransient >= minInterOnsetFrames) {
                    transients.push_back(pos);
                    lastTransient = pos;
                }
            }
        }

        return transients;
    }

    // Estimate BPM from transient positions using autocorrelation of onset times
    static double estimateBPM(const std::vector<int64_t>& transients,
                               double sampleRate,
                               double minBPM = 60.0,
                               double maxBPM = 200.0) {
        if (transients.size() < 4) return 0.0;

        // Compute inter-onset intervals
        std::vector<double> intervals;
        intervals.reserve(transients.size() - 1);
        for (size_t i = 1; i < transients.size(); ++i) {
            double sec = static_cast<double>(transients[i] - transients[i - 1]) / sampleRate;
            intervals.push_back(sec);
        }

        // BPM histogram approach: test BPM candidates and score by consistency
        double bestBPM = 0.0;
        double bestScore = 0.0;

        for (double bpm = minBPM; bpm <= maxBPM; bpm += 0.5) {
            double beatPeriod = 60.0 / bpm;
            double score = 0.0;

            for (double interval : intervals) {
                // How close is interval to a multiple of beatPeriod?
                double ratio = interval / beatPeriod;
                double nearestInt = std::round(ratio);
                if (nearestInt < 0.5) nearestInt = 1.0;
                double error = std::abs(ratio - nearestInt) / nearestInt;
                score += std::exp(-error * error * 20.0);
            }

            if (score > bestScore) {
                bestScore = score;
                bestBPM = bpm;
            }
        }

        return bestBPM;
    }
};

} // namespace audio
} // namespace yawn
