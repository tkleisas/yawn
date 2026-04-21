# FwToggle — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `FwToggle` class in `src/ui/framework/Primitives.h`

---

## Intent

A two-state persistent on/off control. Looks and behaves like a
button in most respects, but its state (on or off) is intrinsic to
the widget rather than delegated to the caller. Used for: track
mute / solo / arm, loop on/off, metronome on/off, any other binary
setting the user flips from the UI.

Distinct from `FwButton + setHighlighted(bool)` because:

- The user's click is the authoritative source of the state — the
  widget owns its state and broadcasts changes.
- Visual affordance is specifically "toggle", not "activate":
  different shape hints (pill/switch variants) and different
  accessibility role ("switch" in ARIA terms).

## Non-goals

- **Three-state / indeterminate.** Tri-state checkboxes are a
  separate widget (`FwCheckbox`). Toggle is strictly binary.
- **Radio-group behavior.** A set of toggles where only one can be
  on at a time is `FwRadioGroup`. Toggles are independent.
- **Animated slide transitions.** Some design languages do an
  animated-pill switch. We opt for instant state change (audio
  professionals expect immediate response) with just a fade on the
  fill color to soften the transition.

---

## Visual anatomy

Two visual variants, selectable via `setVariant()`:

### Variant A — **Button-style** (default; compact)

```
  ┌────────────┐          ┌────────────┐
  │  M         │   OFF    │  M         │   ON
  └────────────┘          └────────────┘
                             (accent fill)
```

Mimics a `FwButton` but with persistent fill state. Used for track
M/S/A buttons where visual tightness matters. `controlBg` when off,
`accent` when on. Label text color adjusts (`textPrimary` → `textOnAccent`).

### Variant B — **Switch-style** (for preferences / dialogs)

```
  OFF:  ┌─────────────┐         ON:  ┌─────────────┐
        │ ●           │              │           ● │
        └─────────────┘              └─────────────┘
         (grey track,                 (accent track,
          left knob)                   right knob)
```

Used when visual clarity matters more than density — Preferences
dialog, feature flags, "enable X" settings. The knob position itself
conveys state without needing to read a label.

Parts (both variants), in paint order:

1. Background fill — off or on color depending on state.
2. Border (subtle, optional — governed by `theme().metrics.borderWidth`).
3. Content:
   - Variant A: label + optional icon, centered.
   - Variant B: circular knob indicator, positioned left (off) or right (on).
4. Hover / press tint — overlay that lightens fills slightly.
5. Focus ring.

---

## States

Composes button states with the toggle's own on/off state.

| Toggle state | Interaction state | Visual |
|---|---|---|
| off, idle | | `controlBg` fill |
| off, hovered | | `controlHover` fill |
| off, pressed | | `controlActive` fill (briefly during click) |
| on, idle | | `accent` fill |
| on, hovered | | `accent` fill + 10% brighten overlay |
| on, pressed | | `accent` darkened (press feedback) |
| focused | (any) | focus ring |
| disabled | (any) | desaturate 40%, ignore input |

---

## Gestures

### Pointer — left button

| Gesture | Result |
|---|---|
| **Click** | Flips state. Fires `onChange(newState)`. Press flash animation. |
| **Click-cancel** (drag out + release outside) | No state change, no callback, same cancel rules as `FwButton`. |

### Pointer — right button

| Gesture | Result |
|---|---|
| **Right-click** | Fires `onRightClick` with screen coords (for MIDI Learn / context menu). Does NOT flip state. |

### Pointer — scroll wheel

| Gesture | Result |
|---|---|
| **Scroll up** | Sets state on (if not already). Fires `onChange(true)` if changed. |
| **Scroll down** | Sets state off. Fires `onChange(false)` if changed. |

Optional — disabled by default via `setScrollFlipsState(bool)`.
Enabled for mixer channel strip M/S buttons where hovering and
scrolling to toggle feels right.

### Keyboard (when focused)

