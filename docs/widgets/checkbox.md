# FwCheckbox — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Related:** [toggle.md](toggle.md) — structurally similar (binary
state, click-to-flip), but semantically distinct (checkbox = "yes/no
option in a form", toggle = "switch between two modes"). Different
visual language + accessibility role (ARIA `checkbox` vs `switch`).

---

## Intent

A form-style on/off checkbox with optional **indeterminate** state.
Used where a checkmark feels natural: preference dialogs, "apply to
all tracks" confirmations, nested-option parents that reflect the
state of their children (indeterminate when some but not all are on).

Why a separate widget from FwToggle: although their state machines are
nearly identical, checkboxes communicate a different intent visually
("fill in this form field") vs. toggles ("flip this running behavior").
Ableton Live mixes both deliberately — mute buttons are toggles,
"launch quantize" dropdowns use toggles, but the export dialog's
"include in export" row uses checkboxes.

## Non-goals

- **Multi-line text.** The label is a single-line FwLabel; multi-line
  descriptions belong in a sibling Label in a FlexBox.
- **Animated checkmark draw.** v2.0 draws the checkmark instantly.
  Fancy stroke-tracing animations can come later if anyone notices
  they're missing.

---

## Visual anatomy

```
    ┌─┐
    │ │  Label text           ← off (empty box)
    └─┘

    ┌─┐
    │✓│  Label text           ← on (checkmark)
    └─┘

    ┌─┐
    │▇│  Label text           ← indeterminate (filled dash or partial fill)
    └─┘
```

Parts:
1. **Box** — rounded square, `metrics.controlHeight × 0.6` tall
   (≈17 px at default 28-px control height). Border `border`
   (off/indeterminate) or `accent` (on).
2. **Checkmark** — for on state; stroked path, `textOnAccent`.
3. **Indeterminate fill** — a horizontal bar across the box's middle
   (indeterminate state).
4. **Label** — FwLabel child, baseline-aligned with the box, inset
   `baseUnit` right.
5. **Hover / press tint** — overlay on the box only; label
   unaffected.
6. **Focus ring** — around the box (not the label).

Clicking anywhere in the widget (box OR label) toggles. This is a
standard accessibility pattern: larger hit target for the user.

---

## States

Inherits Toggle's state composition. Adds tri-state value:

| Value state | Trigger | Visual |
|---|---|---|
| **off** | default; `setState(false)` | empty box |
| **on** | user click or `setState(true)` | checkmark + accent fill |
| **indeterminate** | `setIndeterminate()` programmatic only | dash or partial fill |

Indeterminate is **programmatic-only**. User clicks always flip
between off and on (clicking an indeterminate checkbox sets it to off,
then a second click sets it to on). Rationale: user-visible
indeterminate state is useful for "some children on, some off"
parent-checkbox patterns where the aggregate state is computed, not
user-entered.

---

## Gestures

Identical to Toggle: click / scroll (opt-in) / keyboard Space/Enter /
right-click (MIDI Learn hook). Keyboard Space/Enter flips the same
way mouse click does.

---

## Public API

```cpp
enum class CheckState {
    Off,
    On,
    Indeterminate,
};

class FwCheckbox : public Widget {
public:
    using StateCallback = std::function<void(CheckState newState)>;

    FwCheckbox();
    explicit FwCheckbox(std::string label);

    // State
    void setChecked(bool on, ValueChangeSource = ValueChangeSource::Programmatic);
    void setIndeterminate(ValueChangeSource = ValueChangeSource::Programmatic);
    CheckState state() const;
    bool isChecked() const { return state() == CheckState::On; }

    // Label
    void setLabel(std::string label);
    const std::string& label() const;

    // Appearance
    void setAccentColor(Color c);
    void clearAccentColor();

    // Callbacks
    void setOnChange(StateCallback cb);
    void setOnRightClick(std::function<void(Point)>);

    // Sizing — inherited Label measure rules (label-driven width)
    void setMinWidth(float);

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### Click behavior

User clicks cycle state as:

```
Off    → On
On     → Off
Indet. → On
```

Programmatic `setIndeterminate()` puts the widget into indeterminate;
next user click transitions to On.

### onChange firing rules

- Fires for every actual state change (same rules as Toggle).
- Click on indeterminate → state becomes On → `onChange(On)` fires.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Box width = box height = `metrics.controlHeight × 0.6`. Total width
= box + gap + label width.

```cpp
Size FwCheckbox::onMeasure(Constraints c, UIContext& ctx) {
    float boxSize = theme().metrics.controlHeight * 0.6f;
    float gap = m_label.empty() ? 0.0f : theme().metrics.baseUnit;
    float labelW = m_label.empty() ? 0.0f
                                   : ctx.font->textWidth(m_label, fontScale());
    float w = boxSize + gap + labelW;
    if (m_minWidth > 0) w = std::max(w, m_minWidth);
    float h = theme().metrics.controlHeight;  // total height (label line-height)
    return c.constrain({w, h});
}
```

### Size policy

```cpp
SizePolicy{ width = Fixed, height = Fixed }
```

### Relayout boundary

**Yes.** Content-driven width invalidates on label change; state
changes are paint-only.

### Caching

Measure cache key: `(constraints, label, minWidth, font scale)`.
State transitions are paint-only.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Box fill (off) |
| `palette.accent` | Box fill (on, indeterminate) |
| `palette.border` | Box border (off) |
| `palette.textOnAccent` | Checkmark / indeterminate bar |
| `palette.textPrimary` | Label |
| `palette.accent.withAlpha(80)` | Focus ring |
| `metrics.controlHeight` | Height (and 60% of this = box size) |
| `metrics.cornerRadius` | Box corners (slightly sharper than buttons — ~2px at control height 28) |
| `metrics.borderWidth` | Box border |
| `metrics.baseUnit` | Box-to-label gap |

---

## Invalidation triggers

- Measure-invalidating: `setLabel`, `setMinWidth`, DPI/theme/font.
- Paint-only: `setChecked`, `setIndeterminate`, `setAccentColor`, all
  interactive state transitions.

---

## Focus behavior

- Tab-focusable; auto-focus on click (click-anywhere hit target).
- Focus ring around the box only.

---

## Accessibility

- Role: `checkbox` (ARIA — distinct from Toggle's `switch`).
- `aria-checked`: "true", "false", or "mixed" (indeterminate).

---

## Test surface

Unit tests in `tests/test_fw2_Checkbox.cpp`:

1. `InitialStateOff` — `state() == Off`.
2. `ClickTurnsOn` — simulated click fires `onChange(On)`.
3. `SecondClickTurnsOff` — click on `On` → `Off`.
4. `SetIndeterminateProgrammatic` — `setIndeterminate()` enters that state.
5. `ClickOnIndeterminateTurnsOn` — per spec, indeterminate + click =
   On (not Off).
6. `LabelClickAlsoToggles` — click on label area (not box) flips state.
7. `SpaceFlips` — focused + Space flips state.
8. `MeasureIncludesLabel` — width = box + gap + label width.
9. `MinWidthRespected` — constraint above content overrides.
10. `DisabledBlocksAll` — click / scroll / keyboard no-op when disabled.
11. `RightClickFiresCallback` — doesn't flip state.
12. `OnChangeNotFiredProgrammaticSame` — `setChecked(false)` on
    already-off is no-op.
13. `OnChangeFiredForAutomation` — automation source fires (per
    Toggle precedent).
14. `IndeterminateNotTriggerableByClick` — clicks never produce
    indeterminate; only `setIndeterminate()` does.

---

## Migration from v1

v1 has no standalone Checkbox; ad-hoc "toggle drawn as checkbox"
hacks in preference dialogs. v2 replaces those with real FwCheckbox.
No backward-compat concerns.

---

## Open questions

1. **Checkmark shape?** Heavy vs thin stroke? Matter of theme
   direction; leaving to `theme().metrics.checkmarkStrokeWidth` or
   similar once we add it.

2. **Auto-indeterminate tree?** Future helper: `FwCheckboxGroup`
   where a parent checkbox reflects the aggregate state of children
   (off → all off; on → all on; indeterminate → mixed). Click on
   parent in indeterminate → sets all children off.

3. **Vertical vs horizontal label layout?** Label currently right of
   the box. Rare designs want label above the box. `setLabelPosition`
   deferred until needed.
