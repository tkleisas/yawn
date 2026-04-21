# SnapScrollContainer — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** `SnapScrollContainer` in `src/ui/framework/`.
**Related:** [scroll_view.md](scroll_view.md) — ScrollView is the
free-form scrolling container; SnapScrollContainer is the **paged**
variant with quantized positions.

---

## Intent

A **horizontal paged scroll container** that snaps its scroll offset
to discrete "page" boundaries. Used for the device chain panel in
YAWN — each device is its own page, the user navigates with prev/next
buttons or swipes/drags to switch pages, and each release snaps to
the nearest page boundary.

Conceptually this is ScrollView with quantization. We could have
built it as a mode on ScrollView, but the behaviors diverge in UX
(nav buttons, page indicators, different drag semantics) enough to
warrant separate widgets. A caller picks: free-form (ScrollView) or
paged (SnapScrollContainer).

## Non-goals

- **Vertical paged scroll.** The DAW use cases are horizontal
  (device chains, preset banks, clip bars). Vertical paging is
  uncommon; add a `setOrientation` if it ever becomes needed.
- **Variable page sizes.** All pages are the same width (configurable
  globally, not per-child).
- **Free-scrolling mode.** If you want to scroll between snap points,
  use ScrollView instead. SnapScrollContainer is strict about
  snapping.
- **Infinite paging.** First and last pages are hard edges — no wrap
  around.

---

## Visual anatomy

```
    ┌──┬──────────────────────────────────────┬──┐
    │  │                                       │  │
    │◀ │  ░░ Page 2 (fully visible) ░░         │ ▶│
    │  │                                       │  │
    ├──┴──────────────────────────────────────┴──┤
    │           ○   ●   ○   ○     (page dots)    │
    └─────────────────────────────────────────────┘

     ←── prev button           next button ──→
```

Parts:
1. **Viewport** — clipped rect holding the current page and parts of
   neighbors during drag.
2. **Navigation buttons** — optional left/right arrows at edges.
   Disabled at first/last page.
3. **Page indicator dots** — optional row below viewport; one dot per
   page, filled for current.
4. **Page content** — each direct child is one page; laid out in a
   horizontal strip, only the viewport-intersecting page(s) visible.

---

## Behavior model

Children of SnapScrollContainer are **pages**. One child = one page.
At any moment exactly one page is "current." Navigation changes which
page is current; the scroll offset animates to align that page with
the viewport's left edge.

### Snap behavior

When the user drags (middle-button drag or touch swipe), the scroll
offset moves with the drag. On release:

- If drag moved > 50% of page width from current page → snap to next
  (or previous) page.
- Else → snap back to current page.

When the user uses nav buttons, wheel, or keyboard, the change is
discrete (one page at a time) with an animated transition.

### Page sizing

The container's width determines visible area, typically the width
of one page (± some slack for neighbor peek). Each page child's width
is `pageWidth` — configurable, defaults to the viewport width.

Height is driven by the tallest child.

---

## Public API

