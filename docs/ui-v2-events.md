# YAWN UI v2 — Event Model Deep Dive

Comprehensive reference for the three-level event model (raw,
gesture, command), event propagation through the widget tree and
layer stack, focus management, mouse capture, and DPI
normalization.

Companion docs:
[`ui-v2-architecture.md`](ui-v2-architecture.md),
[`ui-v2-layer-stack.md`](ui-v2-layer-stack.md),
[`ui-v2-measure-layout.md`](ui-v2-measure-layout.md).

---

## Three levels

v2 widgets interact with input at one of three levels, each built on
top of the one below:

1. **Raw events** — `onMouseDown` / `onMouseUp` / `onMouseMove`,
   `onKeyDown` / `onKeyUp` / `onTextInput`, `onScroll`. The lowest
   level — direct translations of SDL events. Used by widgets that
   need fine-grained control (TextInput's caret handling, WaveformWidget's
   marker dragging, custom viewports).

2. **Gesture events** — `onClick` / `onDoubleClick` / `onRightClick`,
   `onDragStart` / `onDrag` / `onDragEnd`, `onHoverChanged`,
   `onScroll` (at this level includes horizontal-scroll abstraction
   etc.). The normal level for most widgets. The base class runs a
   state machine on top of raw events to derive these.

3. **Commands** — high-level typed actions: `Copy`, `Paste`,
   `SelectAll`, `Undo`, etc. TextInput and a few other widgets
   consume commands instead of raw keys. Commands abstract over
   platform-specific shortcut differences (Ctrl vs Cmd) and give
   menus / shortcuts a common interface.

Most widgets use level 2. Level 1 is fallback; level 3 is opt-in.

---

## Raw event types

### Pointer

```cpp
enum class MouseButton : uint8_t {
    Left, Middle, Right,
    // Future: Back, Forward, …
};

struct MouseEvent {
    float x, y;              // screen coordinates (logical pixels)
    float lx, ly;            // local coordinates (within target widget)
    MouseButton button;
    uint16_t modifiers;      // bitmask of ModifierKey
    uint64_t timestampMs;
    EventPhase phase;        // Capture, Target, Bubble
    bool consumed = false;
};

struct MouseMoveEvent {
    float x, y;              // screen (logical)
    float lx, ly;             // local
    float dx, dy;             // delta since last move (logical)
    uint32_t buttonMask;      // bitmask of held buttons
    uint16_t modifiers;
    uint64_t timestampMs;
    bool consumed = false;
};

struct ScrollEvent {
    float x, y;              // pointer position
    float lx, ly;             // local
    float dx, dy;             // scroll delta (positive = down/right)
    bool isPrecise;           // true for trackpads (smooth), false for wheels (detents)
    uint16_t modifiers;
    uint64_t timestampMs;
    bool consumed = false;
};

struct ModifierKey {
    enum : uint16_t {
        Shift   = 1 << 0,
        Ctrl    = 1 << 1,
        Alt     = 1 << 2,
        Super   = 1 << 3,    // Win key / Cmd key
        CapsLock = 1 << 4,
    };
};
```

### Keyboard

```cpp
enum class Key {
    // Letters
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // Digits
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // Special
    Enter, Escape, Space, Tab, Backspace, Delete, Insert,
    Home, End, PageUp, PageDown,
    Up, Down, Left, Right,
    Minus, Plus, Equals, Comma, Period, Slash, Backslash,
    Quote, Semicolon, LeftBracket, RightBracket, Backquote,
    // …
};

struct KeyEvent {
    Key key;
    uint16_t modifiers;
    bool isRepeat;           // true for auto-repeat (key held)
    uint64_t timestampMs;
    bool consumed = false;
};

struct TextInputEvent {
    uint32_t codepoint;      // UTF-32 codepoint from IME
    uint64_t timestampMs;
    bool consumed = false;
};
```

### Focus

```cpp
struct FocusEvent {
    Widget* previousFocus;   // where focus came from
    Widget* newFocus;         // where focus is going
    bool isGaining;           // true for the widget gaining focus; false for losing
};
```

---

## Propagation: Capture → Target → Bubble

Mouse events traverse the widget tree in three phases:

```
    Root
     │
     ├─ CapturePhase (Root → parent → target): Root gets first look
     │
     ├─ TargetPhase: deepest hit widget handles
     │
     └─ BubblePhase (target → parent → Root): event walks back up
```

In YAWN v2:

