# FwNumberInput — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the numeric text-entry logic embedded in v1
`FwKnob::onDoubleClick` and inlined in various preference fields.
**Related:** [text_input.md](text_input.md) — FwNumberInput is
structurally a specialization of FwTextInput with numeric parsing,
formatting, clamping, and stepping added on top.

---

## Intent

A **numeric text field** with range clamping, increment/decrement
gestures, and user-friendly parsing. Used everywhere a number needs
to be typed:

- Inline value entry when the user double-clicks a Fader or Knob.
- BPM field in the transport bar.
- Length / duration fields in dialogs.
- Sample-rate / buffer-size dropdown-adjacent fields.
- Any parameter whose user-facing label is a number with units.

FwNumberInput inherits all of FwTextInput's editing + keyboard +
clipboard + focus behavior. The additions are:

- Value is a `double`, not a string.
- Range `[min, max]` with automatic clamping.
- Step increment for arrows / scroll / ±buttons.
- Pluggable parse / format functions for units and locale.

## Non-goals

- **Multiple-value fields.** Enter just one number. Fields like
  "min,max" range or "W×H" size are composed as two FwNumberInputs
  in a FlexBox row.
- **Expression evaluation.** `3 + 5` won't compute to 8 on Enter.
  That's a niche power-user feature; callers who want it can layer
  expression parsing on top (the widget calls `parse(text)` and the
  caller's parse function can choose to do whatever it wants).
- **Different number systems.** Decimal only. Hex inputs belong to a
  different widget.

---

## Visual anatomy

```
    ┌──────────────────┐
    │                   │
    │     120.5   Hz    │     value + optional unit suffix
    │                   │
    └──────────────────┘
      └─ padding        └─ unit

    ┌──────────────────┐
    │                   │
    │  ┼   120.5  ┼ Hz  │     optional ± buttons either side
    │                   │
    └──────────────────┘
```

Inherits the TextInput anatomy (fill, border, selection, caret).
Adds two optional affordances:

1. **Unit suffix** — dim label to the right of the numeric text.
   Not editable; purely display. Set via `setUnit(std::string)`.
2. **Increment / decrement buttons** — optional small ±buttons
   flanking the field. When present, clicking either buttons changes
   the value by `step`. Hidden by default; enable via
   `setShowStepButtons(true)`.

Both affordances are measured as part of the widget's width — a
NumberInput with step buttons and a unit suffix is wider than a bare
one.

---

## States

All states inherited from FwTextInput, plus:

| State | Trigger | Visual cue |
|---|---|---|
| **out-of-range** | parsed value outside `[min, max]` and NOT yet committed | red border tint; tooltip shows "must be between X and Y" |

The out-of-range state appears while the user is typing a provisional
out-of-range value — it's a hint that clamping will happen on
submit. Does not prevent typing; the user can always type freely, and
the widget clamps the final value.

---

## Gestures

Inherits all TextInput gestures. Adds:

### Pointer — scroll wheel

| Gesture | Result |
|---|---|
| **Scroll up** | Value += `step` (clamped to `max`) |
| **Scroll down** | Value -= `step` (clamped to `min`) |
| **Shift + scroll** | Fine mode (× 0.1) |
| **Ctrl + scroll** | Coarse mode (× 10) |

Scroll works whether or not the field is focused — hovering the
widget and scrolling nudges the value. Can be disabled via
`setScrollToStep(false)` if the widget is inside a vertically-
scrolling panel and the conflict bothers the user.

### Keyboard (when focused)

| Key | Action |
|---|---|
| `Up` | Value += `step` (no text editing) |
| `Down` | Value -= `step` |
| `Shift + Up/Down` | fine mode |
| `Ctrl + Up/Down` | coarse mode |
| `PgUp` / `PgDn` | coarse mode (×10) |
| (all TextInput keys) | normal text editing |

Arrow up/down here specifically hijacks the arrow keys from TextInput,
where they'd otherwise move the caret vertically (no-op for single
line) or left/right (which they still do: `Left`/`Right` are
caret navigation as per TextInput).

