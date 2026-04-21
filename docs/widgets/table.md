# Table — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**New widget.** Supersedes hand-rolled table-like code in the MIDI
Monitor panel and preset browsers.
**Related:** [scroll_view.md](scroll_view.md) — Table inherits
ScrollView's viewport / offset mechanics, adds **measure
virtualization** via an item-builder callback for thousands of rows.
[grid.md](grid.md) — Table is not Grid; Table is tabular data (rows
× columns with headers, sorting, selection), Grid is 2D layout for
arbitrary children. The distinction is data-vs-layout.

---

## Intent

A **virtualized tabular data grid**: fixed column headers over
scrollable rows, with selection, sort, optional filter, and the
critical performance property of **only building widgets for
visible rows**. This is how we display large datasets (MIDI monitor
event log, preset library, VST3 plugin browser) without creating
thousands of widgets up-front.

Table is the payoff for having spec'd ScrollView carefully —
everything ScrollView does (clip, pan, scrollbar integration) Table
reuses. What Table adds:

- **Row virtualization** — rows are built on demand via a builder
  callback, destroyed when they scroll out of view.
- **Column headers** — sticky at top, with sort indicators, column
  resize handles, optional drag-to-reorder.
- **Selection model** — single, multi, range; keyboard-navigable;
  persisted across scroll.
- **Sort** — per-column, ascending/descending, caller-provided
  comparator.
- **Column types** — text, numeric, checkbox, custom renderer.

## Non-goals

- **Inline cell editing.** Tables display data; edits happen via
  separate edit dialogs or right-click context menus. (In-cell edit
  is a future `FwEditableTable` extension.)
- **Tree / grouping.** Flat rows only. Hierarchical tables
  (file trees, grouped data) are a future `FwTreeTable`.
- **Pivot / aggregation.** Totals, subtotals, pivot transformations
  — not part of the widget; data layer responsibility.
- **Frozen rows (other than header).** Sticky arbitrary rows is a
  future enhancement; v2.0 has sticky header only.
- **Column virtualization.** Rows virtualize; columns are assumed to
  fit in memory and measure. DAW data tables are wide-columned but
  not 1000-columned.

---

## Visual anatomy

```
    ┌──────────┬──────┬──────────┬──────────┐ ← column headers (sticky)
    │ Name  ▲  │ Size │ Modified │ Type     │    ↑ header height
    ├──────────┼──────┼──────────┼──────────┤
    │ Item 1   │ 120  │ 2026-04… │ Audio    │
    │ Item 2   │ 340  │ 2026-04… │ MIDI     │    ↑ row height (fixed or per-row)
    ├──────────┼──────┼──────────┼──────────┤
    │▓Item 3▓▓▓│▓204▓▓│▓2026-04…▓│▓Audio▓▓▓│   ← selected row
    ├──────────┼──────┼──────────┼──────────┤
    │ Item 4   │  58  │ 2026-04… │ Preset   │
    │ ...      │ ...  │ ...      │ ...      │
    └──────────┴──────┴──────────┴──────────┘

    ├─── column resize grip ─┤
```

Parts:
1. **Header row** — sticky at top; always painted regardless of
   scroll position. Divided into columns matching body.
2. **Sort indicator** — ▲ / ▼ glyph next to active sort column's
   label.
3. **Column resize grip** — 4-px draggable zone at each column
   boundary.
4. **Body rows** — scrollable vertically; only visible rows have
   widgets.
5. **Selection highlight** — `accent.withAlpha(80)` fill behind
   selected rows.
6. **Alternating row striping** — optional; off by default.
7. **Vertical scrollbar** — inherited from the embedded ScrollView.

---

## Virtualization model

The key performance concept. Unlike ScrollView where all children
exist as widgets up front, Table holds no row widgets by default.
The caller supplies:

```cpp
table.setRowCount(1000);
table.setRowHeight(24);     // uniform
table.setCellBuilder([this](int row, int col) -> Widget* {
    // Called on-demand for each (row, col) in the visible window.
    // Table retains or destroys the returned widget as the window shifts.
    return makeCellForRow(row, col);
});
```

At render time:
1. Table computes the visible-row window from scroll offset + viewport
   size + row height: `[firstVisibleRow, lastVisibleRow]`, plus a small
   buffer (5 rows above and below) to reduce rebuild flicker during
   fast scroll.
2. Rows outside the window are destroyed (or returned to a pool for
   recycling).
3. Rows inside the window that don't yet have widgets are built via
   `cellBuilder`.