- **Phase 1: Hit-test** — identifies the deepest widget whose bounds
  contain the event. O(tree depth).
- **Phase 2: Capture** — not typically overridden. Widgets that
  want to intercept before children (e.g., drag-and-drop zones) mark
  themselves as capture-phase consumers.
- **Phase 3: Target** — the hit widget's `onMouseDown` runs. If it
  returns true (or sets `e.consumed`), dispatch stops.
- **Phase 4: Bubble** — if not consumed, walk back up the parent
  chain calling `onMouseDown` on each ancestor.

### Consumption

A widget consumes an event by either returning `true` from its
handler OR setting `e.consumed = true`. Both are equivalent.

Consumption rules:
- **Immediate stop**: consumed events don't propagate further.
- **Capture can't un-consume**: once consumed, no ancestor / descendant
  sees it.
- **Inherited behavior**: default handlers return `false`
  (pass-through). Widget authors must explicitly return `true` when
  they handle an event.

### Layer stack + widget tree order

Full event dispatch order:

1. LayerStack (Toast → Tooltip → Overlay → Modal) — see
   [`ui-v2-layer-stack.md`](ui-v2-layer-stack.md).
2. Captured widget (if any — see Mouse Capture below).
3. Main layer's widget tree (Capture → Target → Bubble).

Each consumption stops traversal at that point.

---

## Gesture layer

The gesture layer is a **state machine built into the Widget base
class** that watches raw mouse events and emits higher-level
callbacks.

```cpp
class Widget {
protected:
    // Level 1 (raw) — override for direct control
    virtual bool onMouseDown(MouseEvent&) { return false; }
    virtual bool onMouseUp(MouseEvent&)   { return false; }
    virtual bool onMouseMove(MouseMoveEvent&) { return false; }

    // Level 2 (gestures) — override for the normal case
    virtual void onClick(const ClickEvent&)      {}
    virtual void onDoubleClick(const ClickEvent&){}
    virtual void onRightClick(const ClickEvent&) {}
    virtual void onDragStart(const DragEvent&)   {}
    virtual void onDrag(const DragEvent&)         {}
    virtual void onDragEnd(const DragEvent&)      {}
    virtual void onHoverChanged(bool hovered)    {}
    virtual bool onScroll(const ScrollEvent&)    { return false; }

private:
    // Framework-owned state machine
    struct GestureState {
        bool pressed = false;
        bool dragging = false;
        Point pressLocal;
        Point pressScreen;
        uint64_t pressTimeMs;
        MouseButton pressButton;

        bool waitingForSecondClick = false;
        uint64_t firstClickTimeMs;

        bool hovered = false;
    };
    GestureState m_gesture;
};
```

### Click vs drag threshold

```cpp
constexpr float kClickDragThresholdPx = 3.0f;  // logical pixels
```

When the user presses mouse button and moves the pointer, the
gesture engine watches:

- Movement < threshold: still in "press" state.
- Movement ≥ threshold before release: commits to drag; fires
  `onDragStart`.
- Release before threshold crossed: fires `onClick`.

This tolerance is what lets users wiggle slightly during a click
without canceling the click. 3 px is the convention in most
frameworks.

### Double-click window

```cpp
constexpr uint64_t kDoubleClickWindowMs = 400;
```

Two clicks within 400 ms at approximately the same position (within
threshold) fire `onDoubleClick` instead of two separate `onClick`s.
The first click still fires `onClick` — double-click is an
additional event, not a replacement. Widget authors that handle
double-click distinctly check and respond to both.

### Default gesture behavior

```cpp
// Base class default — translates raw to gestures
bool Widget::onMouseDown(MouseEvent& e) {
    m_gesture.pressed = true;
    m_gesture.pressLocal = {e.lx, e.ly};
    m_gesture.pressScreen = {e.x, e.y};
    m_gesture.pressTimeMs = e.timestampMs;
    m_gesture.pressButton = e.button;

    if (e.button == MouseButton::Left) {
        captureMouse();  // receive all subsequent events regardless of hit
    }
    return false;  // allow subclasses to also handle
}

bool Widget::onMouseMove(MouseMoveEvent& e) {
    if (m_gesture.pressed && !m_gesture.dragging) {
        // Check if we've crossed the drag threshold.
        float dx = e.x - m_gesture.pressScreen.x;
        float dy = e.y - m_gesture.pressScreen.y;
        float dist = std::sqrt(dx*dx + dy*dy);
        if (dist >= kClickDragThresholdPx) {
            m_gesture.dragging = true;
            DragEvent de = makeDragStart(e);
            onDragStart(de);
        }
    }
    if (m_gesture.dragging) {
        DragEvent de = makeDragContinue(e);
        onDrag(de);
    }
    return false;
}

bool Widget::onMouseUp(MouseEvent& e) {
    if (m_gesture.pressed) {
        if (m_gesture.dragging) {
            DragEvent de = makeDragEnd(e);
            onDragEnd(de);
        } else {
            // Click or double-click
            ClickEvent ce = makeClick(e);
            if (shouldTreatAsDouble(e)) {
                onDoubleClick(ce);
            } else {
                onClick(ce);
            }
        }
    }
    m_gesture.pressed = false;
    m_gesture.dragging = false;
    if (hasMouseCapture()) releaseMouse();
    return false;
}
```

