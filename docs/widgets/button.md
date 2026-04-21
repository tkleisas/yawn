# FwButton — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `FwButton` class in `src/ui/framework/Primitives.h`

---

## Intent

A rectangular clickable element with a label and/or icon. The simplest
interactive widget in the framework — it doesn't carry a value, it
just fires an action callback when the user clicks. Used everywhere:
transport bar, dialog OK/Cancel, browser "Add Folder", device headers,
menu items.

Establishes the gesture template that Toggle, Knob, Fader, Checkbox
and every other value-free interactive widget inherits from: **click
with optional drag threshold, hover state, disabled state, focus
ring**.

## Non-goals

- **State / value.** A button that remembers whether it was clicked is
  a `FwToggle`, not a `FwButton`. Buttons are stateless from the
  framework's perspective; the caller's action callback is responsible
  for whatever state change happens.
- **Drag behavior.** Dragging from a button does nothing by default.
  Drag-to-drag-out patterns (e.g. drag a preset out of a browser tile)
  use a separate `FwDragHandle` composite built on top of a Button.
- **Built-in icon loading.** Icons are `GLuint` textures owned by the
  caller; Button paints from a provided texture handle. Image loading
  lives in `util/IconLoader`.

---

## Visual anatomy

```
  ┌────────────────────────┐ ←── bounds
  │                        │
  │  [icon]  Label text    │  content: icon (optional) + label (optional)
  │                        │
  └────────────────────────┘
  └─ corner radius from theme
```

Parts, in paint order:

1. **Background fill** — rounded rectangle, full bounds.
2. **Hover / press tint** — same rect, overlaid with `controlHover`
   / `controlActive` at the appropriate state.
3. **Border** — 1 logical px, `theme().palette.border`, subtle
   subtracted-from-alpha effect on disabled.
4. **Content row** — icon (if any) + text (if any), centered both
   axes by default. Content row's internal spacing is
   `metrics.baseUnit` (4 logical px default).
5. **Focus ring** — 2 logical px, inset 2 px, `accent.withAlpha(80)`,
   only drawn when `isFocused()`.

### Layout variants

| Variant | Constructor | Notes |
|---|---|---|
| Text-only | `FwButton("Save")` | Content is just the label, centered. |
| Icon + text | `FwButton(iconTex, "Save")` | Icon left of label, `baseUnit` gap. |
| Icon-only | `FwButton(iconTex)` | Square-ish button, icon centered. |

Default size comes from `theme().metrics.controlHeight` (28 logical
px). Width is driven by content unless constrained tighter.

---

## States

| State | Trigger | Visual cue |
|---|---|---|
| **idle** | default | `controlBg` |
| **hovered** | pointer inside bounds, no button held | `controlHover` fill |
| **pressed** | left mouse button held with pointer still inside | `controlActive` fill, content offset 1 px down |
| **pressed-outside** | pressed state + pointer dragged out of bounds | reverts to `controlBg`; releasing here cancels the click |
| **focused** | keyboard focus (Tab-to) | focus ring drawn |
| **disabled** | `setEnabled(false)` | 40% desaturated, no hover/press response |
| **highlighted** | `setHighlighted(true)` | Soft accent fill, persists across hover states. Used for "currently-selected action" (e.g. active tool in a toolbar). |

Combination rules:

- `disabled` overrides all interactive states — a disabled+hovered
  button looks disabled, not hovered.
- `focused` stacks on top of any state.
- `highlighted` + `hovered` → blend (brighten the accent fill
  slightly).
- `pressed` + `hovered` → pressed wins.
- `pressed-outside` is a distinct sub-state of pressed, not a separate
  top-level state — internally tracked, visually identical to idle.

### State machine (press → release)

```
       ┌─────────┐
       │  idle   │
       └─────────┘
          │ mouse enter
          ▼
       ┌─────────┐
       │ hovered │
       └─────────┘
          │ left-button down inside
          ▼
       ┌─────────┐       pointer drags out       ┌──────────────────┐
       │ pressed │ ───────────────────────────▶ │ pressed-outside  │
       └─────────┘ ◀─────────────────────────── └──────────────────┘
          │ release inside       │ pointer re-enters while held
          ▼                      │ release outside (cancels click)
       onClick fires             └──▶ no onClick — state returns to hovered/idle
       (state returns to hovered)
```

---

## Gestures

### Pointer — left button

| Gesture | Result |
|---|---|
| **Click** (press inside + release inside, no drag) | Fires `onClick`. Animates a brief press flash (80 ms). |
| **Press + drag out + release outside** | No click fires. Button returns to idle/hovered. |
| **Press + drag out + drag back + release inside** | Click fires. The out-and-back counts as a single click. |
| **Multi-click** (rapid) | Each up-down-up sequence fires one `onClick`. No double-click detection at this level; composites that want it wrap Button. |

### Pointer — right button

| Gesture | Result |
|---|---|
| **Right-click** | Fires `onRightClick` (if set) with screen position. No visual change. Used for context menus. |

### Pointer — scroll wheel

Ignored. Buttons don't respond to scrolling.

### Keyboard (when focused)