4. Paint and hit-test only see rows in the window.

For a 10,000-row table, the widget tree holds ~30 row widgets at any
time. The `cellBuilder` callback is the caller's only touch point
with row data.

### Variable row heights

```cpp
table.setRowHeight([this](int row) -> float {
    return m_rowHeights[row];   // caller-computed, e.g. varying font sizes
});
```

Variable heights break the O(1) "row N's y = N × height" math. Table
pre-computes a prefix-sum of row heights on `setRowCount` and when
`invalidateRowHeights(rangeStart, rangeEnd)` is called. Prefix sum is
O(N) to build, O(log N) binary search to find the row at a given y
coordinate.

Fixed height is the fast path; variable height is opt-in for callers
that need it (and will pay the O(N) setup cost).

### Widget recycling

Table keeps a small pool of destroyed widgets per column, reusable
when new rows enter the window. The builder is replaced with a
reset-on-reuse callback:

```cpp
table.setCellBuilder([this](Widget* recycledOrNull, int row, int col) -> Widget* {
    auto* cell = recycledOrNull ? recycledOrNull : new FwLabel();
    static_cast<FwLabel*>(cell)->setText(m_rows[row].values[col]);
    return cell;
});
```

Recycling eliminates widget-creation garbage during fast scroll. The
builder may return the same widget with updated state, or a new
widget if the recycled one is wrong type for this row.

---

## Columns

Each column is defined by a `ColumnSpec`:

```cpp
enum class ColumnAlign { Left, Center, Right };
enum class ColumnSortMode { None, Ascending, Descending };

struct ColumnSpec {
    std::string    label;
    TrackSize      width = TrackSize::fr(1);   // Fixed / Fraction / Auto / MinContent / MaxContent
    ColumnAlign    align = ColumnAlign::Left;
    bool           sortable = false;
    bool           resizable = true;
    bool           draggableReorder = false;    // v2.0: false; deferred
    float          minWidth = 40.0f;
    float          maxWidth = 9999.0f;
    std::optional<std::function<int(int a, int b)>> sortComparator;
    // Comparator receives row indices and returns <0/0/>0 (std::sort convention).
};
```

Columns are defined once; the list can be mutated (add/remove/reorder)
but not typically per-frame.

---

## Selection model

```cpp
enum class SelectionMode {
    None,          // selection disabled
    Single,        // one row at a time
    Multi,         // Ctrl+click adds/removes; no range semantics
    Range,         // Shift+click selects range; Ctrl+click toggles individual; full desktop pattern
};
```

Selection state is a `std::vector<int>` of selected row indices,
maintained by Table. Accessible via `selectedRows()`.

Selection persists through scroll — rebuilding row widgets at new
scroll positions doesn't lose selection (state lives in Table, not in
row widgets).

### Selection events

| Event | Trigger |
|---|---|
| `onSelectionChanged(std::vector<int>)` | Any change to selection set |
| `onRowActivated(int row)` | Double-click or Enter on a row — the "commit" gesture |
| `onRowRightClick(int row, Point screen)` | Context menu trigger |

---

## Public API