### Pointer — ± buttons (when enabled)

| Gesture | Result |
|---|---|
| **Click +** | Value += `step` |
| **Click −** | Value -= `step` |
| **Hold +/−** | Repeat at 10 Hz after an initial 300 ms delay |
| **Shift + click** | fine mode |
| **Ctrl + click** | coarse mode |

---

## Public API

```cpp
class FwNumberInput : public FwTextInput {   // inheritance for clarity in spec; impl may delegate
public:
    using ValueCallback = std::function<void(double)>;

    FwNumberInput();
    explicit FwNumberInput(double initialValue);

    // Value
    void   setValue(double v, ValueChangeSource src = ValueChangeSource::Programmatic);
    double value() const;

    // Range
    void setRange(double min, double max);       // default [-inf, inf]
    double min() const;
    double max() const;

    // Stepping
    void setStep(double s);                       // default 1.0
    void setFineMultiplier(double m);             // default 0.1
    void setCoarseMultiplier(double m);           // default 10.0

    // Formatting
    void setUnit(std::string unit);               // e.g. "Hz", "%", "dB", " bars"
    void setDecimals(int n);                      // default 2
    void setFormatter(std::function<std::string(double)>);
    // Formatter receives raw double, returns display text.
    // Default: sprintf("%.*f", decimals, value) + unit.

    void setParser(std::function<std::optional<double>(const std::string&)>);
    // Parser receives display text, returns parsed value or nullopt.
    // Default: std::strtod with locale-insensitive parsing; accepts
    // trailing unit suffix (strips "Hz", "%" etc. matching setUnit).

    // Step buttons
    void setShowStepButtons(bool);                // default false
    void setStepButtonSize(float px);             // default metrics.controlHeight

    // Scroll wheel
    void setScrollToStep(bool);                   // default true

    // Callbacks
    void setOnChange(ValueCallback cb);           // fires on every committed value change
    // (note: onChange, not onSubmit — submit fires on Enter but value
    //  commits on every increment/decrement; onChange unifies them)

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### Value commit timing

Unlike FwTextInput where the stored text == the displayed text at
all times, FwNumberInput distinguishes between:

- **Display text** — what the user is currently typing (may be
  out-of-range, partial like "1.", or unparseable like "abc").
- **Committed value** — the last valid parsed double that got clamped
  into `[min, max]`. What `value()` returns.

Commit triggers:

- `Enter` or `Tab` — parse display text; if valid, clamp and commit;
  else revert display text to the current committed value.
- **Blur** (focus lost) — same as Enter, unless `setSubmitOnBlur(false)`.
- **± buttons, scroll, arrow keys** — commit immediately, display text
  re-formatted from new value.

`onChange` fires on every successful commit regardless of source.

### Parse tolerance

The default parser is lenient:

- Strips whitespace.
- Strips trailing unit string (if `setUnit` was used) case-insensitively.
- Accepts comma as decimal separator (European locale hint). Note:
  this is locale-insensitive deliberately — DAW users moving files
  between machines want "500.0" parsed the same regardless of OS
  locale.
- Accepts scientific notation (`1e3` == 1000).
- Rejects everything else as `std::nullopt`.

Callers needing stricter parsing (e.g. exact decimal-separator match,
regex-constrained formats) replace the parser via `setParser`.

### Format precision

`setDecimals(2)` is the default; the number is always rendered with
exactly 2 decimal places unless `setFormatter` overrides. Some fields
prefer `setDecimals(0)` (integer-feeling BPM) or `setDecimals(3)` (fine
detune).

Custom formatters can do rounding, k/M/G suffixing, or time-format
rendering. Example:

```cpp
input.setFormatter([](double v) {
    if (v < 1000) return fmt("{:.1f} Hz", v);
    return fmt("{:.2f} kHz", v / 1000.0);
});
```

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size FwNumberInput::onMeasure(Constraints c, UIContext& ctx) {
    float w = m_preferredWidth > 0 ? m_preferredWidth : 80.0f;     // narrower default than TextInput
    // Reserve room for unit suffix
    if (!m_unit.empty()) {
        float unitW = ctx.font->textWidth(m_unit, fontScale())
                      + theme().metrics.baseUnit;
        w += unitW;
    }
    if (m_showStepButtons) {
        w += 2 * m_stepButtonSize + 2 * theme().metrics.baseUnit;
    }
    if (m_minWidth > 0) w = std::max(w, m_minWidth);

    float h = m_fixedHeight > 0 ? m_fixedHeight : theme().metrics.controlHeight;
    return c.constrain({w, h});
}
```

