# TabView — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**New widget.**
**Related:** [split_view.md](split_view.md) — alternative pattern for
showing multiple content regions; TabView shows one at a time,
SplitView shows all simultaneously. [flex_box.md](flex_box.md) —
used internally for the tab strip layout.

---

## Intent

A **tab strip + content area** where one tab is visible at a time.
Classic "category switcher" UI: Preferences dialog's Audio / MIDI /
Defaults / Metronome sections, Browser panel's Files / Library / MIDI
Monitor / Clips, arrangement view's Details / Automation / Clip
properties panels.

TabView encapsulates the "only one visible, switch via click" pattern
with consistent keyboard, visual affordance, and optional dynamic
tab management (add / remove / reorder at runtime for multi-document
interfaces).

## Non-goals

- **Vertical tab strip on the side.** Future variant; v2.0 is
  horizontal top-tabs only.
- **Tab-within-tab nesting.** Supported trivially by nesting TabView
  widgets; no special spec.
- **Moveable / detachable tabs.** Drag-tab-to-new-window is out of
  scope.
- **Tab overflow menu for too-many-tabs.** Future: when tab strip
  exceeds width, surface "More ▾" menu. For now, strip scrolls
  horizontally when overflowing.
- **Lazy content building.** All tab contents exist as widgets; only
  the active one is painted and hit-tested. Lazy content (don't
  build tab N until it's activated) deferred — use Tab's
  `onActivated` callback to lazy-init in callers that care.

---

## Visual anatomy

```
    ┌──────┬──────┬───────┬──────────────────────┐
    │ Audio│ MIDI │ Defs  │                       │
    │      │ ────│       │                       │   ← inactive tabs (subdued)
    │  ☓  │  ☓  │  ☓   │                       │     ↑ close buttons (optional)
    ├──────┴──────┴───────┤                       │   ← bottom border under tabs
    │▓ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ ▓│                       │   ← active-tab indicator
    │                                              │
    │   content area (only active tab's pane)    │
    │                                              │
    └──────────────────────────────────────────────┘
```

Parts:
1. **Tab strip** — horizontal row of tab buttons at top.
2. **Tab button** — each displays label, optional icon, optional
   close button (×). Active tab is visually distinguished.
3. **Active-tab indicator** — 2 px accent-colored bar under the
   active tab, or equivalent visual (underline, top-highlight).
4. **Content area** — below the strip; shows the active tab's pane.

### Tab button anatomy

```
    ┌──────────────────────────┐
    │  [icon]  Label text   ☓  │   ← icon | label | close-X (if enabled)
    └──────────────────────────┘
```

Inactive tabs use `textSecondary` for label. Active tab uses
`textPrimary` + accent-color underline. Hover state lightens the tab
background; pressed state darkens.

---

## Behavior

### Tabs as data

Tabs are defined as a list of `TabSpec`:

```cpp
struct TabSpec {
    std::string id;                 // stable ID (for persistence)
    std::string label;
    std::optional<GLuint> iconTex;
    Widget*    content = nullptr;   // the pane widget for this tab
    bool       closeable = false;   // show × button?
    bool       enabled = true;
};
```

Adding/removing tabs happens via dedicated methods so TabView can
update the strip and keep selection sensible.

### Active tab

Exactly one tab is active at any time (unless the list is empty).
When tabs are added, the first becomes active. When the active tab
is removed, the next one (or previous if was last) becomes active.

### Content lifetime

All tab content widgets are long-lived — kept in memory even when
their tab isn't active. Only the active tab is **painted** and
**hit-tested**; inactive tabs' widgets still exist and can be
measured (for keyboard tab-order purposes, though we skip them
anyway).

Rationale: switching between tabs should be instant. Re-building a
complex tab's content on every activation would be noticeable lag.
For memory-heavy tabs, caller can listen to `onTabActivated` and
lazy-build content the first time.

---

## Public API

```cpp
class TabView : public Widget {
public:
    using IdCallback = std::function<void(const std::string& tabId)>;

    TabView();

    // Tabs
    void addTab(TabSpec spec);
    void removeTab(const std::string& id);
    void clearTabs();
    int  tabCount() const;
    const TabSpec& tabAt(int idx) const;

    // Mutate existing tab
    void setTabLabel(const std::string& id, std::string label);
    void setTabIcon(const std::string& id, GLuint tex);
    void setTabEnabled(const std::string& id, bool enabled);
    void setTabCloseable(const std::string& id, bool closeable);

    // Active tab
    void setActiveTab(const std::string& id, ValueChangeSource = ValueChangeSource::Programmatic);
    const std::string& activeTabId() const;
    int activeTabIndex() const;
    void selectNextTab();
    void selectPrevTab();

    // Tab strip configuration
    void setTabMinWidth(float px);
    void setTabPreferredWidth(float px);          // 0 = content-driven
    void setTabMaxWidth(float px);
    void setTabStripHeight(float px);             // default metrics.controlHeight + 4
    void setShowActiveIndicator(bool);            // default true

    // Behavior
    void setAllowKeyboardNav(bool);               // default true (Ctrl+Tab, Ctrl+Shift+Tab)
    void setScrollStripOnOverflow(bool);          // default true

    // Callbacks
    void setOnActivated(IdCallback);              // fires on active-tab change
    void setOnTabCloseRequested(IdCallback);
    // Caller decides whether to honor the close request. To actually
    // close, caller calls removeTab(id) after any confirmation logic.

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### `onActivated` firing rules

- Fires every time `activeTabId()` changes, regardless of trigger
  (user or programmatic).
- Does NOT fire for `setActiveTab(same)` no-op.
- Fires once on initial addTab that creates the first tab (activation
  by virtue of being the only tab).
- Fires once when tab removal cascades activation to another tab.

---

## Gestures

### Tab strip

| Gesture | Result |
|---|---|
| **Click on tab** | Activates that tab. |
| **Click on tab's × button** | Fires `onTabCloseRequested(id)`. Does NOT remove the tab automatically. |
| **Middle-click on closeable tab** | Same as × click (UX convention). |
| **Right-click on tab** | Fires `onTabRightClick(id, screenPos)` for context menu (close others, close to right, etc.). |
| **Drag on tab** | Reserved for future tab-reorder. v2.0: no-op. |
| **Scroll wheel over strip (when overflowing)** | Horizontal scroll of strip. |

### Keyboard

| Key | Result |
|---|---|
| `Ctrl + Tab` | Activate next tab (cycles). |
| `Ctrl + Shift + Tab` | Activate previous tab. |
| `Ctrl + 1..9` | Activate tab at index 0–8. |
| `Ctrl + W` | Fire `onTabCloseRequested` for active tab if closeable. |
| `Left` / `Right` (when tab strip focused) | Move keyboard highlight ±1 through strip; Enter activates. |

Ctrl+Tab is global while TabView has focus (or a descendant does);
the TabView participates in focus chain so "Ctrl+Tab to switch
settings category" works without needing to click the strip first.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size TabView::onMeasure(Constraints c, UIContext& ctx) {
    // Strip height is fixed.
    float stripH = m_tabStripHeight > 0 ? m_tabStripHeight
                                        : theme().metrics.controlHeight + 4;
    // Content height = whatever the parent gives minus strip.
    float contentH = (c.hasBoundedHeight() ? c.maxH : 400.0f) - stripH;
    float w = c.hasBoundedWidth() ? c.maxW : 400.0f;
    return c.constrain({w, stripH + contentH});
}
```

### `onLayout(Rect bounds, UIContext& ctx)`

1. Lay out tab strip along top: `stripRect = {bounds.x, bounds.y,
   bounds.w, stripHeight}`.
2. Inside strip, compute each tab's width:
   - Content-driven if `tabPreferredWidth == 0`: tab width = icon +
     label + close-button + padding.
   - Clamped by `tabMinWidth` / `tabMaxWidth`.
3. If sum of tab widths > stripW and `scrollStripOnOverflow` is
   true: strip scrolls horizontally; user can scroll via wheel or
   overflow indicator ("›" at right edge). v2.0: horizontal scroll
   but no auto-scroll-to-active-tab (keyboard nav adjusts scroll so
   active tab is visible).
4. Lay out active tab's content widget in `contentRect = {bounds.x,
   bounds.y + stripH, bounds.w, bounds.h - stripH}`.
5. Inactive tab content widgets: laid out with size 0 (not measured
   for layout purposes) — their own bounds are updated so global-
   coords remain sane.

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

### Relayout boundary

**Yes, automatically** — bounds are fully parent-driven; active-tab
content changes don't affect TabView's own size.

### Caching

Measure cache: `(constraints, stripHeight)`. Trivial.

Layout cache key: `(bounds, tab list version, active tab id, strip
scroll offset)`. Adding / removing tabs invalidates layout;
activating a tab invalidates layout (different content laid out +
indicator moves).

---

## Paint order

1. Content area (active tab's content).
2. Tab strip background (opaque fill so it's visually a "container").
3. Tab buttons (inactive first, active on top for the indicator to
   overlap edge cleanly).
4. Active-tab indicator bar.

Inactive tab contents are NOT painted.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.panelBg` | Strip background, content area background |
| `palette.surface` | Active tab background (subtly raised) |
| `palette.controlBg` | Inactive tab background |
| `palette.controlHover` | Inactive tab hover |
| `palette.accent` | Active-tab indicator bar |
| `palette.textPrimary` | Active tab label |
| `palette.textSecondary` | Inactive tab label, close × |
| `palette.textDim` | Disabled tab label |
| `palette.border` | Tab-strip bottom border |
| `metrics.controlHeight` | Tab height |
| `metrics.cornerRadius` | Tab corners (top-only for top-anchored tabs) |
| `metrics.baseUnit` | Internal tab padding, icon-label gap |