Widgets that override `onClick` without touching raw events get the
full gesture state machine for free. Widgets that override raw events
opt out of the state machine for that event type — responsible for
emitting gestures themselves if they want them.

---

## Gesture event types

```cpp
struct ClickEvent {
    Point local;              // widget-local coordinates
    Point screen;
    MouseButton button;
    uint16_t modifiers;
    int clickCount;           // 1 or 2 (for double-click events)
    uint64_t timestampMs;
};

struct DragEvent {
    Point startLocal;         // where drag began (widget-local)
    Point startScreen;
    Point currentLocal;
    Point currentScreen;
    float dx, dy;             // delta since last event
    float cumDx, cumDy;       // cumulative delta from startScreen
    MouseButton button;
    uint16_t modifiers;
    uint64_t timestampMs;
};
```

### DPI-normalized deltas

Critical feature: `dx, dy, cumDx, cumDy` are in **logical pixels**,
not physical. The framework divides raw SDL delta by
`ctx.dpiScale()` before populating DragEvent.

Consequence: a fader's drag math is the same at 1× DPI and 2× DPI.
Widgets don't have to think about it.

---

## Scroll events

Scroll events come in two flavors:

- **Wheel scroll** — discrete detents (mouse wheel). `isPrecise =
  false`, `dy` typically ±1 per detent.
- **Precise scroll** — trackpad / trackball smooth scroll.
  `isPrecise = true`, `dy` in sub-detent units (often 0.1–1.0).

Widgets often multiply by a sensitivity factor to translate detents
to their own units. For ScrollView: `pxScroll = detents × 40` by
default. For Knob: `paramDelta = detents × step`.

### Horizontal scroll

Shift modifier during wheel scroll conventionally means horizontal.
SDL3 also reports horizontal scroll separately on some hardware
(precise trackpads); we collapse both into `dx` on `ScrollEvent`.
Widgets handle `dx != 0` as horizontal scrolling.

---

## Command layer

High-level typed commands — abstraction over platform shortcuts.

```cpp
enum class Command {
    Undo, Redo,
    Copy, Cut, Paste, SelectAll,
    Delete, Duplicate,
    ZoomIn, ZoomOut, ZoomReset,
    // Custom app commands
    Save, SaveAs, Open, New, Close,
    PlayPause, TapTempo,
    // …
};

struct CommandEvent {
    Command command;
    uint16_t modifiers;      // the exact mods at dispatch time (some commands behave differently with mods)
    bool consumed = false;
};

class Widget {
    virtual bool onCommand(CommandEvent&) { return false; }
};
```

### Dispatch

The app's key handler maps key combinations to commands:

```cpp
void App::handleKeyDown(KeyEvent& e) {
    // 1. Try focused widget's raw onKeyDown first.
    if (focusedWidget && focusedWidget->dispatchKeyDown(e)) return;

    // 2. Check if the key combo maps to a command.
    if (auto cmd = keyToCommand(e)) {
        CommandEvent ce{*cmd, e.modifiers, false};
        // Dispatch to focused widget first.
        if (focusedWidget && focusedWidget->onCommand(ce)) return;
        // Then application-level handler.
        if (handleAppCommand(ce)) return;
    }

    // 3. MenuBar accelerator dispatch.
    if (m_menuBar.tryDispatchAccelerator(e)) return;

    // 4. Unhandled — ignored.
}
```

### Platform abstraction

`keyToCommand` is the platform-specific mapping:

| Command | Windows/Linux | macOS |
|---|---|---|
| Copy | Ctrl+C | Cmd+C |
| Undo | Ctrl+Z | Cmd+Z |
| SelectAll | Ctrl+A | Cmd+A |
| PlayPause | Space | Space |

