# Grid — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**New widget.**
**Related:** [flex_box.md](flex_box.md) — 1D sibling layout; Grid is
its 2D counterpart. [fw_grid.md] — the simpler row-major grid in
`src/ui/framework/FwGrid.h` is a different widget, kept for compact
parameter knob layouts. Grid (this spec) is full CSS-grid-style.

---

## Intent

A **2D layout container** with explicit row and column tracks.
Children occupy one or more cells; track sizing is declarative
(fixed px, fractional, content-driven); children can span multiple
cells. Inspired by CSS Grid, with simplifications appropriate for a
C++ framework that doesn't need to match a web-spec edge-for-edge.

Used where sibling FlexBoxes become awkward:

- **Preferences dialog** — label-on-left, control-on-right rows where
  every label column should align to the widest label.
- **Instrument parameter layouts** — complex panels with heterogeneous
  cell sizes (knob, display, label) arranged in 2D.
- **Dialog forms** — multi-column form with field groups.

Grid is opt-in for layouts where 2D alignment matters. For rows of
uniform children, FwGrid or FlexBox-of-FlexBoxes is simpler.

## Non-goals

- **Named tracks** (`grid-template-columns: [start] 100px [content]
  1fr [end]`). Useful in CSS for maintainability but adds complexity.
  Placement in v2 is by integer row/column indices only.
- **Subgrid** (`grid-template-columns: subgrid`). Deep feature; adds
  algorithmic complexity. Call sites that need nested-alignment
  build their own composites.
- **Masonry / grid-auto-flow: dense**. Simple auto-flow only (row by
  row, filling the next empty cell).