```cpp
class SnapScrollContainer : public Widget {
public:
    using PageCallback = std::function<void(int pageIndex)>;

    SnapScrollContainer();

    // Children — each addChild adds a new page
    void addPage(Widget* child);
    void removePage(Widget* child);
    void clearPages();
    int  pageCount() const;

    // Navigation
    void setCurrentPage(int idx, ValueChangeSource = ValueChangeSource::Programmatic);
    int  currentPage() const;
    void nextPage();
    void prevPage();
    bool canGoNext() const;
    bool canGoPrev() const;

    // Appearance
    void setShowNavButtons(bool);        // default true
    void setShowPageDots(bool);          // default true when pageCount > 1
    void setNavButtonIcons(GLuint prev, GLuint next);   // default arrow glyphs
    void setPageWidth(float px);         // default 0 = match viewport

    // Behavior
    void setDragToNavigate(bool);        // default true; middle-button drag pans
    void setKeyboardNavigation(bool);    // default true; Left/Right keys
    void setWheelHorizontal(bool);       // default true — wheel scrolls between pages
    void setSnapThreshold(float fraction); // default 0.5 — how far drag must go

    // Animation
    void setPageTransitionMs(float);     // default 250

    // Callbacks
    void setOnPageChanged(PageCallback);    // fires when currentPage changes

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### `onPageChanged` firing rules

- Fires whenever `currentPage()` changes, regardless of trigger (user
  or programmatic).
- Does NOT fire for `setCurrentPage(same)` no-op.
- Fires after the animation settles (for user-initiated navigation)
  so callers responding to page changes don't fight the animation.
  Call `currentPage()` during the animation if you need the "target"
  page synchronously.

---

## Gestures

### Pointer — middle-button drag

When `setDragToNavigate(true)`:

| Gesture | Result |
|---|---|
| Middle-button drag horizontal | Content pans with the drag; visual preview of adjacent pages. |
| Middle-button release with drag > snapThreshold × pageWidth from current | Snap to next/prev page. |
| Middle-button release with drag < snapThreshold | Snap back to current page. |

### Pointer — scroll wheel

`setWheelHorizontal(true)`: vertical scroll wheel input advances
pages (up = prev, down = next). One detent = one page. Some users
prefer horizontal wheel input only; then set wheel horizontal off
and provide horizontal-wheel handling yourself.

### Pointer — nav buttons

Clicking prev / next button moves by one page with animation.
Disabled (no-op + visual gray) at edges.

### Keyboard (when focused)

| Key | Action |
|---|---|
| `Left` | Previous page |
| `Right` | Next page |
| `Home` | First page |
| `End` | Last page |
| `PgUp` / `PgDn` | Same as Left / Right |
| `Tab` | Focus nav out of container |

Keyboard navigation only active when SnapScrollContainer itself has
focus. Children with focus consume keys normally; the container
doesn't steal arrow keys from a focused text input inside a page.

### Touch

- Swipe left/right to navigate.
- Flick speed affects snap decision (fast flick → next page
  regardless of distance; slow drag → use threshold).

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size SnapScrollContainer::onMeasure(Constraints c, UIContext& ctx) {
    // Viewport width = constraint or page width if set.
    float vw = m_pageWidth > 0 ? m_pageWidth : c.maxW;

    // Measure each page with tight width = pageWidth, flexible height.
    float maxPageH = 0;
    for (Widget* page : m_children) {
        Constraints pc{m_pageWidth > 0 ? m_pageWidth : vw,
                        m_pageWidth > 0 ? m_pageWidth : vw,
                        0, c.maxH};
        Size ps = page->measure(pc, ctx);
        maxPageH = std::max(maxPageH, ps.h);
    }

    float h = maxPageH;
    // Add page dots + nav button height if shown.
    if (m_showPageDots) h += theme().metrics.baseUnit * 3;

    return c.constrain({vw, h});
}
```

### `onLayout(Rect bounds, UIContext& ctx)`

