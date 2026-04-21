# ContextMenu — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `ContextMenu` class in `src/ui/ContextMenu.h`
**Related:** [dropdown.md](dropdown.md) — structurally similar
Overlay-layer consumer; ContextMenu adds **hierarchical submenus**,
**shortcuts**, **icons**, **separators**, **disabled items**, and
integrates with [menu_bar.md] for the top-menu-bar flyout pattern.

---

## Intent

A **right-click or programmatic popup menu** with hierarchical item
structure. The primary way users access less-common actions in YAWN:
track header context menus (type / instruments / effects / delete),
clip right-click menus (stop / send to arrangement / MIDI learn),
scene labels (insert / duplicate / delete), transport knobs (reset /
MIDI learn), and the application menu bar (File / Edit / View / …).

ContextMenu is strictly overlay-driven — it never lives in the
widget tree as a child. The host widget calls
`ContextMenu::show(entries, pos)` and forgets about it; the menu's
lifecycle is owned entirely by the Overlay layer.

## Non-goals

- **Tear-off / floating palettes.** Tool palettes that persist
  outside right-click context aren't menus; they're `FwToolPalette`
  (future).
- **Search inside the menu.** Deep menus with a "filter" field are a
  separate widget (`FwCommandPalette`, Ctrl+Shift+P style launcher).
  ContextMenu is click-driven.
- **Drag-and-drop rearranging of items.** Menu structure is fixed at
  open time.
- **Touch.** Long-press-to-context-menu support is deferred —
  ContextMenu is mouse/keyboard only in v2.0.

---

## Architecture

Like DropDown and Tooltip, ContextMenu lives on an Overlay layer.
Specifically Layer 2 (Overlay) — same layer as dropdowns, because
both are user-triggered popups that should stack naturally with each
other (clicking a submenu's trigger opens a child menu, which stacks
above the parent menu on the same layer). Tooltips sit on Layer 3
above both.

### Item types

An `MenuEntry` is a tagged union:

```cpp
struct MenuEntry {
    enum class Kind {
        Item,         // leaf action with label and callback
        Submenu,      // label with nested entries; hover/click expands
        Separator,    // visual separator line, non-interactive
        Header,       // bold section header, non-interactive
        Checkable,    // item with a checkbox indicator that reflects state
        Radio,        // item with a radio indicator; part of a group
    };

    Kind kind = Kind::Item;
    std::string label;
    std::string shortcut;           // display-only, e.g. "Ctrl+S"
    std::optional<GLuint> iconTex;  // left-side icon
    std::function<void()> onClick;  // action for Item / Checkable / Radio
    std::vector<MenuEntry> children; // only for Submenu kind
    bool enabled = true;
    bool checked = false;            // for Checkable / Radio
    std::string radioGroup;          // group ID; radio items with same ID exclude each other
};
```

### Show API

```cpp
class ContextMenu {
public:
    static void show(std::vector<MenuEntry> entries, Point screenPos,
                      const MenuShowOptions& options = {});

    static void close();        // dismisses any open menu
    static bool isOpen();
};

struct MenuShowOptions {
    bool anchorAbove = false;    // like DropDown upward — prefer above the anchor
    Widget* returnFocusTo = nullptr;  // regains focus when menu closes
    float preferredMinWidth = 0.0f;
    int maxVisibleItems = 20;
    // … future extension hooks
};
```

`ContextMenu::show(entries, pos)` pushes an OverlayEntry and returns
immediately. Actions fire from within the entry's hit-test callback
before dismissal.

---

## Visual anatomy

```
    ┌──────────────────────────────────────────┐
    │ [icon]  Label text             Shortcut  │  ← Item
    │                                           │
    │ ─────────────── (separator) ──────────────│  ← Separator
    │                                           │
    │         SECTION                           │  ← Header
    │                                           │
    │ [icon]  ✓  Another thing                  │  ← Checkable (checked)
    │                                           │
    │ [icon]  Some item                  ▶      │  ← Submenu (arrow indicator)
    │                                           │
    │ [icon]  Disabled item          (Ctrl+X)   │  ← Disabled (dim)
    │                                           │
    └──────────────────────────────────────────┘
```

