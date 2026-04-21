# FlexBox — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `FlexBox` class in `src/ui/framework/FlexBox.h`

---

## Intent

A one-dimensional container that distributes its children along a
**main axis** (horizontal in `Row`, vertical in `Column`). It is the
workhorse layout container in YAWN — virtually every panel, toolbar,
and widget composite is built from nested FlexBoxes. Device chains,
transport bars, mixer channel strips, dialog button rows, menu
entries, and the panel header/body splits all reduce to FlexBox
arrangements.

Modeled on **CSS Flexbox** and **Yoga** (the production flexbox
engine used by React Native), with the simplifications a
professional-audio C++ DAW can afford:

- Single axis per container (no wrapping into a second axis — use a
  Grid for that).
- No writing-mode / RTL handling in v2.0.
- Explicit `flex` / `basis` / `shrink` on children, matching CSS.

## Non-goals

- **Wrap onto a second axis** (`flex-wrap` in CSS). Ableton-style UIs
  typically don't need this and it complicates the distribution
  algorithm significantly. Callers who need wrapping compose a Grid
  or a custom container.
- **Alignment within a single cross-axis row** beyond simple
  start/center/end/stretch. Multi-row cross-axis alignment needs
  `flex-wrap` support first.
- **Reverse direction** (`row-reverse` / `column-reverse`). Easy to
  add if needed; not obviously useful today.
- **Gap shorthand for row-gap / column-gap.** Single `gap` value;
  wrap support would make two-axis gaps relevant but that's when the
  spec should expand.

---

## Visual anatomy

FlexBox has no visual of its own. It lays out children along one
axis.

```
Row (main axis ═════▶, cross axis║, ║):

  ┌──────────────────────────────────────────┐
  │ ┌──┐  ┌────────┐       ┌──┐  ┌────────┐ │
  │ │A │  │   B    │ (gap) │C │  │   D    │ │
  │ └──┘  └────────┘       └──┘  └────────┘ │
  └──────────────────────────────────────────┘
    padding                         padding
```

Cross-axis alignment (for Row, this is vertical):

```
  AlignStart:       AlignCenter:      AlignEnd:        AlignStretch:
  ┌───────────┐     ┌───────────┐     ┌───────────┐    ┌───────────┐
  │┌─┐        │     │           │     │           │    │┌─────────┐│
  │└─┘        │     │┌─┐        │     │           │    ││         ││
  │           │     │└─┘        │     │┌─┐        │    ││         ││
  │           │     │           │     │└─┘        │    │└─────────┘│
  └───────────┘     └───────────┘     └───────────┘    └───────────┘
```

Main-axis justification (for Row, this is horizontal):

```
  JustifyStart:                 JustifyEnd:
  ┌─────────────────┐           ┌─────────────────┐
  │┌─┐┌─┐┌─┐        │           │        ┌─┐┌─┐┌─┐│
  │└─┘└─┘└─┘        │           │        └─┘└─┘└─┘│
  └─────────────────┘           └─────────────────┘

  JustifyCenter:                JustifySpaceBetween:
  ┌─────────────────┐           ┌─────────────────┐
  │    ┌─┐┌─┐┌─┐    │           │┌─┐    ┌─┐    ┌─┐│
  │    └─┘└─┘└─┘    │           │└─┘    └─┘    └─┘│
  └─────────────────┘           └─────────────────┘

  JustifySpaceAround:           JustifySpaceEvenly:
  ┌─────────────────┐           ┌─────────────────┐
  │  ┌─┐  ┌─┐  ┌─┐  │           │ ┌─┐  ┌─┐  ┌─┐  │  (equal spaces including edges)
  │  └─┘  └─┘  └─┘  │           │ └─┘  └─┘  └─┘  │
  └─────────────────┘           └─────────────────┘
```

---

## Core concepts

**Main axis.** Horizontal in `Row`, vertical in `Column`. Children
are placed one after another along the main axis.

