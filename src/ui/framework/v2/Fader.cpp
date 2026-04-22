#include "Fader.h"

namespace yawn {
namespace ui {
namespace fw2 {

FwFader::FwFader() {
    // Faders have fixed width (narrow, handle-driven) but stretch
    // vertically in flex layouts — flexWeight > 0 on height via a
    // flex policy. v1's SizePolicy lumps width + height into one
    // struct; we model this by setting a modest flex weight so
    // parent FlexBoxes give us extra vertical space when available.
    // Auto-boundary detection (flexWeight == 0) would mark us as
    // boundary; we don't want that because our measured height
    // depends on constraints. Use explicit boundary = true since
    // our measure result is stable given the same constraints and
    // value changes are paint-only.
    setSizePolicy(SizePolicy::flex(1.0f));
    setRelayoutBoundary(true);   // value changes don't propagate upward
    setFocusable(true);
    m_value = clampVal(m_value, m_min, m_max);
}

void FwFader::setRange(float mn, float mx) {
    if (mn > mx) std::swap(mn, mx);
    if (m_min == mn && m_max == mx) return;
    m_min = mn;
    m_max = mx;
    const float newVal = clampVal(m_value, m_min, m_max);
    if (newVal != m_value) {
        m_value = newVal;
        fireOnChange(ValueChangeSource::Programmatic);
    }
    // Range change doesn't affect our measured size — paint-only.
}

void FwFader::setValue(float v, ValueChangeSource src) {
    const float clamped = clampVal(v, m_min, m_max);
    if (clamped == m_value) return;
    m_value = clamped;
    fireOnChange(src);
}

void FwFader::setVisualMetrics(FaderMetrics m) {
    m_metrics = m;
    invalidate();   // affects measured size
}

Size FwFader::onMeasure(Constraints c, UIContext& /*ctx*/) {
    // Width: handle-driven (narrow).
    float w = std::max(m_metrics.minWidth, m_metrics.handleWidth);
    // Height: preferred if constraint allows; otherwise fill.
    float h = m_metrics.preferredHeight;
    return c.constrain({w, h});
}

void FwFader::onLayout(Rect b, UIContext& /*ctx*/) {
    // No children; storing bounds handled by base `layout()`.
    (void)b;
}

// ─── Drag handling ───────────────────────────────────────────────────

void FwFader::onDragStart(const DragEvent& /*e*/) {
    m_dragStartValue = m_value;
    m_dragging       = true;
}

void FwFader::onDrag(const DragEvent& e) {
    // Per-tick delta in LOGICAL pixels (framework already divided by
    // dpiScale). Vertical: screen-down is positive dy; that should
    // DECREASE value (drag down = quieter). Hence negate.
    applyVerticalDelta(e.dy, e.modifiers);
}

void FwFader::onDragEnd(const DragEvent& /*e*/) {
    m_dragging = false;
    if (m_onDragEnd) m_onDragEnd(m_dragStartValue, m_value);
}

void FwFader::applyVerticalDelta(float logicalDy, uint16_t modifiers) {
    if (logicalDy == 0.0f) return;
    const float range = m_max - m_min;
    if (range <= 0.0f || m_pixelsPerFullRange <= 0.0f) return;

    float multiplier = 1.0f;
    if (m_shiftFine) {
        if (modifiers & ModifierKey::Shift) multiplier = 0.1f;
        if (modifiers & ModifierKey::Ctrl)  multiplier = 10.0f;
    }

    // Mouse move down (logicalDy > 0) → value decreases.
    const float delta = -logicalDy * (range / m_pixelsPerFullRange) * multiplier;
    const float newVal = clampVal(m_value + delta, m_min, m_max);
    if (newVal != m_value) {
        m_value = newVal;
        fireOnChange(ValueChangeSource::User);
    }
}

// ─── Click handling ──────────────────────────────────────────────────

void FwFader::onClick(const ClickEvent& /*e*/) {
    if (m_onClick) m_onClick();
}

void FwFader::onRightClick(const ClickEvent& e) {
    if (m_onRightClick) m_onRightClick(e.screen);
}

// ─── Hover ───────────────────────────────────────────────────────────

bool FwFader::onMouseMove(MouseMoveEvent& e) {
    // Maintain hover state transitions for paint-only UI feedback.
    // We're using local hit-test to know if pointer is still within
    // widget bounds.
    const bool inside = hitTest(e.lx, e.ly);
    if (inside != m_hovered) {
        m_hovered = inside;
        onHoverChanged(inside);
    }
    // Return false so base class gesture state machine still runs.
    return false;
}

void FwFader::fireOnChange(ValueChangeSource src) {
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_value);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
