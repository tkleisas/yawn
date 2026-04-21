# FwKnob — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `FwKnob` class in `src/ui/framework/Primitives.h`
**Related:** [fader.md](fader.md) — FwKnob shares the drag-value
infrastructure and diverges in visual presentation and default gestures.

---

## Intent

A **circular rotary control** for continuous values. Where Fader is
the mixer channel-strip workhorse, Knob is the device-panel workhorse
— every synth parameter, every effect knob, every mixer pan control.
Knobs pack dense (you can fit 16 knobs where you could fit 4 faders)
and their shape is a strong affordance for "rotary control."

Knob reuses Fader's drag math verbatim — the drag is **vertical**, not
radial, because radial dragging is a usability disaster on a trackpad
and most professional DAWs agree (Ableton, Reaper, Logic). The
rotation visible on screen is pure visual feedback for where in the
range the value sits.

## Non-goals

- **Radial / circular dragging.** Tested and rejected: radial drag
  makes precise adjustments impossible when the knob is small, and
  makes "drag to the edge of the screen" common. Vertical drag is
  universally better for audio parameters.
- **Continuously-rotatable (encoders).** Some UIs model knobs as
  stepless encoders with no visible endpoint. Ours always have a
  `[min, max]` range with a visible arc. Hardware MIDI encoders are
  mapped via MIDI Learn, not built into the widget.
- **Bipolar center-detent visual by default.** Some knobs (pan) want
  a visual centered indicator. That's supported via `setBipolar(true)`
  but not the default — most parameters are unipolar.
- **Stepped / quantized behavior built-in.** For integer-stepped
  parameters (filter types, algorithm selectors), use a separate
  `FwDiscreteKnob` or a `FwDropDown`. Knob values are continuous.

---

## Visual anatomy

```
         ┌────────────┐
         │   ╭────╮   │       ARC (background)  = full unlit arc
         │  ╱╌╌╌╌╌╲  │
         │ │  ●   │ │         ARC (filled)      = from min to current value
         │  ╲╌╌╌╌╌╱  │
         │   ╰────╯   │       INDICATOR          = line pointing at current value
         │            │
         │  [value]   │       VALUE LABEL        = optional, below
         └────────────┘
```

Parts, in paint order:

1. **Background arc** — 24-segment (or more) polyline tracing the
   full min→max sweep. `controlBg`.
2. **Filled arc** — from min to current value. `accent` (or instance
   override). Thicker stroke than background arc.
3. **Knob disc** — optional inner filled circle. `elevated`. The
   "physical knob body" between arcs and indicator.
4. **Indicator line** — from center of disc to edge, pointing at
   current value. `textPrimary`.
5. **Hover / press tint** — subtle glow overlay.
6. **Focus ring** — outside the widget bounds when focused.
7. **Value label** — optional, below the knob, showing formatted
   value. Its own embedded Label widget.

### Sweep geometry

- Knob arc sweeps from **7 o'clock** to **5 o'clock** going clockwise,
  a 300° sweep. The 30° gap at the bottom is visual breathing room
  and matches convention (Ableton Live, Reaper, every rack plugin).
- For bipolar knobs (`setBipolar(true)`), the arc is centered at the
  12 o'clock position — the value's arc fill extends left or right
  from 12 depending on sign. Useful for pan, detune, any +/− control.

### Variants

Small / medium / large — different preferred sizes and fatter or
thinner arc strokes. Not separate widgets; just `setSize(KnobSize)`
or explicit dimensions.

| Size | Diameter | Arc thickness | Default use |
|---|---|---|---|
| `Compact` | 24 px | 2 px | Mixer pan, dense parameter grids |
| `Normal` | 44 px | 3 px | Default; device parameter knobs |
| `Large` | 72 px | 4 px | Hero controls (master cutoff, drive) |

---

## Secondary value display (modulation overlay)

A knob often represents **two** values simultaneously:

- The **primary value** — what the user set (static, tied to the
  parameter's stored value; what's saved in the project).
- The **effective value** — what actually arrives at the parameter
  after modulation: LFO, envelope, automation lane while the
  user-set position holds steady, MIDI-learned CC, sidechain, etc.

The classic example is "knob sweeping smoothly because an LFO is
modulating the cutoff, while the user's base setting is unchanged."
Without a visual for this, the user can't tell whether the knob's
motion is their fault or the modulator's — and can't tell whether the
value is currently above or below their setting.