### `onLayout(Rect b, UIContext& ctx)`

Splits bounds into: [−button] [text field] [+button] [unit label]
with configurable layout when buttons are / aren't shown.

### Size policy

```cpp
SizePolicy{ width = Fixed, height = Fixed }
```

Number inputs typically want a stable, predictable width — showing
"10.00" and "10000.00" shouldn't cause the surrounding layout to
jitter. If more numbers fit, fine; otherwise the text scrolls
horizontally like TextInput.

### Relayout boundary

**Yes, automatically.** Value changes are paint-only.

### Caching

Measure cache: `(constraints, preferredWidth, minWidth, fixedHeight,
unit, showStepButtons, stepButtonSize, font scale)`. Value changes,
parse state changes, out-of-range state are all paint-only.

---

## Theme tokens consumed

Inherits TextInput's token set, plus:

| Token | Use |
|---|---|
| `palette.error` | Out-of-range border tint |
| `palette.textSecondary` | Unit suffix text |
| `palette.controlBg` | Step button background |
| `palette.controlHover` | Step button hover |
| `palette.controlActive` | Step button press |

---

## Events fired

- `onChange(value)` — every committed value change.
- `onSubmit(text)` — inherited from TextInput (Enter / Tab / blur
  per config). Usually callers ignore this and listen to `onChange`
  instead; both fire on submit but `onChange` provides the parsed
  value.
- `onCancel()` — inherited from TextInput. Escape restores the
  committed value into the display text.
- `FocusEvent` — from framework.

---

## Invalidation triggers

### Measure-invalidating

- `setPreferredWidth`, `setMinWidth`, `setHeight`
- `setUnit` (changes total width reservation)
- `setShowStepButtons`, `setStepButtonSize`
- DPI / theme / font (global epoch)

### Paint-only invalidating

- `setValue`
- `setRange` (affects clamp; may change committed value)
- `setStep`, multiplier changes
- `setDecimals`, `setFormatter`, `setParser`
- All TextInput paint-only triggers

---

## Focus behavior

- **Tab-focusable:** yes.
- **Auto-focus on click:** yes — same as TextInput.
- **`setSelectAllOnFocus(true)` default** — NumberInput overrides
  TextInput's `false` default. Rationale: almost every NumberInput
  interaction is "click in, type new value"; selecting all makes the
  new type replace the old value immediately.
- **Focus ring:** inherited.

---

## Accessibility (reserved)

- Role: `spinbutton` (ARIA).
- `aria-valuenow`, `aria-valuemin`, `aria-valuemax`, `aria-valuetext`
  (formatted).

---

## Animation

Inherits caret blink + focus border fade from TextInput. No
additional animations on step buttons.

---

## Test surface

Unit tests in `tests/test_fw2_NumberInput.cpp`. All TextInput tests
apply (inherited); NumberInput-specific tests:

### Value semantics

1. `InitialValue` — `FwNumberInput(42.0)` has `value() == 42.0` and
   display "42.00".
2. `SetValueUpdatesDisplay` — `setValue(3.14)` displays "3.14".
3. `TypingUpdatesDisplay` — typing "55.5" in field displays "55.5"
   but `value()` is still old value until commit.
4. `EnterCommitsTyped` — typing "55.5" + Enter commits 55.5.
5. `EscapeRevertsDisplay` — typing "99" + Escape displays previous
   committed value.
6. `TabCommits` — Tab commits typed value and moves focus.

