#include "NumberInput.h"
#include "UIContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace yawn {
namespace ui {
namespace fw2 {

FwNumberInput::FwNumberInput() {
    setSizePolicy(SizePolicy::fixed());
    setRelayoutBoundary(true);
    setFocusable(true);
    m_value = clampVal(m_value, m_min, m_max);
}

// ───────────────────────────────────────────────────────────────────
// Value / range / format
// ───────────────────────────────────────────────────────────────────

void FwNumberInput::setValue(float v, ValueChangeSource src) {
    const float clamped = clampVal(v, m_min, m_max);
    if (clamped == m_value) return;
    m_value = clamped;
    fireOnChange(src);
}

void FwNumberInput::setRange(float mn, float mx) {
    if (mn > mx) std::swap(mn, mx);
    if (m_min == mn && m_max == mx) return;
    m_min = mn;
    m_max = mx;
    const float clamped = clampVal(m_value, m_min, m_max);
    if (clamped != m_value) {
        m_value = clamped;
        fireOnChange(ValueChangeSource::Programmatic);
    }
}

void FwNumberInput::setFormat(std::string fmt) {
    if (fmt == m_format) return;
    m_format = std::move(fmt);
    invalidate();
}

void FwNumberInput::setSuffix(std::string s) {
    if (s == m_suffix) return;
    m_suffix = std::move(s);
    invalidate();
}

std::string FwNumberInput::formattedValue() const {
    if (m_formatter) return m_formatter(m_value);
    char buf[32];
    std::snprintf(buf, sizeof(buf), m_format.c_str(), m_value);
    std::string out = buf;
    if (!m_suffix.empty()) out += m_suffix;
    return out;
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwNumberInput::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float fs = m.fontSize;
    const float lh = ctx.textMetrics ? ctx.textMetrics->lineHeight(fs)
                                       : fs * 1.2f;

    // Width: fit the widest of (min value, max value, formatted value,
    // suffix) so the field doesn't jiggle as digits roll over.
    float w = 60.0f;
    if (ctx.textMetrics) {
        auto candidate = [&](float v) -> float {
            char buf[32];
            std::snprintf(buf, sizeof(buf), m_format.c_str(), v);
            std::string s = buf;
            if (!m_suffix.empty()) s += m_suffix;
            return ctx.textMetrics->textWidth(s, fs);
        };
        w = std::max({candidate(m_min), candidate(m_max), candidate(m_value),
                      60.0f});
        w += m.baseUnit * 2.0f;
    }
    float h = std::max(lh + m.baseUnit * 0.5f, 22.0f);

    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Drag
// ───────────────────────────────────────────────────────────────────

void FwNumberInput::onDragStart(const DragEvent& /*e*/) {
    m_dragStartValue = m_value;
    m_dragging       = true;
}

void FwNumberInput::onDrag(const DragEvent& e) {
    if (!m_enabled) return;
    if (m_editing) return;                // drags ignored while typing
    applyVerticalDelta(e.dy, e.modifiers);
}

void FwNumberInput::onDragEnd(const DragEvent& /*e*/) {
    m_dragging = false;
    if (m_onDragEnd && m_value != m_dragStartValue)
        m_onDragEnd(m_dragStartValue, m_value);
}

void FwNumberInput::applyVerticalDelta(float logicalDy, uint16_t modifiers) {
    if (logicalDy == 0.0f) return;
    const float range = m_max - m_min;
    if (range <= 0.0f || m_pixelsPerFullRange <= 0.0f) return;

    float multiplier = 1.0f;
    if (m_shiftFine) {
        if (modifiers & ModifierKey::Shift) multiplier = 0.1f;
        if (modifiers & ModifierKey::Ctrl)  multiplier = 10.0f;
    }

    // Mouse down (positive dy) DECREASES value — same feel as FwKnob
    // / FwFader: dragging "downward" on the right side of a control
    // reduces it.
    const float delta = -logicalDy * (range / m_pixelsPerFullRange) * multiplier;
    float newVal = m_value + delta;
    if (m_step > 0.0f) {
        newVal = std::round(newVal / m_step) * m_step;
    }
    newVal = clampVal(newVal, m_min, m_max);
    if (newVal != m_value) {
        m_value = newVal;
        fireOnChange(ValueChangeSource::User);
    }
}

// ───────────────────────────────────────────────────────────────────
// Inline edit mode
// ───────────────────────────────────────────────────────────────────

void FwNumberInput::onDoubleClick(const ClickEvent& /*e*/) {
    if (!m_enabled || m_editing) return;
    beginEdit();
}

void FwNumberInput::beginEdit() {
    if (m_editing) return;
    m_editing = true;
    // Pre-fill with the current value. Users typing to replace don't
    // want to wade through "120.0000"; the formatted value trims to
    // what they'd see before they started.
    m_editBuffer = formattedValue();
    // Strip any trailing suffix — users shouldn't type over it.
    if (!m_suffix.empty()) {
        const size_t pos = m_editBuffer.rfind(m_suffix);
        if (pos != std::string::npos && pos + m_suffix.size() == m_editBuffer.size())
            m_editBuffer.erase(pos);
    }
}

void FwNumberInput::endEdit(bool commit) {
    if (!m_editing) return;
    m_editing = false;
    if (!commit) { m_editBuffer.clear(); return; }
    if (m_editBuffer.empty()) return;
    const char* src = m_editBuffer.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(src, &end);
    const bool parsedAnything = (end != nullptr && end != src);
    m_editBuffer.clear();
    if (!parsedAnything) return;
    setValue(parsed, ValueChangeSource::User);
}

void FwNumberInput::takeTextInput(const std::string& text) {
    if (!m_editing) return;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            m_editBuffer.push_back(c);
        } else if (c == '-' && m_editBuffer.empty()) {
            m_editBuffer.push_back(c);
        } else if (c == '.' && m_editBuffer.find('.') == std::string::npos) {
            m_editBuffer.push_back(c);
        }
    }
}

bool FwNumberInput::onKeyDown(KeyEvent& e) {
    if (!m_editing) {
        // Focused + Enter enters edit mode — matches v1's behaviour.
        if (e.key == Key::Enter) { beginEdit(); return true; }
        return false;
    }
    switch (e.key) {
        case Key::Enter:  endEdit(/*commit*/true);  return true;
        case Key::Escape: endEdit(/*commit*/false); return true;
        case Key::Backspace:
            if (!m_editBuffer.empty()) m_editBuffer.pop_back();
            return true;
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

void FwNumberInput::fireOnChange(ValueChangeSource src) {
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(m_value);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