- **Implicit track generation**. Children placed past the declared
  track count grow the grid implicitly (last track's size is repeated).
  CSS grid has richer `grid-auto-rows` / `grid-auto-columns` —
  simplification: all auto-generated tracks use the same size as the
  last explicit track.
- **Baseline alignment across rows**. FlexBox has this for text in a
  row. Grid's cross-row baseline would need a third pass; deferred.

---

## Visual anatomy

```
    ┌────────────────┬──────────┬───────────────┐
    │                │          │                │
    │  (0, 0)        │ (0, 1)   │  (0, 2)        │   row 0
    │                │          │                │
    ├────────────────┼──────────┼───────────────┤
    │                │          │                │
    │  (1, 0)        │ (1, 1)   │  (1, 2)        │   row 1
    │                │          │                │
    │                │          │                │
    ├────────────────┼──────────┴───────────────┤
    │                │                           │
    │  (2, 0)        │  (2, 1–2) spans 2 cols   │   row 2
    │                │                           │
    └────────────────┴───────────────────────────┘
       col 0             col 1        col 2

       ↕ row-gap          ↕ row-gap
       ↔ col-gap         ↔ col-gap
```

No visible decoration — Grid is structural.

---

## Track definitions

A **track** is a row or column. Each track has a sizing rule:

```cpp
enum class TrackSizeKind {
    Fixed,       // exact pixels
    Fraction,    // share of remaining space (like CSS `fr`)
    Auto,        // content-driven (largest child in that track)
    MinContent,  // smallest size to fit content without overflow
    MaxContent,  // size to fit content fully without wrapping
};

struct TrackSize {
    TrackSizeKind kind;
    float         value;   // px for Fixed, fractions for Fraction, unused otherwise

    static TrackSize px(float p)       { return {Fixed, p}; }
    static TrackSize fr(float f = 1)   { return {Fraction, f}; }
    static TrackSize auto_()           { return {Auto, 0}; }
    static TrackSize minContent()      { return {MinContent, 0}; }
    static TrackSize maxContent()      { return {MaxContent, 0}; }
};
```

### Sizing algorithm (per axis)

1. Compute intrinsic sizes for `Auto`, `MinContent`, `MaxContent`
   tracks — measure all children in each track with loose constraints,
   pick largest intrinsic width.
2. Compute total fixed space: sum of `Fixed` track sizes + intrinsic
   sizes for content-driven tracks + gaps.
3. Compute remaining space: `maxAvailable - fixedSpace`.
4. Distribute remaining to `Fraction` tracks proportionally.
5. If total > maxAvailable, shrink content-driven and fraction tracks
   proportionally (intrinsic first, then fraction).

Same algorithm applied to rows and columns independently.

---

## Cell placement

Two modes per child:

### Auto-placement (default)

Children are placed in row-major order into the next empty cell. If
a child spans multiple cells, the placement algorithm skips rows /
columns that would create overlap.

```cpp
grid.addChild(&label1);   // placed at (0, 0)
grid.addChild(&field1);   // placed at (0, 1)
grid.addChild(&label2);   // placed at (1, 0)
grid.addChild(&field2);   // placed at (1, 1)
```

If the grid has `colCount = 3`, placement wraps:

```cpp
grid.addChild(&a);  // (0, 0)
grid.addChild(&b);  // (0, 1)
grid.addChild(&c);  // (0, 2)
grid.addChild(&d);  // (1, 0)  ← wrapped
```

### Explicit placement

```cpp
grid.addChild(&hero, {.row = 0, .col = 0, .rowSpan = 2, .colSpan = 2});
grid.addChild(&sidebar, {.row = 0, .col = 2, .rowSpan = 2});
grid.addChild(&footer, {.row = 2, .col = 0, .colSpan = 3});
```

Auto-placed children skip cells occupied by explicit-placed children.

---

## Public API

```cpp
struct GridItem {
    int row = -1;         // -1 = auto-place
    int col = -1;
    int rowSpan = 1;
    int colSpan = 1;
    Alignment alignSelf = Align::Fill;   // per-cell override
};

enum class GridAutoFlow {
    Row,         // auto-place row by row (default)
    Column,      // auto-place column by column
};

class Grid : public Widget {
public:
    Grid();

    // Track definitions
    void setColumns(std::vector<TrackSize> cols);
    void setRows(std::vector<TrackSize> rows);
    // If fewer rows than children require, extra rows generated with
    // the same size as the last defined row.

    // Gaps
    void setGap(float px);                  // both axes
    void setRowGap(float px);
    void setColumnGap(float px);

    // Default cell alignment (overridden per-child by GridItem.alignSelf)
    void setJustifyItems(Alignment::HAlign); // default Fill
    void setAlignItems(Alignment::VAlign);   // default Fill

    // Auto-flow
    void setAutoFlow(GridAutoFlow flow);     // default Row

    // Children
    void addChild(Widget* child);                         // auto-placed with defaults
    void addChild(Widget* child, GridItem item);
    void setItem(Widget* child, GridItem item);           // mutate after add
    GridItem itemOf(Widget* child) const;

    // Padding (inside grid bounds, before gaps)
    void setPadding(Insets p);
};
```

### Convenience constructors

Common grid patterns deserve shorthand:

```cpp
// 2-column "label : widget" grid with gap and left-aligned first column
auto g = Grid::labelValue({TrackSize::auto_(), TrackSize::fr(1)},
                           /*rowGap=*/ 6);

// Uniform NxM grid with equal-weighted tracks
auto g = Grid::uniform(cols=4, rows=3);
```

These are static factory helpers, not separate classes.

---

## Gestures

**None.** Pure layout container. Events pass through to children.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Implements the CSS grid sizing algorithm, but only as far as needed
for our subset:

```cpp
Size Grid::onMeasure(Constraints c, UIContext& ctx) {
    // 1. Resolve implicit tracks — count children's required cells,
    //    generate additional rows/cols if explicit tracks insufficient.
    int effectiveRows = std::max(declaredRows.size(), maxChildRow + 1);
    int effectiveCols = std::max(declaredCols.size(), maxChildCol + 1);

    // 2. Measure children once with loose constraints to collect
    //    intrinsic sizes per track.
    for (Widget* child : m_children) {
        Size s = child->measure(looseConstraints(), ctx);
        // Attribute to tracks based on GridItem's (row, col, span).
        recordIntrinsic(trackIndex, s.wOrH);
    }

    // 3. Resolve each track's size.
    std::vector<float> colSizes = resolveTrackSizes(columns, c.maxW, intrinsics);
    std::vector<float> rowSizes = resolveTrackSizes(rows,    c.maxH, intrinsics);

    // 4. Total grid size = sum of tracks + gaps + padding.
    float totalW = sum(colSizes) + (effectiveCols-1) * colGap + padding.horiz();
    float totalH = sum(rowSizes) + (effectiveRows-1) * rowGap + padding.vert();

    return c.constrain({totalW, totalH});
}
```

### `onLayout(Rect bounds, UIContext& ctx)`

1. Compute final track sizes (re-runs the measure if bounds differ
   from measured result — cache handles this).
2. For each child, compute its cell rect from track positions +
   spans:

```cpp
Rect childRect;
childRect.x = padding.left + sumPrefix(colSizes, item.col) + (item.col) * colGap;
childRect.y = padding.top  + sumPrefix(rowSizes, item.row) + (item.row) * rowGap;
childRect.w = sum(colSizes.subrange(item.col, item.colSpan))
            + (item.colSpan - 1) * colGap;
childRect.h = sum(rowSizes.subrange(item.row, item.rowSpan))
            + (item.rowSpan - 1) * rowGap;
```

3. Apply per-child alignment within the cell rect (same semantics as
   Stack — Fill stretches, others position).
4. Measure child with computed cell rect as constraint; lay out.

### Size policy

```cpp
SizePolicy{ width = Fixed, height = Fixed }
```

Size driven by track rules, which derive from constraints + content.

### Relayout boundary

**Opt-in.** Grid's size depends on children's intrinsic sizes when
any track is Auto / MinContent / MaxContent. Callers who know their
tracks are all Fixed or Fraction may flip this on for a perf win.

### Caching

Measure cache key:
- Constraints
- Column track definitions (hash)
- Row track definitions (hash)
- Gap values
- Child placement versions (GridItem per child)
- Child measure versions

Measure runs the full track-resolution algorithm; caching on all of
the above is essential for dense grids.

Layout cache adds `bounds`; re-layout when bounds differ from
measure's context.

---

## Theme tokens consumed

**None directly.** Grid is structural.

---

## Events fired

**None.**

---

## Invalidation triggers

### Measure-invalidating

- `setColumns`, `setRows`
- `setGap`, `setRowGap`, `setColumnGap`
- `setJustifyItems`, `setAlignItems`
- `setAutoFlow`
- `addChild`, `removeChild`
- `setItem` (GridItem change)
- `setPadding`
- Any child's measure invalidation

### Paint-only

- Per-child state changes (hover, press) — invalidate only that child.

---

## Focus behavior

Non-focusable. Focus goes to children. Tab order:

- By default: follows row-major cell order (row 0 left-to-right, then
  row 1, etc.). Explicit-placed children slot into this order based
  on their (row, col).
- Opt-in: `setTabOrder(TabOrder::Column)` for column-major.

---

## Accessibility (reserved)

- Role: `grid` when content is data-tabular (overlap with Table —
  we recommend Table for actual tabular data).
- Role: `none` / `presentation` when used as a structural layout
  (preferences form, parameter panel).
- Caller sets via `setAriaRole(...)` hint.

---

## Animation

None at Grid level. Children animate themselves.

---

## Test surface

Unit tests in `tests/test_fw2_Grid.cpp`:

### Track sizing

1. `FixedTracks` — 3 columns of `[100, 200, 50]` → row width = 350
   + gaps.
2. `FractionalTracks` — 3 columns of `[1fr, 2fr, 1fr]` in 400px
   viewport → widths 100, 200, 100.
3. `MixedFixedAndFraction` — `[100, 1fr, 1fr]` in 500px → widths
   100, 200, 200.
4. `AutoTrackFromContent` — column `[auto]`, child measures 150 →
   column width = 150.
5. `MinContentTrack` — text that wraps under pressure; MinContent
   gives the smallest-that-still-fits size.
6. `MaxContentTrack` — `[maxContent]` sizes to fit content without
   wrap.

### Placement

7. `AutoPlacementRowMajor` — 3 children in a 2-col grid: (0,0),
   (0,1), (1,0).
8. `ExplicitPlacement` — child with `{.row=1, .col=2}` placed at
   that cell.
9. `SpanningChild` — child with `colSpan=2` occupies two columns.
10. `RowSpanningChild` — `rowSpan=2` occupies two rows.
11. `AutoSkipsExplicitCells` — explicit child at (0, 1); auto-
    placed children fill around it.
12. `ImplicitRowGrowth` — more children than declared rows → extra
    rows generated.

### Alignment

13. `CellFillStretchChild` — Fill alignment stretches child to cell
    bounds.
14. `CellCenterAlignsChild` — smaller child centered in larger
    cell.
15. `PerChildSelfAlignOverrides` — GridItem.alignSelf = Center on a
    grid with default Fill: that child centers, siblings stretch.

### Gaps

16. `GapBetweenTracks` — `setGap(10)` inserts 10 px between adjacent
    cells.
17. `RowGapColGapIndependent` — different values on each axis.

### Padding

18. `PaddingReducesCellArea` — setPadding(8) shrinks available
    grid area; tracks redistribute.

### Caching

19. `MeasureCacheHit` — repeated measure with same state → cached.
20. `ChildMeasureBubbles` — grandchild invalidation invalidates
    grid's measure.
21. `RelayoutBoundaryStopsBubble` — with boundary set, invalidation
    doesn't bubble up.

### Edge cases

22. `EmptyGridMeasuresToPadding` — no children → size = padding.
23. `SingleChildFullGridWhenOneTrack` — 1×1 grid → child fills.
24. `TooManyChildrenGrowsImplicitly` — 4 children in 1-col grid
    generate 4 implicit rows.
25. `ConstraintTooSmallShrinks` — grid constrained to 100px, fixed
    tracks summing to 200 → tracks shrink proportionally.

---

## Migration from v1

v1 has `FwGrid` (`src/ui/framework/FwGrid.h`) — a simple row-major
layout, different widget. Grid (v2 spec'd here) is new.

`FwGrid` kept as-is for compact uniform layouts (knob grids on
instrument panels). Grid is for layouts needing 2D alignment.

---

## Open questions

1. **Named tracks?** Proposed later via `setColumns({ "start", 100,
   "mid", 1fr, "end" })`. v2.0 uses indices only.

2. **Subgrid?** CSS feature where a grid child can inherit parent's
   track definition. Deferred.

3. **Child minimum size interaction?** If a child has `setMinWidth(200)`
   and its cell is only 100 px wide (because sibling fractional tracks
   dominated), does the grid overflow or does the cell honor min?
   Proposed: cell respects child's min-size by expanding the track;
   if that would exceed constraints, the constraint wins and the
   child overflows. Covered in test `ChildMinWidthOverridesTrackSize`.

4. **Track templates / reuse?** Future: `GridLayoutTemplate` with
   named row/col definitions reusable across grids. Out of scope.

5. **Masonry layout?** `grid-auto-flow: dense` fills gaps aggressively
   so later small children plug holes left by earlier large ones.
   Deferred; niche.

6. **Per-track alignment?** Justify/align items are grid-wide;
   per-track override would be `setTrackAlign(col, hAlign)`. Add if
   real use case emerges.
