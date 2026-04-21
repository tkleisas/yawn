# FwFader — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `FwFader` class in `src/ui/framework/Primitives.h`
(v1 reference implementation; retained until v2 lands)

---

## Intent

A fader is a **vertical linear control** for continuous values — the
signature gesture is "drag up, value goes up; drag down, value goes
down." It's the archetypal mixer channel strip control. YAWN uses it
for track volume, master volume, and any other value the user expects
to see represented as a travelling handle on a rail.

This spec defines how **one** fader behaves. Multiple faders side-by-
side are just multiple independent widgets; the mixer panel is
responsible for their layout, not this widget.

## Non-goals

- **Horizontal orientation.** If a horizontal slider is needed, it's a
  separate `FwSlider` widget. Faders are vertical. Period.
- **Bipolar / center-detent behavior.** Faders work over a range
  `[min, max]` with a linear visual representation. A pan knob that
  snaps to 12 o'clock is a different widget.
- **Scale markings.** A decoratable rail with tick marks and numeric
  labels is a follow-on (`FwFaderMarkings` composite) — the bare fader
  draws only track + handle.
- **Logarithmic / dB scaling of the VALUE.** The widget treats its
  value as a linear number in `[min, max]`. Callers who want a dB
  mapping pre-convert the value before calling `setValue()`. Future
  extension point: `setValueMapper(std::function<float(float)>)` for
  non-linear visual-position-to-value mapping without changing the
  stored value. Not in v2.0.

---

## Visual anatomy

```
  ┌───────┐ ←── bounds.top
  │       │
  │   ╔═╗ │ ←── HANDLE (travels along rail)
  │   ║ ║ │        filled with accent color
  │   ╚═╝ │
  │  ┌─┐  │
  │  │ │  │ ←── TRACK (vertical rail)
  │  │ │  │        the "groove" the handle runs in
  │  └─┘  │
  │       │
  └───────┘ ←── bounds.bottom
```

Parts, in paint order:

1. **Track** — narrow vertical rect centered horizontally in the
   widget bounds. Drawn with `theme().palette.controlBg` and an inset
   shadow for depth.
2. **Track fill** — from `min` up to the current value. Drawn with
   `theme().palette.accent` (or a per-instance override via
   `setTrackColor()`).
3. **Handle** — short horizontal rect positioned at the current
   value's Y coordinate. Filled with `theme().palette.elevated`, outlined
   with `theme().palette.border`.
4. **Hover / press / focus rings** — 1 px outline outside the handle
   when the state applies.

Geometry defaults (logical pixels, all scaled by `Theme::effectiveScale()`
at paint time, not at store time):

| Dimension | Default | Range |
|---|---|---|
| Track width | 4 | 2–8 |
| Handle width | 20 | 12–28 |
| Handle height | 10 | 6–16 |
| Widget min width | 20 (handle width) | |
| Widget min height | 80 | |
| Widget preferred height | 120 | |

All overridable via `setVisualMetrics(const FaderMetrics&)` for cases
where the default doesn't fit (e.g. compact mixer strips).

---

## States

A fader is in exactly one of these states at any time:

| State | Trigger | Visual cue |
|---|---|---|
| **idle** | default | Handle in theme elevated color |
| **hovered** | pointer inside bounds, no button held | Handle 10% brighter, subtle outline |
| **pressed** | left mouse button held, no drag yet | Handle 5% darker, thicker outline |
| **dragging** | pressed state + movement ≥ threshold | Handle accent-colored outline, cursor hidden/replaced |
| **focused** | keyboard focus (Tab-to) | Focus ring drawn around bounds |
| **disabled** | `setEnabled(false)` | Everything desaturated to 40% |

States compose: `disabled` overrides everything (hovered + disabled
still looks disabled). `focused` stacks on top of any other state.

---

## Gestures

### Pointer — left button

| Gesture | Result |
|---|---|
| **Click** (press + release, no movement > threshold) | No value change; fires `onClickCallback` if set (used for "highlight this fader without touching its value", e.g. selecting a channel strip). |
| **Drag** (press + movement ≥ 3 logical px) | Value tracks vertical movement. See *Drag sensitivity* below. |
| **Drag end** | Fires `onDragEndCallback` with `(startValue, finalValue)`. Used for undo coalescing. |
| **Double-click** | Opens an inline text entry overlay (`FwNumberInput` on Layer 2) pre-filled with the current value. Enter commits, Escape cancels. |