### Visual

```
         ┌────────────┐
         │   ╭────╮   │
         │  ╱╌╌▓╌╌╲  │        primary arc (solid):   user's set value
         │ │  ●   │ │
         │  ╲╌╌╌╌╌╱  │        modulation tail (thin): from user value → live modulated value
         │   ╰─•──╯   │        ●    = primary indicator (user)
         │            │        •    = modulation indicator (live)
         └────────────┘
```

Three layers, all optional:

1. **Primary fill arc** — as described above; always shown.
2. **Modulation tail arc** — thin arc drawn in
   `palette.modulation` color, stretching between the primary value
   position and the modulated value position. Makes the delta
   immediately readable.
3. **Modulation range halo** — a dim, wide arc spanning the full
   excursion of the modulator (if known, via
   `setModulationRange(min, max)`). Gives a sense of how far the
   modulator can push the value, not just where it is right this
   frame.
4. **Modulation indicator** — a small dot or tick at the modulated
   value position on the perimeter.

All three are purely display concerns — the user's primary gestures
(click, drag, scroll, keyboard) still operate on and read the
**primary** value exclusively. Modulation is view-only.

### API

```cpp
// Live modulated value — what the parameter actually sees right now.
// std::nullopt means "no modulation active", and only the primary arc
// is drawn. Setting a value enables the modulation tail + indicator.
void setModulatedValue(std::optional<float> v);
std::optional<float> modulatedValue() const;

// Full excursion range of the modulator. Optional — without it, only
// the current modulated value is shown (tail + dot). With it, the
// dim halo arc is drawn across the full [min, max] excursion window.
void setModulationRange(std::optional<std::pair<float, float>> range);

// Override the modulation color. Default = theme().palette.modulation.
void setModulationColor(Color c);
void clearModulationColor();

// Convenience — when modulation goes away entirely (e.g. LFO is
// unlinked from this param), clear all three overlay layers at once.
void clearModulation();
```

### Paint order

The layers paint in this order over the main arc:

1. Range halo (widest, dimmest)
2. Primary fill arc (from min to primary value, bright)
3. Modulation tail arc (from primary value to modulated value,
   modulation color, thin)
4. Primary indicator line (from center to primary value position)
5. Modulation indicator dot (at modulated value position)

This order makes the primary arc the visually dominant element (it's
what the user controls) while the modulation overlay is readable but
clearly secondary.

### Update cadence

The caller sets the modulated value as fast as they want to update
it. For audio-rate modulation that would flood the UI thread, callers
should throttle — typical target is 30 Hz (once per frame). The widget
doesn't poll; it just renders whatever value was last set. A simple
pattern: a per-track `FxParamWatcher` samples the modulated value at
frame rate and calls `setModulatedValue()`.

---

## States

Identical to Fader.

| State | Trigger | Visual cue |
|---|---|---|
| **idle** | default | Arc fills to value, knob body elevated color |
| **hovered** | pointer inside bounds | Arc fill brightens 10%, subtle glow |
| **pressed** | left-button held | Arc fill slightly darkened |
| **dragging** | pressed + movement ≥ threshold | Cursor hidden or replaced with up/down indicator, floating value label shown |
| **focused** | keyboard focus | Focus ring outside bounds |
| **disabled** | `setEnabled(false)` | Everything desaturated 40% |

States compose same as Fader.

---

## Gestures

### Pointer — left button

| Gesture | Result |
|---|---|
| **Click** (no drag) | Fires `onClick` if set. Some callers use this to "arm" a knob for editing via another surface (MIDI). |
| **Drag vertical** | Value tracks vertical movement — see drag sensitivity below. |
| **Drag end** | Fires `onDragEnd(startValue, endValue)`. |
| **Double-click** | Opens inline `FwNumberInput` overlay pre-filled with current value. Enter commits, Esc cancels. |
| **Ctrl+Double-click** | Reset to `defaultValue` (if set via `setDefaultValue`). Faster than arrow-through-menus reset. |

### Pointer — right button

| Gesture | Result |
|---|---|
| **Right-click** | Fires `onRightClick(screenPos)` for MIDI Learn context menu. |

### Pointer — scroll wheel

Value tracks scroll input — one detent = `defaultStep` (default 1% of
range). `Shift` = ×0.1, `Ctrl` = ×10.

### Keyboard (when focused)