```cpp
void SnapScrollContainer::onLayout(Rect bounds, UIContext& ctx) {
    m_bounds = bounds;

    // 1. Reserve space for nav buttons (if shown) + page dots (if shown).
    float navBtnW = m_showNavButtons ? theme().metrics.controlHeight : 0;
    float dotsH = m_showPageDots ? theme().metrics.baseUnit * 3 : 0;

    Rect viewport = { bounds.x + navBtnW, bounds.y,
                       bounds.w - 2*navBtnW, bounds.h - dotsH };

    float pw = m_pageWidth > 0 ? m_pageWidth : viewport.w;

    // 2. Compute current scroll offset based on currentPage + animation state.
    float targetOffset = m_currentPage * pw;
    m_scrollX = animating ? lerp(m_animStart, targetOffset, m_animProgress)
                          : targetOffset;

    // 3. Lay out each page at its x position.
    for (int i = 0; i < pageCount(); ++i) {
        Widget* page = m_children[i];
        float px = i * pw - m_scrollX + viewport.x;
        page->layout({px, viewport.y, pw, viewport.h}, ctx);
    }

    // 4. Lay out nav buttons + page dots.
    // ...
}
```

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Fixed }
```

Height is content-driven (tallest page). Width stretches to fill,
but if `setPageWidth()` is set, the container may be wider than its
content (empty space to the sides).

### Relayout boundary

**Yes, automatically.** Page navigation changes scrolling, not size.
Changes to individual pages bubble through normally.

### Caching

Measure cached on `(constraints, pageWidth, pageCount, show nav,
show dots, child measure versions)`. Current page change is
paint/layout-only (scroll offset is a layout concern but doesn't
change bounds).

### Paint culling

Like ScrollView: only paint pages whose bounds intersect the viewport.
At rest this is one page. During animation / drag, up to two
(current + neighbor being peeked at).

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Nav button fill |
| `palette.controlHover`, `controlActive` | Nav button states |
| `palette.textPrimary` | Nav button glyphs, dot indicators |
| `palette.accent` | Current page dot |
| `palette.textDim` | Disabled nav button glyph |
| `metrics.controlHeight` | Nav button width/height |
| `metrics.baseUnit` | Dot spacing |
| `metrics.cornerRadius` | Nav button corners |

---

## Events fired

- `onPageChanged(idx)` — fires after page transition settles.
- `FocusEvent` — gain/loss from framework.

---

## Invalidation triggers

### Measure-invalidating

- `addPage`, `removePage`, `clearPages`
- `setPageWidth`
- `setShowNavButtons`, `setShowPageDots`
- DPI / theme / font (global)
- Any page's measure invalidation

### Paint-only / layout-only

- `setCurrentPage`
- Animation frame advance during page transition
- Nav button hover / press states

---

## Focus behavior

- Container is tab-focusable (for keyboard page navigation).
- Children within pages are tab-focusable normally. Tab cycles
  through focusables within the current page; at page end, Tab
  exits the container (does NOT auto-advance to next page).
- Focus-change → auto-scroll: if a page change results from
  programmatic focus to a child in a non-current page, the
  container auto-navigates to that page. Opt-out:
  `setAutoPageOnChildFocus(false)`.

---

## Accessibility (reserved)

- Role: `region` with `roledescription="carousel"` (ARIA carousel
  pattern).
- `aria-live="polite"` — page changes announced to screen readers.
- `aria-current="page"` on the visible page.

---

## Animation

### Page transition

Animated via cubic ease-in-out over `pageTransitionMs` (default 250).
User-initiated navigation (click, keyboard, wheel) always animates.
Programmatic navigation can opt out: `setCurrentPage(idx,
Programmatic)` animates by default; pass `ValueChangeSource::
Immediate` (new enum value if we add it) or call a separate
`jumpToPage(int)` that skips animation.

### Drag preview

While mid-drag, content tracks the pointer 1:1 (no animation). On
release, remaining distance to nearest page animates via the
standard transition.

### Nav button fade

Disabled nav buttons fade to 40% alpha over 150 ms when reaching
first/last page.

---

## Test surface

Unit tests in `tests/test_fw2_SnapScrollContainer.cpp`:

### Basic

1. `EmptyContainerMeasuresZero` — no pages: width = nav button area,
   height = 0 or dot area.
2. `AddPageIncreasesCount` — adding a page bumps pageCount.
3. `InitialCurrentPageZero` — default `currentPage() == 0`.
4. `CanGoNextFalseAtEnd` — `currentPage == last` → canGoNext false.

### Navigation

5. `NextPageAdvances` — nextPage() increments currentPage, fires
   onPageChanged.
6. `NextPageAtEndNoOp` — nextPage on last page: no change, no
   callback.
7. `PrevPageAtStartNoOp` — prevPage on first: no-op.
8. `SetCurrentPageClamps` — setCurrentPage(99) on 5-page container:
   clamps to 4.
9. `SetCurrentPageSameNoOp` — set to current: no callback.

### Layout

10. `PageChildTakesFullWidth` — page child laid out with pageWidth
    (or viewport width).
11. `PageHeightDeterminesContainer` — container height = tallest
    page + extras.
12. `ScrollOffsetReflectsCurrentPage` — scrollX = currentPage ×
    pageWidth at rest.

### Nav buttons

13. `NavButtonClickNext` — click next button advances.
14. `NavButtonClickPrev` — click prev advances backward.
15. `NavButtonDisabledAtEdge` — prev button at first page is disabled.
16. `NavButtonHiddenWhenOff` — `setShowNavButtons(false)` hides them.

### Keyboard

17. `ArrowRightAdvances` — focused + Right arrow → next page.
18. `ArrowLeftReverses` — Left arrow → prev.
19. `HomeEndJumps` — Home/End go to first/last.
20. `FocusedChildConsumesKey` — arrow in focused text input inside
    page doesn't navigate.

### Drag

21. `DragPastThresholdSnaps` — middle-drag 60% of pageWidth → snap
    to next on release.
22. `DragBelowThresholdReverts` — 30% drag → snap back.
23. `DragAnimationAnimates` — after release, scroll animates to
    target over pageTransitionMs.

### Wheel

24. `WheelUpPrev` — wheel up → prev page.
25. `WheelDownNext` — wheel down → next.
26. `WheelHorizontalOff` — `setWheelHorizontal(false)` → wheel
    passes through.

### Paint culling

27. `OffScreenPagesNotPainted` — at rest: only current page drawn.
28. `DuringAnimationTwoPages` — mid-transition: current and target
    both drawn.

### Page dots

29. `DotsMatchPageCount` — N pages → N dots.
30. `ActiveDotIsCurrent` — current dot uses accent color.
31. `ClickDotNavigates` — clicking a dot jumps to that page.

### Firing

32. `OnPageChangedAfterSettle` — callback fires after animation
    completes.
33. `OnPageChangedOnceDuringRapidNav` — rapid nextPage() → nextPage()
    coalesces? (Decision: each call fires, but callers should throttle
    if they don't want that.)

---

## Migration from v1

v1 `SnapScrollContainer` in `src/ui/framework/SnapScrollContainer.h`
is used by the device chain panel. The API is roughly similar but
v1:

- Has no page dots.
- Has no keyboard navigation.
- Uses left-button drag for panning (awkward because it conflicts
  with child widgets). v2 switches to middle-button drag.
- Has no animation; page changes snap instantly.

Migration: device chain panel wraps v1 into v2 with minor signature
adjustments. Animation becomes visible for free.

---

## Open questions

1. **Multi-page visible?** Current spec: one page visible at a time.
   Some UIs want "show 3 pages, peek at neighbors." Add `setVisible
   PageCount(int)` + adjust snap math. Out of scope for v2.0.

2. **Variable page widths?** Per-child width would support
   "thumbnail carousel with varied-size entries." Significant layout
   complexity; deferred. SnapScrollContainer v2.0 is strict about
   equal page widths.

3. **Page dots vs tab strip?** Page dots are nice for < 10 pages;
   above that they become pixel soup. Consider automatic fallback
   to "3 / 20" numeric indicator at higher page counts.

4. **Touch flick physics?** Threshold-based snap works for slow
   drags; fast flicks deserve flick-direction based snap regardless
   of distance. Flick detection: release velocity > threshold.
   Already spec'd above.

5. **Loop pages?** First ←→ last wrap behavior. Some apps do it
   (settings screens). DAW use cases don't; leave opt-in
   `setLoop(bool)` for later.

6. **Nested SnapScrollContainers?** Horizontal outer + horizontal
   inner is confusing. Horizontal outer + vertical inner ScrollView
   is fine. Mixed nesting out of scope.
