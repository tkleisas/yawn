# FwRadioGroup / FwRadioButton — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Related:** [checkbox.md](checkbox.md) — similar visual language
(small indicator + label), different semantic (mutually exclusive).
[dropdown.md](dropdown.md) — alternate affordance for "choose one";
use RadioGroup when you want all options visible simultaneously.

---

## Intent

Mutually-exclusive selection from a small set of options, with all
options visible at once. Where DropDown collapses options behind a
button, RadioGroup shows them inline — appropriate when:

- There are few (≤6) options.
- All options are short.
- Comparing options side-by-side is valuable.

Typical uses: scale category filter, mixer view mode (Narrow/Normal/
Wide), record quantize resolution (Off/Bar/Beat), snap-to-grid mode.

## Non-goals

- **Hierarchical groups.** Nested radio groups don't work in HTML
  either; use a DropDown for hierarchy.
- **Multi-select.** A row of checkboxes if you need that.
- **Free-form layout of button children.** RadioGroup manages its
  own layout (horizontal row by default, vertical column opt-in).
  Callers who want a bespoke radio-button layout use naked
  `FwRadioButton`s in their own FlexBox and manage exclusivity
  themselves.

---

## Architecture

Two cooperating widgets:

1. **`FwRadioButton`** — leaf widget. Indicator circle + label.
   Visually identical to Checkbox but with a circular indicator.
   Can exist standalone (rare) or as a RadioGroup child (normal
   case).
2. **`FwRadioGroup`** — container that owns a set of RadioButton
   children, enforces mutual exclusivity, and exposes group-level
   selection API. Children are added via `addOption(label)` or the
   caller can add pre-made RadioButton widgets via `addChild()`.

When a RadioButton is clicked:
- If it has a parent RadioGroup, the group intercepts the click and
  decides which (single) button ends up selected.
- If standalone, the button is just a visual toggle (caller manages
  state).

---

## FwRadioButton

### Visual anatomy

```
    ◯  Option A          ← deselected (circle outline)

    ◉  Option B          ← selected (filled circle with accent dot)
```

Parts:
1. **Outer circle** — `border` stroke, `controlBg` fill.
2. **Inner dot** — for selected state; `accent` fill, ~50% of
   outer diameter.
3. **Label** — FwLabel child, right of circle, baseline-aligned.
4. **Hover / press tint** — overlay on circle only.
5. **Focus ring** — around circle.

Clicking circle OR label selects.

### States

Same state machine as Checkbox minus indeterminate.

| State | Visual |
|---|---|
| **deselected** | empty circle |
| **selected** | circle with dot |
| **hovered / pressed / focused / disabled** | as per Button / Checkbox |

### Gestures

Identical to Checkbox (click / scroll-opt-in / keyboard Space-Enter /
right-click).

### Public API

```cpp
class FwRadioButton : public Widget {
public:
    using StateCallback = std::function<void(bool selected)>;

    FwRadioButton();
    explicit FwRadioButton(std::string label);

    // State
    void setSelected(bool s, ValueChangeSource = ValueChangeSource::Programmatic);
    bool isSelected() const;

    // Label
    void setLabel(std::string);
    const std::string& label() const;

    // Appearance
    void setAccentColor(Color);
    void clearAccentColor();

    // Callbacks — only fires when standalone (no parent group).
    // When in a group, the group's onChange fires instead.
    void setOnChange(StateCallback);
    void setOnRightClick(std::function<void(Point)>);

    void setAriaLabel(const std::string&);
};
```

### Layout / theme / invalidation / testing

Analogous to Checkbox. Measure returns `circle + gap + label`.
Tests mirror Checkbox's test surface minus the indeterminate cases,
plus group-aware variants (see below).

---

## FwRadioGroup

### Intent

Container that manages a set of RadioButtons with exactly-one
selected. Enforces exclusivity on click: clicking any button
deselects all siblings.

### Visual anatomy

The group has no visual of its own. It lays out its RadioButton
children in a FlexBox (horizontal by default, vertical opt-in).

### Public API

```cpp
enum class RadioOrientation {
    Horizontal,
    Vertical,
};

class FwRadioGroup : public Widget {
public:
    using SelectionCallback = std::function<void(int index, const std::string& label)>;

    FwRadioGroup();
    explicit FwRadioGroup(std::vector<std::string> options);

    // Options
    void addOption(std::string label);                  // creates + adds a RadioButton
    void addButton(FwRadioButton* btn);                 // existing button, group takes ownership of exclusivity
    void clearOptions();
    int optionCount() const;

    // Selection
    void setSelectedIndex(int idx, ValueChangeSource = ValueChangeSource::Programmatic);
    int selectedIndex() const;
    const std::string& selectedLabel() const;           // "" if none

    // Layout
    void setOrientation(RadioOrientation);              // default Horizontal
    void setGap(float px);                              // default baseUnit × 2
    void setAllowDeselect(bool);                        // default false — clicking selected doesn't deselect
    // If true: clicking the currently-selected button deselects (no option selected).

    // Callbacks
    void setOnChange(SelectionCallback);                // fires on selection change

    // Appearance (delegates to each button via theme)
    void setAccentColor(Color);
    void clearAccentColor();
};
```

### Exclusivity enforcement

Implemented in the group's event dispatch:

1. A click on any child RadioButton is intercepted by the group
   (via the framework's gesture layer; the group captures the click
   before the child fires its own `onChange`).
2. Group computes new selection:
   - If the clicked button is already selected: stay selected (or
     deselect if `setAllowDeselect(true)`).
   - Otherwise: deselect all children, select the clicked one.
3. Group fires its own `onChange(idx, label)` with the new selection.
4. Children's individual `onChange` is NOT fired (the group's
   callback is the single source of truth).

### Layout contract

