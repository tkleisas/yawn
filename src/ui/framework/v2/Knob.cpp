#include "Knob.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>   // std::strtof

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
        m_rawValue = m_value;
        fireOnChange(ValueChangeSource::Programmatic);
    }
    // Range doesn't affect measured size — paint-only.
}

void FwKnob::setValue(float v, ValueChangeSource src) {
    const float clamped = clampVal(v, m_min, m_max);
    if (clamped == m_value) return;
    m_value = clamped;
    m_rawValue = m_value;
    fireOnChange(src);
}

void FwKnob::addDetent(float value, float snapRangeFrac) {
    m_detents.push_back({clampVal(value, m_min, m_max), snapRangeFrac});
}

void FwKnob::setWrapMode(bool w) {
    if (m_wrapMode == w) return;
    m_wrapMode = w;
    invalidate();   // paint changes (360° vs 300° sweep)
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
    m_dragging       = true;
    // Re-seed the detent-tracking "raw" position so the first delta
    // compares against the on-screen value, not a stale cache from
    // the previous gesture.
    m_rawValue       = m_value;
}

void FwKnob::onDrag(const DragEvent& e) {
    if (!m_enabled) return;
    // DragEvent.dy is already logical (framework divided by dpiScale
    // when the event was constructed). Feed it through unchanged —
    // same shape as FwFader::onDrag.
    applyVerticalDelta(e.dy, e.modifiers);
}

void FwKnob::onDragEnd(const DragEvent& /*e*/) {
    m_dragging = false;
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

    // Accumulate against the raw drag position. Snap detents and wrap
    // mode both operate on m_rawValue → m_value; writing to m_rawValue
    // first lets a dragger pass THROUGH a detent (the raw leaves the
    // snap zone on the other side and m_value resumes tracking).
    m_rawValue += delta;

    // Wrap mode uses modular arithmetic; otherwise clamp.
    if (m_wrapMode) {
        while (m_rawValue > m_max) m_rawValue -= range;
        while (m_rawValue < m_min) m_rawValue += range;
    } else {
        m_rawValue = clampVal(m_rawValue, m_min, m_max);
    }

    // Detent snap: if the raw position is within snapRangeFrac × range
    // of any detent, the displayed value lands exactly on the detent.
    // First match wins — overlapping detents aren't meaningful.
    float newVal = m_rawValue;
    for (const auto& d : m_detents) {
        const float zone = d.snapRangeFrac * range;
        if (std::abs(m_rawValue - d.value) < zone) {
            newVal = d.value;
            break;
        }
    }

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

void FwKnob::onDoubleClick(const ClickEvent& /*e*/) {
    // Double-click opens inline edit. Skip if the knob is disabled
    // or already editing (stray gesture SM double-fire).
    if (!m_enabled || m_editing) return;
    beginEdit();
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
// Inline edit mode
// ───────────────────────────────────────────────────────────────────

void FwKnob::beginEdit() {
    if (m_editing) return;
    m_editing = true;
    // Seed buffer with the current value rendered without trailing
    // zeros — users typing to replace don't want to wade through
    // "0.500000". Use formatter for consistency with the paint path
    // so what they see is what they'd clear.
    //
    // Strip the non-numeric tail of the formatter output (units like
    // " Hz", " ms", "x", etc.) so the edit buffer holds JUST the
    // number — typed digits append to a clean numeric prefix instead
    // of trying to insert into "5.0k Hz". Backspace from the end
    // would otherwise have to delete the unit letters first before
    // even reaching the digits.
    const std::string formatted = formattedValue();
    m_editBuffer.clear();
    bool sawDot = false;
    for (size_t i = 0; i < formatted.size(); ++i) {
        char c = formatted[i];
        if (c >= '0' && c <= '9') {
            m_editBuffer.push_back(c);
        } else if (c == '-' && m_editBuffer.empty()) {
            m_editBuffer.push_back(c);
        } else if (c == '.' && !sawDot) {
            m_editBuffer.push_back(c);
            sawDot = true;
        } else if ((c == ' ' || c == '+') && m_editBuffer.empty()) {
            // Skip leading whitespace / plus.
        } else {
            // First non-numeric (excluding allowed leading chars)
            // ends the numeric prefix — everything after is the
            // unit suffix and must not appear in the edit buffer.
            break;
        }
    }
}

void FwKnob::endEdit(bool commit) {
    if (!m_editing) return;
    m_editing = false;
    if (!commit) { m_editBuffer.clear(); return; }
    if (m_editBuffer.empty()) return;
    // strtof accepts "1e3", "-0.5", etc. Anything unparseable → no
    // commit; same as Escape. Compare `end` against the original
    // c_str() BEFORE clearing — post-clear c_str() is a freshly
    // allocated/reset pointer and the comparison would be a lie.
    const char* src = m_editBuffer.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(src, &end);
    const bool parsedAnything = (end != nullptr && end != src);
    m_editBuffer.clear();
    if (!parsedAnything) return;
    setValue(parsed, ValueChangeSource::User);
}

void FwKnob::takeTextInput(const std::string& text) {
    if (!m_editing) return;
    for (char c : text) {
        // Accept digits, minus (only as first char), and one decimal
        // point. Everything else silently ignored.
        if (c >= '0' && c <= '9') {
            m_editBuffer.push_back(c);
        } else if (c == '-' && m_editBuffer.empty()) {
            m_editBuffer.push_back(c);
        } else if (c == '.' && m_editBuffer.find('.') == std::string::npos) {
            m_editBuffer.push_back(c);
        }
    }
}

bool FwKnob::onKeyDown(KeyEvent& e) {
    if (!m_editing) return false;
    switch (e.key) {
        case Key::Enter:
            endEdit(/*commit*/true);
            return true;
        case Key::Escape:
            endEdit(/*commit*/false);
            return true;
        case Key::Backspace:
            if (!m_editBuffer.empty()) m_editBuffer.pop_back();
            return true;
        // Digit keys — host SDL routing typically fires TEXT_INPUT
        // for these, but keys can also fire when the host is not
        // set up for text input. Accept them as a fallback.
        case Key::Num0: takeTextInput("0"); return true;
        case Key::Num1: takeTextInput("1"); return true;
        case Key::Num2: takeTextInput("2"); return true;
        case Key::Num3: takeTextInput("3"); return true;
        case Key::Num4: takeTextInput("4"); return true;
        case Key::Num5: takeTextInput("5"); return true;
        case Key::Num6: takeTextInput("6"); return true;
        case Key::Num7: takeTextInput("7"); return true;
        case Key::Num8: takeTextInput("8"); return true;
        case Key::Num9: takeTextInput("9"); return true;
        default:
            return false;
    }
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
