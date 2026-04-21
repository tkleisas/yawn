# FwDropDown — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `FwDropDown` class in `src/ui/framework/Primitives.h`

**Architectural importance:** This is the **first Overlay-layer
consumer** specified. Every concept in the architecture doc's layer-
stack section (`OverlayEntry`, hit-testing-across-layers, outside-
click dismissal, upward positioning, focus transfer) gets concrete
requirements here. Tooltip and ContextMenu will follow the same
pattern; getting DropDown right shapes them.

---

## Intent

A **single-selection picker**: button-looking widget displaying the
current selection; clicking it opens a popup list of options; clicking
an item selects it and closes the list. The most common "choose one"
UI element — used for VST3 plugin picker, track type, scale picker,
preferences enums, export format.

FwDropDown's whole job is to make "select one value from a list of
enumerated options" trivial for the caller: pass a list of strings +
a callback, done. Under the hood it coordinates with the layer stack
to draw the popup correctly regardless of the button's on-screen
position.

## Non-goals

- **Multi-select.** A multi-select picker (checkboxes in a list) is
  a different widget (`FwMultiSelect`). DropDown selects exactly one.
- **Filter-as-you-type combo box.** If you need a searchable, open-
  ended text+list hybrid, that's `FwCombobox`. DropDown's list is
  fixed; users navigate by arrow keys or type-ahead jump-to-letter.
- **Hierarchical submenus.** Those are `ContextMenu`. DropDown lists
  are flat.
- **Item icons / rich item rendering.** v2.0 keeps items as plain
  strings. Rich items (icon + label + shortcut) are a separate
  `FwRichDropDown` or a future extension point (`setItemRenderer`).
- **Editable options list.** The caller sets the item list once
  (or when data changes); end users don't add items through the
  widget.

---

## Visual anatomy

### Closed state (button-like)

```
    ┌────────────────────────┐
    │                         │
    │  C Major          ▾    │    selection text, dropdown glyph right-aligned
    │                         │
    └────────────────────────┘
```

Parts:
1. Background — `controlBg`, corner radius from theme.
2. Border — `border`, 1 px (2 px + `accent` when focused).
3. Selection text — `textPrimary`, left-aligned, inset by padding.
4. Dropdown glyph — `▾` (or similar), right-aligned, `textSecondary`.
5. Hover / press tint — overlay while idle→hovered→pressed.
6. Focus ring — border styled as focus ring.

### Open state

The button is unchanged except for a press-looking fill and the
glyph rotates to ▴ (hint: clicking again closes). The popup list is
drawn on Layer 2 (Overlay) via an OverlayEntry owned by the DropDown.

```
    ┌────────────────────────┐
    │  C Major          ▴    │   ← button (still on Layer 0, pressed look)
    ├────────────────────────┤
    │  C Major               │   ← popup list on Layer 2 (Overlay)
    │  D Dorian              │      highlighted row = hover or kb selection
    │▓▓E Phrygian▓▓▓▓▓▓▓▓▓▓▓▓│
    │  F Lydian              │
    │  G Mixolydian          │
    │  …                     │   ← more items (scrollable)
    └────────────────────────┘
```

Popup layout:
1. **Popup background** — `elevated` with drop-shadow
   (`theme().palette.dropShadow`, offset 2 px, blur 8 px).
2. **Item rows** — each `itemHeight` tall, full-popup-width.
3. **Highlight row** — `accent.withAlpha(80)` fill for the currently-
   highlighted row (either mouse-hovered or keyboard-focused).
4. **Selected row indicator** — subtle left-edge accent stripe for
   the row matching current selection, drawn independently of
   highlight. Easy to spot "where am I" when scrolled.
5. **Item text** — `textPrimary`. `textDim` for disabled items.
6. **Scroll bar** — if the list is longer than the visible popup
   height, a compact scroll bar appears on the right.

### Upward-opening variant

If the button is near the bottom of the window and opening downward
would clip the popup, the popup opens **upward** instead. The glyph
rotation flips too (▴ when closed means "opens upward"). User never
sees a clipped list — this is a correctness feature, not a nicety.

```
    ┌────────────────────────┐
    │  Penta Maj             │   ← popup above
    │  …                     │
    │  Dorian                │
    │▓▓C Major▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
    ├────────────────────────┤
    │  C Major          ▾    │   ← button (near bottom of screen)
    └────────────────────────┘
                                  ↑ screen bottom edge
```

---

