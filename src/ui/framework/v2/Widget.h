#pragma once

// UI v2 — Widget base class.
//
// Two-pass layout (measure → layout), both cached with:
//   1. Global epoch — bumped on DPI/theme/font change.
//   2. Local version — bumped by invalidate(), bubbles up to first
//      relayout-boundary ancestor.
//   3. For measure: constraint equality — different constraints
//      invalidate.
//   4. For layout: bounds equality + last-seen measure version.
//
// Gesture layer (Level 2 events) built on top of raw (Level 1) via a
// small state machine — see onMouseDown/Up/Move overrides. Drag
// threshold, double-click window, DPI normalization handled here
// once, used by every widget.
//
// See docs/ui-v2-architecture.md, ui-v2-measure-layout.md,
// ui-v2-events.md for the full design.

#include "ui/framework/Types.h"      // reuse v1 geometric types
#include "UIContext.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// Reuse v1 geometric types — data-only structs, no behavior divergence.
using ::yawn::ui::fw::Point;
using ::yawn::ui::fw::Size;
using ::yawn::ui::fw::Rect;
using ::yawn::ui::fw::Insets;
using ::yawn::ui::fw::Constraints;
using ::yawn::ui::fw::SizePolicy;

// ───────────────────────────────────────────────────────────────────
// Events
// ───────────────────────────────────────────────────────────────────

enum class MouseButton : uint8_t { Left, Middle, Right };

namespace ModifierKey {
enum : uint16_t {
    None     = 0,
    Shift    = 1 << 0,
    Ctrl     = 1 << 1,
    Alt      = 1 << 2,
    Super    = 1 << 3,
    CapsLock = 1 << 4,
};
}

struct MouseEvent {
    float x = 0, y = 0;          // screen coords, logical pixels
    float lx = 0, ly = 0;         // widget-local coords
    MouseButton button = MouseButton::Left;
    uint16_t    modifiers = 0;
    uint64_t    timestampMs = 0;
    bool        consumed = false;
};

struct MouseMoveEvent {
    float x = 0, y = 0;
    float lx = 0, ly = 0;
    float dx = 0, dy = 0;          // logical-pixel delta since last move
    uint32_t buttonMask = 0;       // bit per MouseButton held
    uint16_t modifiers = 0;
    uint64_t timestampMs = 0;
    bool     consumed = false;
};

struct ScrollEvent {
    float x = 0, y = 0;
    float lx = 0, ly = 0;
    float dx = 0, dy = 0;
    bool  isPrecise = false;
    uint16_t modifiers = 0;
    uint64_t timestampMs = 0;
    bool     consumed = false;
};

struct ClickEvent {
    Point local;
    Point screen;
    MouseButton button = MouseButton::Left;
    uint16_t    modifiers = 0;
    int         clickCount = 1;    // 1 or 2 (double)
    uint64_t    timestampMs = 0;
};

struct DragEvent {
    Point startLocal;              // where drag began in widget-local coords
    Point startScreen;
    Point currentLocal;
    Point currentScreen;
    float dx = 0, dy = 0;           // delta since last onDrag (logical)
    float cumDx = 0, cumDy = 0;      // cumulative from start (logical)
    MouseButton button = MouseButton::Left;
    uint16_t    modifiers = 0;
    uint64_t    timestampMs = 0;
};

enum class Key {
    None = 0,
    // Letters
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // Digits
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    // Function
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // Special
    Enter, Escape, Space, Tab, Backspace, Delete, Insert,
    Home, End, PageUp, PageDown,
    Up, Down, Left, Right,
};

struct KeyEvent {
    Key      key = Key::None;
    uint16_t modifiers = 0;
    bool     isRepeat = false;
    uint64_t timestampMs = 0;
    bool     consumed = false;
};

// ───────────────────────────────────────────────────────────────────
// ValueChangeSource — Fader, Knob, Toggle etc. distinguish the
// origin of a value change. Automation-driven writes don't fire
// user callbacks (avoids cascading to MIDI learn etc.); Programmatic
// writes on existing value are no-ops.
// ───────────────────────────────────────────────────────────────────
enum class ValueChangeSource {
    User,           // user-initiated (mouse drag, keyboard, etc.)
    Programmatic,   // code called setValue
    Automation,     // automation engine wrote the value
};

// ───────────────────────────────────────────────────────────────────
// Cache structs
// ───────────────────────────────────────────────────────────────────