### Pointer — right button

| Gesture | Result |
|---|---|
| **Right-click** | Fires `onRightClickCallback` with the screen position. Used by the App to show a MIDI Learn context menu. |

### Pointer — scroll wheel

Vertical scroll on the fader nudges the value. `1 detent = defaultStep`
(configurable, default 1% of range). `Shift + wheel` = ×0.1 (fine).
`Ctrl + wheel` = ×10 (coarse).

### Keyboard (when focused)

| Key | Result |
|---|---|
| `Up` / `Down` | ± `defaultStep` |
| `Shift + Up/Down` | ± `defaultStep × 0.1` (fine) |
| `PgUp` / `PgDn` | ± `defaultStep × 10` (coarse) |
| `Home` | Jump to `max` |
| `End` | Jump to `min` |
| `Enter` / `Space` | Open inline text entry (same as double-click) |
| `Backspace` / `Delete` | Reset to `defaultValue` (if one was set via `setDefaultValue`) |
| `Tab` / `Shift+Tab` | Move focus to next / previous focusable widget |

### Touch / trackpad

Touch drags behave identically to mouse drags. Trackpad gestures (two-
finger scroll, pinch) go through the scroll wheel handler. No pinch-
specific behavior.

---

## Value semantics

```cpp
class FwFader : public Widget {
public:
    // Value & range
    void  setRange(float min, float max);            // default [0, 1]
    void  setValue(float v, ValueChangeSource src);  // clamps into [min, max]
    void  setDefaultValue(float v);                  // for reset gesture
    void  setStep(float s);                          // keyboard / wheel step
    float value() const;
    float min() const;
    float max() const;

    // Mapping (rarely overridden — linear by default)
    void  setValueMapper(std::function<float(float)>);   // visualPos → value
    void  setInverseMapper(std::function<float(float)>); // value → visualPos

    // Callbacks
    void  setOnChange(std::function<void(float)>);
    void  setOnDragEnd(std::function<void(float start, float end)>);
    void  setOnRightClick(std::function<void(Point screen)>);
    void  setOnClick(std::function<void()>);

    // Appearance
    void  setTrackColor(Color c);       // override theme accent
    void  setVisualMetrics(FaderMetrics m);
    void  setValueFormatter(std::function<std::string(float)>);  // for text-entry display

    // Behavior tuning
    void  setPixelsPerFullRange(float px);   // default 200
    void  setFineMode(bool shiftIsFine);     // default true

    // Display
    void  setShowValueOnDrag(bool);   // default true — shows floating label while dragging
    void  setAriaLabel(const std::string&);   // e.g. "Track 1 volume"
};

enum class ValueChangeSource {
    User,            // user-driven gesture (mouse, keyboard)
    Programmatic,    // someone called setValue() from code
    Automation,      // automation engine wrote the value
};
```

**Clamping:** `setValue` silently clamps into `[min, max]`. No errors.

**`onChange` fires** on every value change regardless of source
**except** when `src == Automation` — automation-driven changes
don't fire `onChange` (callers who need that subscribe to the
automation engine directly).

**`onDragEnd` fires** once at the end of each drag, with `(startValue,
finalValue)`. Used to create a single undo entry per drag rather than
thousands of mid-drag entries.

---

## Drag sensitivity (the headline fix)

v1 computed:
```cpp
float delta = -dy * sensitivity * range / bounds.h;
```

That's wrong in two ways:

1. **`dy` is in physical pixels** (SDL reports raw physical deltas).
   Dividing by `bounds.h` which is in logical pixels gives a
   DPI-dependent result.
2. **`bounds.h`** changes per-instance. A 40 px mini fader and a
   500 px master fader have the same conceptual behavior but
   wildly different drag ratios.

v2 computes:
```cpp
float logicalDy = dy / ctx.dpiScale();           // physical → logical pixels
float delta = -logicalDy * range / pixelsPerFullRange;
```