**Cross axis.** Perpendicular. Each child's size on the cross axis is
determined by its own measure, optionally stretched.

**Flex property.** Per-child positive integer. A child with `flex=0`
(default) takes its preferred size. Children with `flex>0` share the
**remaining main-axis space after fixed-size children are measured**,
proportional to their flex values. Flex=1 on all means equal
distribution.

**Basis.** Per-child optional "preferred size" on main axis. If set,
overrides `measure()` for layout purposes — it's the size the child
would take before any flex-grow redistribution. Matches CSS `flex-
basis`.

**Shrink.** When children's preferred sizes exceed container size,
they shrink proportional to `shrink` (default 1). Shrink=0 means
the child keeps its preferred size even if it overflows.

**Gap.** Space between adjacent children on the main axis. No gap at
the outer edges.

**Padding.** Inside-container padding applied before laying out
children.

---

## Public API

```cpp
enum class FlexDirection { Row, Column };

enum class MainAxisJustify {
    Start,
    End,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class CrossAxisAlign {
    Start,
    End,
    Center,
    Stretch,    // fill cross axis
    Baseline,   // align text baselines (rows only)
};

struct FlexProps {
    int flex          = 0;               // grow factor; 0 = don't grow
    int shrink        = 1;               // shrink factor; 0 = don't shrink
    float basis       = -1.0f;           // preferred main-axis size; < 0 = use measure()
    CrossAxisAlign selfAlign = CrossAxisAlign::Stretch;   // per-child override
    // (Stretch matches CSS align-self: stretch; container default wins unless this is set.)
    bool selfAlignSet = false;
};

class FlexBox : public Widget {
public:
    FlexBox();
    explicit FlexBox(FlexDirection d);

    // Direction
    void setDirection(FlexDirection d);
    FlexDirection direction() const;

    // Distribution
    void setJustify(MainAxisJustify j);    // default Start
    void setCrossAlign(CrossAxisAlign a);  // default Stretch

    // Spacing
    void setGap(float px);                 // main-axis gap between children
    void setPadding(Insets p);             // inside-padding; same as Widget::setPadding

    // Children — adds a child with optional flex props
    void addChild(Widget* child);                              // FlexProps{}
    void addChild(Widget* child, const FlexProps& props);
    void setFlex(Widget* child, int flex);                     // setters work post-add
    void setShrink(Widget* child, int shrink);
    void setBasis(Widget* child, float basis);
    void setSelfAlign(Widget* child, CrossAxisAlign a);

    // Query
    const FlexProps& propsOf(const Widget* child) const;
};
```

Convenience helpers on `Widget`:

```cpp
Widget& Widget::flex(int f) { setFlex(f); return *this; }
Widget& Widget::basis(float b) { setBasis(b); return *this; }
```

Lets call sites chain: `btn.flex(1).basis(80)`. FlexBox reads these
when the child is added (props stored in FlexBox, not in Widget, to
keep Widget clean of layout-container-specific state — the chaining
helpers just forward to the parent container).

Actually, for simplicity, we do store flex props **in the FlexBox's
internal child map**, not on the Widget itself. The chaining helpers
on Widget are sugar that walks up to the containing FlexBox. If no
FlexBox parent yet, they're queued and applied on the next `addChild`.

---

## Layout algorithm

FlexBox's `onMeasure` and `onLayout` are where the constraint system
gets exercised most heavily. The algorithm mirrors Yoga's.

### Phase 1: collect child preferences

For each child, compute a **preferred main-axis size**:

```cpp
// For Row, mainSize = width; for Column, mainSize = height.
float preferredMain(Widget* c, Constraints parentConstraints) {
    const FlexProps& p = propsOf(c);
    if (p.basis >= 0) return p.basis;

    // Measure with cross axis constrained to parent's cross, main axis unbounded.
    Constraints childConstraints = parentConstraints;
    childConstraints.setMainAxisUnbounded(direction);
    Size s = c->measure(childConstraints, ctx);
    return s.mainAxis(direction);
}
```