struct MeasureCache {
    Constraints lastConstraints{};
    Size        lastResult{};
    int         globalEpoch   = -1;
    int         localVersion  = 0;     // bumped by invalidate()
    int         lastLocalSeen = -1;
};

struct LayoutCache {
    Rect lastBounds{};
    int  globalEpoch    = -1;
    int  measureVersion = -1;          // last-seen m_measure.localVersion
};

// ───────────────────────────────────────────────────────────────────
// Widget base class
// ───────────────────────────────────────────────────────────────────

class Widget {
public:
    Widget() = default;
    virtual ~Widget();

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // ─── Tree management ──────────────────────────────────────────
    void addChild(Widget* child);
    void removeChild(Widget* child);
    void removeAllChildren();
    Widget* parent() const                        { return m_parent; }
    const std::vector<Widget*>& children() const  { return m_children; }
    int childCount() const { return static_cast<int>(m_children.size()); }

    // ─── Layout: two-pass with caching ────────────────────────────
    // Call these from parents; they route through cache checks then
    // to onMeasure / onLayout as needed.
    Size measure(Constraints c, UIContext& ctx);
    void layout(Rect bounds, UIContext& ctx);

    // ─── Rendering ────────────────────────────────────────────────
    // Called from parent's render or from framework root. Base
    // recurses into children after paint.
    virtual void render(UIContext& ctx);

    // ─── Geometry ─────────────────────────────────────────────────
    const Rect& bounds() const { return m_bounds; }
    Rect globalBounds() const;
    Point toLocal(float sx, float sy) const;
    Point toGlobal(float lx, float ly) const;
    bool  hitTest(float lx, float ly) const {
        return lx >= 0 && lx < m_bounds.w && ly >= 0 && ly < m_bounds.h;
    }
    bool  hitTestGlobal(float sx, float sy) const;

    // ─── Invalidation ─────────────────────────────────────────────
    // Call when the widget's own state changes in a way that could
    // affect its measured size. Bubbles up to first relayout
    // boundary ancestor.
    void invalidate();

    // ─── Relayout boundaries ──────────────────────────────────────
    // Auto: widgets with SizePolicy::fixed() (flexWeight=0, no
    // constraint surprise) are boundaries by default. Opt-in:
    // setRelayoutBoundary(true) forces on; false forces off.
    bool isRelayoutBoundary() const;
    void setRelayoutBoundary(bool b);
    void clearRelayoutBoundaryOverride() { m_explicitBoundary.reset(); }

    // ─── State ────────────────────────────────────────────────────
    bool isVisible() const { return m_visible; }
    void setVisible(bool v);
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool v);
    bool isFocused() const { return m_focused; }
    bool isHovered() const { return m_hovered; }
    bool isPressed() const { return m_gesture.pressed; }
    bool isFocusable() const { return m_focusable; }
    void setFocusable(bool b) { m_focusable = b; }

    // Click-only widgets opt OUT of the gesture SM's drag-threshold
    // behaviour. Normally, moving the pointer more than kClickDrag
    // ThresholdPx between mouseDown and mouseUp flips the SM into
    // drag mode (firing onDragStart/Drag/End instead of onClick).
    // For widgets that don't have a drag behaviour — Button, Toggle,
    // Checkbox — that's a bug trap: hand-jitter during a press
    // swallows the click. setClickOnly(true) makes the SM NEVER
    // enter drag mode for this widget, so any press-release pair
    // fires onClick regardless of how much the pointer moved.
    bool isClickOnly() const { return m_clickOnly; }
    void setClickOnly(bool b) { m_clickOnly = b; }

    // ─── Size policy ──────────────────────────────────────────────
    SizePolicy sizePolicy() const { return m_sizePolicy; }
    void setSizePolicy(SizePolicy sp);

    Insets padding() const { return m_padding; }
    void setPadding(Insets p);

    // ─── Debug / accessibility ────────────────────────────────────
    const std::string& name() const       { return m_name; }
    void setName(std::string n)           { m_name = std::move(n); }
    const std::string& ariaLabel() const  { return m_ariaLabel; }
    void setAriaLabel(std::string a)      { m_ariaLabel = std::move(a); }

    // ─── Mouse capture (drag across bounds) ──────────────────────
    void captureMouse();
    void releaseMouse();
    bool hasMouseCapture() const;
    static Widget* capturedWidget();

    // ─── Raw event dispatch (called by framework event loop) ──────
    // Base implementations run gesture state machine then call onXxx.
    // Widgets override onMouseDown etc. to participate.
    virtual bool dispatchMouseDown(MouseEvent& e);
    virtual bool dispatchMouseUp(MouseEvent& e);
    virtual bool dispatchMouseMove(MouseMoveEvent& e);
    virtual bool dispatchScroll(ScrollEvent& e);
    virtual bool dispatchKeyDown(KeyEvent& e);
    virtual bool dispatchKeyUp(KeyEvent& e);

    // Find the deepest visible + enabled widget containing the point.
    Widget* findWidgetAt(float sx, float sy);

    // ─── Cache debug accessors (for tests) ─────────────────────
    int  measureVersion() const   { return m_measure.localVersion; }
    int  measureCacheEpoch() const { return m_measure.globalEpoch; }
    bool measureCacheValid(const Constraints& c, int epoch) const;
    int  measureCallCount() const { return m_measureCallCount; }

    // ─── Gesture state ─────────────────────────────────────────
    // Exposed publicly so free helpers in Widget.cpp can construct
    // drag/click events from it. (The struct is a detail of dispatch;
    // users never construct it.)
    struct GestureState {
        bool pressed       = false;
        bool dragging      = false;
        Point pressLocal;
        Point pressScreen;
        uint64_t pressTimeMs = 0;
        MouseButton pressButton = MouseButton::Left;
        uint16_t pressMods   = 0;

        Point lastMoveScreen;

        uint64_t lastClickEndMs = 0;
        Point    lastClickScreen;
    };