| Key | Result |
|---|---|
| `Space` or `Enter` | Fires `onClick` (same as a mouse click). Brief press flash. |
| `Tab` / `Shift+Tab` | Focus next / previous focusable widget. |
| `Escape` | If focused, removes focus; otherwise passed to parent. |

### Touch

Tap = click. Long-press currently unused (reserved for future tooltip
trigger).

---

## Public API

```cpp
class FwButton : public Widget {
public:
    using ClickCallback = std::function<void()>;
    using RightClickCallback = std::function<void(Point screen)>;

    // Construction
    FwButton();
    explicit FwButton(std::string label);
    FwButton(GLuint iconTex, std::string label);
    FwButton(GLuint iconTex);  // icon-only

    // Content
    void setLabel(std::string label);
    void setIcon(GLuint tex, int w, int h);     // w/h in logical pixels
    void clearIcon();
    const std::string& label() const;

    // Callbacks
    void setOnClick(ClickCallback cb);
    void setOnRightClick(RightClickCallback cb);

    // Appearance
    void setHighlighted(bool h);                // persistent accent state
    bool isHighlighted() const;
    void setAccentColor(Color c);               // override theme accent
    void clearAccentColor();                    // revert to theme

    // Behavior
    void setCancelOnPointerOut(bool cancel);    // default true
    // If false: dragging out doesn't cancel; release anywhere fires.
    // Used for toolbars where users commonly slide off buttons.

    // Size hints
    void setMinWidth(float w);                  // 0 = content-driven
    void setFixedWidth(float w);                // overrides content-driven width

    // Accessibility (reserved)
    void setAriaLabel(const std::string& label);
    void setAriaDescription(const std::string& desc);
};
```

**`onClick` firing rules:**

- Fires exactly once per successful click gesture.
- Does NOT fire for right-clicks, middle-clicks, keyboard Tab, hover.
- Fires for Space/Enter when focused (keyboard equivalent of click).
- Does NOT fire when `isEnabled() == false`.

**`setHighlighted`:** persistent visual state for "active" semantics
— a toolbar showing which tool is selected, a tab strip showing the
active tab. Not a click-toggle; the caller sets/unsets explicitly.

---

## Drag / input sensitivity

n/a — Button has no drag-driven values.

