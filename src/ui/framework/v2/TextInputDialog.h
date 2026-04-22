#pragma once

// UI v2 — FwTextInputDialog.
//
// Modal text-input prompt ("Save Preset", "Live Input URL", etc.).
// Runs on LayerStack::Modal via an OverlayEntry. Internally hosts an
// FwTextInput widget; the dialog paints chrome (title bar + OK/Cancel
// buttons) and routes mouse/key events to the input.
//
// Replaces v1 fw::TextInputDialogWidget. Public API stays close to
// the original so App call sites barely change.

#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/TextInput.h"

#include <functional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class FwTextInputDialog {
public:
    using ConfirmCallback = std::function<void(const std::string&)>;

    FwTextInputDialog();
    ~FwTextInputDialog();

    FwTextInputDialog(const FwTextInputDialog&)            = delete;
    FwTextInputDialog& operator=(const FwTextInputDialog&) = delete;

    // Show the dialog with a prompt + default text. maxLength caps the
    // number of UTF-8 bytes the field accepts (0 = unlimited). Fires
    // onConfirm with the entered text on OK / Enter; fires nothing on
    // Cancel / Escape / outside-click.
    void prompt(std::string title,
                std::string defaultText,
                ConfirmCallback onConfirm,
                int maxLength = 0);

    // Dismiss without firing onConfirm.
    void dismiss();

    bool isOpen() const { return m_handle.active(); }

    // Host-forwarded SDL text input. Caller funnels TEXT_INPUT events
    // into here while the dialog is open so IMEs / composed text
    // work.
    void takeTextInput(const std::string& text);

    // Paint hook — invoked by the overlay-entry paint closure. Public
    // so the lambda set up in prompt() can dispatch straight through.
    void paintBody(UIContext& ctx);

private:
    // Chrome metrics.
    static constexpr float kTitleBarHeight = 32.0f;
    static constexpr float kFooterHeight   = 40.0f;
    static constexpr float kPadding        = 8.0f;

    // Layout helpers.
    Rect bodyRect(const UIContext& ctx) const;
    Rect okButtonRect(const Rect& body) const;
    Rect cancelButtonRect(const Rect& body) const;
    Rect inputRect(const Rect& body) const;

    // Overlay entry callbacks.
    bool onMouseDown(MouseEvent& e);
    bool onMouseUp(MouseEvent& e);
    bool onMouseMove(MouseMoveEvent& e);
    bool onKey(KeyEvent& e);
    void onDismiss();

    void close(bool commit);

    // Mouse forward helpers (shared with FwPreferencesDialog's pattern).
    void forwardMouse(Widget* w, MouseEvent& e,
                       bool (Widget::*fn)(MouseEvent&));
    void forwardMouseMove(Widget* w, MouseMoveEvent& e);

    // State
    std::string     m_title;
    int             m_maxLength = 0;
    ConfirmCallback m_onConfirm;
    bool            m_confirmOnClose = false;

    // Embedded text field.
    FwTextInput m_input;

    // Dialog metrics (viewport-centered 420 × 130 — matches v1).
    float m_preferredW = 420.0f;
    float m_preferredH = 130.0f;
    Rect  m_body{};

    OverlayHandle m_handle;
    bool          m_dismissingInternally = false;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