---

## Events fired

- `onActivated(id)` — active tab changed.
- `onTabCloseRequested(id)` — user clicked × or Ctrl+W.
- `onTabRightClick(id, screenPos)` — for context menus.
- FocusEvent — tab strip or content children gain/lose focus.

---

## Invalidation triggers

### Measure-invalidating

- `setTabStripHeight`
- DPI / theme / font (global)

### Layout-invalidating

- `addTab`, `removeTab`, `clearTabs`
- `setTabLabel`, `setTabIcon` (tab width changes)
- `setActiveTab`
- `setTabPreferredWidth`, `setTabMinWidth`, `setTabMaxWidth`
- Active content child's measure invalidation (bubbles normally)

### Paint-only

- Hover / press / focus transitions on tab buttons
- Strip scroll offset
- `setShowActiveIndicator`
- Disabled / enabled tab state (only paint, since tab width is stable)

---

## Focus behavior

- **Tab strip is a focus group:** Tab enters the strip, then Left /
  Right move through tabs.
- **Tab from strip enters content:** after the last tab in the strip,
  Tab dives into the active tab's content.
- **Tab out of content:** moves to next focusable after TabView.
- **Ctrl+Tab** is tree-wide — works when focus is anywhere inside
  the active tab's content subtree.

---

## Accessibility (reserved)