Parts:
1. **Body** — `elevated` background, rounded corners, drop shadow.
2. **Row highlight** — `accent.withAlpha(80)` for hovered/kb-highlighted
   row.
3. **Icon column** — left, reserves fixed width (`metrics.controlHeight`)
   even for rows without icons so label text aligns across rows.
4. **Check/radio indicator** — between icon and label when row is
   Checkable or Radio.
5. **Label** — primary text, `textPrimary` or `textDim` (disabled).
6. **Shortcut** — right-aligned, `textSecondary`, smaller font.
7. **Submenu arrow** — right-aligned, for Submenu rows. `▶` glyph.

Separator is a 1 px horizontal line spanning the body minus the
body padding, drawn in `border` color.

Header is styled with bold font (or uppercase + letter-spaced if
we don't have a bold weight loaded) in `textSecondary` — clearly
non-clickable.

---

## Submenus

A Submenu row:

- Highlights on hover like any other row.
- After a 200 ms hover delay, opens its child menu to the right (or
  left, if near window right edge) of the Submenu row.
- While the child menu is open, the Submenu row stays highlighted
  (so the user can see the click-path).
- Moving the pointer from the Submenu row to the child menu is
  tolerant of a small "travel cone" — as long as the pointer is
  moving toward the child menu, the delay to auto-close other
  submenus is extended. Matches macOS / Windows menu behavior —
  prevents accidental submenu close when the mouse cuts across a
  sibling row briefly.
- Clicking a Submenu row (vs hovering) commits to that submenu
  immediately — open without delay.
- Closing a submenu does NOT close the parent menu. Pressing Escape
  in a submenu returns focus to the parent; another Escape closes
  the parent.

Submenus can nest to any depth, though >2 levels is usually a UX
smell.

---

## States

### Menu-level

| State | Trigger | Visual |
|---|---|---|
| **closed** | default | nothing rendered |
| **opening** | show() called; fade-in animation | body fading up from alpha 0 |
| **open** | entry active on Layer 2 | all rows painted |
| **closing** | dismissed; fade-out | fading down |
| **submenu-tracking** | pointer moving toward child menu | submenu close timers extended |

### Item-level

| State | Trigger | Visual |
|---|---|---|
| **idle** | default | row bg = `elevated` |
| **hovered** | pointer over row | row bg = `accent.withAlpha(80)` |
| **kb-highlighted** | arrow-key navigation landed on row | same as hovered |
| **pressed** | mouse button held on row | subtle darker tint |
| **disabled** | `enabled == false` | label in `textDim`; hover produces no highlight, click/keyboard skip |

---

## Gestures

### Pointer

| Gesture | Result |
|---|---|
| Hover over row | Highlight that row. If Submenu, start 200 ms child-open timer. |
| Move to another row | Cancel previous row's submenu open timer. |
| Click on Item | Fire `onClick`, close entire menu chain. |
| Click on Checkable / Radio | Fire `onClick`, toggle indicator, close. |
| Click on Submenu row | Open child immediately (skip delay). Don't close. |
| Click outside any menu in the chain | Close the entire chain. |
| Click on disabled row | Ignored; no highlight change. |
| Right-click inside menu | Ignored (no "menu within a menu" via right-click). |

### Keyboard (when menu is open)

| Key | Result |
|---|---|
| `Up` / `Down` | Move highlight, skipping separators / headers / disabled. |
| `Home` / `End` | First / last interactive row. |
| `Right` | On Submenu row: open child + move highlight to first child row. On non-submenu: no-op. |
| `Left` | If in a submenu: close child, return to parent. If root menu: no-op. |
| `Enter` / `Space` | Commit highlighted row (same as click). |
| `Escape` | Close current menu (returns to parent submenu if in one; else closes chain). |
| printable key | Type-ahead within current menu — same as DropDown, jumps to label starting with typed buffer. |
| accelerator keys | If the menu entry has `shortcut = "Ctrl+S"` and the user types Ctrl+S while the menu is open → fire that entry. (Bare menu bar has different behavior; see MenuBar spec.) |

### Touch

Deferred — context menus are mouse/keyboard only in v2.0.

---

## Show lifecycle

```cpp
// Caller:
ContextMenu::show({
    { MenuEntry::Kind::Item, "Cut",       "Ctrl+X", ..., [this]{ doCut(); } },
    { MenuEntry::Kind::Item, "Copy",      "Ctrl+C", ..., [this]{ doCopy(); } },
    { MenuEntry::Kind::Separator },
    { MenuEntry::Kind::Submenu, "Arrange", "", ..., /*children=*/ {
        { MenuEntry::Kind::Item, "Send to arrangement", "", ..., [this]{ sendToArr(); } },
        { MenuEntry::Kind::Item, "Duplicate",           "", ..., [this]{ duplicate(); } },
    }},
    { MenuEntry::Kind::Checkable, "Metronome", "", ..., [this]{ toggleMetronome(); }, /*checked=*/ true },
}, event.screenPos());
```

Framework:
1. Creates a `MenuState` for the root menu (items + geometry).
2. Computes popup rect (default: anchored at `screenPos`, extending
   right+down; if near window edges, shifts left/up).
3. Pushes `OverlayEntry` on Layer 2 with paint + hitTest referencing
   the MenuState.
4. Returns immediately.
5. User interactions modify MenuState and push/pop submenu entries
   as needed.
6. On dismissal (click on action, outside click, Escape), fires the
   selected action's callback (if any) and removes all entries for
   this menu chain.

---

## Positioning

Root menu appears at the provided anchor position (typically the
click-event position). If the menu would extend past the window's
right edge, it shifts left. If it would extend past the bottom, it
shifts up.

For submenus:

- Default: open to the right of the parent row, vertically aligned.
- If that would clip off-screen: open to the left instead.
- Vertical: align with parent row's top, but shift up if child menu
  would extend past window bottom.

---

## Public API

```cpp
struct MenuShowOptions {
    Widget* returnFocusTo = nullptr;
    int maxVisibleItems = 20;
    bool anchorAbove = false;              // for bottom-anchored menus
    float preferredMinWidth = 0.0f;
};

class ContextMenu {
public:
    // Primary entry point — shows a menu at the given screen position.
    static void show(std::vector<MenuEntry> entries, Point screenPos,
                      const MenuShowOptions& opts = {});

    // Attach-to-widget helper — shows the menu anchored at the widget
    // (convenient for MenuBar flyouts).
    static void showAtWidget(Widget* anchor, std::vector<MenuEntry> entries,
                              const MenuShowOptions& opts = {});

    static void close();
    static bool isOpen();
    static int  openDepth();   // 0 = closed, 1 = root only, 2+ = submenus
};
```

`MenuEntry` is constructable via helper functions for readability:

```cpp
namespace Menu {
    MenuEntry item(std::string label, std::function<void()> action,
                    std::string shortcut = "", GLuint icon = 0);
    MenuEntry submenu(std::string label, std::vector<MenuEntry> children);
    MenuEntry separator();
    MenuEntry header(std::string label);
    MenuEntry checkable(std::string label, bool checked,
                         std::function<void()> action);
    MenuEntry radio(std::string group, std::string label, bool checked,
                     std::function<void()> action);
    MenuEntry disabled(MenuEntry base);   // marks a pre-made entry as disabled
}
```

Callers write:

```cpp
ContextMenu::show({
    Menu::item("Cut",  [&]{ doCut();  }, "Ctrl+X"),
    Menu::item("Copy", [&]{ doCopy(); }, "Ctrl+C"),
    Menu::separator(),
    Menu::submenu("Arrange", {
        Menu::item("Duplicate", [&]{ dup(); }, "Ctrl+D"),
        Menu::item("Reverse",   [&]{ rev(); }),
    }),
    Menu::checkable("Metronome", metronome.enabled(),
                     [&]{ metronome.toggle(); }),
}, screenPos);
```

---

## Layout contract

Menu body size is computed at show time (like Tooltip). No widget-
tree layout involvement.

For a menu with N items:

```cpp
float maxLabelW = max over items of font.textWidth(item.label);
float maxShortcutW = max over items of font.textWidth(item.shortcut);
float iconCol = theme().metrics.controlHeight;
float checkCol = theme().metrics.controlHeight * 0.7f;
float arrowCol = font.textWidth("▶", scale);
float gap = theme().metrics.baseUnit;

float menuW = iconCol + checkCol + maxLabelW + 2 * gap + maxShortcutW + arrowCol + 2 * gap;
menuW = max(menuW, opts.preferredMinWidth);

float itemH = theme().metrics.controlHeight;
float separatorH = 8;
float headerH = itemH;
float menuH = sum of each item's height;
```

If `menuH > maxVisibleItems × itemH`, the menu gets a vertical scroll
— FwScrollBar embedded on the right side of the menu body. Scroll
keeps the highlighted item in view via `scrollIntoView`.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.elevated` | Menu body fill |
| `palette.border` | Separator line |
| `palette.accent.withAlpha(80)` | Hovered / kb-highlighted row |
| `palette.dropShadow` | Body shadow |
| `palette.textPrimary` | Item label, enabled |
| `palette.textDim` | Disabled label |
| `palette.textSecondary` | Shortcut text, header text, arrow glyph |
| `palette.accent` | Checkmark / radio dot |
| `metrics.controlHeight` | Row height |
| `metrics.cornerRadius` | Body corners |
| `metrics.baseUnit` | Row internal padding |
| `metrics.fontSize` / `fontSizeSmall` | Label / shortcut |

---

## Events fired

- Per-entry `onClick` — fires when user commits to that entry.
- No menu-level `onClose` or `onOpen` today (callers haven't
  needed them). Add if requested.

---

## Invalidation triggers

Menu body is recomputed at show time — no widget-tree measure
cache. Once shown, paint invalidates on:

- Row highlight change (mouse hover, kb navigation)
- Submenu open/close
- Scroll offset change (for scrolled menus)

All paint-only; no measure work during the menu's lifetime unless
the menu re-opens with a different entry list.

---

## Focus behavior

- `returnFocusTo` option tracks the widget that held focus before
  menu opened; focus returns there when the menu closes.
- While menu is open, menu owns keyboard focus (LayerStack forwards
  keyboard to the top menu entry).

---

## Accessibility (reserved)

- Menu body role: `menu`.
- Items: `menuitem`, `menuitemcheckbox`, `menuitemradio`.
- `aria-expanded` on Submenu rows.
- `aria-keyshortcuts` reflects the `shortcut` string.

---

## Animation

- **Menu fade-in**: 80 ms.
- **Menu fade-out**: 120 ms.
- **Submenu open**: 80 ms fade-in (independent of parent).
- **Highlight transitions**: instant (no fade).
- **Scroll-into-view**: 150 ms smooth scroll when keyboard navigation
  jumps to an off-screen row.

---

## Test surface

Unit tests in `tests/test_fw2_ContextMenu.cpp`:

### Basic lifecycle

1. `ShowOpensOverlay` — `ContextMenu::show({...}, pos)` pushes an
   OverlayEntry on Layer 2.
2. `OutsideClickDismisses` — click on main layer outside menu
   bounds dismisses without firing any callback.
3. `ClickItemFiresAndDismisses` — click on an Item fires its
   `onClick` and closes.
4. `EscapeDismisses` — Escape closes the menu, fires no callback.
5. `CloseAPI` — `ContextMenu::close()` dismisses programmatically.

### Items & rendering

6. `SeparatorNotHighlightable` — hovering a separator does not
   produce a highlight row.
7. `HeaderNotHighlightable` — same for header.
8. `DisabledNotHighlightable` — disabled items don't highlight,
   don't fire `onClick`.
9. `ShortcutRightAligned` — shortcut text renders right-aligned.
10. `CheckableIndicator` — Checkable item with checked=true shows
    checkmark; false shows no mark; column width reserved either way.
11. `RadioIndicator` — Radio item with checked shows dot.
12. `IconColumn` — icon drawn in left column; rows without icon
    still align label to icon column.

### Submenus

13. `SubmenuHoverDelayThenOpens` — hover over Submenu row for >200
    ms opens child menu.
14. `SubmenuClickOpensImmediately` — click on Submenu row opens
    child without delay.
15. `SubmenuPositionRight` — child menu to the right of parent by
    default.
16. `SubmenuPositionLeftOnClip` — near window right edge → child
    opens to left.
17. `NavigatingFromSubmenuToParentClosesChild` — moving focus out
    of child closes the child.
18. `RightArrowInSubmenuRowOpens` — arrow-key navigation opens
    child.
19. `LeftArrowInChildReturnsToParent` — closes child, returns
    highlight to parent's submenu row.
20. `TravelConeKeepsSubmenuOpen` — moving mouse from parent row
    toward child menu does not close the child even when briefly
    leaving parent row.

### Keyboard

21. `DownArrowSkipsSeparators` — navigation skips non-interactive
    rows.
22. `TypeAheadJumps` — typing 'C' jumps to first label starting
    with 'C'.
23. `AcceleratorFires` — menu with Ctrl+S entry and user typing
    Ctrl+S fires that entry directly.
24. `HomeEndJumps` — Home to first interactive, End to last.

### Positioning

25. `ClipsToRightEdge` — menu past window right → shifts left.
26. `ClipsToBottom` — menu past window bottom → shifts up.
27. `AnchorAbove` — `anchorAbove=true` in options → menu opens
    upward from anchor point.
28. `MaxVisibleItemsScrolls` — menu with more entries than
    maxVisibleItems embeds scrollbar.

### Firing rules

29. `OnClickOnlyFiresOnce` — clicking an Item fires its callback
    exactly once.
30. `OnClickNotFiredOnClose` — closing without selection doesn't
    fire any callback.
31. `NestedItemsFireOwn` — clicking an item in a submenu fires
    that submenu's item's callback, not the parent's.

### Return focus

32. `ReturnFocusRestoresOnClose` — `returnFocusTo` widget receives
    focus when menu closes.

### Stacking with other overlays

33. `AboveDropdown` — ContextMenu opened over an open DropDown
    draws above it (both on Layer 2, later-pushed entries on top).
34. `BelowTooltip` — Tooltip (Layer 3) draws above context menu
    (Layer 2).

---

## Migration from v1

v1 `ContextMenu` exists in `src/ui/ContextMenu.h` and works via
paintOverlay() — same plumbing as the v1 DropDown. The v1 API is
close to v2's, just without:
- Submenus (v1 is flat).
- Shortcuts (v1 shows label only).
- Check/Radio items (v1 uses label text "✓" manually).
- Accelerator-key handling.
- Travel-cone submenu logic (moot since no submenus).

Call sites use `ContextMenu::show(...)` with a flat vector of entries.
v2 keeps that shape but widens MenuEntry to the tagged-union. Old
flat-list callers still work (Items become the default kind).

**Source-compatible for flat menus; submenus are opt-in.**

---

## Open questions

1. **Menu bar integration.** The application MenuBar (File / Edit /
   …) is basically a horizontal row of triggers that open
   ContextMenus. Is MenuBar a separate widget that uses ContextMenu
   under the hood, or does it share more plumbing? Answer belongs in
   MenuBar spec — but the direction is "MenuBar wraps ContextMenu."

2. **Persistent shortcuts in menu bar.** When the menu bar is
   closed but the user types Ctrl+S, that should fire the
   corresponding File menu's Save item. That's MenuBar's job —
   ContextMenu just surfaces the shortcut display.

3. **Command pattern integration.** Instead of callbacks in
   MenuEntry, we could use a `Command` abstraction where menu
   entries reference commands by ID, and commands are registered
   once centrally (so Undo/Redo can wrap them, accelerators can
   dispatch them, tests can invoke them). Bigger architectural
   question; deferred to an Open Question in the architecture doc.

4. **Icon size in menu.** Currently same as `controlHeight`. Some
   menus want smaller icons. `setIconSize` future extension.

5. **Recent files menu dynamic contents.** Menu is currently passed
   a static `std::vector<MenuEntry>` at show time. For dynamic
   submenus ("Open Recent" fetching live project list), the
   submenu's `children` field could take a callable:
   `std::function<std::vector<MenuEntry>()>`. Deferred — for now,
   callers rebuild the vector before calling `show`.

6. **Animation polish.** Should Menu show slide-in from anchor
   (subtle "growing out" effect)? Matches macOS. Currently spec
   has plain fade. Low priority.
