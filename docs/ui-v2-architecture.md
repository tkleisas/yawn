# YAWN UI v2 — Architecture

This document defines the foundation every widget in `src/ui/framework/`
builds on. Per-widget specifications in `docs/widgets/*.md` reference
the concepts defined here. It is the framework's authoritative
reference; if code and this doc disagree, this doc is wrong and should
be updated.

The v1 framework (status quo on master) was cobbled together quickly
to unblock other work. v2 is a deliberate rebuild taking the
**Flutter + Yoga** architecture as its primary inspiration, adapted to
a C++17, retained-widget-tree, immediate-mode-rendering codebase.

---

## Goals

1. **Correct.** Faders feel responsive, dropdowns are never obscured,
   widgets behave identically regardless of where they are on screen.
2. **Composable.** Every widget follows the same lifecycle, the same
   event model, the same layout contract. A well-behaved widget is
   one that obeys these rules; the rules are few and explicit.
3. **Themeable.** Every color, size, and font is a token, swappable at
   runtime. No `Theme::foo` compile-time constants embedded in widget
   source.
4. **Fast enough.** Measure/layout is O(touched widgets per frame),
   not O(all widgets per frame). A static UI costs ~nothing; an
   animating panel costs proportional to what it affects.
5. **Predictable.** Invalidation rules are documented and short.
   You can look at a widget and know what triggers a re-measure.

## Non-goals

- Retained scene graph with GPU-level z-sorting. Painter's algorithm
  with explicit layers is enough for a DAW UI.
- Full CSS layout. Flexbox, 2D grid, and table layouts cover what we
  need; we don't need floats, inline-block, writing modes, etc.
- Declarative DSL. Widgets are constructed imperatively in C++.
- Accessibility (screen reader, AT-SPI). Designed to be addable later,
  not built out now.

## Inspirations

- **Flutter** — measure-then-layout two-pass system, relayout
  boundaries, `constraints → size` caching, invalidation that bubbles
  up and stops at boundaries.
- **Yoga / Taffy** — flexbox engine that caches `(node, constraints)
  → size` across frames and invalidates on mutation.
- **React** — portals for overlays; the logical parent keeps the
  child, rendering happens elsewhere.
- **Dear ImGui** — deferred overlay command lists; popups are drawn
  last regardless of when they were emitted.

---

## Scene root — the layer stack

The app's UI no longer has a single root widget. The `App` owns a
**layer stack** — an ordered list of layers, each a widget tree. Paint
happens lowest-layer-first; hit-testing happens highest-layer-first.

```
Layer 4  Toast      (status banners — positioned, stackable)
Layer 3  Tooltip    (transient hovers — at most one at a time)
Layer 2  Overlay    (dropdowns, context menus, drag previews)
Layer 1  Modal      (dialogs — Confirm, About, Preferences, Export, …)
Layer 0  Main       (panels, menu bar — the regular UI)
```

### Per-layer behavior

| Layer | Multiple entries? | Hit-test | Outside-click dismiss | Paint |
|---|---|---|---|---|
| Toast | yes (per position) | no — purely visual | n/a | last |
| Tooltip | no (single slot) | no | on mouse move | late |
| Overlay | yes | yes, topmost first | yes (default; opt-out via `OverlayEntry::modal=true`) | above Modal |
| Modal | yes (stacked, topmost blocks input) | yes, topmost only | no | above Main |
| Main | yes (tree) | yes, tree traversal | n/a | first |

**Invariant:** a widget on layer N cannot be obscured by a widget on
layer M < N. A dropdown opened from inside the mixer panel (Main)
lives on Overlay and therefore renders above **every** Main layer
widget — no exceptions, no race conditions.

### OverlayEntry

Widgets that need to "escape" their parent's clip rect or z-order
register an `OverlayEntry` on the appropriate layer:

```cpp
struct OverlayEntry {
    std::function<void(UIContext&)> paint;          // draw into the layer
    std::function<bool(MouseEvent&)> hitTest;       // optional; nullptr = bounds test
    Rect bounds;                                    // for default hit test + dismiss-outside
    bool modal = false;                             // true = block input to layers below
    bool dismissOnOutsideClick = true;
    Widget* owner = nullptr;                        // logical parent; receives callbacks
};

class LayerStack {
    void  push(int layer, OverlayEntry entry);      // returns opaque handle
    void  remove(OverlayEntryHandle h);
    // … hit test, paint, etc.
};
```

