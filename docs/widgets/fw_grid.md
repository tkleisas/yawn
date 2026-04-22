# FwGrid — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Related:** [flex_box.md](flex_box.md) — 1D layout; FwGrid is the simple
2D cousin. [grid.md](grid.md) — CSS-style 2D grid with named tracks and
span rules; FwGrid is the "just line knobs up in a grid" variant.

---

## Intent

A **uniform-cell grid** — fixed cell size, row-major placement, children
flow left-to-right then wrap to the next row when the row-count cap is
reached. The canonical use is a device's knob bank: 8 knobs at 48×48
that line up in two rows of four.

FwGrid is deliberately minimal compared to `Grid` (the CSS-grid
widget). Where `Grid` handles named tracks, span declarations, and
explicit per-child placement, FwGrid handles the 90 % case where you
just want "N uniform cells, wrap at some row count."

## Non-goals

- **Variable cell size.** Every cell has the same `cellW × cellH`.
  Mixed-size grids use `Grid`.
- **Explicit per-child placement.** No `setCell(child, row, col)` —
  placement follows insertion order.
- **Cell spanning.** A child always occupies exactly one cell. Span
  behavior lives in `Grid`.
- **Cross-axis alignment inside a cell.** Children fill the cell
  (tight constraints on both axes). If a child wants to center within
  a larger cell, wrap it in a `Stack` first.

---

## Visual anatomy

Example: 8 children, cellW=48, cellH=50, gap=8, maxRows=2 → 2 rows × 4 cols.

```
    ┌─padding──────────────────────────────────────────────────┐
    │                                                          │
    │  ┌──┐ gap ┌──┐ gap ┌──┐ gap ┌──┐                        │
    │  │ 0│     │ 1│     │ 2│     │ 3│                        │
    │  └──┘     └──┘     └──┘     └──┘                        │
    │   gap                                                    │
    │  ┌──┐     ┌──┐     ┌──┐     ┌──┐                        │
    │  │ 4│     │ 5│     │ 6│     │ 7│                        │
    │  └──┘     └──┘     └──┘     └──┘                        │
    │                                                          │
    └──────────────────────────────────────────────────────────┘
```

FwGrid has no visual of its own — no background, no borders. Cells
are just positioned slots.

---

## Layout algorithm

```cpp
rows = min(childCount, maxRows)
cols = ceil(childCount / maxRows)

for each child i:
    row = i / cols
    col = i % cols
    cellX = padding.left + col * (cellW + gapX)
    cellY = padding.top  + row * (cellH + gapY)
    child.layout({cellX, cellY, cellW, cellH})
```

### Why "cap rows, grow cols"

The v1 convention that this widget ports from: knob banks want to cap
how tall the row stack is (device panels are short) and grow
horizontally. Setting `maxRows = 2` with 9 knobs yields 2×5 (not 3×3).

Callers that want the inverse ("cap cols, grow rows") flip the
orientation by swapping `cellW` / `cellH` and reading children out in
different order, OR use `Grid` which supports both.

---

## States

None. FwGrid is a structural container.

---

## Gestures

**None directly.** Events pass through to children.

---

## Public API

```cpp
class FwGrid : public Widget {
public:
    FwGrid() = default;

    // Cell size. Default: 64 × 70 (48 px knob + 16 px spacing /
    // 48 px knob + 22 px label space, matching the v1 defaults).
    void  setCellSize(float w, float h);
    float cellWidth()  const;
    float cellHeight() const;

    // Max rows. Children wrap to the next column once a row
    // reaches maxRows children.
    void setMaxRows(int rows);
    int  maxRows() const;

    // Gap between cells (horizontal + vertical separately).
    void setGap(float gx, float gy);
    float gapX() const;
    float gapY() const;

    // Uniform padding inside the grid, wrapping all cells.
    void setPadding(float px);
    float padding() const;

    // Computed after measure() / layout() — number of rows/cols
    // actually produced.
    int currentRows() const;
    int currentCols() const;
};
```

