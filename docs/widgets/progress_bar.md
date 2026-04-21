# FwProgressBar — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0

---

## Intent

A **non-interactive progress indicator** — a horizontal (or vertical)
bar that displays completion of a long-running operation. Used for:
video import, offline render, project save, sample analysis, any
task where the user needs feedback that "yes, something is happening"
+ "roughly how much is left."

Two modes:

- **Determinate** — `setValue(float)` between 0 and 1. Fill bar
  grows to match.
- **Indeterminate** — no known completion percentage; draws an
  animated "sweep" to indicate activity.

Purely display; no input gestures beyond a right-click for a
context menu (cancel, retry, etc.) that the caller wires.

## Non-goals

- **Interactive seeking.** A seekable progress bar is a different
  widget (media playback scrubber). FwProgressBar doesn't change
  value on click.
- **Text label integration.** Labels go in sibling widgets. Don't
  embed a percentage label inside the bar; compose
  `FlexBox { ProgressBar; Label("42%") }` if you want both.
- **Multi-segment / stacked progress.** Single contiguous fill only.
  Multi-phase operations that want "extracting / converting /
  finalizing" are best served by separate bars or step indicators
  (future `FwStepIndicator` widget).

---

## Visual anatomy

### Horizontal determinate

```
    ┌──────────────────────────────────┐
    │████████████████                   │  ← filled portion (value × width)
    └──────────────────────────────────┘
```

### Horizontal indeterminate

```
    ┌──────────────────────────────────┐
    │     ███████████                   │  ← animated sweep (moving left-right)
    └──────────────────────────────────┘
```

Parts:
1. **Track** — rounded rect spanning full widget bounds.
   `controlBg`.
2. **Fill** — rounded rect from start to `value × trackLen`.
   `accent` by default.
3. **Sweep (indeterminate only)** — animated gradient rectangle
   traveling back and forth inside the track at ~1 Hz, with a
   softer leading and trailing edge.

Vertical orientation: same parts rotated 90°; fill grows bottom-to-
top.

---

## States

| State | Trigger | Visual |
|---|---|---|
| **determinate idle** | `setValue(v)` with known value | Fill at proportion |
| **determinate updating** | continuous setValue | Fill re-drawn each frame (no animation needed) |
| **indeterminate** | `setIndeterminate()` | Sweep animation running |
| **complete** | value reaches 1.0 (in determinate mode) | Full fill, optional hold + fade (see below) |
| **error** | `setError(true)` | Fill color switches to `palette.error`, sweep stops if indeterminate |
| **disabled** | `setEnabled(false)` | Desaturated, no sweep |

No hover / press / focus / drag states. Not interactive.

---

## Gestures

Right-click only:

| Gesture | Result |
|---|---|
| **Right-click** | Fires `onRightClick(screenPos)`. Callers typically wire this to a "Cancel" context menu. |

No hover effects, no click, no keyboard. Progress bars are
decorative; interaction sits on sibling widgets (Cancel button,
status label).

---

## Public API

```cpp
enum class ProgressOrientation {
    Horizontal,
    Vertical,
};

class FwProgressBar : public Widget {
public:
    FwProgressBar();
    explicit FwProgressBar(ProgressOrientation o);

    // Determinate mode
    void  setValue(float v, ValueChangeSource = ValueChangeSource::Programmatic);
    // v is 0.0..1.0; values outside clamp.
    float value() const;
    bool  isDeterminate() const;

    // Indeterminate mode
    void setIndeterminate();          // enters indeterminate; sweep starts
    void setDeterminate();            // back to determinate at current value

    // Error state
    void setError(bool err);
    bool hasError() const;

    // Orientation
    void setOrientation(ProgressOrientation);

    // Appearance
    void setAccentColor(Color c);
    void clearAccentColor();
    void setCompleteColor(Color c);   // used when value == 1.0 (default = success green)
    void setCompleteFade(bool);       // default true — fade bar out 500 ms after reaching 1.0
    void setThickness(float px);      // default controlHeight * 0.3 (≈8 px)

    // Indeterminate animation
    void setSweepDurationMs(float);    // default 1200

    // Right-click hook (for cancel menus)
    void setOnRightClick(std::function<void(Point)>);

    // Sizing
    void setMinLength(float);          // minimum length on main axis (default 60)

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### Value firing

There's no `onChange` callback — progress bars are output-only, the
caller is driving the value. Value changes just update the visual.

Automation source doesn't apply (progress isn't an automatable
parameter).

### Complete fade behavior

When `value` reaches 1.0 (or very close — within 0.001) and
`setCompleteFade(true)`:

1. Bar holds at full for 500 ms.
2. Fades opacity to 0 over the next 500 ms.
3. At 0 opacity, the widget returns to `value = 0` silently and
   becomes invisible.

`setCompleteFade(false)` skips fade; bar stays full. Callers
removing the bar manually after complete should use `false` so their
removal logic can run without the widget animating out first.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size FwProgressBar::onMeasure(Constraints c, UIContext& ctx) {
    float thickness = m_thickness > 0 ? m_thickness
                                      : theme().metrics.controlHeight * 0.3f;
    if (m_orientation == Horizontal) {
        float len = std::max(m_minLength, c.maxW);
        return c.constrain({len, thickness});
    } else {
        float len = std::max(m_minLength, c.maxH);
        return c.constrain({thickness, len});
    }
}
```