Lifecycle is explicit: the widget that opens the overlay keeps the
handle and must call `remove()` to dismiss. Example flow for a
dropdown click:

1. `FwDropDown::onClick` — pushes an `OverlayEntry` on Layer 2
   with a paint callback that renders the item list, a hit-test that
   maps clicks to item selection, and `dismissOnOutsideClick = true`.
2. User clicks an item — the hit test fires a selection callback,
   the handle is removed.
3. User clicks outside — `LayerStack` detects outside-click, fires
   the `onDismiss` callback, removes the handle.
4. Dropdown widget is destroyed — its destructor calls `remove()` on
   any still-live overlay handle.

### Hit testing across layers

Top-down: Toast (never hits) → Tooltip (never hits) → Overlay (hit-test,
consume-or-fall-through based on modal flag) → Modal (hit-test topmost
dialog; block if clicked outside) → Main (normal tree walk).

The framework's main-loop event dispatch becomes:

```cpp
for (auto& layer : layers | std::views::reverse) {
    if (layer.dispatch(event)) return;   // consumed
}
```

### Paint order

Bottom-up: Main → Modal → Overlay → Tooltip → Toast. Each layer does
its own push/pop of clip state; the framework guarantees a clean slate
between layers.

### Why not portals?

React-style portals (keep-logical-parent, re-parent-for-render) are
more flexible but demand a retained scene graph and a diffing
reconciler. Flutter's Overlay is cheaper and matches our imperative,
explicit-lifetime widget model better. Overlay entries aren't widgets
— they're closure pairs — which keeps the widget tree clean.

---

## Measure & layout with caching

The v1 system re-measures and re-lays-out every widget every frame.
That's fine for 50 widgets. We're past 500 in complex projects
(device chains, automation lanes, piano roll). v2 caches.

### Constraints

```cpp
struct Constraints {
    float minW = 0.0f;
    float maxW = std::numeric_limits<float>::infinity();
    float minH = 0.0f;
    float maxH = std::numeric_limits<float>::infinity();

    bool operator==(const Constraints&) const;          // for cache lookup
    Size  constrain(Size desired) const;                // clamps into range
    bool  isTight() const;                              // min == max on both axes
    bool  hasBoundedWidth() const { return std::isfinite(maxW); }
    bool  hasBoundedHeight() const { return std::isfinite(maxH); }
};
```

Constraints flow **down** the tree. Sizes flow **up**. A parent passes
constraints to each child's `measure()`; the child returns a desired
size; the parent chooses each child's final rect in its `layout()`.

This is Flutter's model verbatim. Yoga implements the same contract
for flexbox.

### The two passes

1. **Measure** — recursive, bottom-up-effective (child measured before
   parent can decide its own size). Deterministic: `measure(c) == measure(c)`
   for the same widget state. Pure: no side effects beyond cache writes.
2. **Layout** — recursive, top-down. Parent assigns each child's
   `Rect` given measured sizes. Children recurse into their own
   `layout()` with the assigned rect.

```cpp
class Widget {
public:
    Size measure(Constraints c, UIContext& ctx);   // cached wrapper
    void layout(Rect bounds, UIContext& ctx);       // cached wrapper

protected:
    virtual Size onMeasure(Constraints, UIContext&) = 0;   // override
    virtual void onLayout(Rect, UIContext&);               // override
};
```

Subclasses override `onMeasure` / `onLayout`. The base class handles
caching.

### Measure cache

```cpp
struct MeasureCache {
    Constraints lastConstraints{};
    Size         lastResult{};
    int          globalEpoch   = -1;   // compared against UIContext::epoch
    int          localVersion  = 0;    // bumped when this widget's content changes
    int          lastLocalVersionSeen = -1;
};

Size Widget::measure(Constraints c, UIContext& ctx) {
    const int epoch = ctx.epoch();
    if (m_measureCache.globalEpoch == epoch
     && m_measureCache.lastLocalVersionSeen == m_measureCache.localVersion
     && m_measureCache.lastConstraints == c) {
        return m_measureCache.lastResult;
    }
    Size result = onMeasure(c, ctx);
    m_measureCache.lastConstraints = c;
    m_measureCache.lastResult      = result;
    m_measureCache.globalEpoch     = epoch;
    m_measureCache.lastLocalVersionSeen = m_measureCache.localVersion;
    return result;
}
```