| Key | Result |
|---|---|
| `Space` or `Enter` | Flips state. |
| `Tab` / `Shift+Tab` | Focus navigation. |
| `Home` (on Switch variant) | Set to off. |
| `End` (on Switch variant) | Set to on. |

### Touch

Tap = click = flip.

---

## Public API

```cpp
enum class ToggleVariant {
    Button,     // compact, rectangular (default)
    Switch,     // wider, with slider knob
};

class FwToggle : public Widget {
public:
    using StateCallback = std::function<void(bool newState)>;
    using RightClickCallback = std::function<void(Point screen)>;

    FwToggle();
    explicit FwToggle(std::string label);
    FwToggle(GLuint iconTex, std::string label);

    // State
    void setState(bool on, ValueChangeSource src = ValueChangeSource::Programmatic);
    bool state() const;
    void toggle();                   // setState(!state(), User)

    // Content (Button variant)
    void setLabel(std::string label);
    void setIcon(GLuint tex, int w, int h);
    void clearIcon();

    // Appearance
    void setVariant(ToggleVariant v);    // default Button
    void setAccentColor(Color c);        // override theme when on
    void clearAccentColor();

    // Callbacks
    void setOnChange(StateCallback cb);
    void setOnRightClick(RightClickCallback cb);

    // Behavior
    void setScrollFlipsState(bool);      // default false
    void setCancelOnPointerOut(bool);    // default true (matches Button)

    // Sizing
    void setMinWidth(float w);
    void setFixedWidth(float w);

    // Accessibility
    void setAriaLabel(const std::string&);
    // Role is "switch", distinct from Button's "button".
};
```

**`onChange` firing rules:**

- Fires once per state change, regardless of source.
- **Except** when `setState(v, Programmatic)` is called with `v ==
  current state` (no change → no callback).
- Fires for `setState(v, Automation)` unlike Fader — because automated
  toggle changes are visually significant events and UIs listening to
  state should always rerender. (Faders are excluded from this rule
  because per-sample automation would flood callbacks.)

---

## Drag / input sensitivity

n/a — Toggle is click-to-flip, no drag.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

**Button variant**: identical to FwButton's measure.

**Switch variant**:

```cpp
float w = m.baseUnit * 10;      // ~40 logical px — fixed width
float h = m.controlHeight;       // same as button
return c.constrain({w, h});
```

Switch variant is intentionally fixed-width (no label inline). Labels
are external — typically a sibling `Label` widget in a row FlexBox.

### `onLayout(Rect b, UIContext& ctx)`

- Button variant: store bounds, no children.
- Switch variant: compute knob position from state (animated via
  `animationProgress`), store bounds.

### Size policy

```cpp
SizePolicy{ width = Fixed, height = Fixed }
```

### Relayout boundary

**Yes, automatically.**

### Caching

Measure cached on: variant, label, icon presence, constraints, font
scale. Invalidated as Button's is. State changes are paint-only (they
can re-position the knob indicator but don't affect bounds).

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Off fill |
| `palette.controlHover` | Hover on off state |
| `palette.controlActive` | Press feedback |
| `palette.accent` | On fill (override via `setAccentColor`) |
| `palette.border` | Border |
| `palette.textPrimary` | Label when off |
| `palette.textOnAccent` | Label when on |
| `palette.elevated` | Switch knob |
| `metrics.controlHeight` | Height |
| `metrics.cornerRadius` | Corner / knob radius |
| `metrics.borderWidth` | Border |
| `metrics.baseUnit` | Internal padding |

---

## Events fired

- `onChange(newState)` — on every state change except no-op
  programmatic set.
- `onRightClick(screenPos)` — as Button.
- FocusEvent from framework.

---

## Invalidation triggers

### Measure-invalidating

- `setLabel`, `setIcon`, `clearIcon`
- `setVariant` (Button vs Switch have different preferred widths)
- `setMinWidth`, `setFixedWidth`
- DPI / theme / font (global epoch)