```cpp
Size FwRadioGroup::onMeasure(Constraints c, UIContext& ctx) {
    // Delegate to an internal FlexBox
    return m_flex.measure(c, ctx);
}
```

The group internally holds a FlexBox configured per orientation. Gap,
cross-align, and other layout properties pass through. Child widget
measure/layout works via normal FlexBox semantics.

### Size policy

Inherits from the underlying FlexBox — content-driven width unless
children are flexible.

### Relayout boundary

**Yes, automatically** (like any fixed-set-of-children FlexBox
derivative). Option additions / removals invalidate.

---

## Caching

Handled by the internal FlexBox. Selection changes are paint-only
(they affect child rendering, not bounds).

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Circle fill |
| `palette.border` | Circle border (deselected) |
| `palette.accent` | Circle border + dot fill (selected) |
| `palette.textPrimary` | Label text |
| `palette.accent.withAlpha(80)` | Focus ring |
| `metrics.controlHeight` | Row height |
| `metrics.baseUnit` | Circle-to-label gap, inter-option gap |
| `metrics.fontSize` | Label |

---

## Events fired

- **Group `onChange(index, label)`** — on selection change (user or
  programmatic non-same).
- **RadioButton `onRightClick(screenPos)`** — for MIDI Learn (the
  button itself fires this; group doesn't intercept right-clicks).
- **FocusEvent** — per-button focus gain/loss.

---

## Invalidation triggers

### Group measure-invalidating

- `addOption`, `addButton`, `clearOptions`
- `setOrientation`
- `setGap`
- DPI / theme / font (global)

### Group paint-only

- `setSelectedIndex`
- `setAccentColor`, `clearAccentColor`

### RadioButton-level

Same pattern as Checkbox — label / minWidth invalidate measure;
state / accentColor are paint-only.

---

## Focus behavior

- **Group is not focusable** on its own; each RadioButton is
  tab-focusable.
- **Intra-group arrow keys:** when any RadioButton in the group is
  focused, Left/Right (horizontal) or Up/Down (vertical) move focus
  to adjacent sibling buttons AND select that button. This is the
  standard "roving tab index" pattern for radio groups — keyboard
  users navigate between options with arrows, not Tab.
- **Tab** leaves the group entirely (moves to next focusable widget
  outside the group).
- Selecting via arrows fires `onChange` — the selection follows
  focus. (Some UIs prefer "Space to commit", but for radio groups
  the arrow-selects convention is universal and expected.)

---

## Accessibility

- Group role: `radiogroup` (ARIA).
- Button role: `radio`.
- Group `aria-label` describes the question ("Record quantize").
- Each radio `aria-checked`: true/false.
- Arrow-nav-to-select is the ARIA-recommended behavior.

---

## Animation

- **Indicator dot**: no animation; instant swap.
- **Focus ring**: 80 ms fade in, as per other widgets.

---

## Test surface

Unit tests in `tests/test_fw2_RadioGroup.cpp`:

### Single radio button (unit)

1. `StandaloneSelectionToggle` — without a group, click toggles
   selected state (or stays on if user can't deselect).
2. `SelectedPaintsDot` — visual state correct in each state.

### Group exclusivity

3. `AddOptionCreatesButton` — `addOption("A")` adds a RadioButton
   child.
4. `ClickingSelectsAndDeselectsSiblings` — clicking option 2 of
   three unchecks options 1 and 3.
5. `AllowDeselectFalse` — clicking already-selected = no-op.
6. `AllowDeselectTrue` — clicking already-selected → index = -1,
   `onChange(-1, "")` fires.
7. `OnlyGroupOnChangeFires` — button's own `onChange` doesn't fire
   when it's in a group (group's onChange is the source of truth).
8. `SetSelectedIndexProgrammatic` — `setSelectedIndex(2)` updates
   visuals and fires onChange.
9. `SetSelectedIndexOutOfRangeClears` — `setSelectedIndex(99)` sets
   to -1 (or no-op, depending on setAllowDeselect — spec: clamps
   to -1 only if AllowDeselect is true, else ignored).

### Layout

10. `HorizontalDefault` — buttons laid out in a row.
11. `VerticalOrientation` — `setOrientation(Vertical)` stacks.
12. `GapRespected` — `setGap(20)` adds 20 px between buttons.

### Keyboard roving-index

13. `ArrowMovesFocus` — focus on option 0, right arrow → focus on 1
    AND selection changes to 1.
14. `ArrowWrapsAtEnd` — at last option, right arrow wraps to 0.
15. `TabExitsGroup` — Tab moves focus outside the group, not to next
    option.
16. `DisabledSkippedByKeyboard` — disabled radio button is not a
    keyboard target; arrow skips it.

### Firing rules

17. `OnChangeNotFiredProgrammaticSame` — `setSelectedIndex(current)`
    = no-op.
18. `OnChangeFiredForAutomation` — automation source fires.

---

## Migration from v1

v1 has no radio widget. Ad-hoc "three FwToggles where clicking one
unsets the others" patterns exist in dialogs. v2 consolidates into
FwRadioGroup.

No backward-compat concerns.

---

## Open questions

1. **Radio in a FlexBox with arbitrary siblings?** Group enforces
   exclusivity via parent-owned child list. If caller wants a radio
   button inside another container, they use `FwRadioButton` standalone
   and manage exclusivity manually. Fine — group is the convenience
   for the common case.

2. **Required-selection behavior?** `setAllowDeselect(false)` +
   initial selection = -1 means user can never deselect and index
   0 is silently selected on first click. Should we instead default
   to index 0 when `setAllowDeselect(false)`? Feels cleaner. Flagging
   for review.

3. **Custom per-option icons?** Same extension point as DropDown —
   `setOptionRenderer`. Out of scope for v2.0.