## Layer integration

Central mechanism: the popup is an **`OverlayEntry`** on Layer 2
(Overlay), not a child widget. This is the architectural reason
dropdowns can never be obscured by later-drawn sibling widgets in
Layer 0 — they literally don't live there.

### Entry lifecycle

```cpp
// Pseudocode — actual API in LayerStack.h

void FwDropDown::openPopup() {
    if (m_overlayHandle.valid()) return;

    OverlayEntry entry;
    entry.owner = this;
    entry.paint = [this](UIContext& ctx) { paintPopup(ctx); };
    entry.hitTest = [this](MouseEvent& e) { return handlePopupMouse(e); };
    entry.bounds = computePopupRect();                  // global coords
    entry.dismissOnOutsideClick = true;
    entry.modal = false;     // clicks outside dismiss + bubble to main layer
    entry.onDismiss = [this]() { m_overlayHandle.clear(); m_open = false; };

    m_overlayHandle = ctx.layerStack().push(OverlayLayer::Overlay, std::move(entry));
    m_open = true;
    m_hoverIndex = m_selectedIndex;    // keyboard starts on current selection
    m_scrollOffset = 0;
    invalidate();    // redraw button in open state
}

void FwDropDown::closePopup() {
    if (m_overlayHandle.valid()) {
        ctx.layerStack().remove(m_overlayHandle);
        m_overlayHandle.clear();
    }
    m_open = false;
    invalidate();
}
```

### Popup rect computation

```cpp
Rect FwDropDown::computePopupRect() const {
    Rect button = globalBounds();
    int n = static_cast<int>(m_items.size());
    float itemH = m_itemHeight;
    float listH = std::min(n, m_maxVisibleItems) * itemH
                  + 2 * theme().metrics.baseUnit;   // padding

    // Width: longest item, min = button width, max = button width × 3
    float contentW = longestItemWidthLogical();
    float listW = std::max(button.w, std::min(contentW + 2 * theme().metrics.baseUnit,
                                                button.w * 3.0f));

    // Try below the button first.
    float spaceBelow = windowHeight - (button.y + button.h);
    float spaceAbove = button.y;
    bool openDown = (spaceBelow >= listH) || (spaceBelow >= spaceAbove);

    float listY;
    if (openDown) {
        listY = button.y + button.h;
        listH = std::min(listH, spaceBelow - theme().metrics.baseUnit);
    } else {
        listY = button.y - listH;
        listH = std::min(listH, spaceAbove - theme().metrics.baseUnit);
        // If even the upward space isn't enough, clamp list height — the
        // internal scrollbar handles overflow.
    }

    float listX = button.x;
    // Clamp horizontally: if listX + listW > windowWidth, shift left.
    if (listX + listW > windowWidth)
        listX = windowWidth - listW - theme().metrics.baseUnit;
    listX = std::max(listX, float(theme().metrics.baseUnit));

    return { listX, listY, listW, listH };
}
```

### Hit test

The OverlayEntry's hitTest function maps input:
- Inside popup rect → handle as item hover / click / scroll.
- Outside popup rect → return false; LayerStack's outside-click
  dismissal fires.

### Paint

The popup paint function runs **after** the main Layer 0 paint
completes, so the popup always draws on top. Internal clip rect is
set to the popup's bounds (prevents overflow into the button or
other screen areas).

### Closing conditions

| Trigger | Result |
|---|---|
| Click on an item | Select item, close, fire `onChange`. |
| Click outside popup | Close without selecting (no callback). |
| Escape key (with popup focused) | Close without selecting. |
| Tab key | Close, commit current hover as selection (if any), move focus. |
| Button clicked again | Close without selecting (toggle behavior). |
| Dropdown removed from tree | Close; destructor calls `remove(handle)`. |
| Window loses focus | Popup closes (optional, default on). |

---

## States

| State | Trigger | Visual cue |
|---|---|---|
| **closed-idle** | default | button `controlBg` |
| **closed-hovered** | pointer over button | `controlHover` |
| **closed-pressed** | left-click in progress | `controlActive` briefly |
| **open** | overlay entry active | button shows pressed look, glyph rotated |
| **focused** | keyboard focus | border recolored |
| **disabled** | `setEnabled(false)` | desaturate; no input, no open |

Popup-internal states (within the OverlayEntry):

- **hover-index** — mouse over an item.
- **keyboard-index** — arrow-key navigation position.
- **typed-buffer** — type-ahead accumulation (see below).

