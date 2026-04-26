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
    static std::vector<int64_t> detect(const AudioBuffer& buffer);
    static std::vector<int64_t> detect(const AudioBuffer& buffer, const Config& config);

    // Estimate BPM from transient positions using autocorrelation of onset times
    static double estimateBPM(const std::vector<int64_t>& transients,
                               double sampleRate,
                               double minBPM = 60.0,
                               double maxBPM = 200.0);
};

} // namespace audio
} // namespace yawn
