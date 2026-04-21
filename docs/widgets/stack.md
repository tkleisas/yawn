# Stack — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**New widget.**
**Related:** [flex_box.md](flex_box.md) — one-dimensional sibling
layout; Stack is the "children overlap" counterpart.

---

## Intent

A **z-ordered overlap container** — all children occupy the same
rect, paint order determines z-order (later = on top), and per-child
alignment decides where the child sits within the stack's bounds if
the child is smaller.

Used where widgets need to layer:

- A filled background rect behind a Label (`Stack{ FillRect; Label }`
  produces a "text with background").
- A loading spinner overlaid on content (`Stack{ Content; Centered
  Spinner }`).
- A thumbnail image with a play-state indicator badge
  (`Stack{ Image; BottomLeft Badge }`).
- A group header with a right-aligned action button
  (`Stack{ Spanning Header; RightTop Button }`).

Stack is NOT about popups or modals — those use the LayerStack
infrastructure (Overlay / Modal / Tooltip layers). Stack overlaps
widgets within a single, contiguous layout area.

## Non-goals

- **Absolute positioning.** No explicit `setPosition(x, y)` for
  children. Position is driven by per-child alignment; if you need
  pixel-exact custom positioning, wrap the child in a FlexBox with
  padding, or build a custom container.
- **Global z-index property.** Paint order = child order. Callers
  reorder children if they need different stacking.

---

## Visual anatomy

```
    ┌──────────────────────────────────┐
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│  ← child 0 (painted first, bottom of stack)
    │▓▓┌─────────────────┐▓▓▓▓▓▓▓▓▓▓▓▓▓│
    │▓▓│ child 1         │▓▓▓▓▓▓▓▓▓▓▓▓▓│  ← child 1 (painted next, centered)
    │▓▓│ (centered)      │▓▓▓▓▓▓▓▓▓▓▓▓▓│
    │▓▓└─────────────────┘▓▓▓▓▓▓▓▓▓▓▓▓▓│
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
    └──────────────────────────────────┘
```

No decoration. Stack is invisible structure.

---

## Alignment model

Each child has an `Alignment` spec: where to position the child if
it's smaller than the stack's bounds on either axis.

```cpp
struct Alignment {
    enum class HAlign { Left, Center, Right, Fill };
    enum class VAlign { Top, Center, Bottom, Fill };
    HAlign h = HAlign::Fill;
    VAlign v = VAlign::Fill;
};
```

Defaults to `Fill, Fill` — the child expands to fill the entire
stack. This is the common case (a background fill, a main content
panel).

`Fill` on an axis means the child is measured with tight constraints
on that axis (matches stack bounds exactly). `Left/Center/Right`
or `Top/Center/Bottom` mean the child is measured with loose
constraints and placed at the specified position.

### Convenience aliases

```cpp
namespace Align {
    const Alignment Fill      { HAlign::Fill,   VAlign::Fill };
    const Alignment Center    { HAlign::Center, VAlign::Center };
    const Alignment TopLeft   { HAlign::Left,   VAlign::Top };
    const Alignment TopCenter { HAlign::Center, VAlign::Top };
    const Alignment TopRight  { HAlign::Right,  VAlign::Top };
    const Alignment MidLeft   { HAlign::Left,   VAlign::Center };
    const Alignment MidRight  { HAlign::Right,  VAlign::Center };
    const Alignment BottomLeft{ HAlign::Left,   VAlign::Bottom };
    const Alignment BottomCenter{ HAlign::Center, VAlign::Bottom };
    const Alignment BottomRight{ HAlign::Right, VAlign::Bottom };
}
```

---

## Public API

```cpp
class Stack : public Widget {
public:
    Stack();

    // Adding children — each child has an alignment spec.
    void addChild(Widget* child, Alignment align = Align::Fill);

    // Mutate alignment post-add.
    void setAlignment(Widget* child, Alignment align);
    Alignment alignmentOf(Widget* child) const;

    // Per-child insets — additional padding relative to stack bounds
    // after alignment is applied. Useful for badges that should be
    // "at top-right, but 8 px inside the stack."
    void setInsets(Widget* child, Insets insets);

    // Sizing
    void setPreferredSize(Size s);           // 0,0 = driven by largest child
};
```