Stretches along main axis, fixed on cross axis.

### Size policy

```cpp
// Horizontal: width = Stretch, height = Fixed
// Vertical:   width = Fixed,   height = Stretch
```

### Relayout boundary

**Yes.** Value changes are paint-only.

### Caching

Measure cache on `(constraints, thickness, orientation)`. Value /
state / error / color changes are paint-only.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Track |
| `palette.accent` | Fill (default color) |
| `palette.success` | Complete fill color (default) |
| `palette.error` | Error state fill color |
| `palette.textSecondary.withAlpha(80)` | Sweep gradient highlight |
| `metrics.controlHeight` | Default thickness (0.3 ×) |
| `metrics.cornerRadius` | Track + fill corners |

---

## Events fired

- `onRightClick(screenPos)` — for cancel menus.
- No value-change events (output-only widget).

---

## Invalidation triggers

### Measure-invalidating

- `setThickness`
- `setOrientation`
- `setMinLength`
- DPI / theme (global)

### Paint-only

- `setValue`, `setIndeterminate`, `setDeterminate`
- `setError`
- `setAccentColor`, `setCompleteColor`
- Sweep animation frame advance (only when indeterminate)

### Continuous-paint flag

Indeterminate progress bars are the **only** widget in the framework
that requires continuous repainting even when nothing changes
externally. The widget sets a flag `requiresContinuousRepaint()` →
true while indeterminate, false otherwise. The framework's main loop
consults this and keeps repainting the widget every frame while it's
true. Complete-fade animation sets the same flag for its duration.

This mechanism is general — Tooltip and animated state transitions
use it too.

---

## Focus behavior

**Non-focusable.** Progress bars can't receive keyboard focus.

---

## Accessibility

- Role: `progressbar`.
- `aria-valuenow`, `aria-valuemin`, `aria-valuemax` in determinate
  mode.
- `aria-valuetext` — if caller provides a formatter, surface its
  output here (e.g. "45 of 100 samples processed").

Not implemented in v2.0; designed for future expansion.

---

## Animation

### Indeterminate sweep

1 Hz back-and-forth (configurable via `setSweepDurationMs`). Motion
uses ease-in-out; the sweep's trailing edge fades from fill color
to transparent over its own width, so it doesn't look like a sharp
rectangle.

### Complete fade

500 ms hold at full, then 500 ms fade to alpha 0.

### Determinate value change

No animation. The bar snaps to each new value. Some UX designs lerp
the fill over 100 ms for smoother motion, but for long operations
with many small updates that reads as "laggy" — YAWN's choice is
"show the truth right now."

---

## Test surface

Unit tests in `tests/test_fw2_ProgressBar.cpp`:

### Determinate

1. `InitialValueZero` — default `value() == 0`.
2. `SetValueClamps` — `setValue(1.5)` clamps to 1.0.
3. `SetValueNegative` — `setValue(-0.1)` clamps to 0.0.
4. `FillProportional` — `setValue(0.5)` paints fill at half the
   track length.
5. `CompleteFadeTriggers` — `setValue(1.0)` with fade=true starts
   the fade sequence.
6. `CompleteFadeDisabled` — with fade=false, bar holds at full
   indefinitely.

### Indeterminate

7. `IndeterminateStartsSweep` — `setIndeterminate()` sets the
   continuous-paint flag and starts sweep animation.
8. `DeterminateStopsSweep` — `setDeterminate()` clears flag.
9. `SweepPositionAdvances` — between two successive paint calls
   separated by 100 ms, sweep position differs by expected amount.

### Error

10. `ErrorChangesFillColor` — `setError(true)` paints fill in
    error color.
11. `ErrorStopsSweep` — error + indeterminate stops the sweep
    animation (error is a final state).

### Orientation

12. `HorizontalMeasure` — width = constraint-stretch, height =
    thickness.
13. `VerticalMeasure` — reversed.

### Cache

14. `SetValuePaintOnly` — does not bump measure cache version.
15. `SetErrorPaintOnly` — ditto.

### Right-click

16. `RightClickFiresCallback` — `onRightClick` called with screen
    position; no value change.

---

## Migration from v1

v1 has ad-hoc progress UI embedded in specific panels (video import
progress baked into SessionPanel's clip slot painting, export dialog
progress baked into its own paint). v2 consolidates.

First migration target: export dialog progress. Then video import
progress — though that pattern (progress drawn inside a clip slot) may
want a smaller variant, `FwMiniProgressBar` or just FwProgressBar
with setThickness(4 px) and `setMinLength(32)`.

No backward-compat concerns at the widget level.

---

## Open questions

1. **Progress as segments?** Multi-phase operations (video import
   does extract → transcode → thumbnail) could benefit from segmented
   progress showing each stage. Out of scope; each phase can be its
   own bar or a text label plus bar.

2. **Frame-rate-independent animation?** Sweep is currently
   time-based (setSweepDurationMs), which handles variable frame rate.
   Good; no action.

3. **Programmatic complete-without-fade?** `setValue(1.0)` triggers
   fade. If a caller wants to force "done, no fade, now delete me",
   they can `setCompleteFade(false)` first. That's awkward; consider
   `markComplete(bool fade)` method as syntactic sugar.

4. **Accessibility announcements?** Screen readers announce progress
   bar updates at intervals. We have no hook for that; plan for
   `setAriaAnnounceInterval(float sec)` in a future AT pass.