Invalidation:

- `localVersion` bumped by the widget itself when anything affecting
  size changes (text content, value when displayed numerically,
  visibility of children, child-list mutation, font size, …).
- `globalEpoch` bumped by the framework on events that affect
  everything: DPI change, window resize, theme change, font reload.

### Layout cache

```cpp
struct LayoutCache {
    Rect lastBounds{};
    int  globalEpoch   = -1;
    int  measureVersion = -1;   // cached layout invalid if measure changed
};

void Widget::layout(Rect b, UIContext& ctx) {
    if (m_layoutCache.globalEpoch == ctx.epoch()
     && m_layoutCache.lastBounds == b
     && m_layoutCache.measureVersion == m_measureCache.localVersion) {
        return;   // still valid, no-op
    }
    onLayout(b, ctx);
    m_bounds = b;
    m_layoutCache.lastBounds      = b;
    m_layoutCache.globalEpoch     = ctx.epoch();
    m_layoutCache.measureVersion  = m_measureCache.localVersion;
}
```

### Relayout boundaries

A widget marked as a **relayout boundary** declares: "my own size
does not depend on my children, and my children's layout does not
depend on my position." This lets the framework stop dirty
propagation at this widget and only re-layout its subtree, not the
whole ancestor chain.

Two ways to become a boundary:

1. **Auto-detect** — any widget whose `SizePolicy` is fixed (in both
   dimensions) is automatically a boundary. A 200×48 `FwButton` doesn't
   need to re-notify its parent when its label changes.
2. **Opt-in** — `setRelayoutBoundary(true)`. For cases where auto-detect
   gets it wrong: a scroll viewport, a fixed-footprint panel, a widget
   that is conceptually self-contained even if its size policy is
   stretchy.

When a widget calls `invalidate()`:

```cpp
void Widget::invalidate() {
    m_measureCache.localVersion++;
    m_layoutCache.measureVersion = -1;     // force re-layout next pass

    // Bubble up — stops at first relayout boundary.
    for (Widget* p = m_parent; p; p = p->m_parent) {
        if (p->isRelayoutBoundary()) break;
        p->m_measureCache.localVersion++;
        p->m_layoutCache.measureVersion = -1;
    }
}
```

### Invalidation triggers

**Global (bump `UIContext::epoch`):**
- DPI change (`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` or equivalent)
- Window resize
- Theme palette swap
- Font reload

**Per-widget (bump own `localVersion` + bubble):**
- Label/text content mutated
- Numeric value mutated, if displayed as text
- `setVisible()` / `setEnabled()` (these can affect layout)
- `addChild()` / `removeChild()` / child list reorder
- `setPadding()` / `setSizePolicy()`
- Font size override changed

The base `Widget` class provides `invalidate()`; setters that affect
layout call it automatically. Widget authors never need to remember
to call it manually if they use the provided setters.

### Animation interaction

A resizing panel is the worst case: its size changes every frame over
~300ms. Naïvely that forces a re-measure every frame.

Policy: **measure at animation start and end; interpolate bounds in
between.** The animation driver holds two cached `Rect`s — start and
end — and sets the widget's bounds to a lerp each frame. Measure cache
stays valid; only the layout cache is bumped, and even that can be
skipped if the widget's children are relayout boundaries.

Widgets that genuinely need to re-measure mid-animation (e.g. a
WaveformWidget streaming new samples) call `invalidate()` on their
own schedule, unaffected by the panel animation above them.

---

## Widget lifecycle (v2)

```
construct
  └─ addChild(into parent tree)
       └─ first frame:
          │   measure()   — triggered by parent
          │   layout()    — triggered by parent
          │   paint()     — if visible and bounds non-empty
          └─ per-frame:
             │   hit-test (on input)
             │   event handlers fire
             │   invalidate() as needed
             │   measure/layout/paint re-run only if cache invalid
  └─ removeChild(from parent tree)
       └─ destructor
          └─ unregister any overlay entries
          └─ release mouse capture if held
```

No frame-driver loop in the widget itself — everything is driven by
the framework's per-frame sweep starting from the `App`.

---

## Events

### Three levels

**Level 1 — raw** (still available for special cases):
```cpp
virtual bool onMouseDown(MouseEvent&);
virtual bool onMouseUp(MouseEvent&);
virtual bool onMouseMove(MouseMoveEvent&);
```

