# FwScrollBar — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0

---

## Intent

A **standalone draggable scrollbar**: track + thumb with drag and
click-to-page semantics. Exposed as its own widget so that:

1. It can be embedded wherever a scrollable region exists (ScrollView,
   SnapScrollContainer, Table, future custom containers).
2. Tests can verify scrollbar behavior without instantiating a whole
   scrolling container.
3. Callers can opt for always-visible vs overlay-style visibility
   independently of the container's policy.

FwScrollBar does NOT do any scrolling itself — it's a UI control that
emits position changes via callback. A ScrollView wires its own
scroll offset to the scrollbar's `setValue` / `onChange` and keeps
them in sync.

## Non-goals

- **Scroll momentum / inertia.** That's the container's job (or a
  platform-level input feature).
- **Rubber-band overshoot.** Same — the container decides whether
  overscroll produces rubber-band bounce. FwScrollBar's value is
  strictly clamped to `[0, max]`.
- **Custom thumb shape.** Rounded rectangle is the only variant.

---

## Visual anatomy

### Vertical

```
   ┌──┐
   │  │    ← track (full height, dim)
   │██│    ← thumb (accent, at position * scrollable range)
   │██│
   │  │
   │  │
   └──┘
```

### Horizontal

```
   ┌──────────────────────┐
   │            ██████     │   ← track (horizontal), thumb at value
   └──────────────────────┘
```

Parts:
1. **Track** — full-length rounded rectangle. `controlBg` at dim
   opacity by default; brighter when hovered (hover zone extends to
   the full track, not just the thumb).
2. **Thumb** — rounded rectangle, length proportional to
   `visibleRange / totalRange`, positioned at
   `value / (totalRange - visibleRange)`.
3. **Optional arrow buttons** — disabled by default (OS-dependent
   convention). Enable via `setShowArrows(true)` for scrollbars in
   long lists where "nudge by a small step" is common.

---

## States

| State | Trigger | Visual cue |
|---|---|---|
| **idle** | default | Dim track, dim thumb |
| **hover-track** | pointer over track (not thumb) | Track slightly brighter |
| **hover-thumb** | pointer over thumb | Thumb 10% brighter |
| **dragging** | left-button held on thumb, moving | Thumb brightest, drag feedback visible |
| **focused** | keyboard focus | Thumb outlined with accent |
| **disabled** | `setEnabled(false)` or `totalRange ≤ visibleRange` | Thumb+track faded; no input |

When the content isn't scrollable (everything fits), the scrollbar
enters the disabled state automatically — it's often desirable for
scroll containers to hide it entirely in that case (via
`setHideWhenNoOverflow(true)`), which is the default.

---

## Gestures

### Pointer — left button

| Gesture | Result |
|---|---|
| **Drag thumb** | Move thumb tracking pointer position; value changes proportional to main-axis delta. |
| **Click on thumb (no drag)** | No value change; thumb just "jumps to pressed" visually then returns on release. |
| **Click on track (above/below thumb)** | Page: value jumps by ±`visibleRange` in the direction of the click. Cumulative if held (key-repeat rate). |
| **Middle-click on track** | Jump thumb to the clicked position (absolute positioning — common macOS convention). Optional behavior, opt-in via `setMiddleClickJumpsTo(true)`. |

### Pointer — scroll wheel

Scroll wheel over the scrollbar nudges the value by `stepSize`
(default 1/10 of visibleRange). Same as scrolling over the content
area.

### Keyboard (when focused)

| Key | Action |
|---|---|
| `Up` / `Down` (vertical) or `Left` / `Right` (horizontal) | ± `stepSize` (default 1/10 of visibleRange) |
| `PgUp` / `PgDn` | ± `visibleRange` (page) |
| `Home` / `End` | Jump to 0 / max |
| `Tab` / `Shift+Tab` | Focus nav |

### Touch

Drag thumb directly. No momentum within the scrollbar itself (the
parent scroll container does that if configured).

---

## Public API

