#include "Toggle.h"

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

FwToggle::FwToggle() {
    setFocusable(true);
    setRelayoutBoundary(true);   // state flips are paint-only
    // Click-only: toggles don't drag, so any press-release pair
    // should flip the state even with some pointer jitter.
    setClickOnly(true);
}

FwToggle::FwToggle(std::string label) : FwToggle() {
    m_label = std::move(label);
}

// ───────────────────────────────────────────────────────────────────
// State / content
// ───────────────────────────────────────────────────────────────────

void FwToggle::setLabel(std::string l) {
    if (l == m_label) return;
    m_label = std::move(l);
    invalidate();   // label width may change measured size
}

void FwToggle::setState(bool on, ValueChangeSource src) {
    if (on == m_on) return;
    m_on = on;
    // Match FwFader / FwDropDown convention — Automation writes stay
    // silent (prevents cascading into MIDI learn etc.), everything
    // else fires onChange.
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_on);
}

// ───────────────────────────────────────────────────────────────────
// Appearance
// ───────────────────────────────────────────────────────────────────

void FwToggle::setVariant(ToggleVariant v) {
    if (v == m_variant) return;
    m_variant = v;
    invalidate();   // Switch vs Button differ in measured width
}

void FwToggle::setMinWidth(float w) {
    if (w == m_minWidth) return;
    m_minWidth = w;
    invalidate();
}

void FwToggle::setFixedWidth(float w) {
    if (w == m_fixedWidth) return;
    m_fixedWidth = w;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

float FwToggle::measureLabelWidth(UIContext& ctx) const {
    if (m_label.empty()) return 0.0f;
    const float fontSize = theme().metrics.fontSize;
    if (ctx.textMetrics) return ctx.textMetrics->textWidth(m_label, fontSize);
    return static_cast<float>(utf8CodepointCount(m_label)) * kFallbackPxPerChar;
}

Size FwToggle::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;

    float w = 0.0f;
    if (m_variant == ToggleVariant::Switch) {
        // Fixed pill width — baseUnit × 10 is tuned for a comfortable
        // knob throw that still feels compact in a settings row.
        w = m.baseUnit * 10.0f;
    } else {
        // Button variant — label-driven like FwButton.
        if (m_fixedWidth > 0.0f) {
            w = m_fixedWidth;
        } else {
            const float pad = m.baseUnit * 4.0f;    // 16 px
            w = measureLabelWidth(ctx) + pad;
        }
        if (m_minWidth > 0.0f) w = std::max(w, m_minWidth);
    }

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

void FwToggle::onClick(const ClickEvent& /*e*/) {
    if (!m_enabled) return;
    setState(!m_on, ValueChangeSource::User);
}

void FwToggle::onRightClick(const ClickEvent& e) {
    if (!m_enabled) return;
    if (m_onRightClick) m_onRightClick(e.screen);
}

bool FwToggle::onKeyDown(KeyEvent& e) {
    if (!m_enabled || e.consumed) return false;
    switch (e.key) {
        case Key::Space:
        case Key::Enter:
            setState(!m_on, ValueChangeSource::User);
            return true;
        case Key::Home:
            if (m_variant == ToggleVariant::Switch) {
                setState(false, ValueChangeSource::User);
                return true;
            }
            return false;
        case Key::End:
            if (m_variant == ToggleVariant::Switch) {
                setState(true, ValueChangeSource::User);
                return true;
            }
            return false;
        default:
            return false;
    }
}

bool FwToggle::onScroll(ScrollEvent& e) {
    if (!m_enabled || !m_scrollFlipsState) return false;
    if (e.dy > 0.0f) {
        setState(true, ValueChangeSource::User);
        return true;
    }
    if (e.dy < 0.0f) {
        setState(false, ValueChangeSource::User);
        return true;
    }
    return false;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
