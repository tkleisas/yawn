# ScrollView — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**New widget** (no v1 equivalent — ad-hoc scroll code lives in panels).
**Related:** [scroll_bar.md](scroll_bar.md) — embedded as a child;
[flex_box.md](flex_box.md) — ScrollView content is typically a FlexBox;
[table.md] — Table extends ScrollView with row virtualization.

---

## Intent

A **scrollable viewport** that clips a larger content area and lets
the user pan through it via scrollbar, wheel, drag, or keyboard.
The framework's standard way to fit content larger than its bounds
into a fixed-size region. Direct consumers: preset browser, plugin
list, long preference panels, preset tag clouds, anywhere the UI
needs to show "more than fits."

ScrollView is **fixed-list**: it scrolls over a known set of child
widgets. For **lazy, virtualized** content (thousands of rows, where
we can't afford to measure all of them), [`Table`](table.md) extends
this pattern with an item-builder callback. ScrollView v2.0 does not
do lazy virtualization; all children are measured and bounds-computed
up front, and paint/hit-test culling skips off-screen children.

## Non-goals

- **Infinite / lazy content.** Known child set only. Dynamic lazy
  loading is Table's job.
- **Zoom / pinch.** The viewport pans but doesn't zoom content.
  Zoomable viewports (for the arrangement timeline, piano roll) are
  purpose-built composites, not generic ScrollView.
- **Parallax / sticky headers.** Those belong to specialized composite
  widgets. ScrollView is dumb — content scrolls uniformly.
- **Pull-to-refresh.** Touch-specific gesture; deferred with the rest
  of touch support.

---

## Visual anatomy

```
    ┌────────────────────────────────┬─┐
    │ ░░░ child 0 (fully visible)░░░ │█│   ← vertical scrollbar (if overflow)
    │ ░░░ child 1 ░░░░░░░░░░░░░░░░░ │█│
    │ ░░░ child 2 (clipped at top)░░░░│█│
    ├─────────────────────────────────┤─┤
    │ <──── horizontal scrollbar ───> │ │   ← horizontal scrollbar (if overflow)
    └────────────────────────────────┴─┘

    ←── content area clipping rect ──→
```

Parts, in paint order:
1. **Viewport background** — optional, `controlBg` or transparent.
2. **Content children** — painted offset by `-scrollOffset`; clipped
   to viewport rect via push/popClip.
3. **Vertical scrollbar** — `FwScrollBar` child, positioned along
   right edge. Shown only when content overflows vertically (per
   overflow policy).
4. **Horizontal scrollbar** — along bottom edge. Shown only when
   content overflows horizontally.
5. **Corner** — the small square where vertical and horizontal
   scrollbars meet; painted `controlBg` if both bars are visible,
   otherwise transparent.

---

## Behavior model

### Scroll offset

ScrollView maintains a 2D scroll offset: `(scrollX, scrollY)`. Origin
is top-left of the content area; offset represents how much the
content is translated upward and leftward relative to the viewport.

- `scrollX = 0, scrollY = 0` → content's top-left is at viewport's
  top-left.
- `scrollX = 100` → content shifts 100 px left within the viewport;
  the user sees content that was 100 px to the right.

Offset is clamped to `[0, contentSize - viewportSize]` on each axis.
When content is smaller than viewport on an axis, offset is pinned
at 0.

### Content size determination

On every `onMeasure` / `onLayout`, ScrollView:

1. Asks its content child (or children) to measure with **unbounded**
   constraints on the scroll axes and **tight** bounds on non-scroll
   axes.
2. Records the content's measured size as `contentSize`.
3. Clamps `scrollOffset` into the new valid range (content shrank →
   scroll must retract).
4. Lays out the content child at `(-scrollX, -scrollY)` with its
   measured size.
5. Lays out the scrollbars based on `contentSize` vs `viewportSize`.

### Overflow policy

Per-axis enum: `Auto / Always / Never / Scroll`:

- **Auto** (default): scrollbar visible when content exceeds viewport.
- **Always**: scrollbar always visible (reserves its space even when
  unused).
- **Never**: scrollbar never visible; user can still scroll via wheel
  / drag / keyboard if content overflows. Useful for compact
  embedded content where a visible bar would be noise.
- **Scroll**: scrollbar always visible AND axis is always scrollable
  even without overflow — used for always-at-bottom terminals and
  similar.

---

## Public API

```cpp
enum class Overflow {
    Auto,
    Always,
    Never,
    Scroll,
};

class ScrollView : public Widget {
public:
    ScrollView();

    // Content
    void setContent(Widget* content);       // single content child (usually a FlexBox)
    Widget* content() const;

    // Scroll offset
    void setScrollOffset(Point offset, ValueChangeSource = ValueChangeSource::Programmatic);
    Point scrollOffset() const;
    Point maxScrollOffset() const;          // maxX = contentW - viewportW, etc.

    // Convenience
    void scrollTo(Point offset);            // alias for setScrollOffset(offset, Programmatic)
    void scrollBy(Point delta);
    void scrollRectIntoView(Rect contentRelative, float marginPx = 0.0f);
    void scrollToTop();
    void scrollToBottom();

    // Overflow policy
    void setHorizontalOverflow(Overflow o);
    void setVerticalOverflow(Overflow o);

    // Scrolling behavior
    void setScrollWheelMultiplier(float m);  // default 40 logical px per detent
    void setDragToScroll(bool);              // default true — middle-button drag pans content
    void setTouchMomentum(bool);             // default true — touch/trackpad flings decelerate

    // Keyboard scrolling
    void setArrowStep(float px);             // default 40
    void setPageStep(float px);              // default = viewport size - 40

    // Appearance
    void setBackgroundColor(std::optional<Color>);  // default nullopt = transparent
    void setScrollbarThickness(float px);

    // Callbacks
    void setOnScroll(std::function<void(Point offset)>);
    void setOnScrollStart(std::function<void()>);   // drag or momentum begin
    void setOnScrollEnd(std::function<void()>);     // momentum settled

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### `scrollRectIntoView`

Given a rect in content-relative coordinates, adjust `scrollOffset` to
ensure the rect is visible, with optional margin. Used for:

- Keyboard focus-follow — focused widget scrolls into view.
- "Go to selected" menu actions.
- Programmatic "scroll to where I just added a new item."

Uses minimal-movement policy: only scrolls far enough to make the rect
visible with the specified margin. Already-visible rects don't move.

---

## Gestures

### Pointer — wheel

| Gesture | Result |
|---|---|
| Scroll wheel vertical | `scrollY += detents × multiplier`. Clamped to range. |
| Shift + wheel | Horizontal scroll. |
| Ctrl + wheel | Passed through to content (e.g., for zoom-capable children). |

### Pointer — middle-button drag

| Gesture | Result |
|---|---|
| Middle-button drag | Pan content by drag delta. Opt-out via `setDragToScroll(false)`. |

### Pointer — left-button drag

Does NOT scroll — left-drag within ScrollView passes through to
content. This is important for a ScrollView containing interactive
children that have their own drag handlers (knobs, faders, etc.).

### Keyboard (when ScrollView or any child has focus)

| Key | Action |
|---|---|
| `Up` / `Down` | Scroll by `arrowStep` |
| `Left` / `Right` | Horizontal scroll by `arrowStep` |
| `PgUp` / `PgDn` | Scroll by `pageStep` |
| `Home` | Scroll to top (offset = 0, 0) |
| `End` | Scroll to bottom (offset = maxScroll) |
| `Space` / `Shift+Space` | Page down / page up (classic browser nudge) |

Keyboard scrolling is **consumed only if** the focused widget
doesn't handle the key. Example: a focused FwTextInput consumes arrow
keys for caret movement, so ScrollView doesn't scroll. A focused
button doesn't consume arrows, so ScrollView does.

### Touch

- Single-finger drag → pan content, momentum on release.
- Two-finger scroll → same as wheel.
- Flick (rapid drag+release) → momentum-based continued scroll,
  decelerating over ~800 ms.

Touch gestures deferred with the rest of touch support in v2.0;
design placeholder included.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

ScrollView doesn't have an intrinsic size — it takes whatever the
parent gives:

```cpp
Size ScrollView::onMeasure(Constraints c, UIContext& ctx) {
    // Typical pattern: we want the full constraints (fill whatever
    // we're given). If parent is unbounded (shouldn't normally happen
    // for a scroll viewport — that defeats the purpose), we fall back
    // to a default.
    float w = c.hasBoundedWidth()  ? c.maxW : 400.0f;
    float h = c.hasBoundedHeight() ? c.maxH : 300.0f;
    return c.constrain({w, h});
}
```

Content child is measured separately in `onLayout`, with axis-specific
constraint relaxation (see below).

### `onLayout(Rect bounds, UIContext& ctx)`

This is the nontrivial layout pass:

```cpp
void ScrollView::onLayout(Rect bounds, UIContext& ctx) {
    m_bounds = bounds;

    // 1. Determine viewport size — minus scrollbar thickness if bars
    //    are reserved (Always / Scroll policies).
    float barT = theme().metrics.scrollbarThickness;
    float viewportW = bounds.w;
    float viewportH = bounds.h;
    if (m_vOverflow == Overflow::Always || m_vOverflow == Overflow::Scroll) {
        viewportW -= barT;
    }
    if (m_hOverflow == Overflow::Always || m_hOverflow == Overflow::Scroll) {
        viewportH -= barT;
    }

    // 2. Measure content with scroll axes UNBOUNDED, non-scroll axes TIGHT.
    Constraints cc;
    cc.minW = m_hScrollEnabled ? 0.0f : viewportW;
    cc.maxW = m_hScrollEnabled ? INFINITY : viewportW;
    cc.minH = m_vScrollEnabled ? 0.0f : viewportH;
    cc.maxH = m_vScrollEnabled ? INFINITY : viewportH;
    Size contentSize = m_content->measure(cc, ctx);

    // 3. Reconcile Auto overflow: now that we know content size, show
    //    or hide bars. This may require a second pass if showing one
    //    bar reduces the other axis's viewport and changes the answer.
    // ... simplified re-entrancy avoided by capping at two passes.

    // 4. Clamp scroll offset to new range.
    float maxX = std::max(0.0f, contentSize.w - viewportW);
    float maxY = std::max(0.0f, contentSize.h - viewportH);
    m_scrollX = clamp(m_scrollX, 0.0f, maxX);
    m_scrollY = clamp(m_scrollY, 0.0f, maxY);

    // 5. Lay out content with its measured size, offset by -scroll.
    m_content->layout({-m_scrollX, -m_scrollY, contentSize.w, contentSize.h}, ctx);

    // 6. Lay out scrollbars along edges.
    if (m_vBar) m_vBar->layout({viewportW, 0, barT, viewportH}, ctx);
    if (m_hBar) m_hBar->layout({0, viewportH, viewportW, barT}, ctx);

    // 7. Update scrollbar state (value + totals + visible).
    if (m_vBar) {
        m_vBar->setTotalRange(contentSize.h);
        m_vBar->setVisibleRange(viewportH);
        m_vBar->setValue(m_scrollY);
    }
    // ... analogous for horizontal.
}
```

Re-entry danger: showing a vertical scrollbar reduces viewport width,
which might cause content to re-measure to a taller size, which might
mean the horizontal bar isn't needed anymore. We cap at two layout
passes — if the second pass disagrees with the first, we commit to the
first's result (accept the minor visual imprecision rather than
infinite-loop).

### Paint

```cpp
void ScrollView::paint(UIContext& ctx) {
    if (m_backgroundColor) ctx.renderer->drawRect(m_bounds, *m_backgroundColor);

    // Clip to viewport; children render offset by -scrollOffset.
    ctx.renderer->pushClip(viewportRect());
    for (Widget* c : visibleChildren()) {
        c->render(ctx);
    }
    ctx.renderer->popClip();

    // Scrollbars paint OUTSIDE the viewport clip.
    if (m_vBar) m_vBar->render(ctx);
    if (m_hBar) m_hBar->render(ctx);
}
```

### Paint culling

`visibleChildren()` iterates the content's children and skips those
whose global bounds don't intersect the viewport. This is the
**rendering-side virtualization** — the measure pass still measures
everything (so the content knows its total size for scrollbar math),
but paint only hits what's visible.

For a ScrollView containing a FlexBox of 1000 short widgets:
- Measure: all 1000 measured once (cached after).
- Paint: only ~20 visible are painted per frame.

Measure cost scales with content size; paint cost is bounded by
viewport size. This is good enough for most UI. Table gets further
via measure-virtualization (skip measuring off-screen rows).

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

### Relayout boundary

**Yes, automatically** — ScrollView's size is fully determined by its
parent's constraint. Content changes don't propagate outward because
from the parent's view, "content got bigger" just means "scrollbars
appear." No ancestor re-measure is needed.

### Caching

Measure cache key: `(constraints, h/v overflow policy)`. Since
ScrollView's own measure just returns the constraint (roughly), this
is almost always a cache hit.

Layout cache key: `(bounds, content version, scroll offset, overflow
policy)`. Scroll offset changes invalidate layout (children need
repositioning), but the measure stays cached.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Corner between scrollbars (when both visible) |
| `palette.dropShadow` | Optional inner shadow at content edges (cue that more content is off-screen) — future enhancement |
| `metrics.scrollbarThickness` | Default scrollbar thickness |
| `metrics.baseUnit` | Focus-follow margin |

Scrollbar tokens come from the FwScrollBar children.

---

## Events fired

- `onScroll(offset)` — every offset change (continuous during drag
  and wheel).
- `onScrollStart()` — first movement of a drag / momentum sequence.
- `onScrollEnd()` — drag released and momentum has settled (or
  immediate if no momentum).

---

## Invalidation triggers

### Measure-invalidating

- `setContent` (new child tree)
- `setHorizontalOverflow`, `setVerticalOverflow`
- `setScrollbarThickness`
- DPI / theme / font (global)
- Content child's measure invalidates (normal bubbling applies — but
  ScrollView itself is a relayout boundary, so it stops there)

### Paint-only (layout-invalidating)

- `setScrollOffset` — children must re-position, but none of their
  sizes change.
- Scrollbar hover / drag / focus transitions — contained within
  FwScrollBar children.

---

## Focus behavior

- **ScrollView itself is focusable** for keyboard scrolling when no
  focused child consumes the input. Tab enters the ScrollView's focus
  group; Shift+Tab leaves. Within, Tab moves focus through child
  focusables.
- **Focus-follow**: when a child widget receives focus (via Tab or
  programmatic), ScrollView calls `scrollRectIntoView(focused.
  globalBounds())` to keep focus visible. Opt-out via
  `setAutoScrollToFocus(false)`.

---

## Accessibility (reserved)

- Role: `scrollregion` (ARIA).
- `aria-orientation` and scroll position reflected on embedded
  scrollbars.

---

## Animation

### Smooth scroll

When `scrollTo(target)` or `scrollRectIntoView` runs programmatically:

- If the offset change is small (< viewport size / 2): smooth animate
  over 150 ms (ease-out).
- If larger: instant jump, no animation. Rationale: smoothing a 2000-
  px jump looks laggy, not smooth.
- Configurable via `setSmoothScroll(bool)` and
  `setSmoothScrollDuration(ms)`.

User-driven scroll (wheel, drag, keyboard) is never smooth-animated
— those respond directly to input for tight feel.

### Touch momentum

Touch flick → continues decelerating after finger lifts, ~800 ms
taper (ease-out cubic). Tapping during momentum stops it instantly.

### Scrollbar hover/drag

Inherited from FwScrollBar — 80 ms hover fade, etc.

---

## Test surface

Unit tests in `tests/test_fw2_ScrollView.cpp`:

### Layout

1. `EmptyScrollViewNoBars` — no content → scrollbars hidden.
2. `ContentSmallerThanViewportNoBars` — content fits → bars hidden
   (Auto policy).
3. `ContentLargerShowsBars` — content overflow → appropriate bars
   visible.
4. `AlwaysOverflowShowsEvenWhenNotNeeded` — Always policy
   reserves space for bar regardless of content size.
5. `NeverOverflowHidesButStillScrolls` — Never hides the bar but
   wheel still scrolls overflowing content.
6. `TwoPassOverflowResolution` — content that triggers one bar
   without needing the other re-measures correctly after second pass.

### Scroll offset

7. `SetScrollOffsetClamps` — out-of-range set clamps to
   `maxScrollOffset`.
8. `ShrinkingContentRetractsScroll` — offset = 500; content shrinks
   so maxY = 200; next layout clamps offset to 200.
9. `ScrollToTopZeroes` — `scrollToTop()` sets both axes to 0.
10. `ScrollToBottomMaxesY` — `scrollToBottom()` sets y = maxY, x
    untouched.
11. `ScrollRectIntoViewMinimumMovement` — already-visible rect:
    offset unchanged. Partially-off rect: offset moves only far
    enough to make rect visible.

### Scroll input

12. `WheelScrollsVertical` — wheel event increments scrollY by
    detent × multiplier.
13. `ShiftWheelScrollsHorizontal` — with shift, horizontal.
14. `MiddleDragPans` — middle-button drag pans content by delta.
15. `KeyboardArrowsStep` — Up/Down scrolls by arrowStep.
16. `PgDnPagesDown` — PgDn scrolls by pageStep.
17. `HomeToTop` — Home → (0, 0).
18. `EndToBottom` — End → (0, maxY).
19. `FocusedTextInputConsumesArrows` — child text input with focus
    consumes arrow keys; ScrollView doesn't scroll.
20. `UnfocusedArrowsScroll` — no focused consuming child → arrows
    scroll.

### Paint culling

21. `OffScreenChildrenNotPainted` — children whose bounds don't
    intersect viewport are skipped in paint.
22. `PartiallyVisiblePainted` — partially visible children are
    painted (with viewport clip).
23. `CullingIndependentOfMeasure` — all children still measured
    during layout, even off-screen ones.

### Scrollbar integration

24. `ScrollbarValueSyncsWithOffset` — changing scrollOffset updates
    the embedded FwScrollBar's value.
25. `ScrollbarDragUpdatesOffset` — dragging the scrollbar thumb
    updates `scrollOffset` via `onChange`.
26. `NoHorizontalScrollScrollbarHidden` — content smaller than
    viewport width → horizontal bar hidden.

### Focus follow

27. `FocusOnChildScrollsIntoView` — Tab to a child whose bounds
    are outside viewport → ScrollView auto-scrolls to bring it in.
28. `AutoScrollToFocusOptOut` — with `setAutoScrollToFocus(false)`,
    focus change doesn't scroll.

### Smooth scroll

29. `SmoothScrollAnimates` — `scrollTo(smallTarget)` with smooth
    scroll enabled: offset lerps over 150 ms.
30. `LargeScrollInstant` — jump > viewport/2: instant, no anim.
31. `UserInputInterruptsSmooth` — wheel during smooth animation
    cancels the animation and takes over.

### Events

32. `OnScrollFiresEveryChange` — callback fires on each offset
    change.
33. `OnScrollStartEndFire` — start fires once per drag, end once
    at settle.

### Caching

34. `MeasureCacheHit` — repeated measure with same constraints is
    cached.
35. `ScrollOffsetChangeLayoutOnly` — changing offset triggers
    layout but NOT measure re-computation.
36. `RelayoutBoundary` — content's measure change doesn't bubble
    past ScrollView.

---

## Migration from v1

v1 has no ScrollView. Scrolling is implemented ad-hoc inside:

- `BrowserPanel` — manual y-offset + clip
- `SessionPanel` — manual grid scroll
- `ArrangementPanel` — manual horizontal scroll with its own
  scrollbar rendering
- `PianoRollPanel` — manual both-axis scroll

All of these will migrate to wrap their content in ScrollView and
delete the manual scroll code. Estimated LOC reduction: 400–600
across these panels.

---

## Open questions

1. **Nested ScrollViews?** If the outer ScrollView has a child
   ScrollView, wheel input on the inner should consume first (deepest
   focused), bubbling to outer only at the edge of inner's scroll
   range. Spec places this in ScrollView's wheel handler: if inner
   can scroll further in the wheel direction, it consumes; else it
   passes up. Covered in test `NestedScrollBubbleAtEdge`.

2. **Scroll-linked animations?** Parallax and similar effects want
   to animate children based on scroll offset. `onScroll` callback
   gives callers what they need; framework-level scroll-linked
   animations would be nice but bigger scope.

3. **Custom scroll physics?** Spring-like overscroll bounce, inertia
   with friction curves — all nice touches but big scope. Deferred.

4. **Virtualization opt-in?** Not lazy-measure in v2.0, but for
   content with thousands of children where measure cost is the
   bottleneck, a `setMeasureCulling(bool)` mode that skips
   off-screen measure too. Catch: total size isn't knowable without
   measuring, so the caller has to provide a size estimate. That's
   Table's job; don't add it here.

5. **Horizontal primary axis?** Some use cases (timeline scroll,
   horizontal galleries) consider horizontal the primary. Does the
   keyboard behavior need to flip (Left/Right for primary axis)?
   `setPrimaryAxis(Horizontal)` future extension.

6. **Scroll-to-anchor semantics?** "Scroll so that widget X is at
   the top" vs "Scroll so that widget X is visible." Current
   `scrollRectIntoView` is the latter (minimum movement). Add
   `scrollToAnchor(widget, alignment=Top/Center/Bottom)` if callers
   want the former.