```cpp
enum class ScrollOrientation {
    Vertical,
    Horizontal,
};

class FwScrollBar : public Widget {
public:
    using ValueCallback = std::function<void(float newValue)>;

    FwScrollBar();
    explicit FwScrollBar(ScrollOrientation o);

    // Range
    void setTotalRange(float total);                  // total scrollable extent (content size)
    void setVisibleRange(float visible);              // visible extent (viewport size)
    // Maximum scroll position = max(0, total - visible).

    // Value
    void setValue(float v, ValueChangeSource = ValueChangeSource::Programmatic);
    float value() const;
    float maxValue() const { return std::max(0.0f, m_total - m_visible); }

    // Stepping
    void setStepSize(float px);                        // default visibleRange / 10
    void setPageSize(float px);                        // default visibleRange — explicit override

    // Orientation
    void setOrientation(ScrollOrientation o);

    // Appearance
    void setShowArrows(bool);                          // default false
    void setHideWhenNoOverflow(bool);                  // default true
    void setAccentColor(Color c);
    void clearAccentColor();

    // Behavior
    void setMiddleClickJumpsTo(bool);                  // default false

    // Callbacks
    void setOnChange(ValueCallback);                   // fires on every value change
    void setOnDragStart(std::function<void()>);        // useful for container to pause e.g. auto-scroll
    void setOnDragEnd(ValueCallback);                  // called with final value

    // Sizing
    void setThickness(float px);                       // default metrics.scrollbarThickness (~12 px)
    void setMinThumbLength(float px);                  // default 20 px

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### Value clamping & firing

- `setValue` clamps to `[0, maxValue()]` silently.
- `onChange` fires on every clamped value change, including during
  drag (continuous firing).
- If `setValue(same)` is called, no callback fires.
- Automation source (uncommon for scrollbars) fires `onChange`.

### Thumb size & position math

```cpp
// Thumb length in logical pixels
float thumbLen = max(minThumbLength,
                     trackLen * (visibleRange / totalRange));

// Thumb top in logical pixels along the track
float thumbPos = (value / maxValue()) * (trackLen - thumbLen);
```

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Thickness is fixed; length stretches to fill.

```cpp
Size FwScrollBar::onMeasure(Constraints c, UIContext& ctx) {
    float thickness = m_thickness > 0 ? m_thickness
                                      : theme().metrics.scrollbarThickness;
    float len = (m_orientation == Vertical) ? c.maxH : c.maxW;
    // Fill length axis to the constraint — scrollbars grow to the
    // container they sit in.
    return (m_orientation == Vertical)
        ? c.constrain({thickness, len})
        : c.constrain({len, thickness});
}
```

### Size policy

```cpp
// Vertical:
SizePolicy{ width = Fixed, height = Stretch }

// Horizontal:
SizePolicy{ width = Stretch, height = Fixed }
```

### Relayout boundary

**Opt-in.** Scrollbars are often wedged into tight spots; not
defaulted because a scrollbar's "length" does depend on available
space, and the parent does need to know its thickness so it can
reserve room.

### Caching

Measure: `(constraints, thickness, orientation)`. Value changes are
paint-only — this is crucial for performance because continuous drag
fires 60 Hz updates.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg.withAlpha(40)` | Track idle |
| `palette.controlBg.withAlpha(80)` | Track hovered |
| `palette.textSecondary.withAlpha(120)` | Thumb idle |
| `palette.textSecondary.withAlpha(200)` | Thumb hovered |
| `palette.accent` | Thumb dragging |
| `palette.accent.withAlpha(80)` | Focus outline |
| `metrics.scrollbarThickness` | Default thickness (≈12 px) |
| `metrics.cornerRadius` | Thumb corners |

---

## Events fired

- `onChange(value)` — continuous during drag, discrete on click-to-
  page, keyboard, or scroll wheel.
- `onDragStart()` / `onDragEnd(finalValue)` — so containers can
  pause/resume auto-scroll or similar behavior.

---

## Invalidation triggers

### Measure-invalidating

- `setThickness`
- `setOrientation`
- DPI / theme / font (global)

### Paint-only

- `setValue`
- `setTotalRange`, `setVisibleRange` (affect thumb length, not
  widget bounds)
- `setStepSize`, `setPageSize`
- `setAccentColor`
- Hover / press / drag / focus transitions

---

## Focus behavior