Only one of hover-index / keyboard-index is painted with highlight at
a time (last-input wins — if the user started keyboard-navigating
then moves the mouse, mouse wins).

---

## Gestures

### Button (closed state)

| Gesture | Result |
|---|---|
| **Click** | Toggle popup open/close. |
| **Right-click** | Fires `onRightClick(screenPos)` for MIDI Learn / context menu. Does NOT open popup. |
| **Scroll wheel** | Change selection without opening popup: up = previous item, down = next. `onChange` fires. Opt-out via `setScrollChangesSelection(false)`. |

### Popup (open state)

| Gesture | Result |
|---|---|
| **Pointer hover** over item | Highlight that item. |
| **Click** on item | Select, close, `onChange`. |
| **Click** outside popup | Close; no selection change. |
| **Scroll wheel** in popup | Scroll the item list (if more items than visible). |
| **Drag** inside popup | Ignored. |

### Keyboard (focus on button)

When closed:

| Key | Action |
|---|---|
| `Enter` / `Space` | Open popup; keyboard index starts at current selection. |
| `Alt+Down` | Open popup (explicit open; some users expect this). |
| `Up` / `Down` | Change selection ±1 without opening. `onChange` fires. |
| `Home` / `End` | Jump selection to first / last. |
| `Tab` | Pass focus to next widget. |

When open:

| Key | Action |
|---|---|
| `Up` / `Down` | Move keyboard highlight ±1. |
| `PgUp` / `PgDn` | Move highlight by visible page. |
| `Home` / `End` | Highlight first / last item. |
| `Enter` / `Space` | Commit keyboard-highlighted item. |
| `Escape` | Close without selecting. |
| `Tab` | Close, commit highlight as selection, move focus. |
| printable character | Type-ahead — see below. |

### Type-ahead

Pressing a printable key while open adds it to a short buffer (cleared
after 800 ms of no typing). Each keystroke:

1. Appends to the buffer.
2. Finds the first item whose label starts with the buffer (case-
   insensitive), starting from the current keyboard-highlight index
   and wrapping.
3. Highlights that item (scrolls into view).

Examples:
- Type `C` → jump to first item starting with "C".
- Continue typing `CM` within 800 ms → jump to first "CM…" match.
- Stop typing for > 800 ms → buffer resets, next letter starts fresh
  search.

---

## Public API

