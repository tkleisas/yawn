#include "Checkbox.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
int utf8CodepointCount(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}
} // anon

// ───────────────────────────────────────────────────────────────────
// Construction
// ───────────────────────────────────────────────────────────────────

FwCheckbox::FwCheckbox() {
    setFocusable(true);
    setRelayoutBoundary(true);   // state flips are paint-only
    setClickOnly(true);           // press-release always flips state
}

FwCheckbox::FwCheckbox(std::string label) : FwCheckbox() {
    m_label = std::move(label);
}

// ───────────────────────────────────────────────────────────────────
// Content / state
// ───────────────────────────────────────────────────────────────────

void FwCheckbox::setLabel(std::string l) {
    if (l == m_label) return;
    m_label = std::move(l);
    invalidate();
}

void FwCheckbox::setChecked(bool on, ValueChangeSource src) {
    assignState(on ? CheckState::On : CheckState::Off, src);
}

void FwCheckbox::setIndeterminate(ValueChangeSource src) {
    assignState(CheckState::Indeterminate, src);
}

void FwCheckbox::assignState(CheckState s, ValueChangeSource src) {
    if (s == m_state) return;
    m_state = s;
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_state);
}

void FwCheckbox::setMinWidth(float w) {
    if (w == m_minWidth) return;
    m_minWidth = w;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

float FwCheckbox::measureLabelWidth(UIContext& ctx) const {
    if (m_label.empty()) return 0.0f;
    const float fontSize = theme().metrics.fontSize;
    if (ctx.textMetrics) return ctx.textMetrics->textWidth(m_label, fontSize);
    return static_cast<float>(utf8CodepointCount(m_label)) * kFallbackPxPerChar;
}

Size FwCheckbox::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float boxSize = m.controlHeight * 0.6f;
    const float gap     = m.baseUnit;
    const float labelW  = measureLabelWidth(ctx);

    float w = boxSize;
    if (labelW > 0.0f) w += gap + labelW;
    if (m_minWidth > 0.0f) w = std::max(w, m_minWidth);
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);

    float h = m.controlHeight;
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Events
// ───────────────────────────────────────────────────────────────────

void FwCheckbox::onClick(const ClickEvent& /*e*/) {
    if (!m_enabled) return;
    // Indeterminate → On on first user click (standard convention).
    // Otherwise Off ↔ On.
    if (m_state == CheckState::On) assignState(CheckState::Off, ValueChangeSource::User);
    else                            assignState(CheckState::On,  ValueChangeSource::User);
}

void FwCheckbox::onRightClick(const ClickEvent& e) {
    if (!m_enabled) return;
    if (m_onRightClick) m_onRightClick(e.screen);
}

bool FwCheckbox::onKeyDown(KeyEvent& e) {
    if (!m_enabled || e.consumed) return false;
    if (e.key == Key::Space || e.key == Key::Enter) {
        onClick(ClickEvent{});
        return true;
    }
    return false;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