- Tab-focusable; not auto-focus-on-click (click-drag typical usage
  doesn't need keyboard focus transfer).
- Focus ring is a thin accent outline around the thumb.

---

## Accessibility

- Role: `scrollbar`.
- `aria-valuenow`, `aria-valuemin`, `aria-valuemax`,
  `aria-orientation`.

---

## Animation

- **Hover fade**: 80 ms.
- **Auto-hide on idle** (if `setAutoHide(true)`): scrollbar fades
  out after 1.5 s of no activity, fades back in on pointer movement
  within the parent scroll region. Default off for explicitness.

---

## Test surface

Unit tests in `tests/test_fw2_ScrollBar.cpp`:

### Value math

1. `ThumbSizeProportional` — `totalRange=1000, visibleRange=200,
   trackLen=100` → thumb is 20 px.
2. `ThumbMinSize` — tiny-visible-in-huge-total: thumb length clamps
   to `minThumbLength`.
3. `ThumbPositionAtZero` — value=0 → thumb at top of track.
4. `ThumbPositionAtMax` — value=maxValue → thumb at end of track.
5. `SetValueClamps` — `setValue(999999)` clamps to maxValue.
6. `NoScrollWhenVisibleExceedsTotal` — visible=300, total=200 →
   scrollbar is disabled; maxValue is 0.

### Dragging

7. `DragThumbUpdatesValue` — drag thumb by N pixels, value increases
   by proportional amount.
8. `DragFireOnChange` — `onChange` fires continuously during drag.
9. `DragFireOnDragStart` — drag begin fires once.
10. `DragFireOnDragEnd` — drag end fires once with final value.

### Click-to-page

11. `ClickAboveThumbPagesUp` — click on track above thumb decreases
    value by pageSize.
12. `ClickBelowThumbPagesDown` — analogous.
13. `ClickOnThumbNoChange` — click on thumb itself doesn't change
    value.
14. `MiddleClickJumpsWhenEnabled` — with `setMiddleClickJumpsTo(true)`,
    middle-click jumps thumb to clicked position.

### Scroll wheel

15. `ScrollWheelSteps` — mouse wheel over scrollbar changes value
    by `stepSize`.

### Keyboard

16. `ArrowSteps` — focused + arrow key changes value by stepSize.
17. `PgDnPagesDown` — PgDn = +pageSize.
18. `HomeToZero` — Home sets value to 0.
19. `EndToMax` — End sets value to maxValue.

### Hide when no overflow

20. `HideWhenNoOverflow` — totalRange ≤ visibleRange + default hide
    setting → widget returns 0-size from measure OR renders transparent.

### Cache

21. `SetValuePaintOnly` — value change does not bump measure version.
22. `SetTotalRangePaintOnly` — total range change is paint-only.

---

## Migration from v1

v1 doesn't have a dedicated FwScrollBar. Scrolling is ad-hoc inside
panels that implement their own drag / wheel handlers. v2 consolidates
into FwScrollBar which ScrollView, SnapScrollContainer, Table etc. all
embed.

No backward-compat concerns at the scrollbar level; the containers
will need migration separately.

---

## Open questions

1. **Always-visible vs overlay?** OS-level conventions differ: macOS
   fades scrollbars when idle, Windows keeps them solid. `setAutoHide`
   toggles, but the default? Propose: solid by default, auto-hide as
   an opt-in preference (matching Windows-style since YAWN's target is
   cross-platform and the always-visible variant is more discoverable).

2. **Arrow buttons?** Not drawn by default. `setShowArrows(true)`
   adds small +/- buttons at track endpoints. Useful for long lists
   or when the user expects keyboard-like stepping without
   keyboard focus.

3. **Continuous page-hold?** Click-and-hold on the track currently
   fires one page-jump per click. Some UIs repeat the page jump while
   held, at key-repeat rate. Probably fine behind a
   `setPageRepeatOnHold(true)`.

4. **Horizontal scrollbar in RTL languages?** Value=0 means
   left-end in LTR; in RTL it should mean right-end. Framework-level
   RTL concern; deferred.

5. **Programmatic smooth scroll?** A `scrollToValue(target, duration
   ms)` that animates to target over N ms. Useful for "scroll to
   selected item" in Table. Probably lives on ScrollView, which
   orchestrates; FwScrollBar stays as the dumb input/display.
