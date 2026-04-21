# SplitView — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**New widget.**
**Related:** [flex_box.md](flex_box.md) — similar axial layout but
with a user-draggable divider between exactly two panes.

---

## Intent

A **two-pane resizable splitter**. Displays two children separated
by a draggable divider; the user adjusts the split ratio by dragging.
Used where a layout has "primary" + "secondary" regions that benefit
from user-adjustable proportions:

- Main window: left sidebar (browser) + main content.
- Preferences dialog: category list + settings pane.
- Piano roll: keyboard column + note grid (via vertical split).
- Arrangement view: track headers + timeline (via vertical split
  with fixed left pane).

SplitView is strictly two-pane. Multi-pane splits are nested
SplitViews. This keeps the divider interaction unambiguous (one
divider per SplitView) and simplifies persistence (one ratio per
instance).

## Non-goals

- **More than two panes.** Nested SplitViews produce any layout you
  need; a single N-pane widget would have ambiguous divider semantics
  and complicate the min/max rules.
- **Per-pane scroll.** Each pane's content handles its own scrolling
  via ScrollView if needed. SplitView just divides space.
- **Independent drag handles per edge.** Single middle divider only.
  Edge-dragging to resize the whole SplitView belongs to its parent.
- **Animation during drag.** Drag is 1:1 with pointer. Animations
  only on programmatic ratio changes or collapse/expand.

---

## Visual anatomy

### Horizontal (most common — vertical divider between left / right)

```
    ┌─────────────────┬──┬───────────────────┐
    │                  │▓▓│                    │
    │                  │▓▓│                    │
    │  left pane       │▓▓│  right pane        │
    │                  │▓▓│                    │
    │                  │▓▓│                    │
    └─────────────────┴──┴───────────────────┘
                       ↑
                 divider (draggable)
```

### Vertical (horizontal divider between top / bottom)

```
    ┌──────────────────────────────────────────┐
    │                                           │
    │    top pane                               │
    │                                           │
    ├══════════════════════════════════════════┤  ← draggable divider
    │                                           │
    │    bottom pane                            │
    │                                           │
    └──────────────────────────────────────────┘
```

Divider parts:
1. **Track** — thin rect (6 logical px default) spanning the
   cross-axis extent.
2. **Grip indicator** — optional 2-3 dots centered on the divider,
   `textSecondary` color. Improves discoverability.
3. **Hover / drag tint** — divider brightens on hover; `accent`
   during drag.

Cursor changes to resize cursor (N-S or E-W) when pointer is over
the divider.

---

## Behavior

### Sizing model

SplitView stores a **divider position** as either:

- A **ratio** (0.0 = first pane collapsed, 1.0 = second pane
  collapsed) — the default.
- An **absolute px from start** (first pane is N px wide).
- An **absolute px from end** (second pane is N px wide).

Why three modes: different use cases want different persistence
semantics:

- Main window "sidebar on left" wants a **px-from-start** so the
  sidebar's saved width doesn't shift when the main window is
  resized.
- Preferences dialog category list wants ratio — "30% of dialog" is
  a reasonable default that scales to different dialog sizes.
- Footer panel at the bottom wants **px-from-end** — bottom pane
  stays the same size when window grows.

```cpp
enum class SplitMode {
    Ratio,      // 0..1 fraction of SplitView's main-axis size
    FixedStart, // first pane is N px (bounded by min/max)
    FixedEnd,   // second pane is N px
};
```

### Min / max per pane

Each pane has a `minSize` (default 40 px) and `maxSize` (default
∞) on the split axis. User-drag is clamped:

```cpp
firstPane.size >= first.minSize && firstPane.size <= first.maxSize
secondPane.size >= second.minSize && secondPane.size <= second.maxSize
```

When SplitView itself is resized, if the new bounds force a pane
below its min, that pane gets its min size and the other absorbs the
rest (possibly going below its own max).

### Collapse / expand

Optional: either pane can be flagged as **collapsible**. A
collapsible pane can be set to 0 size (pane hidden, divider at
extreme edge). Mouse: double-click the divider collapses the nearer
pane (or expands back if already collapsed). Programmatic:
`collapse(Pane::First)` / `expand(Pane::First)`. Collapse animation
is 200 ms ease-out.

### Persistence

Split state is public:

```cpp
float dividerRatio() const;
float firstPaneSize() const;
float secondPaneSize() const;
```

Caller saves whichever value matches the widget's `SplitMode`, loads
it back on startup. No built-in persistence; caller owns that.

---

## Public API