Cross axis measurement happens on bounded cross-axis constraints —
the child knows how much cross-axis space it has to work with, which
matters for wrapping Labels.

### Phase 2: distribute main-axis space

```cpp
float availableMain = bounds.mainSize(direction) - padding - (numChildren - 1) * gap;
float totalPreferred = sum of preferredMain(c) for non-flex children
                     + sum of basis for flex children (basis=-1 uses preferredMain)
int totalFlex = sum of p.flex for all children

if (totalPreferred <= availableMain && totalFlex > 0) {
    // Grow phase — distribute slack
    float slack = availableMain - totalPreferred;
    for each child with p.flex > 0:
        childMain = preferredMain(c) + slack * (p.flex / totalFlex)
}
else if (totalPreferred > availableMain) {
    // Shrink phase — reduce over-sized children
    float overflow = totalPreferred - availableMain;
    int totalShrink = sum of (p.shrink * preferredMain(c)) for all children
    for each child:
        reduction = overflow * (p.shrink * preferredMain(c)) / totalShrink
        childMain = preferredMain(c) - reduction
}
else {
    // Exact fit — use preferred sizes
    for each child: childMain = preferredMain(c)
}
```

Every child's final main-axis size is now known.

### Phase 3: measure each child with final size

```cpp
for each child:
    Constraints cc;
    cc.setMainAxis(direction, tight(childMain));
    cc.setCrossAxis(direction, bounded(crossAxisRange));
    // Cross-axis range is:
    //   Stretch: [crossSize, crossSize] — tight
    //   Start/Center/End: [0, crossSize] — let child choose its size
    Size s = child->measure(cc, ctx);
    store s for layout pass
```

### Phase 4: justify on main axis

Compute child positions based on `MainAxisJustify`:

- **Start** — pack from main-axis origin, gaps between
- **End** — pack from main-axis end
- **Center** — total width of children + gaps centered
- **SpaceBetween** — first at start, last at end, equal gaps between
- **SpaceAround** — equal gaps, half-gaps at edges
- **SpaceEvenly** — equal gaps everywhere including edges

### Phase 5: align on cross axis

For each child:

- **Start** — child's cross-axis pos = 0
- **End** — child's cross-axis pos = crossSize - child.crossSize
- **Center** — centered
- **Stretch** — child already measured with tight crossSize, just place at 0
- **Baseline** — align `child.baselineOffset()` to `container.baselineY`
  (row direction only; falls back to Start in column)

Per-child `selfAlign` overrides container's `crossAlign`.

### Phase 6: call child->layout() on each child

With the rect computed in phases 4 & 5.

---

## Measure vs layout: what's cached

FlexBox's own `onMeasure` returns the total occupied size given the
constraints it's measured with. If we're a Row with children flexing
to fill, our main-axis measure is simply `min(totalPreferred,
maxMainFromConstraints)` — that is, we "take all we can get" on main
axis when we have flex children.

```cpp
Size FlexBox::onMeasure(Constraints c, UIContext& ctx) {
    // Compute preferredMain for each child (triggers child measure recursion — cached).
    float totalPreferred = ...;   // as in algorithm
    float totalCross = max over children of child.crossMeasured

    float w = direction == Row ? totalPreferred : totalCross;
    float h = direction == Row ? totalCross      : totalPreferred;

    // Add padding + gaps
    w += padding.horiz(); h += padding.vert();
    w += direction == Row    ? (numChildren - 1) * gap : 0;
    h += direction == Column ? (numChildren - 1) * gap : 0;

    // If any child has flex > 0, we accept maxMain from constraints.
    if (hasFlexChildren) {
        if (direction == Row)    w = c.maxW;
        else                     h = c.maxH;
    }

    return c.constrain({w, h});
}
```

**Measure caching** works as specified in the architecture doc:
`(constraints, child list version, child measure versions, flex props
hash, gap, padding, justify, crossAlign, direction)`. The framework
checks the child measure versions via `child->measureVersion()`; any
child's invalidation rolls the parent's as well.