No callbacks. FwGrid fires nothing.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size FwGrid::onMeasure(Constraints c, UIContext& ctx) {
    const int n = childCount();
    if (n == 0) return c.constrain({0, 0});
    const int rows = std::min(n, m_maxRows);
    const int cols = std::ceil(n / float(m_maxRows));
    const float w = m_padding * 2 + cols * m_cellW + (cols - 1) * m_gapX;
    const float h = m_padding * 2 + rows * m_cellH + (rows - 1) * m_gapY;
    return c.constrain({w, h});
}
```

Children are NOT measured during the grid's own measure — their size
is determined by the cell (tight constraints at layout time).

### `onLayout(Rect bounds, UIContext& ctx)`

Positions children in row-major order using the formula in the
Algorithm section. Every child is laid out with tight constraints
matching `cellW × cellH`.

### Size policy

Fixed (bounds are content-driven); caller should `setPreferredSize` at
the parent level if they want the grid to stretch.

### Relayout boundary

**Yes, automatically.** Children changing size doesn't affect the
grid's own size — each cell is fixed. Child invalidation still bubbles
through per the usual rules.

### Caching

Measure cache: `(constraints, cellW, cellH, gapX, gapY, padding,
maxRows, childCount)`. Layout cache depends on the same plus `bounds`.

---

## Theme tokens consumed

**None.** FwGrid has no visual. Cell sizes and gaps come from explicit
setters, not theme metrics — knob banks on different devices wanted
different padding and migrating both through a single token wasn't
clean.

Callers that want theme-driven sizing pass theme values explicitly:
```cpp
grid.setGap(theme().metrics.baseUnit, theme().metrics.baseUnit * 2);
```

---

## Events fired

None.

---

## Invalidation triggers

### Measure-invalidating

- `setCellSize`, `setMaxRows`, `setGap`, `setPadding`
- `addChild`, `removeChild` (bubbled by Widget base)
- DPI / theme / font (global)

### Paint-only

- None — the grid draws nothing.

---

## Focus behavior

Non-focusable. Focus goes to children. Tab order matches insertion
order (which also matches visual left-to-right, top-to-bottom).

---

## Accessibility (reserved)

Role: `group` or `none`. Knob-bank grids should set an `aria-label`
describing the bank ("Modulation Matrix") so screen readers announce
the container.

---

## Animation

None at the grid level. Children animate themselves.

---

## Test surface

Unit tests in `tests/test_fw2_FwGrid.cpp`:

1. `EmptyGridZero` — no children → (0, 0).
2. `SingleChildUnpadded` — 1 child, padding=0, gap=0 → size = cellW × cellH.
3. `TwoRowsFourColsFromEightChildren` — 8 children, maxRows=2 → 4 cols × 2 rows; cell (0,0) at padding, cell (1,3) at known offset.
4. `PaddingRespected` — padding=10 → first cell offset by (10, 10).
5. `GapRespected` — gap=(5, 8), 2 children in first row → second cell's x offset = cellW + 5.
6. `UnfilledBottomRow` — 5 children, maxRows=2 → 2 rows × 3 cols; index 4 in row 1 col 1, no child at (1, 2).
7. `MeasureSize` — size = padding*2 + cols*cellW + (cols-1)*gapX by rows.
8. `ReorderChangesLayout` — v2 `addChild` order determines cell order; reorder helpers change positions.

---

## Migration from v1

Port is near-1:1. v1's `FwGrid` at `src/ui/framework/FwGrid.h` uses
inline `measure` / `layout` / `render` overrides; v2 version moves
them to `onMeasure` / `onLayout` / default `render` (which walks
children via the normal recursion) and uses the cache.

v1 had a custom `render()` override that did its own children
iteration with a clip-bounds check to skip offscreen cells. v2 drops
this — Renderer2D's own scissor clip handles large-grid performance;
the overhead of a few ignored draw calls is immaterial until we see
real profiling evidence otherwise.

v1 had:
```cpp
auto* grid = new fw::FwGrid();
grid->setCellSize(56, 64);
grid->setMaxRows(2);
// … add knobs …
```

v2 (namespace only):
```cpp
auto* grid = new fw2::FwGrid();
grid->setCellSize(56, 64);
grid->setMaxRows(2);
```

No API drift.

---

## Open questions

1. **Col-major variant?** "Cap cols, grow rows" is the inverse use
   case. Could live here as a second orientation flag or stay in the
   bigger `Grid` widget. Current decision: stay in `Grid`; FwGrid is
   the "knob bank" convenience.

2. **Responsive cell sizing?** For a device panel that can be
   resized, cells could auto-stretch. Out of scope — if you want
   that, use `Grid` with flex tracks.

3. **Per-cell alignment when child is smaller than cell?** Currently
   children are laid out at the cell's origin with tight constraints,
   forcing them to fill. If a child has `SizePolicy::fixed()` smaller
   than the cell, it'd visually snap top-left. To center, wrap in a
   `Stack{ child, StackAlign::Center }`. Flagged for consideration —
   could add `setCellAlignment(Alignment)` later.