```cpp
enum class SplitOrientation {
    Horizontal,  // vertical divider, left/right panes (main axis = width)
    Vertical,    // horizontal divider, top/bottom panes (main axis = height)
};

enum class SplitPane {
    First,   // left or top
    Second,  // right or bottom
};

struct SplitConstraints {
    float minSize = 40.0f;
    float maxSize = 9999.0f;
    bool  collapsible = false;
};

class SplitView : public Widget {
public:
    SplitView();
    explicit SplitView(SplitOrientation o);

    // Panes (exactly two children)
    void setFirst(Widget* pane);
    void setSecond(Widget* pane);

    // Orientation
    void setOrientation(SplitOrientation);

    // Divider position — choose ONE mode, setters are mutually exclusive:
    void setRatio(float r);              // 0..1; SplitMode = Ratio
    void setFirstPaneSize(float px);     // SplitMode = FixedStart
    void setSecondPaneSize(float px);    // SplitMode = FixedEnd
    SplitMode splitMode() const;
    float dividerRatio() const;
    float firstPaneSize() const;
    float secondPaneSize() const;

    // Pane constraints
    void setConstraints(SplitPane pane, SplitConstraints c);

    // Collapse / expand
    void collapse(SplitPane pane);           // animates to 0
    void expand(SplitPane pane);             // animates back to pre-collapse size
    void toggleCollapse(SplitPane pane);
    bool isCollapsed(SplitPane pane) const;

    // Divider appearance
    void setDividerThickness(float px);      // default 6
    void setShowDividerGrip(bool);           // default true
    void setDividerColor(Color c);
    void clearDividerColor();

    // Behavior
    void setDoubleClickCollapses(bool);      // default true
    void setKeyboardResize(bool);            // default true — arrows resize divider when focused

    // Callbacks
    void setOnDividerMoved(std::function<void(float newRatioOrSize)>);
    void setOnCollapseChanged(std::function<void(SplitPane, bool collapsed)>);

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

---

## Gestures

### Pointer

| Gesture | Result |
|---|---|
| **Hover divider** | Divider tint brightens; cursor → resize cursor. |
| **Left-button drag on divider** | Divider follows pointer on main axis; pane sizes update continuously. `onDividerMoved` fires. Clamped to min/max of both panes. |
| **Double-click divider** | If either adjacent pane is collapsible, collapse the smaller one (or expand if already collapsed). Opt-out via `setDoubleClickCollapses(false)`. |
| **Right-click divider** | Fires `onDividerRightClick` for future context menu (reset to default ratio, etc.). |

### Keyboard (when divider focused)

| Key | Action |
|---|---|
| `Left` / `Right` (horizontal split) | Move divider ±1 logical px |
| `Up` / `Down` (vertical split) | Move divider ±1 logical px |
| `Shift + arrows` | Move by 10 px |
| `Ctrl + arrows` | Move by 50 px |
| `Home` | Collapse first pane (if collapsible) or clamp to firstMin |
| `End` | Collapse second pane or clamp to firstMax |
| `Enter` / `Space` | Toggle collapse on double-click-target pane |

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size SplitView::onMeasure(Constraints c, UIContext& ctx) {
    // Take what the parent offers on both axes. Children's intrinsic
    // sizes don't drive the SplitView's own size.
    float w = c.hasBoundedWidth()  ? c.maxW : 400.0f;
    float h = c.hasBoundedHeight() ? c.maxH : 300.0f;
    return c.constrain({w, h});
}
```

### `onLayout(Rect bounds, UIContext& ctx)`

1. Determine main-axis extent (`bounds.w` for Horizontal,
   `bounds.h` for Vertical) minus divider thickness.
2. Compute current divider position based on mode:
   - Ratio: `firstSize = ratio × available`
   - FixedStart: `firstSize = storedPx` (clamped to constraints)
   - FixedEnd: `firstSize = available - storedPx`
3. Clamp against both pane constraints (firstMin/Max + secondMin/Max).
4. Lay out first pane in `[0, firstSize]` on main axis.
5. Lay out divider at `firstSize` on main axis, thickness wide.
6. Lay out second pane in `[firstSize + thickness, available]`.

When resized: same algorithm reruns; if the new available space
would violate a constraint, the divider shifts to satisfy minimums
(second pane absorbs remainder).

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

### Relayout boundary

**Yes, automatically** — children's intrinsic sizes don't
propagate outward; SplitView's bounds are fully parent-driven.

### Caching

Measure cache trivial. Layout cache key: `(bounds, ratio/size,
orientation, constraints on both panes, divider thickness, collapse
state)`. Pane child measure invalidation is layout-invalidating for
SplitView (panes need re-layout with possibly-new measured sizes),
but doesn't bubble up past.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.border` | Divider track color (idle) |
| `palette.controlHover` | Divider hover fill |
| `palette.accent` | Divider drag fill |
| `palette.textSecondary` | Grip dots |
| `metrics.splitterThickness` | Default divider thickness (6 px) |

---

## Events fired

- `onDividerMoved(newPosition)` — continuous during drag.
- `onCollapseChanged(pane, collapsed)` — when pane collapse state
  changes.
- FocusEvent — divider gets focus when tabbed to.

---

## Invalidation triggers

### Measure-invalidating

- `setOrientation`
- `setDividerThickness`
- DPI / theme (global)

### Layout-invalidating

- `setRatio`, `setFirstPaneSize`, `setSecondPaneSize`
- `setConstraints`
- `collapse` / `expand` / `toggleCollapse`
- Any child measure invalidation

### Paint-only

- `setShowDividerGrip`
- `setDividerColor`, `clearDividerColor`
- Divider hover / press / focus / drag transitions

