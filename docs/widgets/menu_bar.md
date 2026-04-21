# MenuBar — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `MenuBar` class in `src/ui/MenuBar.h`.
**Related:** [context_menu.md](context_menu.md) — MenuBar is the
horizontal-triggers row that opens ContextMenus. Much of the
behavior (submenus, accelerators, keyboard nav) delegates to
ContextMenu.

---

## Intent

The **application's top menu bar** — a horizontal row of labeled
triggers (File, Edit, View, Track, MIDI, Help, …) that each open a
ContextMenu when clicked or hovered. Classic desktop-app pattern;
global keyboard-shortcut dispatch lives here.

MenuBar also owns the **global accelerator table**: Ctrl+S, Ctrl+Z,
Ctrl+Shift+E, etc. When a shortcut is typed anywhere in the app and
not consumed by the focused widget, MenuBar finds the matching menu
entry and fires its action — so File → Save's Ctrl+S shortcut works
whether or not the user ever opens the File menu.

## Non-goals

- **Multi-row menu bars.** Single horizontal row.
- **Vertical menu bar.** If a platform wants left-side menus, that's
  a separate widget. Out of scope.
- **Auto-hiding menu bar.** Always visible. Minimalist full-screen
  hiding is a window-level feature, not MenuBar.
- **Custom menu widgets beyond standard ContextMenu items.** Menus
  are flat/hierarchical ContextMenu-compatible. No embedded sliders
  or color pickers. (Mac "recent files with thumbnails" is a dynamic
  menu, not a custom widget type.)

---

## Visual anatomy

```
    ┌───────────────────────────────────────────────┐
    │ File   Edit   View   Track   MIDI   Help     │  ← MenuBar row
    └───────────────────────────────────────────────┘
       ↑ trigger labels
```

When a trigger is active:

```
    ┌───────────────────────────────────────────────┐
    │ File  ▓Edit▓  View   Track   MIDI   Help     │  ← active trigger highlighted
    └───────┬────┴──────────────────────────────────┘
            │
            ▼
    ┌─────────────────────────┐
    │ Undo           Ctrl+Z   │  ← ContextMenu rendered on Overlay layer
    │ Redo           Ctrl+Y   │
    │ ─────────────────────── │
    │ Cut            Ctrl+X   │
    │ Copy           Ctrl+C   │
    │ Paste          Ctrl+V   │
    └─────────────────────────┘
```

Parts:
1. **MenuBar strip** — horizontal row, `palette.surface` fill, fixed
   height (from `theme().metrics.controlHeight`).
2. **Trigger buttons** — each is a text label with hover / active
   states. No fill when inactive; highlighted when active (menu
   open), or when keyboard-focused via Alt+letter.
3. **Active-menu ContextMenu** — standard ContextMenu on Layer 2,
   anchored below the trigger.
4. **Bottom border** — 1 px `palette.border` under the strip,
   separating it from the main UI below.

---

## Behavior

### Triggers

Each trigger has:

```cpp
struct MenuBarTrigger {
    std::string label;
    std::string mnemonic;        // single character for Alt-shortcut (e.g. "F" for File)
    std::vector<MenuEntry> entries;   // same type as ContextMenu
    std::function<std::vector<MenuEntry>()> dynamicEntries;
    // If set, called at trigger-click time to produce entries (for "Recent Files" etc.)
};
```

Triggers are added once on app startup and rarely mutate. The
dynamic-entries callback pattern lets menus like "Open Recent" refresh
their list on every open without the MenuBar knowing about the
underlying data source.

### Activation modes

**Click activation:**
- Click trigger → open its menu.
- Click outside → close (standard ContextMenu outside-dismiss).
- Click a different trigger while one is open → close previous, open
  new in one motion (no intermediate flicker).

**Hover tracking:**
- Once a menu is open, hovering a different trigger auto-opens its
  menu and closes the previous (standard desktop menu bar pattern —
  hover to browse).
- Hover with no menu open: just highlights (no auto-open).

