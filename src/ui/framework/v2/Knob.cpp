#include "Knob.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Construction
// ───────────────────────────────────────────────────────────────────

FwKnob::FwKnob() {
    setSizePolicy(SizePolicy::fixed());
    setRelayoutBoundary(true);
    setFocusable(true);
    m_value = clampVal(m_value, m_min, m_max);
}

// ───────────────────────────────────────────────────────────────────
// Value / range
// ───────────────────────────────────────────────────────────────────

void FwKnob::setRange(float mn, float mx) {
    if (mn > mx) std::swap(mn, mx);
    if (m_min == mn && m_max == mx) return;
    m_min = mn;
    m_max = mx;
    const float clamped = clampVal(m_value, m_min, m_max);
    if (clamped != m_value) {
        m_value = clamped;
        fireOnChange(ValueChangeSource::Programmatic);
    }
    // Range doesn't affect measured size — paint-only.
}

void FwKnob::setValue(float v, ValueChangeSource src) {
    const float clamped = clampVal(v, m_min, m_max);
    if (clamped == m_value) return;
    m_value = clamped;
    fireOnChange(src);
}

// ───────────────────────────────────────────────────────────────────
// Appearance / modulation
// ───────────────────────────────────────────────────────────────────

void FwKnob::setBipolar(bool b) {
    // Paint-only — filled arc geometry changes but measured size
    // doesn't.
    m_bipolar = b;
}

void FwKnob::setModulatedValue(std::optional<float> v) {
    if (v) {
        m_modulatedValue = clampVal(*v, m_min, m_max);
    } else {
        m_modulatedValue.reset();
    }
    // Paint-only.
}

void FwKnob::setDiameter(float px) {
    if (px == m_diameter) return;
    m_diameter = px;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Content / display
// ───────────────────────────────────────────────────────────────────

void FwKnob::setLabel(std::string l) {
    if (l == m_label) return;
    m_label = std::move(l);
    invalidate();   // label affects measured height
}

void FwKnob::setShowLabel(bool b) {
    if (b == m_showLabel) return;
    m_showLabel = b;
    invalidate();
}

void FwKnob::setShowValue(bool b) {
    if (b == m_showValue) return;
    m_showValue = b;
    invalidate();
}

std::string FwKnob::formattedValue() const {
    if (m_valueFormatter) return m_valueFormatter(m_value);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", m_value);
    return std::string(buf);
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwKnob::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;

    // Knob disc diameter — explicit override or theme default.
    // Default matches spec's "Normal" preset (controlHeight * 1.6 ≈
    // 44 px given controlHeight 28).
    float disc = m_diameter > 0.0f ? m_diameter : (m.controlHeight * 1.6f);

    // Vertical layout — disc plus optional label + value rows below.
    const float fontSize = m.fontSize;
    const float smallFS  = m.fontSizeSmall;
    const float lh       = ctx.textMetrics ? ctx.textMetrics->lineHeight(fontSize)
                                             : fontSize * 1.2f;
    const float lhSmall  = ctx.textMetrics ? ctx.textMetrics->lineHeight(smallFS)
                                             : smallFS * 1.2f;

    float h = disc;
    const float gap = m.baseUnit * 0.5f;
    if (m_showLabel && !m_label.empty()) h += gap + lh;
    if (m_showValue)                      h += gap + lhSmall;

    // Width — at least the disc; grow for long labels.
    float w = disc;
    if (m_showLabel && !m_label.empty() && ctx.textMetrics) {
        w = std::max(w, ctx.textMetrics->textWidth(m_label, fontSize));
    }
    if (m_showValue && ctx.textMetrics) {
        w = std::max(w, ctx.textMetrics->textWidth(formattedValue(), smallFS));
    }

    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Drag — identical shape to FwFader
// ───────────────────────────────────────────────────────────────────

void FwKnob::onDragStart(const DragEvent& /*e*/) {
    m_dragStartValue = m_value;
}

void FwKnob::onDrag(const DragEvent& e) {
    if (!m_enabled) return;
    // DragEvent.dy is already logical (framework divided by dpiScale
    // when the event was constructed). Feed it through unchanged —
    // same shape as FwFader::onDrag.
    applyVerticalDelta(e.dy, e.modifiers);
}

void FwKnob::onDragEnd(const DragEvent& /*e*/) {
    if (m_onDragEnd) m_onDragEnd(m_dragStartValue, m_value);
}

void FwKnob::applyVerticalDelta(float logicalDy, uint16_t modifiers) {
    if (logicalDy == 0.0f) return;
    const float range = m_max - m_min;
    if (range <= 0.0f || m_pixelsPerFullRange <= 0.0f) return;

    float multiplier = 1.0f;
    if (m_shiftFine) {
        if (modifiers & ModifierKey::Shift) multiplier = 0.1f;
        if (modifiers & ModifierKey::Ctrl)  multiplier = 10.0f;
    }

    // Mouse DOWN (positive dy) → value DECREASES (natural knob feel:
    // rotating downward on the right side of a knob reduces).
    const float delta = -logicalDy * (range / m_pixelsPerFullRange) * multiplier;
    const float newVal = clampVal(m_value + delta, m_min, m_max);
    if (newVal != m_value) {
        m_value = newVal;
        fireOnChange(ValueChangeSource::User);
    }
}

// ───────────────────────────────────────────────────────────────────
// Click / right-click
// ───────────────────────────────────────────────────────────────────

void FwKnob::onClick(const ClickEvent& /*e*/) {
    if (!m_enabled) return;
    if (m_onClick) m_onClick();
}

void FwKnob::onRightClick(const ClickEvent& e) {
    if (!m_enabled) return;
    // Right-click resets to default if one is set. Also fires the
    // onRightClick callback (for MIDI Learn context menus etc.).
    if (m_defaultValue) setValue(*m_defaultValue, ValueChangeSource::User);
    if (m_onRightClick) m_onRightClick(e.screen);
}

// ───────────────────────────────────────────────────────────────────
// Scroll wheel — ± step (Shift=fine, Ctrl=coarse)
// ───────────────────────────────────────────────────────────────────

bool FwKnob::onScroll(ScrollEvent& e) {
    if (!m_enabled) return false;
    if (e.dy == 0.0f) return false;

    float multiplier = 1.0f;
    if (m_shiftFine) {
        if (e.modifiers & ModifierKey::Shift) multiplier = 0.1f;
        if (e.modifiers & ModifierKey::Ctrl)  multiplier = 10.0f;
    }
    const float sign  = (e.dy > 0.0f) ? +1.0f : -1.0f;
    const float delta = sign * m_step * multiplier;
    const float newV  = clampVal(m_value + delta, m_min, m_max);
    if (newV != m_value) {
        m_value = newV;
        fireOnChange(ValueChangeSource::User);
    }
    return true;
}

// ───────────────────────────────────────────────────────────────────
// Callback firing
// ───────────────────────────────────────────────────────────────────

void FwKnob::fireOnChange(ValueChangeSource src) {
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_value);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
