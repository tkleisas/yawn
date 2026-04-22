#pragma once

// UI v2 — Dialog.
//
// Modal dialog on OverlayLayer::Modal. Shares the LayerStack plumbing
// with the overlay-layer widgets (FwDropDown popup, Tooltip,
// ContextMenu), but gets the Modal semantics for free:
//   • Paints above the main tree, under a shared scrim.
//   • Blocks mouse events from reaching the Main widget tree.
//   • Escape dismisses by default.
//
// Dialogs are fed a spec at show() time rather than being long-lived
// widget objects. This keeps call sites declarative — one struct, one
// static call — and matches how v1's ConfirmDialog was used
// (`->prompt(message, onConfirm)`). Heavier dialogs with content
// widgets (text input, preferences, export) will extend the spec or
// grow a real widget-tree subclass later. For now a single DialogSpec
// covers title + message + button row, which handles ConfirmDialog's
// use cases and AboutDialog's with minimal tweaking.
//
// See docs/widgets/dialog.md for the full spec.

#include "LayerStack.h"   // OverlayHandle, Rect
#include "Widget.h"       // MouseEvent, KeyEvent

#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Dialog model
// ───────────────────────────────────────────────────────────────────

enum class DialogCloseReason {
    Dismiss,       // generic close() from code
    Escape,        // user pressed Esc
    OutsideClick,  // user clicked scrim (only if dismissOnScrimClick)
    ButtonClicked, // one of the buttons fired its callback
};

struct DialogButton {
    std::string           label;
    std::function<void()> onClick;
    // Styling / key-binding flags.
    //   primary = accent-coloured + Enter activates it.
    //   cancel  = Escape activates it. Usually left-most button.
    bool primary = false;
    bool cancel  = false;
};

struct DialogSpec {
    std::string title;     // optional — empty = no title row
    std::string message;   // multi-line allowed; '\n' splits lines

    std::vector<DialogButton> buttons;

    // Size. 0 = auto — computed from title/message width and button row.
    float width  = 0.0f;
    float height = 0.0f;

    // When true, clicking the scrim (outside the dialog body) fires
    // onClose(OutsideClick) and dismisses. Off by default — modal
    // dialogs usually require explicit action.
    bool dismissOnScrimClick = false;

    // Fires exactly once when the dialog closes, regardless of reason.
    // A button's own onClick runs BEFORE this (so onClose sees the
    // committed state).
    std::function<void(DialogCloseReason)> onClose;
};

// ───────────────────────────────────────────────────────────────────
// Manager (singleton) + static facades
// ───────────────────────────────────────────────────────────────────

class DialogManager {
public:
    struct State {
        DialogSpec        spec;
        Rect              bounds{};                // body, screen coords
        std::vector<Rect> buttonRects;             // same order as spec.buttons
        int               hoveredButton = -1;      // hover index, or -1
        int               pressedButton = -1;      // mouse-down active
    };

    static DialogManager& instance();

    DialogManager(const DialogManager&)            = delete;
    DialogManager& operator=(const DialogManager&) = delete;

    // Lifecycle. show() replaces any current dialog (fires its
    // onClose(Dismiss) first). close() fires onClose(Dismiss). Both
    // are safe to call with no active dialog.
    void show(DialogSpec spec);
    void close();
    bool isOpen() const { return m_handle.active(); }

    // Painter hook — main exe installs; tests leave null.
    using PaintFn = void(*)(const State& s, UIContext& ctx);
    static void    setPainter(PaintFn fn);
    static PaintFn painter();

    // Accessors for painter + tests.
    const State& state() const { return m_state; }

    // Test-only — forces teardown without firing onClose, used to keep
    // test state clean across fixtures.
    void _testResetAll();

private:
    DialogManager() = default;

    // Event handlers for the OverlayEntry.
    bool onMouseDown(MouseEvent& e);
    bool onMouseUp(MouseEvent& e);
    bool onMouseMove(MouseMoveEvent& e);
    bool onKey(KeyEvent& e);
    void onEscape();

    // Geometry + push.
    void layoutBody(UIContext& ctx);
    void pushEntry(UIContext& ctx);

    // Activation helpers — dismissWith tears the overlay down AFTER
    // firing callbacks so nothing re-enters via the dying state.
    void dismissWith(DialogCloseReason r);
    void fireButton(int idx);

    // Indices of primary / cancel buttons (or -1).
    int primaryButtonIndex() const;
    int cancelButtonIndex()  const;

    // Return the button index under (sx, sy), or -1.
    int hitTestButton(float sx, float sy) const;

    State         m_state;
    OverlayHandle m_handle;
};

// Declarative facade — call sites use this, the manager is internal.
class Dialog {
public:
    Dialog() = delete;

    static void show(DialogSpec spec) {
        DialogManager::instance().show(std::move(spec));
    }
    static void close()  { DialogManager::instance().close(); }
    static bool isOpen() { return DialogManager::instance().isOpen(); }
};

// Convenience: Yes/No prompt. Matches the v1 ConfirmDialog::prompt
// shape so migrating call sites is a near-drop-in swap.
class ConfirmDialog {
public:
    ConfirmDialog() = delete;

    static void prompt(std::string message,
                        std::function<void()> onConfirm,
                        std::function<void()> onCancel = {});
    static void prompt(std::string title, std::string message,
                        std::function<void()> onConfirm,
                        std::function<void()> onCancel = {});

    // Variant for destructive actions — primary button styled accent
    // with a caller-supplied label (e.g. "Delete", "Discard").
    static void promptCustom(std::string title, std::string message,
                              std::string confirmLabel,
                              std::string cancelLabel,
                              std::function<void()> onConfirm,
                              std::function<void()> onCancel = {});
};

} // namespace fw2
} // namespace ui
} // namespace yawn