- Role: `tablist` on the strip, `tab` on each button,
  `tabpanel` on the content area.
- `aria-selected` on active tab, others false.
- `aria-controls` linking each tab to its content panel.
- Keyboard roving-tab-index pattern (like RadioGroup): only one tab
  is in the document tab order at a time.

---

## Animation

- **Tab hover fade**: 80 ms.
- **Active-tab switch**: indicator bar slides to new position over
  150 ms (smooth accent trail). Content swaps instantly (no fade —
  feels slow if animated).
- **Tab close**: if configured via `setAnimateRemoval(true)`, tab
  shrinks to 0 width over 150 ms before being removed from the strip.
  Default off; simpler is instant.

---

## Test surface

Unit tests in `tests/test_fw2_TabView.cpp`:

### Basic

1. `EmptyTabView` — no tabs: only strip (empty) painted; no content
   area consumer.
2. `AddFirstTabBecomesActive` — addTab with no prior tabs → that
   tab is active.
3. `AddSecondTabFirstStaysActive` — adding doesn't change active.
4. `RemoveActiveCascades` — removing the active tab activates the
   next (or previous if last removed).
5. `RemoveAllTabsNoActive` — clearTabs → activeTabId() returns "".

### Activation

6. `SetActiveTabById` — setActiveTab("audio") updates active.
7. `SetActiveNonExistentNoop` — invalid ID is ignored.
8. `SetActiveSameNoCallback` — activating current tab doesn't fire
   onActivated.