Widgets handling `onCommand(Copy)` Just Work regardless of platform.

### Bypass command layer

Widgets not using commands (most) just handle raw keys. Commands
are for:
- Text inputs (clipboard operations, select-all).
- Canvas widgets (zoom commands).
- Any widget where clipboard / navigation / selection keys have
  standard meaning that should be platform-consistent.

---

## Focus

Every widget can be focusable (`setFocusable(true)`). Focus affects:

1. **Keyboard dispatch** — events go to the focused widget first.
2. **Visual state** — focused widgets draw a focus ring.
3. **Tab order** — Tab / Shift+Tab cycle focus through focusables.

### Focus API

```cpp
class Widget {
public:
    bool isFocusable() const;
    void setFocusable(bool);
    bool isFocused() const;

    // Request focus. Returns true if accepted (widget is focusable
    // and enabled).
    bool requestFocus();
    void releaseFocus();

protected:
    virtual void onFocusGained() {}
    virtual void onFocusLost()   {}
};
```

### Focus capture + restore

When the current focused widget is removed from the tree or becomes
hidden / disabled, focus is transferred to:
1. Its previously-focused ancestor, if any.
2. Otherwise, the root widget.

On transfer: `onFocusLost` on old + `onFocusGained` on new.

### Focus traps

Modal dialogs trap focus within their subtree. See
[`ui-v2-layer-stack.md`](ui-v2-layer-stack.md) Focus integration.

### Tab order

Default: depth-first, left-to-right (parent before children,
children in addChild order). Widgets override by implementing custom
focus traversal (rarely needed).

Tab traversal:

```cpp
Widget* nextFocusable(Widget* current, bool backwards) {
    // Walk tree from current, skipping non-focusable widgets,
    // wrapping at boundaries.
    // ...
}
```

Skipped:
- `isFocusable() == false`
- `isEnabled() == false`
- `isVisible() == false`
- Widgets inside hidden ancestors.

### Focus on click

Per-widget policy: `setFocusOnClick(bool)`.

- **true**: clicking the widget focuses it (text inputs, some buttons).
- **false**: clicks don't steal focus (mixer faders, arrangement
  clips — don't want to thrash focus from a text input in the
  browser panel when clicking a mixer channel).

Default: false for most widgets, true for text-input widgets.

---

## Mouse capture

When a widget captures the mouse, all subsequent mouse events
(down, move, up) route directly to it regardless of the pointer's
position. Necessary for drags that might leave the widget's bounds:

- Fader drag: pointer can leave the fader rect mid-drag;
  capture ensures the fader still receives the move and the final
  release.
- Dialog drag: dialog moves with pointer even if pointer leaves the
  title bar briefly.
- DropDown drag: user drags through multiple items; capture keeps
  the popup receiving moves.

### API

```cpp
class Widget {
public:
    void captureMouse();              // next events come directly here
    void releaseMouse();              // stop capturing
    bool hasMouseCapture() const;

    // Framework
    static Widget*& capturedWidget(); // global capture slot
};
```

Only one widget can capture at a time. `captureMouse` on a second
widget silently replaces the first (the first receives no
notification). Pattern: widgets capture on `onMouseDown` and release
on `onMouseUp`.

### Interaction with layers

A captured widget receives events even when a Modal or Overlay layer
entry would normally block them. This is intentional: if you've
started a drag on a fader and a modal dialog appears mid-drag (rare
but possible), the drag continues until release. The alternative
(drag canceled by dialog) would feel broken.

---

## Event flow — putting it all together

Full flow for a mouse down event:

```
SDL raw event
  ↓
App::handleMouseDown(SDL_Event)
  ↓
Translate to logical coords via dpiScale
  ↓
Construct MouseEvent
  ↓
m_layerStack.dispatchMouseDown(e)
  ↓ (if not consumed)
m_rootWidget.dispatchMouseDown(e)
  ├─ captured widget? → dispatch directly to it, done
  ├─ otherwise:
  │   ├─ hit-test tree to find target
  │   ├─ walk Root → target (capture phase)
  │   ├─ target.onMouseDown → gesture state machine → onClick/onDragStart/...
  │   └─ walk target → Root (bubble phase)
  └─ if unconsumed → dropped
```

Paint happens AFTER all event dispatch for the frame. Widgets whose
state changed during event handling will show it in the paint pass.

---

## Performance

- **Hit-test**: O(tree depth). Typically 4–6 widgets on the path.
  Negligible.