`pixelsPerFullRange` defaults to **200 logical pixels**. Feel-tested
against Ableton Live, Reaper, and Logic — all cluster around 100–300
px per full fader travel.

**Fine mode** (Shift held) multiplies the result by 0.1. **Coarse**
(Ctrl held) multiplies by 10.

**Accumulator precision:** we don't lose sub-pixel movements. The
accumulated `logicalDy` is summed as a float across the drag; only
the final value is clamped to `[min, max]`. A 0.5-px-per-frame
trackpad scroll still produces the expected cumulative change.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Returns the fader's preferred size, clamped to the constraints:

```cpp
Size FwFader::onMeasure(Constraints c, UIContext&) {
    float w = std::max(m_metrics.minWidth, m_metrics.handleWidth);
    float h = m_metrics.preferredHeight;
    return c.constrain({w, h});
}
```

- Width: narrow, driven by handle width (default 20 logical px).
- Height: prefers `m_metrics.preferredHeight` (default 120), accepts
  anything ≥ `m_metrics.minHeight` (default 80).

### `onLayout(Rect bounds, UIContext& ctx)`

No children, nothing to recurse into. Just stores `bounds`. The base
class caches layout so repeated `layout()` calls with the same bounds
are free.

### Size policy

```cpp
SizePolicy{
    width  = Fixed,        // narrow, driven by handle metrics
    height = Stretch,      // fills available vertical space in a FlexBox
}
```

### Relayout boundary

**Yes, automatically.** Width is fixed; height stretches but doesn't
depend on content. A value change doesn't need to re-notify the
parent.

### Caching

- **`onMeasure`** — cached on `(constraints, metrics)`. Invalidated by
  `setVisualMetrics`, DPI change, theme change. Value changes do NOT
  invalidate measure.
- **`onLayout`** — trivially cached on bounds.
- **Paint** — never cached; runs every frame. Faders are cheap to paint.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Track background |
| `palette.accent` | Track fill (override via `setTrackColor`) |
| `palette.elevated` | Handle fill |
| `palette.border` | Handle outline |
| `palette.controlHover` | Handle hover brighten |
| `palette.controlActive` | Handle press darken |
| `palette.textPrimary` | Floating value label text |
| `palette.textDim` | Label background |
| `palette.accent.withAlpha(80)` | Focus ring |
| `metrics.cornerRadius` | Handle corner radius |
| `metrics.borderWidth` | Handle outline width |

Swapping the theme at runtime re-paints with new colors; no
geometry changes, so no re-layout is needed.

---

## Events fired

In addition to the gesture callbacks:

- `InvalidateEvent` — bubbles up when `setValue` changes the displayed
  value. Because the fader is a relayout boundary, this stops at the
  fader itself; only the widget repaints, not its parent.
- `FocusEvent` — fires when gaining or losing keyboard focus.

---

## Invalidation triggers

### Measure-invalidating (bump `localVersion` + bubble if not boundary — but we ARE a boundary)

- `setVisualMetrics`
- Theme change (via global epoch bump)
- DPI change (via global epoch bump)

### Paint-only-invalidating (no measure change)

- `setValue`
- `setRange` (unless new range breaks constraints; then measure invalidates too)
- `setTrackColor`
- `setDefaultValue`
- Any state transition (hover, press, focus, drag)

Value changes during a drag are paint-only — no measure work per
frame. This is the key reason drag performance should be fine.

---

## Focus behavior

- **Tab order:** inserted based on widget tree order (parent-then-
  children, depth-first).
- **Focus ring:** 2 px ring at `accent.withAlpha(80)`, drawn outside
  the bounds, inset 2 px.
- **Auto-focus on click:** yes — clicking the fader also focuses it.
  Clicking any other focusable widget unfocuses.
- **Keyboard-only:** a user who never touches the mouse should be
  able to Tab to any fader and operate it with arrow keys.

---

## Accessibility (reserved)

Not implemented in v2.0 but designed for:

- `setAriaLabel("Track 1 volume")` stores a label for future AT use.
- Value, range, step are exposed via getters.
- All actions reachable via keyboard.