```cpp
class FwDropDown : public Widget {
public:
    using ChangeCallback = std::function<void(int index, const std::string& label)>;
    using RightClickCallback = std::function<void(Point screen)>;

    FwDropDown();
    explicit FwDropDown(std::vector<std::string> items);

    // Items
    void setItems(std::vector<std::string> items);
    void addItem(std::string item);
    void clearItems();
    const std::vector<std::string>& items() const;
    int itemCount() const;

    // Per-item options
    void setItemEnabled(int index, bool enabled);
    bool isItemEnabled(int index) const;
    void setItemSeparator(int index, bool isSeparator);  // non-selectable visual line
    bool isItemSeparator(int index) const;

    // Selection
    void setSelectedIndex(int idx, ValueChangeSource src = ValueChangeSource::Programmatic);
    int  selectedIndex() const;
    const std::string& selectedLabel() const;         // "" if none selected or out of range

    // Placeholder (when no selection)
    void setPlaceholder(std::string p);               // shown when selectedIndex < 0

    // Popup sizing
    void setMaxVisibleItems(int n);                   // default 10
    void setItemHeight(float h);                      // default controlHeight

    // Behavior
    void setScrollChangesSelection(bool b);           // default true — wheel on closed button
    void setCloseOnWindowBlur(bool b);                // default true
    void setWidthMatchesButton(bool);                 // default false — popup can be wider

    // Callbacks
    void setOnChange(ChangeCallback cb);               // fires on commit (keyboard or mouse)
    void setOnRightClick(RightClickCallback cb);

    // Appearance
    void setAccentColor(Color c);                     // for selected-row stripe
    void clearAccentColor();

    // Sizing
    void setMinWidth(float w);
    void setPreferredWidth(float w);

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### Firing rules

- **`onChange`** fires exactly when the selection changes, for any
  commit path: mouse click on item, keyboard Enter on highlight, Tab
  commit, Up/Down while closed, scroll wheel while closed.
- Does NOT fire when the popup opens or closes without a selection
  change.
- Fires for `setSelectedIndex(idx, Programmatic)` only if `idx` differs
  from the current selection (no-op otherwise).
- Fires for `setSelectedIndex(idx, Automation)` — automation-driven
  dropdown changes are visually significant; UIs should re-render.
  (Same precedent as Toggle.)

### Separators & disabled items

Separators are visual lines between items (e.g. to group scales by
category). They occupy one item slot but:
- Can't be selected (click is ignored, keyboard skips).
- Render with no label, just a horizontal line.

Disabled items render dim, can't be selected, keyboard skips past them.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Measures only the **button**, not the popup. The popup is an overlay
and doesn't participate in the widget tree's layout.

```cpp
Size FwDropDown::onMeasure(Constraints c, UIContext& ctx) {
    // Prefer: fit longest item label + padding + glyph + padding
    float textW = longestItemWidthLogical();
    if (m_placeholder.length() > 0 && m_selectedIndex < 0) {
        textW = std::max(textW, ctx.font->textWidth(m_placeholder, fontScale()));
    }
    float glyphW = ctx.font->textWidth("▾", fontScale());
    float w = textW + glyphW + 4 * theme().metrics.baseUnit;
    if (m_preferredWidth > 0) w = m_preferredWidth;
    else                      w = std::max(w, m_minWidth);
    float h = theme().metrics.controlHeight;
    return c.constrain({w, h});
}
```

Caching longest-item-width: this is computed once per `setItems()`
call and cached — recomputing it per measure on a dropdown with 30
scales would be wasteful.

### `onLayout(Rect b, UIContext& ctx)`

Store bounds. Popup bounds are recomputed each time the overlay
opens (since the button may have moved due to a panel resize).

### Size policy

```cpp
SizePolicy{ width = Fixed, height = Fixed }
```

### Relayout boundary

**Yes.** Selection changes don't affect size (button width is driven
by the longest *possible* item, not the current). Item list changes
invalidate measure but don't propagate (fixed size from the caller's
perspective).

### Caching

- **Measure cache** — `(constraints, longestItemW, placeholder text,
  font scale, preferredWidth, minWidth)`. Recomputed on `setItems`,
  DPI change, theme change.
- **Layout cache** — trivial, just bounds.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Button fill |
| `palette.controlHover` | Button hover |
| `palette.controlActive` | Button press |
| `palette.border` | Button border |
| `palette.accent` | Focus ring, selected-row indicator stripe |
| `palette.accent.withAlpha(80)` | Popup row highlight |
| `palette.elevated` | Popup background |
| `palette.dropShadow` | Popup drop shadow |
| `palette.textPrimary` | Item text, selection text |
| `palette.textSecondary` | Dropdown glyph, separator line |
| `palette.textDim` | Disabled item, placeholder |
| `metrics.controlHeight` | Button height, default item height |
| `metrics.cornerRadius` | Button corners, popup corners |
| `metrics.borderWidth` | Button border |
| `metrics.baseUnit` | Popup padding |
| `metrics.fontSize` | All text |

---

## Events fired

- `onChange(index, label)` — on selection change.
- `onRightClick(screenPos)` — for MIDI Learn context menu.
- `FocusEvent` — gain/loss.

---

## Invalidation triggers

### Measure-invalidating

- `setItems`, `addItem`, `clearItems` (longest item width may change)
- `setPlaceholder`
- `setPreferredWidth`, `setMinWidth`
- DPI / theme / font (global epoch)

### Paint-only invalidating

- `setSelectedIndex`
- `setItemEnabled`, `setItemSeparator` (visual state, doesn't affect
  button width because disabled items still count toward longest)
- `setMaxVisibleItems`, `setItemHeight` (affects popup only, not
  button)
- `setAccentColor`, `clearAccentColor`
- Hover / press / focus / open-state transitions

### Overlay-state invalidating

Opening or closing the popup invalidates the **overlay layer's**
paint, not the button's measure. The button itself repaints once
(state change from closed → open or vice versa) for the glyph and
fill updates.

---

## Focus behavior

- **Button is tab-focusable** — yes. The popup is NOT independently
  focusable; while open, the button retains focus and forwards
  keyboard events to popup navigation.
- **Auto-focus on click:** yes — clicking the button focuses it (so
  keyboard works immediately even for users who started with a click).
- **Focus ring:** border styled as focus ring.
- **Focus lost while open:** closes the popup if
  `setCloseOnFocusLoss(true)` (default).

---

## Accessibility (reserved)

- Role: `listbox` (button-style), with popup children as `option`
  roles when open.
- `aria-expanded` (true/false), `aria-activedescendant` (current
  keyboard highlight), `aria-selected` on each option.
- `setAriaLabel` — describes the semantic purpose ("Scale", "Plugin
  format", etc.).

---

## Animation

- **Button state transitions:** 80 ms fade on hover / press colors,
  same as other controls.
- **Glyph rotation:** 120 ms rotation on open/close. Adds polish
  without slowing the interaction.
- **Popup appearance:** 100 ms opacity fade. Scale effect (slight
  growth) considered but rejected — DAW users want the list NOW.
- **Keyboard highlight transitions:** instant. No fade on row
  selection changes.
- **Scrolling into view:** 150 ms smooth scroll when jumping to a
  distant item via type-ahead.

---

## Test surface

Unit tests in `tests/test_fw2_DropDown.cpp`. Tests that exercise the
OverlayEntry mechanics need a fake LayerStack harness.

### Measure & layout

1. `MeasureLongestItemDrivesWidth` — dropdown with items
   ["short", "longer", "longest_item_here"] measures to longest +
   padding + glyph.
2. `PreferredWidthOverrides` — `setPreferredWidth(100)` overrides
   content-driven width.
3. `MeasureCacheHit` — second measure with same state is cache hit.
4. `AddItemInvalidatesMeasure` — `addItem("longer-than-all-existing")`
   bumps cache and produces larger measure.

### Basic selection

5. `InitialSelectionNone` — `selectedIndex()` is -1 by default,
   placeholder shown.
6. `SetSelectedIndexShowsLabel` — selection visible as button label.
7. `SetSelectedIndexOutOfRangeClamps` — `setSelectedIndex(999)` →
   clamps to -1 (no selection).

### Open / close

8. `ClickOpens` — clicking the button pushes an OverlayEntry onto
   Layer 2.
9. `ClickAgainCloses` — clicking the button a second time removes
   the entry.
10. `EscapeCloses` — Escape while open closes without selecting.
11. `TabClosesAndCommits` — Tab while open commits current highlight
    + moves focus.
12. `OutsideClickDismisses` — click on main layer outside popup
    bounds removes the entry without firing `onChange`.
13. `ClickItemSelectsAndCloses` — click on an item fires `onChange`
    and removes the entry.

### Positioning

14. `PopupOpensDownByDefault` — button at y=50 with room below →
    popup at y = 50 + buttonHeight.
15. `PopupOpensUpWhenNoRoomBelow` — button near window bottom +
    10 items (popup height > spaceBelow) → popup above button.
16. `PopupClampsHorizontally` — button at right edge of window →
    popup shifts left to stay within window.
17. `PopupHeightCappedByMaxVisible` — 30 items + maxVisible=10 → popup
    height = 10 × itemHeight + padding.
18. `ScrollIntoViewOnOpen` — selectedIndex=15, maxVisible=10 → open
    with list scrolled so item 15 is visible.

### Keyboard

19. `DownArrowWhenClosedChangesSelection` — closed + Down changes
    selection.
20. `DownArrowWhenOpenMovesHighlight` — open + Down moves keyboard
    index without committing.
21. `EnterCommitsHighlight` — open + Enter commits current highlight.
22. `HomeEndJumps` — Home/End jump highlight to first/last item.
23. `PgDnMovesByPage` — PgDn moves highlight by `maxVisibleItems`.

### Type-ahead

24. `TypeAheadJumps` — open + type 'C' → first "C…" item highlighted.
25. `TypeAheadMultiChar` — rapid 'C'+'M' jumps to first "CM…" item.
26. `TypeAheadResetsAfterTimeout` — gap >800 ms clears buffer.
27. `TypeAheadCaseInsensitive` — 'c' matches "C Major".
28. `TypeAheadSkipsDisabled` — disabled items are skipped.

### Separators & disabled items

29. `SeparatorNotSelectable` — clicking a separator row does nothing.
30. `KeyboardSkipsDisabled` — Down arrow skips past disabled items
    when navigating.

### Scroll-wheel selection when closed

31. `ScrollUpSelectsPrevious` — wheel up on closed button changes
    selection to previous item.
32. `ScrollOptOut` — with `setScrollChangesSelection(false)`, wheel
    passes through.

### Firing rules

33. `OnChangeFiresForKeyboardCommit` — arrow + Enter fires once.
34. `OnChangeFiresForMouseClick` — click on item fires once.
35. `OnChangeNotFiredOnOpen` — opening without selecting doesn't fire.
36. `OnChangeNotFiredForSameIndex` — `setSelectedIndex(current)` is
    a no-op.
37. `OnChangeFiredForAutomation` — automation-source updates DO
    fire `onChange` (per precedent with Toggle).

### Right-click

38. `RightClickOnButtonFiresCallback` — doesn't open popup.
39. `RightClickInsidePopupIgnored` — right-click on item row ignored
    (would be weird to show MIDI Learn for an individual item).

### Edge cases

40. `EmptyItemsListClickDoesNothing` — button with no items: click
    does not open a popup.
41. `SingleItemSelectIt` — one item: opening and clicking commits;
    no reason to disallow.
42. `AllDisabledKeyboardDoesNothing` — list of all-disabled items:
    arrow keys don't move (nothing to land on).

### Cache & lifecycle

43. `OpenDoesNotInvalidateMeasure` — opening the popup is pure
    overlay push; button measure cache stays valid.
44. `CloseDoesNotInvalidateMeasure` — symmetrically.
45. `DestroyingDropDownClosesPopup` — FwDropDown destructor removes
    its overlay handle.
46. `WindowBlurClosesPopup` — with default `setCloseOnWindowBlur
    (true)`, window focus loss closes.

---

## Migration from v1

v1 `FwDropDown`:

```cpp
FwDropDown d;
d.setItems({"A", "B", "C"});
d.setSelectedIndex(0);
d.setOnChange([](int idx){ ... });
```

v1 uses `paintOverlay()` — requires the parent panel to remember to
call it after its main paint loop. v2 uses LayerStack, so the call
site doesn't have to think about overlay at all.

**Source-compatible:** yes for basic usage. Signature change:

- `setOnChange(std::function<void(int)>)` → `setOnChange(
  std::function<void(int, const std::string&)>)`. v2 passes both
  index and label for convenience. Mechanical migration via `sed`.

New in v2:

- `setItemEnabled`, `setItemSeparator`, disabled + separator rendering.
- Keyboard navigation (v1 had minimal keyboard support).
- Type-ahead search.
- Upward-opening when near screen bottom (v1 always opened downward,
  sometimes clipped).
- Scroll-wheel selection change on closed button.
- `setCloseOnWindowBlur`, `setCloseOnFocusLoss`, etc.
- Right-click callback for MIDI Learn.

---

## Open questions

1. **Multi-column items?** Some dropdowns benefit from "Name |
   Category" two-column rendering. Definitely out of scope for v2.0
   — when needed, it's `setItemRenderer(std::function)` that's paint-
   only and returns the rendered height.

2. **Virtualization for 1000+ items?** The VST3 plugin browser might
   have hundreds of entries. Current design paints every item in the
   popup every frame the popup is open. For 1000 items at 24 px
   each that's 24,000 px of paint work — fine. If that becomes a
   bottleneck, add paint-window virtualization (only paint visible
   rows + scroll margin).

3. **Maxim popup width as percent of window?** Currently clamped at
   3× button width. Some UIs want "always fit the window width no
   matter the button." Proposed: `setPopupMaxWidth(float)` with 0 =
   use default (3× button), > 0 = cap at that value.

4. **Selected-item placement on open?** Should the popup open with
   the selected item at the TOP of the popup, or AT THE MOUSE
   position? (The latter — "scroll the list so the selected item is
   at the mouse pointer" — is classic macOS behavior.) We're going
   with top-of-popup for v2.0 as a simpler default; revisit if
   complaints surface.

5. **Keyboard nav when button is NOT focused but popup is open?**
   If the user clicked the button to open, mouse moves, then they
   start pressing arrow keys — should we capture them? We say yes,
   because the popup being open is a clear-intent signal. Cover in
   test `KeyboardWorksWhileOverlayOpenRegardlessOfButtonFocus`.

6. **Nested dropdowns?** Some panels might want dropdowns-in-
   dropdowns. Out of scope; if that pattern emerges, it probably
   wants to be ContextMenu submenus instead.

7. **Disabled button as placeholder?** If the selected index goes
   out of range (e.g. items list shrinks), what happens? Current
   design: displays placeholder, `selectedIndex()` returns -1. Some
   UIs prefer "stays on previous label". Proposed: current design
   is safer; caller is responsible for explicitly re-selecting if
   they want to preserve.
