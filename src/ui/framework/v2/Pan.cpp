#include "Pan.h"
#include "UIContext.h"

namespace yawn {
namespace ui {
namespace fw2 {

FwPan::FwPan() {
    // Flexible width (stretch to fill strip), fixed height.
    setSizePolicy(SizePolicy::flex(1.0f));
    setRelayoutBoundary(true);
    setFocusable(true);
}

// ───────────────────────────────────────────────────────────────────
// Value
// ───────────────────────────────────────────────────────────────────

void FwPan::setValue(float v, ValueChangeSource src) {
    const float clamped = clampVal(v);
    if (clamped == m_value) return;
    m_value = clamped;
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_value);
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwPan::onMeasure(Constraints c, UIContext& /*ctx*/) {
    // Narrow horizontal strip. Take full available width (flexWeight),
    // fixed compact height matching v1's 16 px.
    float h = 16.0f;
    float w = 60.0f;   // minimum preferred
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Drag
// ───────────────────────────────────────────────────────────────────

void FwPan::onDragStart(const DragEvent& /*e*/) {
    m_dragStartValue = m_value;
    m_dragging       = true;
}

void FwPan::onDrag(const DragEvent& e) {
    if (!m_enabled) return;
    // Horizontal drag scaled by the widget's width so one full-width
    // sweep = full -1..+1 range. Matches v1 semantics.
    const float w = bounds().w;
    if (w <= 0.0f) return;
    const float delta = (e.dx / w) * 2.0f;
    const float newVal = clampVal(m_value + delta);
    if (newVal != m_value) {
        m_value = newVal;
        if (m_onChange) m_onChange(m_value);
    }
}

void FwPan::onDragEnd(const DragEvent& /*e*/) {
    m_dragging = false;
    if (m_onDragEnd) m_onDragEnd(m_dragStartValue, m_value);
}

void FwPan::onRightClick(const ClickEvent& e) {
    // Right-click resets to center; callers can also hook their own
    // right-click handler (MIDI Learn etc.) before the reset.
    if (m_onRightClick) m_onRightClick(e.screen);
    if (m_value != 0.0f) {
        m_value = 0.0f;
        if (m_onChange) m_onChange(m_value);
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