**Layout caching** reuses the same keys, plus bounds.

---

## Size policy

```cpp
// For a Row FlexBox:
SizePolicy{
    width  = Flex (if any child has flex>0) else Fixed (content-driven)
    height = Fixed (cross-axis driven by tallest child)
}

// Column symmetrically.
```

### Relayout boundary

**Opt-in.** FlexBox is not a boundary by default because parents often
need to know its size. Useful cases to set boundary on:

- Outer scroll viewport FlexBox (its size doesn't propagate, because
  the scroll container clips anyway)
- Panel-root FlexBox where the panel's bounds are controlled
  externally

---

## Theme tokens consumed

None directly. FlexBox is a layout container; children are what
consume theme.

If `setPadding` / `setGap` are called with explicit values, those
values are pixel-literal. If they're derived from `theme().metrics`
(e.g. `metrics.baseUnit * 2`), the caller is responsible for
re-applying on theme change — but since `metrics.baseUnit` rarely
changes across themes, this is usually once at construction.

---

## Gestures

None — FlexBox is a pure layout container. All events pass through to
children.

---

## Events fired

None beyond child-list mutation events (handled by Widget base).

---

## Invalidation triggers

### Measure-invalidating

- `setDirection`
- `setJustify`, `setCrossAlign`
- `setGap`, `setPadding`
- Any child's measure invalidation (bubbled up; FlexBox depends on
  child sizes)
- `addChild` / `removeChild` / reorder
- `setFlex` / `setShrink` / `setBasis` / `setSelfAlign`

### Paint-only invalidating

None at this level. FlexBox doesn't paint — its re-measures or
re-layouts are what expose work. If any state change matters for the
layout, it's measure-invalidating.

---

## Focus behavior

**Non-focusable.** FlexBox has no focus state of its own. Focus
belongs to its interactive children.

Tab order follows child order (unless reordered with explicit
`setTabIndex`, not in v2.0).

---

## Accessibility

**Role:** `group` or `none` depending on whether the caller sets
`setAriaRole`. By default, a FlexBox is accessibility-transparent —
it's structural.

---

## Animation

None at FlexBox level. Child bounds animations come from the Widget
base (panel animations lerp child bounds between two computed layouts;
FlexBox is just the source of those layouts, not the animator).

---

## Test surface

Unit tests in `tests/test_fw2_FlexBox.cpp`:

### Measure / layout basics

1. `EmptyBoxZero` — no children, `measure()` returns `(padding.w,
   padding.h)`.
2. `SingleChildRow` — one fixed-size child, Row: width = child.width,
   height = child.height.
3. `SingleChildColumn` — symmetric.
4. `TwoChildrenWithGap` — Row of two 50-px-wide children + 10-px gap
   → width = 110.
5. `PaddingRespected` — padding adds to both axes.

### Justification

6. `JustifyStart` — children packed against start.
7. `JustifyEnd` — children packed against end.
8. `JustifyCenter` — children centered.
9. `JustifySpaceBetween` — first at 0, last at end, equal gaps.
10. `JustifySpaceAround` — equal gaps with half-gaps at edges.
11. `JustifySpaceEvenly` — equal gaps everywhere.

### Cross-axis alignment

12. `CrossStart` — children pinned to cross-axis start.
13. `CrossCenter` — children centered cross-axis.
14. `CrossEnd` — pinned to end.
15. `CrossStretch` — children stretched to fill cross axis.
16. `SelfAlignOverride` — per-child selfAlign beats container default.
17. `BaselineAlignment` — Row with different-size labels aligns
    baselines.

### Flex distribution

18. `FlexEqual` — three children all flex=1 share equally.
19. `FlexUnequal` — flex=1, flex=2, flex=3 produce 1:2:3 split.
20. `FlexWithFixedSibling` — fixed-size child keeps its size; flex
    children share the remainder.
