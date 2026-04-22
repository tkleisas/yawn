#include "TextInput.h"
#include "UIContext.h"

#include <algorithm>
#include <cstring>

namespace yawn {
namespace ui {
namespace fw2 {

FwTextInput::FwTextInput() {
    setSizePolicy(SizePolicy::flex(1.0f));
    setRelayoutBoundary(true);
    setFocusable(true);
}

// ───────────────────────────────────────────────────────────────────
// Content
// ───────────────────────────────────────────────────────────────────

void FwTextInput::setText(std::string t) {
    if (t == m_text) { m_cursor = clampCursor(m_cursor); return; }
    m_text = std::move(t);
    m_cursor = static_cast<int>(m_text.size());
    // Not treated as user-typed — no onChange fire. Callers that want
    // to broadcast programmatic sets can emit onChange themselves.
}

void FwTextInput::setPlaceholder(std::string p) {
    if (p == m_placeholder) return;
    m_placeholder = std::move(p);
    invalidate();
}

void FwTextInput::setCursor(int pos) {
    m_cursor = clampCursor(pos);
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwTextInput::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float fs = m.fontSize;
    const float lh = ctx.textMetrics ? ctx.textMetrics->lineHeight(fs)
                                       : fs * 1.2f;
    float w = 120.0f;
    float h = std::max(lh + m.baseUnit * 0.75f, 24.0f);
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Edit mode
// ───────────────────────────────────────────────────────────────────

void FwTextInput::beginEdit() {
    if (m_editing) return;
    m_editing = true;
    m_savedText = m_text;
    m_cursor = static_cast<int>(m_text.size());
}

void FwTextInput::endEdit(bool commit) {
    if (!m_editing) return;
    m_editing = false;
    if (commit) {
        if (m_onCommit) m_onCommit(m_text);
    } else {
        // Cancel — restore the pre-edit snapshot. onChange fires so
        // observers tracking live text can see the restore happen.
        if (m_text != m_savedText) {
            m_text = m_savedText;
            m_cursor = static_cast<int>(m_text.size());
            if (m_onChange) m_onChange(m_text);
        }
    }
}

void FwTextInput::takeTextInput(const std::string& text) {
    if (!m_editing) return;
    if (text.empty()) return;
    m_text.insert(static_cast<size_t>(m_cursor), text);
    m_cursor += static_cast<int>(text.size());
    if (m_onChange) m_onChange(m_text);
}

// ───────────────────────────────────────────────────────────────────
// Gesture handling
// ───────────────────────────────────────────────────────────────────

bool FwTextInput::onMouseDown(MouseEvent& e) {
    if (!m_enabled) return false;
    if (e.button != MouseButton::Left) return false;
    beginEdit();
    // Place cursor at end for now — click-to-position requires text
    // metrics per-character-width which we don't need for the current
    // search-field use case.
    m_cursor = static_cast<int>(m_text.size());
    return true;
}

bool FwTextInput::onKeyDown(KeyEvent& e) {
    if (!m_editing) return false;
    switch (e.key) {
        case Key::Enter:
            endEdit(/*commit*/true);
            return true;
        case Key::Escape:
            endEdit(/*commit*/false);
            return true;
        case Key::Backspace:
            if (m_cursor > 0 && !m_text.empty()) {
                m_text.erase(static_cast<size_t>(m_cursor - 1), 1);
                --m_cursor;
                if (m_onChange) m_onChange(m_text);
            }
            return true;
        case Key::Delete:
            if (m_cursor < static_cast<int>(m_text.size())) {
                m_text.erase(static_cast<size_t>(m_cursor), 1);
                if (m_onChange) m_onChange(m_text);
            }
            return true;
        case Key::Home:
            m_cursor = 0;
            return true;
        case Key::End:
            m_cursor = static_cast<int>(m_text.size());
            return true;
        case Key::Left:
            if (m_cursor > 0) --m_cursor;
            return true;
        case Key::Right:
            if (m_cursor < static_cast<int>(m_text.size())) ++m_cursor;
            return true;
        default:
            return true;   // swallow other keys while editing
    }
}

void FwTextInput::onFocusLost() {
    if (m_editing) endEdit(/*commit*/true);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