`addChild(widget, Align::Center)` is the normal call site. Children
are painted in addition order.

### Ordering

- First child painted = bottom of stack.
- Last child painted = top of stack.
- Use `reorderChildToTop(widget)` / `reorderChildToBottom(widget)`
  helpers (inherited from Widget base) to re-order.

Hit-testing walks children in **reverse** paint order — topmost gets
first shot. Standard widget hit-test rules apply; a child that doesn't
consume the event lets the next-topmost try.

---

## Gestures

**None directly.** Stack is pure layout. Events pass through to
children; hit-test traversal is top-down.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Default (preferredSize = 0): "largest child" rule.

```cpp
Size Stack::onMeasure(Constraints c, UIContext& ctx) {
    if (m_preferredSize.w > 0 && m_preferredSize.h > 0) {
        return c.constrain(m_preferredSize);
    }
    float w = 0, h = 0;
    for (Widget* child : m_children) {
        Alignment a = alignmentOf(child);
        // Children with Fill alignment are measured with tight constraint;
        // they can't meaningfully contribute a "desired width" — they take
        // whatever they're given. Measure only for max size contribution.
        Constraints cc = c;
        // For a Fill child, its contribution is "as large as stack is";
        // for non-fill children, measure with loose constraint to get intrinsic size.
        if (a.h != Alignment::HAlign::Fill) { cc.minW = 0; }
        if (a.v != Alignment::VAlign::Fill) { cc.minH = 0; }
        Size cs = child->measure(cc, ctx);
        w = std::max(w, cs.w);
        h = std::max(h, cs.h);
    }
    return c.constrain({w, h});
}
```

Stack size = max of all children's measured sizes, clamped to
constraints.

### `onLayout(Rect bounds, UIContext& ctx)`

For each child, compute its rect based on its alignment and insets:

```cpp
void Stack::onLayout(Rect bounds, UIContext& ctx) {
    m_bounds = bounds;
    for (Widget* child : m_children) {
        Alignment a = alignmentOf(child);
        Insets insets = insetsOf(child);
        Rect container = insets.apply(bounds);    // bounds minus insets

        // Measure child with appropriate constraints.
        Constraints cc;
        cc.minW = (a.h == HAlign::Fill) ? container.w : 0;
        cc.maxW = container.w;
        cc.minH = (a.v == VAlign::Fill) ? container.h : 0;
        cc.maxH = container.h;
        Size cs = child->measure(cc, ctx);

        // Clamp to container.
        cs.w = std::min(cs.w, container.w);
        cs.h = std::min(cs.h, container.h);

        // Position based on alignment.
        float x = container.x;
        if (a.h == HAlign::Center) x += (container.w - cs.w) * 0.5f;
        else if (a.h == HAlign::Right) x += container.w - cs.w;
        // (Left: x stays at container.x; Fill: x stays, cs.w == container.w)

        float y = container.y;
        if (a.v == VAlign::Center) y += (container.h - cs.h) * 0.5f;
        else if (a.v == VAlign::Bottom) y += container.h - cs.h;

        child->layout({x, y, cs.w, cs.h}, ctx);
    }
}
```

### Size policy

```cpp
// Driven by largest child, clamped by constraints.
SizePolicy{ width = Fixed, height = Fixed }
// Unless setPreferredSize is used with explicit values, in which case
// those drive.
```

### Relayout boundary