| Key | Result |
|---|---|
| `Up` / `Down` | ± `defaultStep` |
| `Shift + Up/Down` | fine mode |
| `PgUp` / `PgDn` | coarse mode |
| `Home` / `End` | Jump to max / min |
| `Enter` / `Space` | Open inline text entry |
| `Backspace` / `Delete` | Reset to default value |
| `Tab` / `Shift+Tab` | Focus nav |

### Touch

Tap + vertical drag. Radial touch is explicitly not supported; a
swipe left/right is interpreted as cross-axis noise and ignored (up
to the 3-px threshold).

---

## Public API

```cpp
enum class KnobSize { Compact, Normal, Large };

class FwKnob : public Widget {
public:
    using ValueCallback = std::function<void(float)>;
    using ClickCallback = std::function<void()>;
    using RightClickCallback = std::function<void(Point screen)>;

    FwKnob();

    // Value & range — identical to Fader API shape
    void setRange(float min, float max);
    void setValue(float v, ValueChangeSource src = ValueChangeSource::Programmatic);
    void setDefaultValue(float v);
    void setStep(float s);
    float value() const;

    // Visual
    void setSize(KnobSize s);                     // preset dimensions
    void setDiameter(float d);                    // explicit override
    void setArcThickness(float t);
    void setBipolar(bool b);                      // arc centered at 12 o'clock
    void setAccentColor(Color c);
    void clearAccentColor();
    void setShowValueLabel(bool);                 // default true
    void setValueFormatter(std::function<std::string(float)>);

    // Secondary value display (see "Secondary value display" section)
    void setModulatedValue(std::optional<float> v);
    std::optional<float> modulatedValue() const;
    void setModulationRange(std::optional<std::pair<float, float>> range);
    void setModulationColor(Color c);
    void clearModulationColor();
    void clearModulation();                       // wipes value + range together

    // Mapping (linear by default — callers pre-convert log-scale values)
    void setValueMapper(std::function<float(float)>);
    void setInverseMapper(std::function<float(float)>);

    // Callbacks
    void setOnChange(ValueCallback cb);
    void setOnDragEnd(std::function<void(float start, float end)>);
    void setOnClick(ClickCallback cb);             // no-drag click
    void setOnRightClick(RightClickCallback cb);

    // Behavior tuning
    void setPixelsPerFullRange(float px);          // default 200 — same as Fader
    void setFineMode(bool shiftIsFine);            // default true

    // Label (for MIDI Learn + accessibility + value-label formatting)
    void setLabel(std::string text);               // displayed below knob disc
    void setUnit(std::string unit);                // appended to formatted value
    const std::string& label() const;

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

`setLabel(std::string)` adds a text label under the knob. This is a
common pattern in device panels — knob + label pair. The label is a
child Label widget embedded internally, so it obeys all the Label
rules (measure, truncation, theme).

---

## Drag sensitivity

**Identical to Fader.** This is deliberate — knob drag and fader
drag should feel the same so the user builds one muscle memory.

```cpp
float logicalDy = dy / ctx.dpiScale();
float delta = -logicalDy * range / pixelsPerFullRange;
// Shift = × 0.1, Ctrl = × 10
```

Default `pixelsPerFullRange = 200`. Independent of knob diameter
(whether the knob is 24 px or 72 px wide, drag-to-full-range is the
same vertical travel).

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size FwKnob::onMeasure(Constraints c, UIContext& ctx) {
    float d = resolvedDiameter();
    float labelH = 0;
    if (!m_label.empty()) {
        Size s = m_labelWidget.measure(
            Constraints{0, c.maxW, 0, INFINITY}, ctx);
        labelH = s.h + theme().metrics.baseUnit;     // gap
    }
    float valueH = 0;
    if (m_showValueLabel) {
        valueH = ctx.font->lineHeight(theme().metrics.fontSize * 0.85f)
               + theme().metrics.baseUnit;
    }
    float w = std::max(d, (m_label.empty() ? 0.0f : m_labelWidget.measuredWidth()));
    float h = d + labelH + valueH;
    return c.constrain({w, h});
}
```

Knob + embedded label widget + optional value label, stacked
vertically. Width is driven by the wider of (knob diameter, label
width).

### `onLayout(Rect b, UIContext& ctx)`

Centers knob disc in the top portion of bounds, places label below,
places value label below that.

### Size policy

```cpp
SizePolicy{ width = Fixed, height = Fixed }
```

### Relayout boundary

**Yes, automatically.** Both dimensions are content-driven and stable
(diameter is a property, not derived from parent).

