#pragma once
// Event types for the YAWN UI framework.
// Events carry input data and propagation state through the widget tree.
// SDL-independent — App.cpp converts SDL events into these types.

#include <cstdint>

namespace yawn {
namespace ui {
namespace fw {

// ─── Event phases ───────────────────────────────────────────────────────────

enum class EventPhase {
    Capture,   // Root → target (top-down)
    Target,    // Reached the target widget
    Bubble,    // Target → root (bottom-up)
};

// ─── Mouse button IDs ───────────────────────────────────────────────────────

enum class MouseButton : uint8_t {
    None   = 0,
    Left   = 1,
    Middle = 2,
    Right  = 3,
};

// ─── Modifier keys ──────────────────────────────────────────────────────────

struct Modifiers {
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;
    bool meta  = false;   // Cmd on macOS, Win on Windows

    constexpr bool none() const { return !shift && !ctrl && !alt && !meta; }
};

// ─── Base event ─────────────────────────────────────────────────────────────

struct Event {
    EventPhase phase = EventPhase::Target;
    bool consumed = false;
    uint32_t timestamp = 0;   // Milliseconds since app start

    void consume() { consumed = true; }
};

// ─── Mouse events ───────────────────────────────────────────────────────────

struct MouseEvent : Event {
    float x  = 0;   // Position in screen coordinates
    float y  = 0;
    float lx = 0;   // Position in local (widget) coordinates — set during dispatch
    float ly = 0;

    MouseButton button = MouseButton::None;
    Modifiers mods;
    int clickCount = 1;    // 1 = single, 2 = double, 3 = triple

    bool isLeftButton()   const { return button == MouseButton::Left; }
    bool isRightButton()  const { return button == MouseButton::Right; }
    bool isMiddleButton() const { return button == MouseButton::Middle; }
    bool isDoubleClick()  const { return clickCount >= 2; }
};

struct MouseMoveEvent : Event {
    float x  = 0;   // Current position (screen coords)
    float y  = 0;
    float lx = 0;   // Local coords
    float ly = 0;
    float dx = 0;   // Delta since last move
    float dy = 0;

    Modifiers mods;
    bool dragging = false;        // True if a button is held during move
    MouseButton dragButton = MouseButton::None;
};

struct ScrollEvent : Event {
    float x  = 0;   // Mouse position (screen coords)
    float y  = 0;
    float lx = 0;   // Local coords
    float ly = 0;
    float dx = 0;   // Scroll delta (horizontal)
    float dy = 0;   // Scroll delta (vertical, positive = up/away)

    Modifiers mods;
};

// ─── Keyboard events ────────────────────────────────────────────────────────
// Key codes are SDL-compatible integers so we don't need SDL headers here.

struct KeyEvent : Event {
    int keyCode  = 0;    // SDL_Keycode value (platform-independent virtual key)
    int scanCode = 0;    // SDL_Scancode value (physical key position)
    Modifiers mods;
    bool repeat = false; // True if this is a key-repeat event

    bool isEscape() const { return keyCode == 0x1B; }
    bool isEnter()  const { return keyCode == 0x0D || keyCode == 0x4000009C; }
    bool isTab()    const { return keyCode == 0x09; }
    bool isDelete() const { return keyCode == 0x7F; }
    bool isBackspace() const { return keyCode == 0x08; }
};

struct TextInputEvent : Event {
    char text[32] = {};   // UTF-8 encoded text (null-terminated)
};

// ─── Focus events ───────────────────────────────────────────────────────────

enum class FocusReason {
    Mouse,      // Clicked on widget
    Tab,        // Tab navigation
    Program,    // Programmatic focus change
};

struct FocusEvent : Event {
    FocusReason reason = FocusReason::Program;
    bool gained = true;  // true = focus gained, false = focus lost
};

// ─── Drag & Drop ────────────────────────────────────────────────────────────

struct DropFileEvent : Event {
    const char* path = nullptr;   // File path (valid only during event handling)
    float x = 0;                  // Drop position
    float y = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