```cpp
class Table : public Widget {
public:
    Table();

    // Data source
    void setRowCount(int n);                        // total, including off-screen
    int  rowCount() const;
    void invalidateRow(int row);                     // caller says "row N's data changed"
    void invalidateAllRows();

    // Cell rendering
    using CellBuilder = std::function<Widget*(Widget* recycled, int row, int col)>;
    void setCellBuilder(CellBuilder);

    // Row sizing
    void setRowHeight(float px);                     // uniform
    void setRowHeight(std::function<float(int row)>); // per-row; rebuilds prefix-sum
    void invalidateRowHeights(int from, int to);

    // Columns
    void setColumns(std::vector<ColumnSpec>);
    void addColumn(ColumnSpec);
    void removeColumn(int idx);
    const std::vector<ColumnSpec>& columns() const;
    void setColumnWidth(int idx, float px);          // override track resolved size
    float columnWidth(int idx) const;                // current resolved width
    void setHeaderHeight(float px);
    void setShowHeader(bool);

    // Sorting
    void setSortColumn(int col, ColumnSortMode mode);
    int  sortColumn() const;
    ColumnSortMode sortMode() const;

    // Selection
    void setSelectionMode(SelectionMode);
    std::vector<int> selectedRows() const;
    void setSelectedRows(std::vector<int>);
    void clearSelection();

    // Scroll-to-row helpers
    void scrollRowIntoView(int row, float margin = 0.0f);
    void scrollToTop();
    void scrollToBottom();

    // Appearance
    void setAlternateRowStriping(bool);              // default false
    void setHeaderFill(Color c);
    void setRowDividerColor(Color c);
    void setRowDividerWidth(float);                  // default 1 px
    void setColumnDividerColor(Color c);
    void setShowColumnDividers(bool);                // default true

    // Callbacks
    void setOnSelectionChanged(std::function<void(std::vector<int>)>);
    void setOnRowActivated(std::function<void(int row)>);
    void setOnRowRightClick(std::function<void(int row, Point screen)>);
    void setOnSortChanged(std::function<void(int col, ColumnSortMode)>);
    void setOnColumnResized(std::function<void(int col, float newWidth)>);

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

---

## Gestures

### Pointer — left button on a row

| Gesture | Result |
|---|---|
| **Click** | Selection change per mode: Single sets current; Multi toggles; Range sets current clearing others. |
| **Shift+Click** (Range mode) | Select range from anchor to clicked row. |
| **Ctrl+Click** (Multi/Range) | Toggle clicked row's membership. |
| **Double-click** | Fires `onRowActivated(row)`. |
| **Drag** on row | If `setRowDragEnabled(true)`: begins drag-and-drop (future). Currently: no-op. |

### Pointer — left button on a header

| Gesture | Result |
|---|---|
| **Click** | If sortable: cycle sort None → Asc → Desc → None. Fires `onSortChanged`. |
| **Drag** on resize grip | Adjust column width live; fires `onColumnResized` on release. |
| **Double-click** on resize grip | Auto-size column to widest cell (measure all currently-built row cells; heuristic). |

### Pointer — right button

| Gesture | Result |
|---|---|
| **Right-click on row** | Selects the row (if not already selected) and fires `onRowRightClick` with screen position for context menu. |
| **Right-click on header** | Fires header-right-click for future "show columns" menu. |

### Pointer — scroll wheel

Delegated to embedded ScrollView; scrolls body rows.

### Keyboard (when Table has focus)

| Key | Action |
|---|---|
| `Up` / `Down` | Move focused row ±1, auto-scroll into view. |
| `PgUp` / `PgDn` | Move by page (viewport rows). |
| `Home` / `End` | Move to first / last row. |
| `Space` | Select focused row (Single) or toggle (Multi/Range). |
| `Enter` | Fire `onRowActivated` for focused row. |
| `Shift+Up/Down` | (Range mode) Extend selection. |
| `Ctrl+A` | (Multi/Range mode) Select all rows. |
| `Escape` | Clear selection. |
| `Tab` / `Shift+Tab` | Leave table; focus next/prev focusable. |

Focused row is a separate concept from selected rows — the focused
row is the keyboard "cursor" position. Space / Enter act on the
focused row.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Table's own measure is simple — it takes whatever constraints the
parent provides. Internal viewport calculations happen in layout.

```cpp
Size Table::onMeasure(Constraints c, UIContext& ctx) {
    // Table is typically flex-sized inside a panel. No intrinsic size.
    float w = c.hasBoundedWidth()  ? c.maxW : 400.0f;
    float h = c.hasBoundedHeight() ? c.maxH : 300.0f;
    return c.constrain({w, h});
}
```

### `onLayout(Rect bounds, UIContext& ctx)`

1. Reserve header row at top.
2. Compute column widths:
   - Gather `ColumnSpec.width` entries (TrackSize values).
   - Run track sizing (same algorithm as Grid's horizontal pass, with
     minWidth / maxWidth per-column).
3. Compute total content height = `rowCount × rowHeight` (uniform)
   or `prefixSum[rowCount]` (variable).
4. Sync to embedded ScrollView:
   - Viewport size = bounds minus header.
   - Content size = (sum of column widths, total content height).
   - Current scroll offset determines visible row window.
5. Compute `[firstVisible, lastVisible]` row indices:
   - Uniform: `firstVisible = floor(scrollY / rowHeight)`.
   - Variable: binary search in prefix-sum.
   - Expand by buffer (5 rows each side, clamped to valid range).
6. Rebuild visible-row widget pool:
   - Destroy widgets for rows that are now outside the window.
   - Call `cellBuilder` for new rows in the window, possibly passing
     recycled widgets from the pool.
7. Lay out each visible row's cells into column rects at
   `y = rowTop(row) - scrollY + headerHeight`.
8. Paint order: header on top of body (so scroll never overlaps it).

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

### Relayout boundary

**Yes, automatically.** Content changes don't affect Table's own
size — only internal viewport state.

### Caching

Measure cached on `(constraints)`. Layout depends on many things
(scroll offset, column widths, row count, row heights, etc.); layout
cache key is large but still cheap to compare. Scroll offset change
triggers only the visible-window-rebuild, not full re-layout.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.panelBg` | Table body background |
| `palette.surface` | Header background |
| `palette.textPrimary` | Header text, cell text |
| `palette.textSecondary` | Sort indicator glyph |
| `palette.border` | Row dividers, column dividers |
| `palette.accent.withAlpha(80)` | Selected row highlight |
| `palette.accent.withAlpha(40)` | Focused row highlight (when distinct from selected) |
| `palette.controlHover` | Header hover |
| `metrics.controlHeight` | Default header height |
| `metrics.baseUnit` | Cell padding |
| `metrics.fontSize` | Cell text |
| `metrics.fontSizeSmall` | Sort indicator |

