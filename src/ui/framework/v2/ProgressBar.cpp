#include "ProgressBar.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Construction
// ───────────────────────────────────────────────────────────────────

FwProgressBar::FwProgressBar() {
    setFocusable(false);
    setRelayoutBoundary(true);   // all mutations are paint-only
    setClickOnly(true);           // right-click is still a click (no drag)
}

FwProgressBar::FwProgressBar(ProgressOrientation o) : FwProgressBar() {
    m_orientation = o;
}

// ───────────────────────────────────────────────────────────────────
// State
// ───────────────────────────────────────────────────────────────────

void FwProgressBar::setValue(float v, ValueChangeSource src) {
    (void)src;
    if (m_error) return;   // error is terminal; ignore further updates
    v = std::clamp(v, 0.0f, 1.0f);
    if (v == m_value && m_determinate) return;
    m_value       = v;
    m_determinate = true;
    // Leaving "complete fade" if we come back down from 1.0.
    if (m_value < 1.0f) {
        m_completeHoldTimer = 0.0f;
        m_completeFadeTimer = 0.0f;
        m_completeAlpha     = 1.0f;
    }
}

void FwProgressBar::setIndeterminate() {
    m_determinate = false;
    m_sweepPhase  = 0.0f;
}

void FwProgressBar::setDeterminate() {
    m_determinate = true;
}

void FwProgressBar::setError(bool e) {
    if (e == m_error) return;
    m_error = e;
    // Error is a final state: stop sweep + fade immediately.
    if (e) {
        m_sweepPhase        = 0.0f;
        m_completeHoldTimer = 0.0f;
        m_completeFadeTimer = 0.0f;
        m_completeAlpha     = 1.0f;
    }
}

void FwProgressBar::setOrientation(ProgressOrientation o) {
    if (o == m_orientation) return;
    m_orientation = o;
    invalidate();
}

void FwProgressBar::setThickness(float px) {
    if (px == m_thickness) return;
    m_thickness = px;
    invalidate();
}

float FwProgressBar::thickness() const {
    return (m_thickness > 0.0f)
        ? m_thickness
        : theme().metrics.controlHeight * 0.3f;
}

void FwProgressBar::setMinLength(float px) {
    if (px == m_minLength) return;
    m_minLength = px;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Animation tick
// ───────────────────────────────────────────────────────────────────

void FwProgressBar::tick(float dtSec) {
    if (m_error) return;

    if (!m_determinate) {
        // Ping-pong sweep phase in [0..1..0]. We store the "time in
        // cycle" mapped so that 0 → leftmost, 0.5 → rightmost,
        // 1 → leftmost again. The painter just uses m_sweepPhase
        // (already in [0..1]) as "left edge of sweep along track".
        const float cycleSec = std::max(0.1f, m_sweepDurationMs * 0.001f);
        const float t = std::fmod(m_sweepPhase * cycleSec + dtSec, cycleSec);
        m_sweepPhase = (cycleSec > 0.0f) ? (t / cycleSec) : 0.0f;
        return;
    }

    // Determinate mode: run complete-fade state machine once value == 1.
    if (m_value >= 0.999f && m_completeFade) {
        constexpr float kHoldSec = 0.5f;
        constexpr float kFadeSec = 0.5f;
        if (m_completeHoldTimer < kHoldSec) {
            m_completeHoldTimer += dtSec;
            m_completeAlpha = 1.0f;
        } else if (m_completeFadeTimer < kFadeSec) {
            m_completeFadeTimer += dtSec;
            const float p = std::clamp(m_completeFadeTimer / kFadeSec, 0.0f, 1.0f);
            m_completeAlpha = 1.0f - p;
        } else {
            // Fully faded — silently reset back to 0.
            m_value             = 0.0f;
            m_completeHoldTimer = 0.0f;
            m_completeFadeTimer = 0.0f;
            m_completeAlpha     = 1.0f;
        }
    }
}

bool FwProgressBar::requiresContinuousRepaint() const {
    if (m_error) return false;
    if (!m_determinate) return true;
    // Determinate + complete-fade in flight.
    if (m_completeFade && m_value >= 0.999f &&
        m_completeAlpha > 0.0f && m_completeAlpha < 1.0f) return true;
    if (m_completeFade && m_value >= 0.999f && m_completeHoldTimer < 0.5f) return true;
    return false;
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwProgressBar::onMeasure(Constraints c, UIContext& ctx) {
    (void)ctx;
    const float t = thickness();
    if (m_orientation == ProgressOrientation::Horizontal) {
        float w = std::max(m_minLength, c.maxW);
        w = std::max(w, c.minW);
        w = std::min(w, c.maxW);
        float h = std::max(t, c.minH);
        h = std::min(h, c.maxH);
        return {w, h};
    }
    float w = std::max(t, c.minW);
    w = std::min(w, c.maxW);
    float h = std::max(m_minLength, c.maxH);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Events
// ───────────────────────────────────────────────────────────────────

void FwProgressBar::onRightClick(const ClickEvent& e) {
    if (!m_enabled) return;
    if (m_onRightClick) m_onRightClick(e.screen);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