**Opt-in.** Stack's size depends on its children's sizes (largest
child rule); so a child measure invalidation bubbles through Stack
to its parent. Callers wanting a boundary (e.g. "this stack is
always 200×100, don't bother parents") set it explicitly.

### Caching

Measure cache: `(constraints, preferredSize, child alignments, child
measure versions)`. Layout cached on `(bounds, child alignment
version, insets version)`.

Reordering children (`reorderChildToTop` etc.) invalidates paint
only — bounds and measures don't change.

---

## Theme tokens consumed

**None directly.** Stack paints nothing of its own.

---

## Events fired

**None.** Purely structural.

---

## Invalidation triggers

### Measure-invalidating

- `addChild`, `removeChild`, `reorderChild*`
- `setAlignment`, `setInsets`
- `setPreferredSize`
- DPI / theme / font (global)
- Child measure invalidation (bubbled)

### Paint-only

- Child reorder via `reorderChildToTop` / `reorderChildToBottom`
  (size unchanged — just paint order swap).
- State changes of individual children (hover / press / etc.) —
  child's own invalidation, Stack doesn't care.

---

## Focus behavior

**Non-focusable.** Focus goes to children. Tab order matches child
list order.

---

## Accessibility (reserved)

- Role: `none` / `presentation` — Stack is structural, not semantic.
  Screen readers see through it to children.

---

## Animation

No animations at the Stack level. Children animate themselves.

Note: a common pattern is "fade in an overlay on top of content." The
Stack doesn't animate; the overlay widget fades its own opacity. Stack
just gives them a shared rect to live in.

---

## Test surface

Unit tests in `tests/test_fw2_Stack.cpp`:

### Measure

1. `EmptyStackZero` — no children → (0, 0).
2. `SingleFillChild` — one Fill child → stack size = constraint.
3. `SingleCenteredChild` — Center-aligned child → stack size =
   child's intrinsic size (or constraint, whichever smaller).
4. `LargestChildWins` — multiple children with different sizes →
   stack size = max(w), max(h).
5. `PreferredSizeOverrides` — `setPreferredSize(200, 100)` forces
   stack to 200×100 regardless of children.

### Layout

6. `FillChildTakesFullBounds` — Fill child's rect equals stack rect.
7. `CenterAlignsChild` — smaller child centered within bounds.
8. `TopLeftAligns` — child at (0, 0) relative to stack.
9. `BottomRightAligns` — child at (stackW - childW, stackH -
   childH).
10. `InsetsShrinkContainer` — `setInsets(child, Insets{10})` makes
    child container 20 px smaller on each axis; child positioned
    within that smaller rect.
11. `FillWithInsets` — Fill child with insets: child takes stack
    minus insets.

### Paint order

12. `FirstChildAtBottom` — first addChild → painted first (bottom).
13. `LastChildAtTop` — last addChild → painted last (top).
14. `ReorderChildToTop` — reorders child to last paint position.

### Hit-testing

15. `TopChildHitFirst` — overlapping children: hit-test finds
    topmost first.
16. `TopChildNotConsumingFallsThrough` — top child's onMouseDown
    returns false → next child tries.

### Events pass through

17. `NoGesturesAtStackLevel` — events not consumed by any child
    pass through Stack without Stack handling them.

### Focus

18. `FocusOrderMatchesChildOrder` — Tab cycles through children in
    insertion order, not paint order.

---

## Migration from v1

v1 has no Stack. Ad-hoc overlapping layouts use:
- Direct rect painting followed by text drawing (no widget tree).
- Panel-level manual layout computing positions.

v2 Stack replaces these with a declarative, measurable, testable
container.

---

## Open questions

1. **Transform support?** Should children be able to declare
   rotation / scale that Stack applies? Out of scope for v2.0; if
   wanted, a `Transform` widget wraps children and provides that
   independent of Stack.

2. **Clip overflow?** Currently children are clamped to stack
   bounds during layout. Should oversized children be clipped at
   paint time instead (i.e. allowed to overflow but clip)?
   Current behavior is safer; overflow opt-in via
   `setClipOverflow(bool)` if needed.

3. **Z-index property on children?** Instead of reordering the child
   list, some callers prefer declarative `setZ(5)`. Could store a
   z-index per child and sort for paint order. Decision: child list
   order is the authoritative z-order; z-index integer is
   unnecessary complexity. Reorder helpers on Widget base already
   cover the use case.