### Caching

Measure cache key: `(constraints, diameter, arc thickness, label
text, show value label, font scale, bipolar flag)`. Value changes
are paint-only.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Background arc |
| `palette.accent` | Primary filled arc |
| `palette.elevated` | Knob disc body |
| `palette.textPrimary` | Indicator line, value label text |
| `palette.textSecondary` | Label below knob |
| `palette.accent.withAlpha(80)` | Focus ring |
| `palette.modulation` | Modulation tail arc + indicator dot (new in v2 theme) |
| `palette.modulation.withAlpha(60)` | Modulation range halo |
| `metrics.fontSize` | Label font size |
| `metrics.fontSizeSmall` | Value label font size |
| `metrics.baseUnit` | Gaps between knob / label / value |

Instance overrides: `setAccentColor(Color)` for the filled arc,
`setValueFormatter(fn)` for the value label string.

---

## Events fired

- Gesture events: `onClick`, `onRightClick`, `onDragEnd`, `onChange`.
- FocusEvent from framework.

Same firing rules as Fader:
- `onChange` fires on any value change except `Automation` source.
- `onDragEnd` fires once per drag with `(startValue, endValue)` for
  undo coalescing.

---

## Invalidation triggers

### Measure-invalidating

- `setSize`, `setDiameter`, `setArcThickness`
- `setLabel`, `setShowValueLabel`
- `setBipolar` (visual sweep changes but not size — actually
  paint-only; still invalidating for safety)
- DPI / theme / font (global epoch)

### Paint-only invalidating

- `setValue`, `setRange`
- `setAccentColor`, `clearAccentColor`
- `setValueFormatter`
- `setModulatedValue`, `setModulationRange`, `setModulationColor`,
  `clearModulationColor`, `clearModulation` — modulation changes are
  visual-only and often happen at 30 Hz, so they absolutely must NOT
  invalidate measure.
- Hover / press / focus / drag transitions

---

## Focus behavior