**Level 2 — gestures** (the normal level):
```cpp
virtual void onClick(const ClickEvent&);
virtual void onDoubleClick(const ClickEvent&);
virtual void onRightClick(const ClickEvent&);

virtual void onDragStart(const DragEvent&);
virtual void onDrag(const DragEvent&);     // e.dx, e.dy, e.cumulativeDx/Dy, e.modifiers
virtual void onDragEnd(const DragEvent&);

virtual bool onScroll(const ScrollEvent&);
virtual bool onHoverChanged(bool hovered);
```

Level 2 is default for widget authors. The base class runs a small
state machine on raw mouse events: press → track movement → if moved
> click-drag threshold (3 logical px) before release, fires drag
events; else fires `onClick`. Double-click detected by timing window.

**Level 3 — commands** (optional, for complex widgets):
```cpp
// e.g. TextInput may want typed commands rather than raw key events
virtual void onCommand(const Command&);   // Copy, Paste, Cut, …
```

### Event objects

Gesture events carry pixel deltas, modifier state, and logical-pixel
deltas (DPI-normalized) so widget authors rarely need to think about
DPI.

```cpp
struct DragEvent {
    float dx, dy;             // logical-pixel delta since last event
    float cumulativeDx, cumulativeDy;
    MouseButton button;
    uint16_t modifiers;       // Shift / Ctrl / Alt / Super
    Point startLocal;         // where the drag began, in widget-local coords
    Point currentLocal;
};
```

Widgets scale their response in **logical pixels**, not physical — so
a fader tuned to "200 logical px per full range" feels the same at 1×,
1.5×, and 2× DPI.

### Capture

`captureMouse()` on any widget routes all subsequent mouse events
(down, move, up) directly to it, bypassing hit testing, until
`releaseMouse()`. The base class handles capture automatically during
drags.

---

## Theme

Runtime-swappable. Replaces the v1 `namespace Theme { constexpr … }`.

```cpp
struct ThemePalette {
    // Fills
    Color background, panelBg, surface, elevated;
    Color controlBg, controlHover, controlActive;
    Color border, borderSubtle;

    // State
    Color accent, accentHover, accentActive;    // primary brand color
    Color success, warn, error;
    Color playing, recording, queued;

    // Modulation overlays (LFO / automation / CC visible-on-knob trails).
    // Distinct from accent so user's set-value and modulated-value are
    // visually separable at a glance.
    Color modulation;          // tail arc + indicator dot
    Color modulationRange;     // range halo (typically `modulation.withAlpha(60)`)

    // Text
    Color textPrimary, textSecondary, textDim;
    Color textOnAccent, textOnError;

    // Overlays
    Color dropShadow;
    Color scrim;      // modal dialog darkener

    // Track color palette (8 slots)
    std::array<Color, 8> trackColors;
};

struct ThemeMetrics {
    float baseUnit     = 4.0f;           // "spacing unit" — all paddings are multiples
    float cornerRadius = 3.0f;
    float borderWidth  = 1.0f;
    float controlHeight = 28.0f;
    float fontSize     = 13.0f;
    float fontSizeSmall= 11.0f;
    // …
};

struct Theme {
    std::string    name;
    ThemePalette   palette;
    ThemeMetrics   metrics;
};

const Theme& theme();                    // current theme
void setTheme(Theme t);                  // swaps, bumps UIContext epoch
```

Widgets read `theme().palette.controlBg` etc. A theme swap is a
single-call operation that invalidates all caches via epoch bump.

Future: load themes from JSON in `~/.yawn/themes/`. Out of scope for
v2's initial landing.

---

## Multi-toast

Toasts get the layer they deserve + full positioning. 9-cell screen
grid (corners + edges + center), each cell a vertical stack with
insertion-order, configurable lifetime, and optional group IDs for
replace-in-place semantics.

```cpp
enum class ToastPosition {
    TopLeft, TopCenter, TopRight,
    MidLeft, MidCenter, MidRight,
    BottomLeft, BottomCenter, BottomRight,
};

struct ToastOptions {
    ToastPosition  position = ToastPosition::TopCenter;
    float          duration = 1.5f;        // hold time (fade is fixed)
    Severity       severity = Severity::Info;
    std::string    groupId;                // replace-latest within group+position
    int            maxStack = 5;           // per-position cap; drop oldest when exceeded
};

void show(const std::string& text, ToastOptions opts = {});
```