21. `FlexZeroNoGrow` — flex=0 children keep preferred size even if
    slack exists.
22. `BasisOverridesMeasure` — child with basis=100 takes 100 even
    if its `measure()` returns 50.

### Shrink

23. `ShrinkProportional` — over-sized children shrink proportional
    to `shrink × preferred`.
24. `ShrinkZeroKeepsSize` — shrink=0 child doesn't shrink; overflow
    pushed onto siblings.
25. `AllShrinkZeroOverflows` — if every child has shrink=0, the row
    overflows past its bounds and children paint outside (framework
    responsibility, not FlexBox).

### Nesting + caching

26. `NestedFlexRespectsConstraints` — Row containing a Column-of-
    Labels lays out correctly.
27. `MeasureCacheHit` — second `measure()` with unchanged state skips
    child measures.
28. `AddingChildInvalidatesCache` — `addChild` forces re-measure.
29. `ChildInvalidationBubblesToFlex` — a grandchild Label calling
    `invalidate()` invalidates the FlexBox's measure cache.
30. `RelayoutBoundaryStopsBubble` — FlexBox marked as relayout
    boundary does NOT propagate its invalidation to parent.

### Edge cases

31. `ZeroWidthConstraint` — container constrained to width 0 →
    children all shrink to 0 if shrinkable, else overflow.
32. `InfinityConstraint` — container with maxW = ∞ → behaves as if
    measuring content-driven.
33. `GapOnEmptyBox` — `gap=20` on empty container → width = padding
    only, no trailing gap.
34. `SingleChildGapIgnored` — one child → no gap applied.

---

## Migration from v1

v1 `FlexBox`:

```cpp
FlexBox row(FlexDirection::Row);
row.addChild(&btn);
row.addChild(&btn2);
row.setSpacing(10);
```

The v1 implementation:

- Has `setSpacing` for inter-child gap. v2: `setGap` (rename).
- Has no `justify` concept — children always pack at start. v2: `setJustify`.
- Has no cross-axis control — children always stretch cross. v2:
  `setCrossAlign` with Start default.
- Per-child flex stored directly on Widget as a property. v2: stored
  in FlexBox.
- Measure always returns whatever the constraint says on main axis.
  v2: returns content-driven unless flex children present.

**Breaking:**
- `setSpacing` → `setGap`.
- FlexBox children no longer stretch cross-axis by default. Existing
  UIs that rely on that need explicit `setCrossAlign(Stretch)` (which
  was the old implicit behavior). This is a **safer default** — CSS
  Flexbox default is Stretch too, and honestly the current explicit-
  stretch everywhere is fine — but verify.

**New:**
- `setJustify` — previously you padded with spacer widgets.
- `setCrossAlign`.
- `basis` / `shrink`.
- Per-child `selfAlign`.
- Relayout boundary support.

---

## Open questions

1. **Wrap support?** Eventually desirable for dynamic toolbars that
   might overflow on narrow windows. Major algorithm change;
   deferred. When added, it's `flex-wrap: wrap` semantics — rows
   within a FlexBox become virtual sub-rows, each justified
   independently.

2. **Order property?** CSS `order: -1` re-orders children without
   actually re-parenting. Useful for language-swapped UIs. Low
   priority.

3. **Subpixel layout?** Currently all FlexBox math is float-precise
   but final child bounds round to integer pixels for paint. Do we
   paint on subpixel boundaries and blend? Probably not needed for a
   desktop DAW.

4. **Reactive min / max?** CSS has `min-width` / `max-width`. Widget
   already has `setMinWidth` / `setMaxWidth` via per-widget methods.
   Do we lift these to FlexProps for per-sibling limits? Probably
   not — use the widget's own properties.

5. **Debug paint mode?** A helper `setDebugPaint(true)` that draws
   colored borders around the FlexBox and each child, with flex /
   basis numbers overlaid. Would be valuable during the v2 rollout;
   belongs on the framework-wide debug overlay rather than per-
   container.
