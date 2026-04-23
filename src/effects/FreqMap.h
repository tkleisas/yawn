#pragma once

// FreqMap — shared helpers for log-scaled frequency parameters.
// Store 0..1 normalized internally for uniform LFO/envelope modulation,
// display as Hz via the log map. Each caller passes its own [minHz, maxHz]
// range so band-specific filters (e.g. EQ low 20..2000, mid 100..10000)
// still get perceptually uniform knob travel within their band.

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace yawn {
namespace effects {

inline float logNormToHz(float x, float minHz, float maxHz) {
    const float t = std::clamp(x, 0.0f, 1.0f);
    return minHz * std::pow(maxHz / minHz, t);
}

inline float logHzToNorm(float hz, float minHz, float maxHz) {
    const float clamped = std::clamp(hz, minHz, maxHz);
    return std::log(clamped / minHz) / std::log(maxHz / minHz);
}

inline void formatHz(float hz, char* buf, int bufSize) {
    if (hz >= 1000.0f) std::snprintf(buf, bufSize, "%.1fk", hz / 1000.0f);
    else                std::snprintf(buf, bufSize, "%.0f",  hz);
}

} // namespace effects
} // namespace yawn