**Keyboard activation:**
- `Alt` alone: focuses the MenuBar (highlights first trigger); Alt
  again cancels.
- `Alt + <mnemonic>`: opens the trigger with that mnemonic directly.
- With MenuBar focused: Left/Right arrows move trigger selection,
  Enter/Down arrow opens the focused trigger's menu.

### Accelerator dispatch

Global keyboard shortcuts (Ctrl+S, Ctrl+Z, F11, …) are registered by
traversing every trigger's entries and collecting entries with a
`shortcut` string. When the app's main keyboard dispatch sees an
unhandled key combo, it asks MenuBar:

```cpp
bool MenuBar::tryDispatchAccelerator(const KeyEvent& e);
```

MenuBar looks up the shortcut in its accelerator table, fires the
corresponding entry's `onClick` if found, returns `true`. If no
matching shortcut, returns `false` and the key propagates elsewhere.

Accelerator table is rebuilt when triggers change or their `entries`
change.

### Dynamic entries cache

When a trigger has `dynamicEntries`, the list is rebuilt every time
the trigger opens. Between opens, the cached list is stale —
accelerators for dynamic entries don't fire until the menu has been
opened at least once. This is a minor quirk; main-menu items
(always static) aren't affected.

Workaround: callers using dynamic entries for time-sensitive
accelerator-worthy actions register them as static entries with a
dynamic label/icon instead.

---

## Public API

```cpp
class MenuBar : public Widget {
public:
    MenuBar();

    // Triggers
    void addTrigger(MenuBarTrigger trigger);
    void removeTrigger(const std::string& label);
    void clearTriggers();
    int  triggerCount() const;

    // Setters for individual triggers
    void setTriggerEntries(const std::string& label, std::vector<MenuEntry> entries);
    void setTriggerDynamicEntries(const std::string& label,
                                    std::function<std::vector<MenuEntry>()> provider);

    // Activation
    void openTrigger(const std::string& label);
    void closeActive();
    bool isTriggerOpen() const;
    std::string activeTriggerLabel() const;     // "" if none

    // Accelerators
    bool tryDispatchAccelerator(const KeyEvent& e);
    // App's main keyboard handler calls this after focused-widget
    // dispatch. Returns true if consumed.

    // Appearance
    void setHeight(float px);                     // default controlHeight
    void setTriggerPadding(float px);             // horizontal padding per trigger

    // Callbacks (rarely needed; most action flow happens in menu entries' onClick)
    void setOnTriggerOpened(std::function<void(const std::string& label)>);
    void setOnTriggerClosed(std::function<void(const std::string& label)>);

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

---

## Gestures

### Pointer

| Gesture | Result |
|---|---|
| **Click trigger** (no menu open) | Open that trigger's menu. |
| **Click trigger** (different one open) | Switch to clicked trigger's menu. |
| **Click same trigger** (while open) | Close (toggle). |
| **Hover trigger** (menu already open) | Switch to hovered trigger's menu. |
| **Click outside strip + menu** | Close menu; also leaves keyboard focus. |
| **Right-click** | No default action. |

### Keyboard

| Key | Action |
|---|---|
| `Alt` (press + release) | Enter menu-navigation mode. MenuBar gets focus; first trigger highlighted. |
| `Alt` again (in nav mode) | Exit, return focus to previous widget. |
| `Alt + <mnemonic letter>` | Open matching trigger directly, from anywhere in app. |
| `Left` / `Right` (menu-nav mode or menu open) | Switch to adjacent trigger; keeps menu state (if open, opens new trigger's menu). |
| `Down` / `Enter` (trigger focused, menu closed) | Open menu, focus first item. |
| `Escape` | Close menu; exit menu-nav mode. |
| Accelerator key combos | Dispatch to matching entry's onClick (global, regardless of focus). |

### Touch

- Tap trigger opens menu. Tap outside closes. No hover.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

MenuBar takes the full width given to it, preferred height is
`controlHeight + 4`.

```cpp
Size MenuBar::onMeasure(Constraints c, UIContext& ctx) {
    float h = m_height > 0 ? m_height : theme().metrics.controlHeight + 4;
    float w = c.hasBoundedWidth() ? c.maxW : 600.0f;
    return c.constrain({w, h});
}
```

Each trigger's width: text width + 2 × padding.

### `onLayout(Rect bounds, UIContext& ctx)`

Lay out triggers left-to-right with no gap. Last trigger can be
right-aligned if caller sets `trigger.alignRight = true` (e.g., a
"Help" trigger aligned to the right edge).

Trigger click + hover bounds match the trigger's visual bounds.

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Fixed }
```

