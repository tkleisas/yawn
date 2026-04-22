#pragma once

// UI v2 — FwMeter.
//
// Display-only stereo VU meter. Caller feeds peak L/R values via
// setPeak(); the widget paints two vertical bars scaled to dB.
// Colour ramps from green (≤ -10 dBFS) to yellow (~-3 dBFS) to red
// (clip) based on the peak value alone — no RMS line, no integrated
// loudness overlay.
//
// No interaction — focus is disabled and no gesture hooks are wired.

#include "Widget.h"
#include "Theme.h"

#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

class FwMeter : public Widget {
public:
    FwMeter();

    // Stereo peak in linear gain. 0.0 = -inf, 1.0 = 0 dBFS, > 1 = clip.
    void setPeak(float left, float right) { m_peakL = left; m_peakR = right; }
    float peakL() const                   { return m_peakL; }
    float peakR() const                   { return m_peakR; }

    // Linear-peak → [0,1] bar-height fraction using a 60 dB floor.
    // Exposed for the painter AND kept as a static helper so v1 call
    // sites that inline the math can swap to the v2 equivalent in one
    // step if they need to.
    static float dbToHeight(float peak) {
        if (peak < 0.001f) return 0.0f;
        const float db = 20.0f * std::log10(peak);
        const float n  = (db + 60.0f) / 60.0f;
        return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    }

    // Peak-value → bar colour.
    static Color meterColor(float peak) {
        if (peak > 1.0f) return {255, 40, 40, 255};
        if (peak > 0.7f) return {255, 200, 50, 255};
        return {80, 220, 80, 255};
    }

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;

private:
    float m_peakL = 0.0f;
    float m_peakR = 0.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
