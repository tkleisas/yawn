#include "TextInput.h"
#include "UIContext.h"

#include <algorithm>
#include <cstring>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
// UTF-8 codepoint-length at position p[0]. 1 for ASCII, 2/3/4 for
// multi-byte sequences. Mirrors ui::utf8CharLen in Font.cpp — kept
// local so FwTextInput stays in yawn_core (ui/Font.h pulls glad).
int utf8ByteCount(const char* p) {
    const auto b = static_cast<unsigned char>(*p);
    if (b < 0x80)            return 1;
    if ((b & 0xE0) == 0xC0)  return 2;
    if ((b & 0xF0) == 0xE0)  return 3;
    if ((b & 0xF8) == 0xF0)  return 4;
    return 1;   // malformed — treat as single byte so we can escape
}
// Bytes to walk backward from `pos` to land on a codepoint boundary.
int utf8PrevBytes(const std::string& s, int pos) {
    if (pos <= 0) return 0;
    int i = pos - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
        --i;
    return pos - i;
}
} // anon

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
                // Delete one full UTF-8 codepoint — a Greek / Cyrillic
                // / emoji char is 2-4 bytes, not 1.
                const int n = utf8PrevBytes(m_text, m_cursor);
                m_text.erase(static_cast<size_t>(m_cursor - n), n);
                m_cursor -= n;
                if (m_onChange) m_onChange(m_text);
            }
            return true;
        case Key::Delete:
            if (m_cursor < static_cast<int>(m_text.size())) {
                const int n = utf8ByteCount(&m_text[m_cursor]);
                m_text.erase(static_cast<size_t>(m_cursor), n);
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
            if (m_cursor > 0) m_cursor -= utf8PrevBytes(m_text, m_cursor);
            return true;
        case Key::Right:
            if (m_cursor < static_cast<int>(m_text.size()))
                m_cursor += utf8ByteCount(&m_text[m_cursor]);
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
