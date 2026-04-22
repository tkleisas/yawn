#pragma once

// UI v2 — FwTextInput.
//
// Single-line text field. Click anywhere on the field to start
// editing; type to append at the end; Backspace / Delete edit around
// the cursor; Enter commits; Escape cancels (restores the pre-edit
// text). A placeholder is shown dim when the field is empty.
//
// Hosts feed SDL TEXT_INPUT events via takeTextInput and SDL key
// events via dispatchKeyDown (the gesture SM path). See the v1
// FwTextInput for the existing behaviour this widget replicates —
// used by BrowserPanel's search fields.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class FwTextInput : public Widget {
public:
    using ChangeCallback = std::function<void(const std::string&)>;

    FwTextInput();

    // ─── Content ──────────────────────────────────────────────────
    void setText(std::string t);
    const std::string& text() const        { return m_text; }

    void setPlaceholder(std::string p);
    const std::string& placeholder() const { return m_placeholder; }

    // ─── Callbacks ────────────────────────────────────────────────
    // onChange fires on every keystroke; onCommit fires only on
    // Enter (or panel-forced commit via endEdit(true)).
    void setOnChange(ChangeCallback cb)    { m_onChange = std::move(cb); }
    void setOnCommit(ChangeCallback cb)    { m_onCommit = std::move(cb); }

    // ─── State ────────────────────────────────────────────────────
    bool isEditing() const                 { return m_editing; }

    // Begin / end editing imperatively. beginEdit snapshots the
    // current text so endEdit(false) can restore it; endEdit(true)
    // fires onCommit.
    void beginEdit();
    void endEdit(bool commit);

    // Host-forwarded text input (SDL_EVENT_TEXT_INPUT → this).
    // Inserts at the cursor position.
    void takeTextInput(const std::string& text);

    // ─── Cursor (in UTF-8 byte offsets — good enough for ASCII /
    // most Latin use) ─────────────────────────────────────────────
    int  cursor() const                    { return m_cursor; }
    void setCursor(int pos);

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // Click enters edit mode. Returning true from onMouseDown skips
    // the gesture SM — we don't need drag / double-click behaviour.
    bool onMouseDown(MouseEvent& e) override;

    bool onKeyDown(KeyEvent& e) override;

    // Focus-lost commits (matches v1). The gesture SM doesn't auto-
    // call this; the hosting panel tracks focus itself and will call
    // endEdit(true) as needed, but we hook here too so nested focus
    // frameworks behave.
    void onFocusLost() override;

private:
    int clampCursor(int pos) const {
        const int n = static_cast<int>(m_text.size());
        return pos < 0 ? 0 : (pos > n ? n : pos);
    }

    // State
    std::string m_text;
    std::string m_savedText;           // pre-edit snapshot for cancel
    std::string m_placeholder;
    int         m_cursor  = 0;         // byte offset into m_text
    bool        m_editing = false;

    // Callbacks
    ChangeCallback m_onChange;
    ChangeCallback m_onCommit;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