---

## Focus behavior

- **Divider is tab-focusable** when `setKeyboardResize(true)` (default).
- **Focus ring** around divider when focused.
- Pane children have normal tab order; Tab flow: first-pane children
  → divider → second-pane children → leave.

---

## Accessibility (reserved)

- Role: `separator` on the divider, with `aria-orientation` and
  `aria-valuemin`/`max`/`now` reflecting position in the allowed
  range (0..100 scaled).

---

## Animation

- **Divider hover fade**: 80 ms.
- **Collapse / expand**: 200 ms ease-out animation of size from
  current to 0 (or from 0 to last-known-size).
- **Drag**: no animation (1:1 tracking).
- **Programmatic setRatio / setSize**: instant by default; opt-in
  smooth-animate via `setRatio(r, Animated)` variant (future).

---

## Test surface

Unit tests in `tests/test_fw2_SplitView.cpp`:

### Basic

1. `EmptyNoPanes` — SplitView without first/second panes measures
   to constraint, paints divider only.
2. `SetFirstPaneLaidOut` — first pane occupies left half with
   default ratio 0.5.
3. `BothPanesLaidOut` — both present: laid out with divider between.

### Orientation

4. `HorizontalSplit` — vertical divider, left/right panes.
5. `VerticalSplit` — horizontal divider, top/bottom panes.
6. `SetOrientationReLayouts` — flipping orientation re-measures.

### Divider modes

7. `RatioMode` — setRatio(0.3) → first pane takes 30% of main axis.
8. `FixedStartMode` — setFirstPaneSize(200) → first pane 200 px
   regardless of total size.
9. `FixedEndMode` — setSecondPaneSize(100) → second pane 100 px.
10. `RatioSurvivesResize` — widget resizes; ratio remains;
    first/second pane sizes both scale.
11. `FixedStartSurvivesResize` — first pane stays 200 px; second
    absorbs delta.

### Constraints

12. `FirstPaneMinEnforced` — drag divider to make first pane < min
    → clamped at min.
13. `SecondPaneMinEnforced` — symmetrically.
14. `ShrinkSplitViewBelowPaneMins` — SplitView resized so sum of
    mins exceeds bounds → first pane at min, second absorbs remainder
    (possibly exceeding second's max: min wins).

### Dragging

15. `DragDividerMovesIt` — drag delta moves divider proportionally;
    onDividerMoved fires.
16. `DragClampedToMin` — drag past first's min clamps at min.
17. `DragReleaseFiresFinal` — final onDividerMoved on drag end.

### Collapse

18. `DoubleClickCollapsesSmallerPane` — double-click divider on
    55:45 split → collapses second pane (smaller).
19. `ExpandRestoresLastSize` — expanding a collapsed pane restores
    pre-collapse size.
20. `CollapseAnimates` — collapse animates size from current to 0
    over 200 ms.
21. `NonCollapsibleDoesNotCollapse` — `collapsible=false`: double-
    click no-op.
22. `CollapsedPaneHides` — widget bounds report 0 size for the
    collapsed pane.

### Keyboard

23. `ArrowResizes` — focused + arrow key moves divider by 1 px.
24. `ShiftArrowMoves10` — with Shift, 10 px.
25. `HomeCollapsesFirst` — Home when collapsible collapses first.

### Firing

26. `OnDividerMovedFiresContinuously` — during drag, multiple
    onDividerMoved events fire.
27. `OnCollapseChangedFires` — collapse / expand each fire callback.

### Cache

28. `ChildMeasureInvalidationCausesRelayout` — pane's measure
    invalidation re-lays out both panes and divider.
29. `RelayoutBoundaryStopsBubble` — SplitView's own size stays
    stable; parent doesn't re-measure.

---

## Migration from v1

v1 has no SplitView. Resizable splits exist ad-hoc in main window
layouts via manual mouse-drag code. v2 consolidates into
SplitView.

No source-compatible path; this is a new widget.

---

## Open questions

1. **Multi-pane via nested SplitView?** Current recommendation.
   Works but adds layout overhead for deep nests. If 3+ panes become
   common, a multi-pane variant might be worth it.

2. **Snap-to-ratios?** Some UIs let the divider "snap" to 25/50/75%
   ratios during drag. Useful for preset layouts. Future extension:
   `setSnapPoints(std::vector<float>)`.

3. **Saved-size persistence helper?** Maybe provide
   `std::string saveState()` / `loadState(str)` for callers to
   easily serialize. Tiny addition; reasonable to include.

4. **Dragging outside window?** Mouse capture during drag keeps
   tracking even if cursor leaves SplitView. Covered by standard
   Widget capture mechanism. No special spec.

5. **Double divider (for nested splits)?** Outermost SplitView's
   divider and inner SplitView's divider can be adjacent, creating
   a visual "double line." Acceptable aesthetically; no action.

6. **Resize while paint-throttled?** Heavy pane contents (WaveformWidget,
   device chains) might lag during drag. SplitView could offer a
   "drag-throttle" mode that repaints panes only on drag end.
   Deferred.