---

## Events fired

Listed above in Selection model + Gestures.

---

## Invalidation triggers

### Measure-invalidating (own measure cache)

- `setColumns`, `addColumn`, `removeColumn`
- `setHeaderHeight`, `setShowHeader`
- DPI / theme / font (global)

### Layout-invalidating (rebuild visible window)

- `setRowCount`
- `setRowHeight` (fixed or variable)
- `invalidateRowHeights`
- `setColumnWidth` (manual override)
- Column auto-resize triggered by header double-click
- Scroll offset change

### Paint-only

- Selection changes
- Focused row changes
- Sort column / direction
- Hover / press / focus state on rows or headers

### Row-data-only

- `invalidateRow(row)` — rebuilds that row's widget (if visible) via
  the builder callback with its recycled widget. Doesn't touch other
  rows, doesn't change layout.

---

## Focus behavior

- **Table is tab-focusable** when `selectionMode != None`.
- **Focus indicator** for the currently-focused row (distinct from
  selected rows).
- **Focus-follow** auto-scrolls focused row into view.
- **Row widgets themselves are not individually tab-focusable.**
  Tab steps into Table (focuses the table), then arrows navigate
  rows. Shift+Tab exits the table. Row widgets are essentially
  "passive" for keyboard; their mouse interactions still work.

Rationale: table rows can hold interactive widgets (checkboxes for
select-all, action buttons). Those widgets ARE individually focusable
via mouse click → focus on the widget. Tab from a row's checkbox
moves to the next focusable within the same row, then to the next
row's focusables, etc.

---

## Accessibility (reserved)

- Role: `grid` or `table` (ARIA).
- `aria-rowcount`, `aria-colcount` reflected.
- `aria-selected` on selected rows.
- `aria-sort` on the sort column.
- `aria-rowindex` on each visible row.

---

## Animation

- **Row highlight transitions**: 80 ms on hover, instant on select.
- **Smooth scroll to row**: inherits from ScrollView.
- **Sort re-order**: currently instant. A future enhancement could
  animate row positions as the sort takes effect (visual clue that
  data re-sorted); complex with virtualization because rows move in
  and out of visibility.

---

## Test surface

Unit tests in `tests/test_fw2_Table.cpp`:

### Virtualization

1. `SmallTableAllVisible` — 10 rows, viewport fits all → all 10
   built.
2. `LargeTableBuildsOnlyVisible` — 10,000 rows + 300px viewport +
   24px row height → ~13 rows built (visible + buffer).
3. `ScrollBuildsNewRowsDestroyedOld` — scroll by 100 rows → 100 rows
   destroyed, 100 new rows built (or recycled).
4. `RecyclingReusesWidgets` — same widget instance appears at
   different row indices over a scroll sequence.
5. `BufferPreventsFlicker` — small scroll within buffer range does
   not rebuild any widgets (already buffered).

### Row heights

6. `UniformHeightO1Lookup` — row-at-y computation is O(1) for
   uniform height.
7. `VariableHeightPrefixSum` — variable height builds prefix-sum on
   setRowCount.
8. `InvalidateRowHeightsRebuilds` — `invalidateRowHeights(10, 20)`
   rebuilds prefix-sum from row 10 onward.

### Columns

9. `FixedColumnWidths` — ColumnSpec widths `[100, 200, 50]` →
   column widths 100/200/50.
10. `FractionalColumnWidths` — fr columns share available space.
11. `ColumnResizeViaGrip` — drag column boundary changes that
    column's width, fires onColumnResized.
12. `ColumnResizeRespectsMinMax` — resize clamped to
    minWidth/maxWidth.