protected:
    // ─── Subclass overrides ───────────────────────────────────────
    // Pure: no side effects beyond cache writes. Returns desired
    // size under given constraints.
    virtual Size onMeasure(Constraints c, UIContext& ctx) = 0;

    // Default: store bounds, lay out children at the same bounds.
    // Containers override to position children.
    virtual void onLayout(Rect bounds, UIContext& ctx);

    // Painter — invoked by render() before children.
    virtual void paint(UIContext& ctx) { (void)ctx; }

    // Gesture callbacks (Level 2 — normal level).
    virtual void onClick(const ClickEvent&)        {}
    virtual void onDoubleClick(const ClickEvent&)  {}
    virtual void onRightClick(const ClickEvent&)   {}
    virtual void onDragStart(const DragEvent&)     {}
    virtual void onDrag(const DragEvent&)          {}
    virtual void onDragEnd(const DragEvent&)       {}
    virtual void onHoverChanged(bool /*hovered*/)  {}

    // Raw callbacks (Level 1 — override for special cases; return
    // true to consume). Defaults pass through to gesture state machine.
    virtual bool onMouseDown(MouseEvent&)          { return false; }
    virtual bool onMouseUp(MouseEvent&)            { return false; }
    virtual bool onMouseMove(MouseMoveEvent&)      { return false; }
    virtual bool onScroll(ScrollEvent&)            { return false; }
    virtual bool onKeyDown(KeyEvent&)              { return false; }
    virtual bool onKeyUp(KeyEvent&)                { return false; }

    virtual void onFocusGained() {}
    virtual void onFocusLost()   {}

    // ─── Internal state ──────────────────────────────────────────
    Rect  m_bounds{};
    Widget* m_parent = nullptr;
    std::vector<Widget*> m_children;

    bool m_visible   = true;
    bool m_enabled   = true;
    bool m_focused   = false;
    bool m_hovered   = false;
    bool m_focusable = false;
    bool m_clickOnly = false;

    SizePolicy m_sizePolicy;
    Insets     m_padding;

    std::string m_name;
    std::string m_ariaLabel;

    MeasureCache m_measure;
    LayoutCache  m_layout;

    // Optional manual override of the auto-detected boundary rule.
    struct BoundaryOverride {
        bool isSet = false;
        bool value = false;
        void set(bool v) { isSet = true; value = v; }
        void reset()     { isSet = false; value = false; }
        explicit operator bool() const { return isSet; }
    };
    BoundaryOverride m_explicitBoundary;

    GestureState m_gesture;

    // Debug/test hook
    int m_measureCallCount = 0;
};

// ───────────────────────────────────────────────────────────────────
// Gesture tuning constants (exposed for testing)
// ───────────────────────────────────────────────────────────────────
namespace gesture {
constexpr float kClickDragThresholdPx = 3.0f;
constexpr uint64_t kDoubleClickWindowMs = 400;
constexpr float kDoubleClickPosToleranceLogical = 4.0f;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