When AT support lands (v3+), the framework iterates widgets tapping
`ariaLabel()` / `ariaValue()` to produce an accessibility tree.

---

## Animation

The fader itself doesn't animate. When the *value* changes externally
(automation writing over it, programmatic setValue), the handle jumps
instantly — there's no built-in lerp. Rationale: smoothing display
changes the perceived value, which is dishonest for a precision
audio control.

Exception: when **hover** or **focus** engages, the handle's hover
brighten fades in over 80ms. This is pure cosmetic.

---

## Test surface

Unit tests in `tests/test_FwFader.cpp`:

1. `MeasurePreferredSize` — `measure()` returns handleWidth × 120 by default.
2. `MeasureClampedByConstraints` — `Constraints{maxH:50}` produces height 50.
3. `SetValueClamps` — `setValue(100)` with range `[0,1]` clamps to 1.
4. `DragChangesValue` — simulated drag of `logicalDy = -100` on a 200-px-per-range fader moves value by 0.5 × range.
5. `DragRespectsDPI` — same simulated `dy` at 2× DPI produces the same value change as at 1× DPI. Critical regression test.
6. `ShiftFineMode` — drag with Shift produces 0.1× the normal delta.
7. `DoubleClickOpensTextEntry` — a rapid click-click fires the text-entry request.
8. `KeyboardStepping` — Up arrow changes value by `step`.
9. `OnDragEndFires` — called once per drag with `(startValue, endValue)`.
10. `OnChangeNotFiredForAutomation` — `setValue(v, Automation)` does not invoke `onChange`.
11. `RelayoutBoundary` — `setValue` bumps the fader's own localVersion but does NOT bubble to parent's measure cache.
12. `MeasureCacheHit` — second measure() with same constraints returns cached value without calling `onMeasure`.
13. `ThemeSwapRepaints` — swapping themes bumps global epoch, triggers one repaint, no re-measure (metrics unchanged).
14. `DisabledStateBlocksInput` — `setEnabled(false)` causes mouse events to pass through.

All tests use a fake `UIContext` with a controllable DPI scale.

---

## Migration from v1

v1 `FwFader` call sites:

```cpp
fader.setValue(0.5f);
fader.setRange(0, 1);
fader.setOnChange([](float v){ ... });
fader.setOnDragEnd([](float v){ ... });
fader.setSensitivity(1.0f);     // GONE in v2 — use setPixelsPerFullRange
fader.setTrackColor({...});
```

v2 is source-compatible except for:

- `setSensitivity(float)` → `setPixelsPerFullRange(float)` with
  *different* semantics. Old `sensitivity=1.0` corresponded roughly
  to `pixelsPerFullRange = boundsHeight / range`, which was height-
  dependent; new default is `200` fixed. Call sites that were fine-
  tuning sensitivity need to re-evaluate.
- `setOnDragEnd(f)` signature changes from `f(endValue)` to `f(startValue, endValue)`.
- `onChange` no longer fires for automation-driven updates (new
  `src == Automation` case). Call sites that relied on that for
  display refresh should subscribe to the automation engine instead
  or poll `value()`.

A one-shot migration pass can `sed`-rename most calls; the two
signature changes need manual review (probably 15–20 call sites).

---

## Open questions

1. **Vertical scroll lock during drag?** Right now if you drag a fader
   that's inside a scrollable panel, a small lateral drift could
   trigger panel scroll. Probably should set a "drag claim" on the
   fader that tells ancestors to ignore scroll input until drag ends.

2. **Log-scale track for dB faders?** Current spec leaves log mapping
   to the caller. A future `FaderCurve::Logarithmic` would let the
   visual position match dB instead of linear. Worth evaluating when
   we rebuild the mixer panel.

3. **Fader group linking?** Mixer workflow: Shift-drag a fader to move
   all selected faders by the same delta. Out of scope for v2.0,
   notable for the mixer rewrite pass.

4. **Touch support smoothing?** Phone/tablet touch tends to be jittery
   at sub-pixel precision. Do we apply a low-pass filter on touch
   drags? Probably yes, behind a `setTouchSmoothing(bool)` default-on
   for touch inputs only.

These don't block landing v2 Fader.