### Paint-only invalidating

- `setState` (knob position, fill color)
- `setAccentColor` / `clearAccentColor`
- `setEnabled`
- Hover / press / focus transitions

---

## Focus behavior

- **Tab-focusable:** yes.
- **Auto-focus on click:** no (same rationale as Button — don't
  thrash focus in dense toolbars).
- **Focus ring:** standard, inset 2 px.

---

## Accessibility (reserved)

- Role: `switch` (ARIA). Separate from Button's `button` role.
- `aria-checked`: true/false.
- `setAriaLabel("Track 1 mute")` — the caller supplies the full
  description; toggles never have auto-generated labels.

---

## Animation

**Switch variant only:**
- Knob slides left↔right over 120 ms when state changes (ease-in-out).
- Fill color cross-fades between off and on during the same 120 ms.

**Button variant:**
- Press flash (80 ms) as per Button.
- State-change fill swap is instant — the state is the source of
  truth, and for a mute button the user expects the color change to
  match the audible change.

---

## Test surface

Unit tests in `tests/test_fw2_Toggle.cpp`:

1. `InitialStateOff` — default `state()` returns false.
2. `ClickFlipsState` — simulated click flips and fires `onChange(true)`.
3. `SecondClickFlipsBack` — fires `onChange(false)`.
4. `SetStateProgrammaticSame` — `setState(false, Programmatic)` on an
   already-false toggle does NOT fire `onChange`.
5. `SetStateProgrammaticDifferent` — different state DOES fire.
6. `ScrollUpSetsOn` — with `setScrollFlipsState(true)`, scroll up
   sets state on if off.
7. `ScrollDoesNothingWhenDisabled` — default-off scroll flipping does
   not fire on scroll.
8. `RightClickFiresCallback` — right click fires `onRightClick`,
   does not flip state.
9. `KeyboardSpaceFlips` — focused toggle + space flips.
10. `DisabledSwallowsInput` — `setEnabled(false)` blocks click +
    keyboard + scroll.
11. `ButtonVariantMeasure` — label-sized bounds.
12. `SwitchVariantMeasure` — fixed-width bounds regardless of label
    (because switch has no inline label).
13. `VariantSwitchInvalidatesMeasure` — `setVariant(Switch)` from
    Button bumps local version.
14. `ClickCanceledByDragOut` — same rules as Button; no flip on
    cancel.
15. `AnimationProgressAdvances` — Switch variant's knob position lerps
    from off-position to on-position over 120 ms when `setState(true)`
    is called.

---

## Migration from v1

v1 `FwToggle`:

```cpp
FwToggle t;
t.setState(true);
t.setOnToggle([](bool v){ ... });
t.setLabel("Mute");
```

v2 renames `setOnToggle` → `setOnChange` for cross-widget
consistency. Otherwise source-compatible.

New in v2:

- `setVariant(ToggleVariant)` — v1 was always button-style.
- `setScrollFlipsState(bool)` — new feature.
- `setCancelOnPointerOut(bool)` — matches Button.
- `ValueChangeSource` on `setState` — disambiguates user vs
  programmatic vs automation changes.

Breaking: `setOnToggle` → `setOnChange`. Mechanical `sed` rename
across call sites (~15 spots).

---

## Open questions

1. **Middle-click?** Currently unused. Could be "momentary toggle
   while held" for workflows like "listen to the solo'd track only
   while holding the middle button". Niche; deferred.

2. **Shift-click for exclusive toggle?** A toolbar row where Shift+
   clicking one toggle unsets all siblings is a common UX pattern.
   Could handle at `FwRadioGroup` level instead; doesn't belong in
   the toggle primitive.

3. **Color themes per state?** Could take both `offColor` and
   `onColor` overrides to allow stronger per-instance visual
   distinction (e.g. red-tinted Record toggle vs green Play toggle).
   Easy to add; wait for a real use case.