- **Gesture state machine**: O(1) per event. ~1 µs.
- **Command dispatch**: O(1) with hash lookup of key-combo →
  Command.
- **Event allocation**: stack-allocated structs, no heap.

Input latency from OS event to widget callback: < 1 ms on modern
hardware. Dominated by SDL's event queue + v-sync.

---

## Testing

Unit tests in `tests/test_fw2_Events.cpp`:

### Raw dispatch

1. `OnMouseDownCalledOnTarget` — hit-test'd widget's onMouseDown
   fires.
2. `BubblesIfNotConsumed` — event not consumed by target bubbles to
   ancestor.
3. `StopsOnConsume` — setting consumed = true halts bubble.
4. `CaptureBypassesHitTest` — captured widget receives events
   regardless of pointer position.

### Gesture

5. `ClickEmittedAfterPressAndRelease` — mousedown + mouseup (no
   move) fires onClick.
6. `DragStartAfterThreshold` — press + move >3 px fires onDragStart.
7. `DragEmittedContinuously` — subsequent moves fire onDrag with
   updated deltas.
8. `DragEndOnRelease` — mouseup after drag fires onDragEnd.
9. `DoubleClickWindow` — two clicks <400 ms fire onDoubleClick.
10. `DoubleClickCountsBoth` — onClick fires once, then onDoubleClick
    fires with count=2. (Depends on widget: some only respond to one
    or the other.)
11. `DragThresholdIsLogical` — same physical dx at 2× DPI gives
    same logical dx in DragEvent.
12. `RightClickFiresRightClick` — right-button click fires
    onRightClick, not onClick.

### Commands

13. `CtrlCMapsToCopy` — Ctrl+C fires onCommand(Copy).
14. `CmdCOnMacMapsToCopy` — platform-specific mapping.
15. `CommandConsumptionStops` — consumed command doesn't propagate
    to app handler.
16. `UnhandledKeyFallsThroughCommands` — key without mapping
    reaches menu accelerator dispatch.

### Focus

17. `RequestFocusSetsFocused` — requestFocus changes focus state.
18. `OnFocusGainedFires` — callback fires on gain.
19. `FocusLostOnNewFocus` — previous focus receives onFocusLost.
20. `TabAdvancesFocus` — Tab moves to next focusable widget.
21. `ShiftTabReverses`.
22. `FocusSkipsDisabled` — disabled widgets are skipped.
23. `FocusSkipsInvisible` — hidden widgets are skipped.
24. `ClickFocusOptIn` — setFocusOnClick(false): click doesn't change
    focus.

### Mouse capture

25. `CaptureReceivesMoves` — captured widget gets moves even outside
    its bounds.
26. `ReleaseEndsCapture` — releaseMouse stops routing to widget.
27. `NewCaptureOverridesOld` — second captureMouse replaces first
    silently.

### Layer integration

28. `LayerEntryInterceptsBeforeTree` — active Overlay entry receives
    events before Main tree.
29. `ModalBlocksMainTree` — modal on Modal layer prevents Main tree
    from receiving events.
30. `UnconsumedEventsFallThrough` — Overlay entry returning false
    (non-modal) allows event to reach Main tree.

---

## Open questions

1. **Pointer events abstraction?** Currently mouse and touch are
   separate code paths. Future unified `PointerEvent` like the web
   spec. Deferred until touch support is a priority.

2. **Gesture customization?** Click-drag threshold, double-click
   window are hardcoded constants. Expose via
   `setClickDragThreshold(px)` on Widget or globally? Defer until a
   real use case needs customization.

3. **Long-press / pinch / rotate?** Additional gestures in the
   state machine. Add when touch support lands.

4. **Gesture recognizers pattern?** iOS-style gesture recognizers
   are composable and cancelable. YAWN's flat state machine is
   simpler but less extensible. Enough for now.

5. **Global event logging?** For debugging, log every event hitting
   every widget to a file. Dev-only flag. Nice-to-have.

6. **Event coalescing?** SDL may deliver 100 mouse-move events in a
   single frame; do we coalesce to one? Current: pass through each
   to the gesture engine, which emits one onDrag per move. For
   widgets that want coalesced moves (heavy paint during drag),
   they can check `e.timestampMs` vs last seen and skip.

7. **Command undo integration?** Built-in Commands (Undo, Redo) need
   an UndoManager hookup. Currently app-level; consider pushing
   closer to Widget so per-widget undo is natural.