Click detection uses the framework's standard 3-logical-px drag
threshold (from the architecture doc's gesture layer). Below threshold
= click; at or above = cancels the click and the base class treats it
as a drag, which Button ignores.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size FwButton::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;

    float iconW = m_hasIcon ? m_iconW : 0.0f;
    float iconGap = (m_hasIcon && !m_label.empty()) ? m.baseUnit : 0.0f;
    float textW = m_label.empty() ? 0.0f : ctx.font->textWidth(m_label);

    float contentW = iconW + iconGap + textW;
    float w = contentW + m.baseUnit * 4;        // horizontal padding
    if (m_fixedWidth > 0) w = m_fixedWidth;
    else                  w = std::max(w, m_minWidth);

    float h = m.controlHeight;

    return c.constrain({w, h});
}
```

Width is content-driven unless `fixedWidth` or `minWidth` intervene.
Height defaults to theme's control height (28 logical px).

### `onLayout(Rect bounds, UIContext& ctx)`

No children. Just store bounds. Trivially cached.

### Size policy

```cpp
SizePolicy{ width = Fixed, height = Fixed }
```

Both dimensions have deterministic sizes from `onMeasure` — Button is
a relayout boundary automatically.

### Relayout boundary

**Yes, automatically.** Content changes (label text) invalidate this
widget's measure but don't propagate above because both dimensions are
fixed. The parent's layout doesn't need to re-run.

### Caching

- **`onMeasure`** — cached on `(constraints, label, icon present,
  fixedWidth, minWidth, font scale)`. Invalidated by `setLabel`,
  `setIcon`, DPI change, theme change, font reload.
- **`onLayout`** — trivially cached.
- **Paint** — per frame; cheap.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Idle fill |
| `palette.controlHover` | Hover fill |
| `palette.controlActive` | Pressed fill |
| `palette.border` | Border |
| `palette.textPrimary` | Label text |
| `palette.accent` | Highlighted fill, focus ring |
| `palette.accent.withAlpha(80)` | Focus ring |
| `metrics.controlHeight` | Default height |
| `metrics.cornerRadius` | Corner radius |
| `metrics.borderWidth` | Border thickness |
| `metrics.baseUnit` | Internal padding, icon-label gap |
| `metrics.fontSize` | Label font size |

Theme swap: full repaint, no re-measure (unless `fontSize` changed,
which bumps the global epoch and invalidates measure caches the
right way anyway).

---

## Events fired

- **Gesture events** (documented above): `onClick`, `onRightClick`.
- **State change events** — when `setHighlighted` / `setEnabled` /
  `setLabel` is called, the widget invalidates its paint cache. These
  aren't public "events" in the framework sense, just internal
  invalidations.
- **FocusEvent** — emitted by the framework on focus gain/loss.

---

## Invalidation triggers

### Measure-invalidating

- `setLabel`
- `setIcon`, `clearIcon`
- `setMinWidth`, `setFixedWidth`
- DPI change (global epoch)
- Theme change (global epoch)
- Font reload (global epoch)

### Paint-only invalidating

- `setHighlighted`, `setEnabled`
- Hover / press / focus state transitions
- `setAccentColor`, `clearAccentColor`
- Icon texture content change (same `GLuint`, new pixels) — manual
  `invalidate()` by the caller, framework has no way to detect this.

---

## Focus behavior

- **Tab-focusable:** yes, by default. `setFocusable(false)` to opt out
  (e.g. a decorative button that's really just a click target for a
  row selection).
- **Auto-focus on click:** no. Buttons do NOT steal focus on click;
  otherwise a typical mixer workflow (click faders repeatedly) would
  thrash keyboard focus. Explicitly opt in via `setFocusOnClick(true)`
  for modal dialog buttons where focus transfer matters.
- **Focus ring:** drawn inset 2 px from bounds, `accent.withAlpha(80)`,
  2 px thick.

---

## Accessibility (reserved)

- `setAriaLabel(std::string)` — fallback label when the visible label
  is an icon-only button (e.g. a close-X button's aria label would be
  "Close").
- `setAriaDescription(std::string)` — optional hint for screen
  readers.
- Role exposed as "button" in the future accessibility tree.

Not implemented in v2.0; designed for future expansion.

---

## Animation

- **Press flash** — 80 ms fade from `controlActive` to `controlBg`
  after a successful click, so visual feedback is visible even for
  sub-frame clicks (keyboard Enter, scripted click).
- **Hover fade in** — 80 ms fade from `controlBg` to `controlHover`
  on pointer enter. Feels less twitchy than an instant snap when
  mousing rapidly across a toolbar.
- **Highlight state** — no fade; snaps immediately. The caller is
  deliberately toggling this, so a fade would feel laggy.

All animations are pure cosmetic; they don't gate the `onClick`
firing or any framework state.

---

## Test surface

Unit tests in `tests/test_fw2_Button.cpp`:

1. `LabelOnlyMeasures` — `measure()` returns `textWidth + padding ×
   controlHeight` for a text-only button.
2. `IconOnlyMeasures` — icon-only button is square-ish with icon
   centered.
3. `IconPlusLabelMeasures` — width = icon + gap + text + padding.
4. `FixedWidthOverrides` — `setFixedWidth(200)` forces `measure()`
   to 200, regardless of content.
5. `MinWidthRespected` — `setMinWidth(100)` with a 50-px label
   produces 100.
6. `ClickFires` — simulated press-release fires `onClick`.
7. `ClickCanceledByDragOut` — press, drag beyond threshold outside
   bounds, release outside → no `onClick`.
8. `ClickRecoveredByDragBack` — press, drag out, drag back in,
   release inside → `onClick` fires.
9. `RightClickFires` — right-button click fires `onRightClick` with
   correct screen coords.
10. `KeyboardSpaceFiresClick` — focused button + Space key fires
    `onClick`.
11. `KeyboardEnterFiresClick` — same for Enter.
12. `DisabledSwallowsEverything` — `setEnabled(false)` causes all
    click/keyboard/hover events to not fire callbacks.
13. `HighlightedRendersDifferently` — visual state test; scene-compare
    or mock-render.
14. `RelayoutBoundary` — `setLabel("longer text")` bumps this
    widget's localVersion but does NOT bubble to parent.
15. `CancelOnPointerOutFalse` — with `setCancelOnPointerOut(false)`,
    drag-out + release outside still fires `onClick`.
16. `MeasureCacheHit` — second `measure()` with same constraints +
    same label returns cached value without calling font's textWidth.

---

## Migration from v1

v1 `FwButton` has a narrower surface. Typical call sites:

```cpp
FwButton btn;
btn.setLabel("Save");
btn.setOnClick([]{ saveProject(); });
```

Source-compatible in v2. New surface additions:

- `setRightClickCallback` → `setOnRightClick` (renamed for
  consistency with other widgets).
- `setHighlighted(bool)` — new; replaces the ad-hoc "draw with accent"
  hack some panels use today.
- `setCancelOnPointerOut(bool)` — new; defaults to old behavior (cancel).
- `setFocusOnClick(bool)` — new; defaults to false (v1 didn't steal
  focus; v2 keeps that). Dialog OK/Cancel buttons should set this
  true explicitly.

No signature-breaking renames.

---

## Open questions

1. **Long-press for tooltip?** If we add hover tooltips later, does a
   long-press on touch surface the same tooltip? Probably yes, behind
   a global preference (touch users discover by long-press; mouse
   users by hover).

2. **Button groups?** A row of mutually-exclusive buttons (toolbar
   picker) is currently hand-rolled. Worth a `FwButtonGroup` composite
   that manages `setHighlighted` across children? Deferred — not
   blocking v2.0.

3. **Badge / dot indicator?** Buttons occasionally want a small badge
   (notification count, unsaved-changes dot). Extension point:
   `setBadge(std::string)` on a corner. Not in v2.0; easy to add later.

4. **Press sound?** Trivial but opinionated. Leave to the app,
   Button just fires `onClick`.