- **Tab-focusable:** yes.
- **Auto-focus on click:** no (same rationale as Fader — dense knob
  grids shouldn't thrash focus).
- **Focus ring:** 2 px at `accent.withAlpha(80)`, inset 2 px outside
  the knob disc (not the full widget bounds — the label below is
  visually separate).

---

## Accessibility (reserved)

- Role: `slider` (ARIA) — same as Fader.
- `aria-valuenow`, `aria-valuemin`, `aria-valuemax`, `aria-
  valuetext` (formatted) are exposed via getters.
- `setAriaLabel("Filter cutoff")` — takes precedence over visible
  label for screen readers when they diverge.

---

## Animation

**Value change:** no animation. Knob snaps to new value instantly,
same as Fader. Audible controls deserve honest visuals.

**Hover / press:** 80 ms fade on arc brightness / tint.

**Focus ring:** 100 ms fade in when focus is gained.

---

## Test surface

Unit tests in `tests/test_fw2_Knob.cpp`:

1. `MeasureCompactSize` — `setSize(Compact)` returns 24×24 + label
   space.
2. `MeasureNormalSize` — 44×44 default.
3. `MeasureLargeSize` — 72×72.
4. `DragChangesValue` — identical math to Fader test — logical
   dy=−100 on a 200-ppR knob moves value by 0.5 × range.
5. `DragRespectsDPI` — same drag at 2× DPI produces same value
   change (critical regression test).
6. `ShiftFineMode` — 0.1× delta.
7. `CtrlCoarseMode` — 10× delta.
8. `DoubleClickOpensTextEntry` — simulated double-click opens
   `FwNumberInput` overlay.
9. `CtrlDoubleClickResetsToDefault` — reset gesture restores
   `defaultValue`.
10. `BipolarCenterWhenValueAtMid` — with bipolar on and value at
    mid-range, filled arc has zero extent.
11. `BipolarArcExtendsLeft` — value below mid produces left-ward
    arc.
12. `BipolarArcExtendsRight` — value above mid produces right-ward
    arc.
13. `LabelRenderedBelowDisc` — `setLabel("Cutoff")` places Label
    widget below knob disc.
14. `ValueFormatterUsed` — `setValueFormatter([](float v){ return
    std::to_string(v) + " Hz"; })` changes displayed text.
15. `OnChangeNotFiredForAutomation` — automation-source updates
    don't fire user callback.
16. `RelayoutBoundary` — value change bumps widget's own local
    version but does not bubble.
17. `MeasureCacheHit` — re-measure with unchanged state skips work.

### Modulation overlay

18. `ModulationDefaultsToNullopt` — `modulatedValue()` returns
    `std::nullopt` on a fresh knob.
19. `SetModulatedValueShowsTail` — setting a value enables the tail +
    indicator dot; paint path draws them.
20. `SetModulatedValueDoesNotTriggerOnChange` — modulation-driven
    visual updates must not fire the user's `onChange` callback; the
    stored value hasn't changed.
21. `SetModulatedValueDoesNotInvalidateMeasure` — 30 Hz modulation
    updates don't bump the measure cache.
22. `ModulationRangeHaloDrawn` — with `setModulationRange(min,max)`,
    halo arc renders spanning that range.
23. `ClearModulationWipes` — `clearModulation()` removes value +
    range + custom color; next paint reverts to primary-only.
24. `ModulationColorOverride` — `setModulationColor(Color)` overrides
    the theme token for that instance.
25. `PrimaryGestureIgnoresModulated` — dragging the knob updates the
    **primary** value only; the modulated value is untouched.
26. `ModulatedBeyondRangeClamps` — if the modulated value would
    exceed `[min, max]`, the indicator clamps to the arc edge but
    doesn't wrap.

Plus all Fader tests re-applied to Knob where semantically equivalent
(they're structurally the same widget).

---

## Migration from v1

v1 `FwKnob`:

```cpp
FwKnob k;
k.setRange(0, 1);
k.setValue(0.5f);
k.setOnChange(...);
k.setSensitivity(1.0f);     // GONE — use setPixelsPerFullRange
```

v1 and v2 are source-compatible except:

- `setSensitivity(float)` → `setPixelsPerFullRange(float)`, same
  semantic change as Fader (from "drag 200-px knob for full range"
  to "drag 200 logical px always").
- `setOnDragEnd` signature: v1 was `f(endValue)`, v2 is
  `f(startValue, endValue)`.
- `setValueFormatter` is new; v1 hardcoded number formatting.
- `setBipolar` is new; v1 had no centered-arc mode.
- `setDefaultValue` + Ctrl+double-click reset is new.

Migration: mechanical rename + revisit any custom sensitivity
overrides (most call sites used default 1.0 and don't need changes).

---

## Open questions

1. **Shift for fine, Alt for coarse, Ctrl for reset?** Mac convention
   uses Cmd for many of these roles. Standardize modifiers across
   platforms or mirror native convention? Currently proposing
   platform-uniform: Shift=fine, Ctrl=coarse, Ctrl+double=reset. Open
   to change.

2. **Knob disc optional?** Some minimal designs don't have a filled
   disc, just arc + indicator line against the background. Add
   `setShowDisc(bool)` for that aesthetic? Tiny feature; add when
   needed.

3. **Value label on hover only?** Desktop DAWs often show the value
   only when the knob is hovered or being dragged. Currently `setShow
   ValueLabel(true)` shows always. Worth a `LabelMode::Always /
   Interactive / Never`? Probably yes — add to the spec before
   implementation.

4. **Keyboard increment rate on hold?** Arrow-key held down —
   should repeat at 30 Hz (one step per tick) or accelerate? Current
   spec leaves it to the framework's default key-repeat rate. Fine
   for v1; may want acceleration later for long-range params.

5. **Color trail on bipolar?** When bipolar, should left-of-center
   and right-of-center use different colors to emphasize sign?
   (E.g. pan left = warm red, right = cool blue.) Common in UI
   design; worth a two-color option. Add as `setBipolarColors(left,
   right)` when someone wants it.

6. **Does Fader get modulation display too?** Almost certainly yes
   — the use cases are identical (LFO modulating a channel fader,
   for instance, is real). Spec not updated yet; the symmetric
   addition would mirror Knob's `setModulatedValue` /
   `setModulationRange` onto the fader's vertical track (thin
   sibling-track rendered next to the main one). Proposed for the
   Fader v2.1 spec update once Knob's version is proven in code.

7. **Multiple modulators?** One LFO + one envelope + one MIDI CC
   can all modulate the same parameter. Do we show each as a
   separate tail, or only the summed result? v2 proposes "summed
   only" — a stack of tails would quickly look like spaghetti.
   Future `setModulationSegments(std::vector<...>)` could visualize
   each source with per-segment color if the need is real.
