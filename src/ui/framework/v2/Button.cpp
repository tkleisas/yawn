#include "Button.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

FwButton::FwButton() {
    // Buttons don't steal focus on click (mixer workflows), but they
    // ARE focusable via Tab.
    setFocusable(true);
    // Fixed-size policy → auto relayout boundary.
}

FwButton::FwButton(std::string label) : FwButton() {
    m_label = std::move(label);
}

void FwButton::setLabel(std::string l) {
    if (l != m_label) {
        m_label = std::move(l);
        invalidate();   // width may change
    }
}

void FwButton::setHighlighted(bool h) {
    if (h != m_highlighted) {
        m_highlighted = h;
        // paint-only
    }
}

void FwButton::setMinWidth(float w) {
    if (w != m_minWidth) { m_minWidth = w; invalidate(); }
}

void FwButton::setFixedWidth(float w) {
    if (w != m_fixedWidth) { m_fixedWidth = w; invalidate(); }
}

Size FwButton::onMeasure(Constraints c, UIContext& ctx) {
    const auto& m = theme().metrics;
    const float padH = m.baseUnit * 4.0f;    // horizontal padding
    const float textW = measureLabelWidth(ctx);

    float w;
    if (m_fixedWidth > 0.0f)      w = m_fixedWidth;
    else {
        w = textW + padH;
        if (m_minWidth > 0.0f) w = std::max(w, m_minWidth);
    }

    const float h = m.controlHeight;
    return c.constrain({w, h});
}

// ─── Helpers ────────────────────────────────────────────────────────

float FwButton::measureLabelWidth(UIContext& ctx) const {
    if (m_label.empty()) return 0.0f;
    if (ctx.textMetrics) {
        return ctx.textMetrics->textWidth(m_label, theme().metrics.fontSize);
    }
    return static_cast<float>(m_label.size()) * kFallbackPxPerChar;
}

// ─── Gesture callbacks ──────────────────────────────────────────────

void FwButton::onClick(const ClickEvent& /*e*/) {
    if (!m_enabled) return;
    if (m_onClick) m_onClick();
}

void FwButton::onDoubleClick(const ClickEvent& /*e*/) {
    if (!m_enabled) return;
    if (m_onDoubleClick) m_onDoubleClick();
}

void FwButton::onRightClick(const ClickEvent& e) {
    if (!m_enabled) return;
    if (m_onRightClick) m_onRightClick(e.screen);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