9. `OnActivatedFires` — real change fires callback with new id.

### Tab strip

10. `ContentDrivenWidth` — tab width = icon + label + padding.
11. `MinWidthEnforced` — short-labeled tabs padded to minWidth.
12. `MaxWidthTruncates` — long labels truncated with ellipsis
    (via embedded Label) to fit maxWidth.
13. `StripScrollsOnOverflow` — many tabs: strip overflow scrollable.
14. `ActiveIndicatorMoves` — indicator bar positions at active tab.

### Content swap

15. `OnlyActivePainted` — paint only invokes active tab's content's
    render().
16. `OnlyActiveHitTested` — clicks within content area route to
    active tab's content, not inactive tabs.
17. `InactiveContentKeptAlive` — widget objects persist across
    switches (test via address equality).

### Keyboard

18. `CtrlTabCyclesNext` — ctrl+tab activates next.
19. `CtrlShiftTabCyclesPrev` — ctrl+shift+tab activates previous.
20. `CtrlNumActivatesByIndex` — ctrl+3 activates 4th tab.
21. `CtrlWFiresCloseRequest` — ctrl+w on closeable active tab fires
    callback.
22. `ArrowInStripMovesHighlight` — focused strip, right arrow
    highlights next tab; Enter activates.

### Close behavior

23. `CloseXFiresRequestNotRemove` — clicking × fires
    onTabCloseRequested; does NOT remove.
24. `MiddleClickAlsoRequests` — middle click on closeable tab fires
    request.
25. `RemoveTabAfterRequest` — caller calling removeTab(id) in
    response actually removes.

### Disabled tabs

26. `DisabledTabIgnoresClick` — clicking disabled tab no-op.
27. `CtrlTabSkipsDisabled` — keyboard nav skips disabled tabs.

### Firing

28. `OnActivatedOnInitialAddOnce` — first tab added fires once.
29. `OnActivatedOnRemovalCascade` — removing active fires once for
    new active.

### Cache

30. `AddTabInvalidatesLayout` — new tab triggers re-layout.
31. `ActiveChangeInvalidatesLayoutNotMeasure` — activation is layout-
    only (strip width unchanged unless tab widths change, which they
    don't on activation alone).
32. `RelayoutBoundary` — tab content's measure invalidation doesn't
    propagate above TabView.

---

## Migration from v1

v1 has no dedicated TabView. The "Preferences dialog with categories"
pattern is hand-rolled with a vertical button list + switch/case in
paint. The "Browser panel tabs" uses a custom tab implementation
specific to that panel.

Migration:
1. Wrap existing tab-ish UIs in TabView.
2. Existing category-button panels (preferences) become TabView with
   Switch-style button strip.
3. No breaking changes; v2 is additive.

---

## Open questions

1. **Vertical tab strip?** Preferences dialogs often prefer vertical
   (left-side) tab lists for readability when labels are long. Add
   `setOrientation(TabOrientation::Vertical)` in a future revision.

2. **Tab reordering by drag?** Useful for multi-document interfaces.
   Requires drag-and-drop framework; deferred.

3. **Detachable tabs?** "Drag tab to new window" is significant
   feature. Separate widget (FwDetachableTabView) when needed.

4. **Lazy content?** Some callers want "don't build the content
   until the user switches to this tab." Currently callers handle
   via `onActivated` + their own first-run flag. If this becomes
   common, add `setLazyContent(id, builder)` convenience.

5. **Tab overflow menu?** "More ▾" dropdown listing hidden tabs
   when strip can't scroll. Add when tab counts commonly exceed
   strip width.

6. **Dirty-state indicator?** Tab displaying an unsaved-document dot
   next to the label. `setTabDirty(id, bool)` + paint indicator.
   Useful for IDE-style UIs; niche for DAWs.