### Range clamping

7. `ClampsAboveMax` — `setRange(0, 100)` + type "500" + Enter →
   `value() == 100`.
8. `ClampsBelowMin` — type "-20" + Enter → `value() == 0`.
9. `OutOfRangeShowsHint` — typing a still-typed-in out-of-range
   value displays the out-of-range border tint.
10. `InRangeClearsHint` — typing a value back into range clears the
    tint.

### Parsing

11. `ParsesTrailingUnit` — with `setUnit("Hz")`, typing "440 Hz" +
    Enter parses to 440.
12. `ParsesCommaDecimal` — "3,14" → 3.14 (locale-insensitive).
13. `ParsesScientific` — "1e3" → 1000.
14. `RejectsGarbage` — typing "abc" + Enter leaves previous value;
    display restored.
15. `CustomParserUsed` — `setParser` function is invoked instead of
    default.

### Stepping

16. `UpArrowSteps` — with `setStep(1)`, Up arrow increments value by 1.
17. `DownArrowSteps` — Down arrow decrements by 1.
18. `ShiftUpFine` — fine mode multiplier applied.
19. `CtrlUpCoarse` — coarse mode multiplier applied.
20. `ScrollWheelSteps` — mouse wheel over widget steps value.
21. `ScrollDisabled` — with `setScrollToStep(false)`, wheel is
    ignored.
22. `StepClampsAtMax` — stepping above `max` clamps (doesn't wrap).
23. `StepClampsAtMin` — stepping below `min` clamps.

### Step buttons

24. `StepButtonsMeasure` — showing step buttons widens the widget.
25. `PlusButtonFires` — clicking + increments value.
26. `MinusButtonFires` — clicking − decrements value.
27. `HoldRepeatsAfterDelay` — holding + for 300 ms triggers repeat
    at 10 Hz.

### Formatting

28. `DecimalsRespected` — `setDecimals(0)` shows integer text; `(4)`
    shows 4 decimal places.
29. `FormatterUsedWhenSet` — custom formatter replaces default.
30. `UnitSuffixShown` — `setUnit(" Hz")` appends in secondary color.

### onChange

31. `OnChangeFiresOnCommit` — Enter fires `onChange` once.
32. `OnChangeFiresOnStep` — ± button fires `onChange` per click.
33. `OnChangeNotFiredForProgrammaticSame` — `setValue(same)` does
    not fire.
34. `OnChangeNotFiredForAutomation` — automation source suppresses
    `onChange`.

---

## Migration from v1

v1 has no standalone FwNumberInput; inline numeric entry lives in
ad-hoc places (FwKnob's double-click enters text, a temporary
TextInput replaces the knob paint, Enter parses with `std::stof`).

v2 consolidation:

1. FwKnob and FwFader's inline text-entry overlay uses FwNumberInput
   on the Overlay layer.
2. Preferences dialog numeric fields become FwNumberInputs.
3. Transport BPM field becomes FwNumberInput.

No source-compatible path; this is a new widget.

---

## Open questions

1. **Drag-to-adjust while typing?** Some DAWs let you drag the
   numeric text itself vertically to scrub the value, keeping the
   field in "editing" mode simultaneously. Niche; deferred.

2. **Smart step at extremes?** When stepping across orders of
   magnitude (e.g. 10 → 11 is different relative change than 0.01 →
   0.02), logarithmic stepping would feel more natural. Would require
   caller-provided log-step curve; not critical for v2.0.

3. **Unit-aware parsing?** If the unit is "dB", should "10" parse as
   10dB, and "inf" parse as `-infinity` (silence)? That's the
   caller's `setParser` job.

4. **Negative-zero display?** `-0.00` showing when clamping rounds
   to zero from below is a visual nitpick. Default formatter can
   normalize `-0.0` → `0.0` before formatting; easy to add.

5. **Spin wrap-around?** Some use cases (pan position, MIDI channel
   1–16) want "end wraps to start." Add `setWrapAround(bool)` behind
   a feature flag; not default.