13. `ColumnAutoSizeOnDoubleClick` — double-click column grip sizes
    to widest visible cell content.

### Sorting

14. `ClickSortableHeaderCyclesMode` — click: None→Asc→Desc→None.
15. `ClickNonSortableHeaderNoop` — sortable=false → no cycle.
16. `SortComparatorCalled` — provided comparator invoked for row
    comparisons.
17. `SortEmitsOnSortChanged` — sort change fires callback.

### Selection

18. `SingleSelectionOnClick` — click sets single selected row.
19. `MultiSelectCtrlClickToggles` — Ctrl+click toggles membership.
20. `RangeSelectShiftClick` — Shift+click extends selection from
    anchor.
21. `CtrlASelectsAll` — in Multi/Range mode.
22. `EscapeClearsSelection`.
23. `SelectionSurvivesScroll` — selection preserved when rows scroll
    out of view and back.
24. `ClearSelectionOnRowCountShrink` — selection reduced to valid
    row indices when setRowCount shrinks below some selected rows.

### Keyboard

25. `ArrowDownMovesFocusedRow` — focused row +1, auto-scrolls.
26. `PgDnMovesByPage` — focused jumps by viewport-rows-count.
27. `HomeEndJumps` — focused to 0 / rowCount-1.
28. `EnterFiresActivated` — Enter on focused row fires callback.
29. `SpaceSelectsFocused`.

### Row activation

30. `DoubleClickFiresActivated` — double-click fires `onRowActivated`.
31. `RightClickFiresContextMenu` — right-click on row fires
    onRowRightClick with position; also selects that row.

### Culling + paint

32. `RowsOutsideViewportNotPainted` — scrolled-out rows are not
    painted.
33. `StickyHeaderAlwaysPainted` — header paints at top regardless
    of scroll offset.

### Cache + invalidation

34. `InvalidateRowRebuildsOne` — invalidateRow(5) rebuilds only
    that row's cells; other rows unaffected.
35. `InvalidateAllRowsRebuilds` — everything rebuilt.
36. `RowCountChangeAdjustsScrollbar` — shrinking row count below
    current scroll offset clamps offset.

---

## Migration from v1

v1 has no Table. Ad-hoc tabular UIs exist in:

- MIDI Monitor (BrowserPanel) — custom-painted list with fixed
  layout.
- Preset browser — custom-painted rows with click-to-load.
- VST3 plugin list in the Instruments / Effects picker — custom-
  painted rows.

Each migrates to Table with an appropriate cellBuilder. Estimated
code reduction: 600–1000 lines across these panels once Table
subsumes the custom render code.

---

## Open questions

1. **Frozen column(s)?** Horizontal scroll with a sticky "Name"
   column on the left. Useful for wide tables. Frozen is structurally
   similar to the sticky header (don't scroll that column with body
   scroll); add `setFrozenColumns(int n)` when real use case emerges.

2. **Column reorder by drag?** `draggableReorder = true` per column
   spec reserved in the API. Spec left vague on v2.0 — implement
   when UX mockup exists.

3. **Row expansion / details?** "Click to expand a row showing
   additional info below" pattern. Currently out of scope; call sites
   can fake it with per-row cellBuilder returning a container that
   sometimes renders tall.

4. **Filter / search?** Not in Table's responsibility. Table owns
   display of a given row list; caller maintains the filtered row
   list and calls setRowCount on change.

5. **Column types & editing?** v2.0 all cells are custom widgets via
   cellBuilder. A future `ColumnType::Text` / `Numeric` / `Checkbox`
   could reduce builder boilerplate, but premature. Wait for real
   patterns.

6. **Virtualization breaks mid-scroll when setRowCount changes?**
   Currently we clamp scroll offset to new valid range and rebuild.
   Visual effect: items may seem to "jump" if the delta is large.
   Acceptable; alternative (preserve anchor row across count change)
   adds complexity.

7. **Touch selection?** Long-press on a row enters multi-select
   mode (mobile / iPad convention). Out of scope until touch support
   is designed.

8. **Lazy loading past the end?** Tables with "infinite scroll"
   where the caller appends rows as the user nears the bottom.
   Exposed via `setOnNearBottom(threshold, callback)` — callback
   fires when scroll is within `threshold` rows of the end.
   Callers append rows + call setRowCount. Future enhancement; not
   v2.0.

9. **Column grouping (multi-row headers)?** `ColumnGroup` spanning
   multiple columns with a top-level label. Not v2.0; compose with
   a header Stack if needed.