Stacking rule: newest toast at the "start" edge of the position (top
for Top* positions, bottom for Bottom*, center for Mid*). Older
toasts offset away by `kToastHeight + kToastGap`. Fade-out gap-closes
via the same layout interpolator used by panels.

`groupId` semantics: when provided, a new toast with the same group
and position replaces the old one in place (no re-layout — same slot,
just swap text/severity/show-time). This is what "controller state
change" toasts (scale, root, track, volume, BPM) should all do: each
encoder gets its own groupId so fast spins update text rather than
stack.

Lua API:
```lua
yawn.toast("Saved")                                     -- defaults
yawn.toast("Volume: 75%", {group="master_vol"})         -- replace-latest
yawn.toast("Error", {severity=2, position="bottom-right", duration=3})
```

Old-style positional args (`yawn.toast(msg, dur, sev)`) remain
supported for migration; internally routes to the same path.

---

## Debug overlay

A key binding (default `Ctrl+Shift+D`) enables a translucent overlay
that:

- Colors widgets by cache state: **green** = served from cache,
  **yellow** = re-measured this frame, **red** = re-layouted this
  frame, **magenta** = painted but not in hit-test tree.
- Prints per-widget bounds + measure-count numbers.
- Draws layer boundaries (so you see exactly which layer an overlay
  lives on).
- Shows hover target name + global bounds.

Essential for catching cache bugs. Modeled loosely on Flutter's
DevTools widget inspector.

---

## Performance targets

| Metric | Target |
|---|---|
| Idle UI (nothing changing) | < 0.1 ms/frame on measure+layout |
| Scrolling waveform | < 1 ms/frame on measure+layout (scroll doesn't change widget sizes) |
| Panel animation | < 0.5 ms/frame extra — no re-measure, just bounds lerp |
| DPI change | Full frame OK (~16ms of bookkeeping) — infrequent event |
| Typing in text input | < 0.1 ms/frame on the text input subtree |

Baseline measured via the debug overlay's reporting. Regressions
caught in code review by spot-checking "how many widgets did this
frame measure?"

---

## Migration from v1

v1 and v2 coexist during the transition. Concretely:

1. Land the new `Widget` base (v2) alongside the current one as
   `fw2::Widget` in a new `src/ui/framework/v2/` directory.
2. Rewrite primitives one at a time — per-widget spec → `fw2::Fader`
   implementation → replace call sites.
3. Containers (FlexBox, FwGrid, etc.) get rewritten once enough
   primitives exist to populate them.
4. When all call sites are migrated and v1 is unused, delete the old
   directory and rename `fw2` → `fw`.

Rough ordering (from the spec index):

1. Architecture + Fader (this pass)
2. Button, Toggle (simpler gestures)
3. Knob (shares Fader's drag infrastructure)
4. TextInput, NumberInput (focus + keyboard)
5. Label (measure-from-font)
6. FlexBox (layout container)
7. DropDown (overlay + keyboard)
8. FwGrid (row-major grid)
9. Grid (2D CSS-grid-style)
10. Table (headers + rows + virtualized scroll + selection)
11. SnapScrollContainer (scrolling viewport)
12. Dialog (modal layer consumer)

Each pass is its own commit / PR with the widget spec checked in
next to it. Old v1 widget stays working through the whole transition;
no master-break events.

---

## Open questions

1. **Should SizePolicy be unified with Constraints?** Currently `SizePolicy`
   is a separate `fixed/flex/stretch` enum. Flutter wraps it into the
   constraints themselves (intrinsic / tight / loose). Worth considering
   as part of the FlexBox rewrite.

2. **Immediate-mode interop?** Some widgets (debug overlay,
   VisualizerWidget) are very immediate-mode in spirit. Do we keep
   the retained model everywhere, or offer an escape hatch?

3. **Undo/redo integration?** Widgets that mutate project state
   (knobs especially) currently call setter callbacks directly. Should
   v2 standardize on "emit a Command; App decides whether to undo-
   wrap"?

4. **Virtualization for long lists?** Table and the piano roll editor
   should virtualize, but what's the contract? Probably a `virtual int
   visibleRowCount()` + `virtual void paintRow(row, ctx)` interface
   rather than full-tree widget children.

These don't block v2.0. Flagged for the widget specs that need them.
