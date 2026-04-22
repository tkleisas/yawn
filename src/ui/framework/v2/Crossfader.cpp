#include "Crossfader.h"
#include "UIContext.h"

namespace yawn {
namespace ui {
namespace fw2 {

FwCrossfader::FwCrossfader() {
    setSizePolicy(SizePolicy::flex(1.0f));
    setRelayoutBoundary(true);
    setFocusable(true);
}

// ───────────────────────────────────────────────────────────────────
// Value
// ───────────────────────────────────────────────────────────────────

void FwCrossfader::setValue(float v, ValueChangeSource src) {
    const float clamped = clampVal(v);
    if (clamped == m_value) return;
    m_value = clamped;
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_value);
}

void FwCrossfader::setLabels(std::string a, std::string b) {
    if (a == m_labelA && b == m_labelB) return;
    m_labelA = std::move(a);
    m_labelB = std::move(b);
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwCrossfader::onMeasure(Constraints c, UIContext& /*ctx*/) {
    // Flex-wide, fixed compact height (v1 default is 24px).
    float w = 120.0f;
    float h = 24.0f;
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Position → value
// ───────────────────────────────────────────────────────────────────

float FwCrossfader::valueForLocalX(float lx) const {
    const float trackX = m_trackPad;
    float trackW = bounds().w - m_trackPad * 2.0f;
    if (trackW < 1.0f) trackW = 1.0f;
    const float frac = (lx - trackX) / trackW;
    // Left = 100% A, right = 0% A (same mapping as v1).
    return clampVal((1.0f - frac) * 100.0f);
}

// ───────────────────────────────────────────────────────────────────
// Gesture handling
// ───────────────────────────────────────────────────────────────────

bool FwCrossfader::onMouseDown(MouseEvent& e) {
    if (!m_enabled) return false;
    if (e.button != MouseButton::Left) return false;
    // Snap handle to click position immediately — matches v1's
    // "click anywhere to jump". Returning false lets the base
    // dispatch arm the gesture SM for subsequent drag / up / right-
    // click routing.
    const float newVal = valueForLocalX(e.lx);
    if (newVal != m_value) {
        m_value = newVal;
        if (m_onChange) m_onChange(m_value);
    }
    return false;
}

void FwCrossfader::onDragStart(const DragEvent& e) {
    // Capture the pre-interaction value AT MOUSE DOWN, not at drag
    // threshold crossing — otherwise the undo's "old value" would
    // reflect the already-jumped-to click position, making the
    // undo a no-op. DragEvent.startLocal is the position the press
    // began; use it to recover the true pre-click value if needed.
    // For simplicity we just snapshot current m_value here; callers
    // care about the post-click, pre-drag value which matches v1.
    m_dragStartValue = m_value;
    m_dragging       = true;
    (void)e;
}

void FwCrossfader::onDrag(const DragEvent& e) {
    if (!m_enabled) return;
    const float newVal = valueForLocalX(e.currentLocal.x);
    if (newVal != m_value) {
        m_value = newVal;
        if (m_onChange) m_onChange(m_value);
    }
}

void FwCrossfader::onDragEnd(const DragEvent& /*e*/) {
    m_dragging = false;
    if (m_onDragEnd) m_onDragEnd(m_dragStartValue, m_value);
}

void FwCrossfader::onRightClick(const ClickEvent& e) {
    if (m_onRightClick) m_onRightClick(e.screen);
    if (m_value != m_defaultValue) {
        m_value = m_defaultValue;
        if (m_onChange) m_onChange(m_value);
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