### Relayout boundary

**Yes, automatically.** Trigger list changes invalidate, but
MenuBar's own size is fixed.

### Caching

Measure: `(constraints, height, triggerCount, trigger labels,
padding, font scale)`. State changes (open/close, hover) are paint-
only. Accelerator table rebuild is separate from measure/layout.

---

## Paint

1. Strip background `palette.surface`.
2. Each trigger: hover / pressed / active tint as applicable.
3. Trigger label text, `palette.textPrimary` normally, or
   `palette.accent` when the trigger's menu is open.
4. Mnemonic underline (when in Alt nav mode): underline the
   mnemonic character of each trigger label, faintly.
5. Bottom border line.

ContextMenu paint is independent — lives on Layer 2, not MenuBar.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.surface` | Strip background |
| `palette.controlHover` | Trigger hover fill |
| `palette.controlActive` | Trigger pressed fill (brief) |
| `palette.accent` | Trigger text when menu open |
| `palette.accent.withAlpha(40)` | Open-trigger background tint |
| `palette.textPrimary` | Trigger label |
| `palette.textSecondary` | Mnemonic underline |
| `palette.border` | Bottom border |
| `metrics.controlHeight` | Default strip height |
| `metrics.baseUnit` | Trigger padding horizontal |
| `metrics.fontSize` | Trigger label |

---

## Events fired

- `onTriggerOpened(label)` — fires when any menu opens.
- `onTriggerClosed(label)` — fires when the active menu closes.
- Individual menu entries' `onClick` callbacks fire via
  ContextMenu normally.

---

## Invalidation triggers

### Measure-invalidating

- `addTrigger`, `removeTrigger`, `clearTriggers`
- `setTriggerEntries` (changes trigger width if label unchanged: paint-
  only; but entries may affect accelerator table → rebuild)
- `setHeight`, `setTriggerPadding`
- DPI / theme / font

### Paint-only

- Trigger hover / press state
- Active trigger change (open/close)
- Alt-nav mode enter/exit (mnemonic underline visible)

### Accelerator table rebuild

Separate from measure/layout — rebuilt when any trigger's entries
change. Table is a `std::unordered_map<std::string shortcut, MenuEntry
ref>`.

---

## Focus behavior

- **MenuBar itself is focusable** only during Alt-nav mode.
  Otherwise, Tab skips past it.
- When an open menu closes, focus returns to the widget that held it
  before menu was opened (MenuBar tracks this).

---

## Accessibility (reserved)

- Role: `menubar`.
- Each trigger: `menuitem` with `aria-haspopup="true"`.
- `aria-expanded` on the active trigger.

---

## Animation

- **Trigger state transitions** (hover / active): 80 ms.
- **Menu open / close**: inherited from ContextMenu.
- **Trigger-to-trigger switch** (hover-to-open): no animation —
  instant close of old menu, instant open of new. Users expect this
  to feel snappy.

---

## Test surface

Unit tests in `tests/test_fw2_MenuBar.cpp`:

### Basic

1. `EmptyNoTriggers` — MenuBar with no triggers measures to just
   strip height, no interactive area.
2. `AddTriggerAppears` — adding trigger adds it to the strip.
3. `RemoveTrigger` — removal clears and re-lays-out.

### Click activation

4. `ClickOpensMenu` — click trigger opens ContextMenu.
5. `ClickSameCloses` — click active trigger closes.
6. `ClickDifferentSwitches` — click different trigger while one
   open: closes first, opens second (no flicker).
7. `ClickOutsideCloses` — click outside strip + menu dismisses.

### Hover tracking

8. `HoverNoMenuNoOpen` — hover trigger with no menu open: just
   highlights, does NOT auto-open.
9. `HoverWithMenuOpenSwitches` — menu open, hover different trigger:
   switches.

### Keyboard

10. `AltFocusesBar` — Alt press enters menu-nav mode, first
    trigger highlighted.
11. `AltAgainExits` — Alt again clears nav mode.
12. `AltMnemonicOpens` — Alt+F opens File menu.
13. `LeftRightArrowsMoveTrigger` — in nav mode, arrows move
    trigger selection.
14. `DownOpensMenu` — Down arrow on focused trigger opens its menu.
15. `EscapeCloses` — Escape closes menu and exits nav mode.

### Accelerator dispatch

16. `AcceleratorFiresOnClick` — Ctrl+S via keyboard fires File →
    Save's onClick.
17. `AcceleratorRebuildsWhenEntriesChange` —
    setTriggerEntries updates accelerator table.
18. `AcceleratorWhenFocusedWidgetDoesntConsume` — Ctrl+Z fires
    Undo if focused widget didn't handle it.
19. `AcceleratorWhenFocusedWidgetConsumes` — Ctrl+Z on a text
    input (text input handles Ctrl+Z for its own undo): MenuBar's
    accelerator does NOT fire.
20. `DynamicEntriesNoAcceleratorUntilOpened` — dynamic entries
    not yet loaded: their accelerators don't fire until menu opens
    once.

### Dynamic entries

21. `DynamicEntriesCalledOnOpen` — dynamic-entries provider invoked
    each trigger open.
22. `DynamicEntriesCachedBetweenOpens` — between opens, no
    spurious provider calls.

### Callbacks

23. `OnTriggerOpenedFires` — fires with trigger label.
24. `OnTriggerClosedFires` — fires on close.

### Cache

25. `TriggerChangeInvalidatesMeasure` — adding trigger bumps
    measure cache.
26. `HoverChangePaintOnly` — hover transitions don't invalidate
    measure.

---

## Migration from v1

v1 `MenuBar` in `src/ui/MenuBar.h` has:
- Triggers and menus hand-built in C++.
- Accelerator dispatch via explicit shortcuts table maintained
  alongside.
- No Alt-nav mode.
- No dynamic entries pattern (recent-files menu hardcoded rebuild).

v2 consolidates:
1. Migrate trigger + entry definitions to the new structure.
2. Remove hand-maintained shortcut table; v2 derives it from
   entries.
3. Add Alt-nav keyboard activation.

No breaking functional changes for end users; code simplifies.

---

## Open questions

1. **Icons in menu bar triggers?** Some apps show a logo icon at the
   left of the bar. Could support as `setLeadingIcon(tex)`. Trivial
   to add.

2. **Per-platform menu bar placement?** On macOS the menu bar lives
   in the system bar, not in the window. v2.0 always renders inside
   the app window. Mac-native menu bar would require SDL
   Menu integration; platform-specific optimization deferred.

3. **Keyboard shortcuts shown in menu**: ContextMenu already does
   this via `MenuEntry.shortcut`. We just ensure MenuBar populates
   it from its accelerator table.

4. **Shortcut conflicts?** Two entries with the same shortcut:
   current spec says first registered wins. Warn in dev builds;
   silently first-wins in release. Simple rule.

5. **Context-sensitive menu items?** Menu item enabled/disabled
   based on app state (e.g., "Paste" grayed when clipboard is empty).
   Pattern: caller updates `MenuEntry.enabled` on every menu open (or
   maintains a command-pattern system that MenuBar reads). Flagged
   as a broader architectural question in ContextMenu spec.
