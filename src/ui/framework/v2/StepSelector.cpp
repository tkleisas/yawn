#include "StepSelector.h"
#include "UIContext.h"

#include <algorithm>
#include <cstdio>

namespace yawn {
namespace ui {
namespace fw2 {

FwStepSelector::FwStepSelector() {
    setSizePolicy(SizePolicy::fixed());
    setRelayoutBoundary(true);
    setFocusable(true);
    // Step selector fires onClick from arrow hits — no drag.
    setClickOnly(true);
}

// ───────────────────────────────────────────────────────────────────
// Value / range
// ───────────────────────────────────────────────────────────────────

void FwStepSelector::setRange(int mn, int mx) {
    if (mn > mx) std::swap(mn, mx);
    if (m_min == mn && m_max == mx) return;
    m_min = mn;
    m_max = mx;
    const int clamped = std::clamp(m_value, m_min, m_max);
    if (clamped != m_value) {
        m_value = clamped;
        if (m_onChange) m_onChange(m_value);
    }
}

void FwStepSelector::setValue(int v, ValueChangeSource src) {
    const int clamped = std::clamp(v, m_min, m_max);
    if (clamped == m_value) return;
    m_value = clamped;
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_value);
}

void FwStepSelector::setLabel(std::string l) {
    if (l == m_label) return;
    m_label = std::move(l);
    invalidate();   // label affects measured height
}

std::string FwStepSelector::formattedValue() const {
    if (m_formatter) return m_formatter(m_value);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", m_value);
    return std::string(buf);
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwStepSelector::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;

    // Value row height = small font line height; display row height =
    // regular line height. Arrow regions each kArrowWidth wide.
    const float fontSize  = m.fontSize;
    const float lh        = ctx.textMetrics ? ctx.textMetrics->lineHeight(fontSize)
                                              : fontSize * 1.2f;

    float h = m.controlHeight;
    if (!m_label.empty()) {
        h += ctx.textMetrics ? ctx.textMetrics->lineHeight(m.fontSizeSmall)
                              : m.fontSizeSmall * 1.2f;
        h += m.baseUnit * 0.25f;
    }

    // Width: arrows + value text + padding. Floor at a reasonable min
    // so arrows stay clickable even with tiny values.
    float valueW = 40.0f;
    if (ctx.textMetrics) {
        // Try formatting min/max and pick whichever is wider as a
        // rough upper bound.
        std::string lo = m_formatter ? m_formatter(m_min) : std::to_string(m_min);
        std::string hi = m_formatter ? m_formatter(m_max) : std::to_string(m_max);
        valueW = std::max(ctx.textMetrics->textWidth(lo, fontSize),
                          ctx.textMetrics->textWidth(hi, fontSize));
        valueW = std::max(24.0f, valueW + m.baseUnit * 2.0f);
    }
    float w = m_arrowWidth * 2.0f + valueW;

    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    (void)lh;
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Gesture handling
// ───────────────────────────────────────────────────────────────────

FwStepSelector::ArrowRegion
FwStepSelector::regionAt(float lx, float ly) const {
    const Rect& b = bounds();
    // Convert local → test against left / right arrow strips. Click
    // outside either arrow band is a no-op (we don't step from a
    // middle-of-widget click).
    if (ly < 0.0f || ly > b.h) return ArrowRegion::None;
    if (lx < m_arrowWidth)          return ArrowRegion::Left;
    if (lx > b.w - m_arrowWidth)    return ArrowRegion::Right;
    return ArrowRegion::None;
}

void FwStepSelector::onClick(const ClickEvent& e) {
    if (!m_enabled) return;
    const auto r = regionAt(e.local.x, e.local.y);
    if (r == ArrowRegion::Left)  step(-m_step, ValueChangeSource::User);
    if (r == ArrowRegion::Right) step(+m_step, ValueChangeSource::User);
}

bool FwStepSelector::onScroll(ScrollEvent& e) {
    if (!m_enabled) return false;
    if (e.dy == 0.0f) return false;
    step(e.dy > 0.0f ? +m_step : -m_step, ValueChangeSource::User);
    return true;
}

void FwStepSelector::step(int delta, ValueChangeSource src) {
    int newVal = m_value + delta;
    if (m_wrap) {
        const int range = m_max - m_min + 1;
        if (range > 0)
            newVal = m_min + ((newVal - m_min) % range + range) % range;
    } else {
        newVal = std::clamp(newVal, m_min, m_max);
    }
    if (newVal == m_value) return;
    m_value = newVal;
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_value);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
