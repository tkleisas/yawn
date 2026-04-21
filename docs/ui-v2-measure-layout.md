# YAWN UI v2 — Measure, Layout & Caching Deep Dive

Reference for the two-pass measure-then-layout system, the cache
behind it, relayout boundaries, and invalidation. The architecture
doc introduces these concepts; this doc is the precise contract for
implementers.

Inspirations: **Flutter's RenderBox** (measure-then-layout,
relayout boundaries, `markNeedsLayout`) and **Yoga / Taffy**'s
constraint caching.

---

## Why caching matters

A naive approach (re-measure every widget every frame) works at 50
widgets but falls over at 500. Complex projects have:
- A mixer with 16+ channel strips, each containing 4+ knobs + a
  visualizer + a button stack.
- A device chain of 5–10 devices, each with 10–30 parameter knobs.
- An arrangement view with hundreds of clips.
- A piano roll with hundreds of MIDI notes.

Measuring widgets involves text width lookups, flexbox distribution,
grid track resolution — not expensive per widget, but multiplied by
hundreds it becomes a per-frame drag. Caching drops measure cost to
near zero on frames where nothing material changed (which is most
frames — hover / press / animation state changes are paint-only).

---

## The two passes

### Measure pass

**Top-down propagation of constraints; bottom-up return of sizes.**

The parent gives a child constraints ("you can be 0–400 px wide, 100
px tall") and asks "what size do you want?". The child may recurse
into its own children before returning a size.

```cpp
class Widget {
public:
    // Cached wrapper — called by framework and parents.
    Size measure(Constraints c, UIContext& ctx);

protected:
    // Implemented by subclasses. Returns desired size for given
    // constraints. Pure: no side effects beyond cache writes, no
    // dependency on state outside the widget + its measured children.
    virtual Size onMeasure(Constraints c, UIContext& ctx) = 0;
};
```

Measure is pure — repeated calls with identical constraints and
unchanged widget state return identical sizes. This is the property
that makes caching sound.

### Layout pass

**Top-down propagation of bounds.**

After measure, the parent knows its own size and decides how to
arrange children. It calls each child's `layout(rect, ctx)` with an
assigned position + size. Children recurse into their own `layout`.

```cpp
class Widget {
public:
    void layout(Rect bounds, UIContext& ctx);

protected:
    virtual void onLayout(Rect bounds, UIContext& ctx);
    // Default implementation stores bounds; containers override to
    // position children.
};
```

Layout is NOT pure — it writes `m_bounds` to each widget, which is
used by paint and hit-testing. But it IS deterministic: same inputs
(bounds + measured-sizes from measure pass) → same child layout.

---

## Constraints type

```cpp
struct Constraints {
    float minW = 0.0f;
    float maxW = std::numeric_limits<float>::infinity();
    float minH = 0.0f;
    float maxH = std::numeric_limits<float>::infinity();

    // Convenience
    static Constraints tight(Size s);         // minW=maxW=s.w, minH=maxH=s.h
    static Constraints loose(Size maxSize);    // min=0, max=maxSize
    static Constraints unbounded();            // all zero/infinity

    // Query
    bool isTight() const;                       // both axes tight
    bool isSatisfied(Size s) const;             // s fits in constraint
    bool hasBoundedWidth() const;
    bool hasBoundedHeight() const;

    // Mutation
    Size constrain(Size desired) const;
    Constraints loosenMin() const;              // sets minW, minH to 0
    Constraints setMainAxis(Axis, float min, float max) const;
    Constraints setCrossAxis(Axis, float min, float max) const;

    // Comparison (for cache lookup)
    bool operator==(const Constraints&) const;
    size_t hash() const;
};
```

### Semantic rules

1. **Constraints are always valid**: `minW <= maxW`, `minH <= maxH`.
2. **Infinity is legal** for `maxW` / `maxH`. A widget that measures
   under unbounded constraints returns its intrinsic size.
3. **Constraints propagate DOWN only.** A child never modifies its
   parent's constraints.
4. **Children receive constraints TIGHTER OR EQUAL to their parent's.**
   E.g., a FlexBox row distributes its width among children; each
   child's maxW is at most the row's maxW.

### Common constraint patterns

| Parent behavior | Constraint to child |
|---|---|
| "Fill a fixed box" | `tight(200, 100)` |
| "Content-driven within max" | `loose({800, 600})` |
| "Scroll axis unbounded" | `{0, ∞, 0, H}` for vertical scroll |
| "Exact height, flexible width" | `{0, parentW, H, H}` |

---

## Size type

```cpp
struct Size {
    float w = 0.0f;
    float h = 0.0f;

    static Size zero();
    bool operator==(const Size&) const;
};
```

Simple value type. Returned from `onMeasure`.

---

## Measure cache

```cpp
struct MeasureCache {
    Constraints lastConstraints{};
    Size         lastResult{};
    int          globalEpoch    = -1;
    int          localVersion   = 0;
    int          lastLocalSeen  = -1;
};
```

Every widget has one `MeasureCache` member.

### Cache hit rules

```cpp
Size Widget::measure(Constraints c, UIContext& ctx) {
    const int epoch = ctx.epoch();
    if (m_measureCache.globalEpoch == epoch
     && m_measureCache.lastLocalSeen == m_measureCache.localVersion
     && m_measureCache.lastConstraints == c) {
        return m_measureCache.lastResult;
    }
    Size s = onMeasure(c, ctx);
    m_measureCache.lastConstraints = c;
    m_measureCache.lastResult      = s;
    m_measureCache.globalEpoch     = epoch;
    m_measureCache.lastLocalSeen   = m_measureCache.localVersion;
    return s;
}
```

Three conditions for cache hit:
1. **Global epoch matches**: no framework-level invalidation since
   last measure (no DPI change, no theme swap, no font reload).
2. **Local version matches**: no widget-level invalidation since
   last measure.
3. **Constraints match exactly**: parent is asking the same
   question.

Any one mismatch → cache miss → re-measure.

### Why constraints must match exactly

A widget measured with `maxW = 400` returns a different size than
measured with `maxW = 200`. Returning cached-for-400 when asked for
200 would lie to the parent. Therefore constraints are part of the
cache key.

### Single-slot vs multi-slot cache

We use a **single-slot cache** per widget — one `(constraints,
result)` pair. If the parent alternates between two constraint
values (rare but possible during flex distribution), we re-measure
on each alternation.

A multi-slot LRU cache would serve those edge cases but adds
complexity and memory. Flutter uses single-slot too, relying on
relayout boundaries + stable constraint flow. YAWN follows suit.

---

## Layout cache

```cpp
struct LayoutCache {
    Rect lastBounds{};
    int  globalEpoch    = -1;
    int  measureVersion = -1;
};
```

### Cache hit rules

```cpp
void Widget::layout(Rect b, UIContext& ctx) {
    if (m_layoutCache.globalEpoch == ctx.epoch()
     && m_layoutCache.lastBounds == b
     && m_layoutCache.measureVersion == m_measureCache.localVersion) {
        return;  // no work needed
    }
    onLayout(b, ctx);
    m_bounds = b;
    m_layoutCache.lastBounds      = b;
    m_layoutCache.globalEpoch     = ctx.epoch();
    m_layoutCache.measureVersion  = m_measureCache.localVersion;
}
```

Three conditions for cache hit:
1. Global epoch unchanged.
2. Bounds identical (parent is laying us out at the same rect).
3. Our measure result hasn't changed since last layout.

Layout cache skips `onLayout` entirely on hit — no traversal into
children, no re-positioning, no work.

### Why bounds equality (not just "non-null")

Parents can call `layout(differentRect, ctx)` when the parent's own
size changed. We must re-lay-out in that case.

---

## Relayout boundaries

A **relayout boundary** is a widget that guarantees its own measured
size is stable regardless of what happens inside it. A boundary's
child-widget invalidation doesn't propagate upward past the
boundary.

### Auto-detect vs opt-in

```cpp
bool Widget::isRelayoutBoundary() const {
    // Auto: fixed-size widgets are boundaries.
    if (m_sizePolicy.width == SizePolicy::Fixed &&
        m_sizePolicy.height == SizePolicy::Fixed)
        return true;
    // Opt-in: manual override.
    return m_explicitBoundary;
}

void Widget::setRelayoutBoundary(bool b) {
    m_explicitBoundary = b;
}
```

Auto-detect handles the common case — FwButton, FwToggle, FwKnob,
FwFader all have fixed size policies and automatically stop
invalidation.

Opt-in covers:
- Scroll containers: internal content grows/shrinks but the
  container's own size is parent-driven.
- Panel roots: external window resize changes them; internal
  changes shouldn't propagate.
- Custom containers that enforce their own size stability.

### Verification

During development, the debug overlay flags widgets where
auto-detect is wrong — a widget that's marked as boundary but whose
`measure()` returns different sizes for different child states. This
indicates a bug: either SizePolicy is lying, or the widget needs
manual `setRelayoutBoundary(false)`.

---

## Invalidation

Two levels: **local** and **global**.

### Local invalidation

A widget's own content has changed in a way that might affect its
size.

```cpp
void Widget::invalidate() {
    m_measureCache.localVersion++;
    m_layoutCache.measureVersion = -1;   // force re-layout next pass

    // Bubble up — stops at first relayout boundary.
    for (Widget* p = m_parent; p; p = p->m_parent) {
        if (p->isRelayoutBoundary()) break;
        p->m_measureCache.localVersion++;
        p->m_layoutCache.measureVersion = -1;
    }
}
```

Called by:
- `setText` / `setLabel` (affects text width)
- `setValue` on a widget that displays value as text
- `addChild` / `removeChild` / child reorder
- `setPadding` / `setSizePolicy` / `setMinWidth` etc.
- Any state change whose visual affects geometry (bold vs regular
  font weight, etc.)
- Each widget spec's "Measure-invalidating" triggers.

### Global invalidation

Something framework-wide has changed; all measure caches are stale.

```cpp
void UIContext::bumpEpoch() {
    m_epoch++;
}
```

Bumped on:
- DPI change (`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`)
- Window resize
- Theme palette or metrics swap
- Font file reload

Global bump is O(1) — no tree walk. The next `measure` call on any
widget sees `globalEpoch != ctx.epoch()` and invalidates.

### Cascading invalidation

Invalidation bubbles up the tree:

```
    InvalidatedWidget           (bump localVersion + clear layout)
          │
          ▼
    Parent (not boundary)       (bump localVersion + clear layout)
          │
          ▼
    Grandparent (boundary)      (stops here)
```

Next `measure` call on any ancestor before the boundary invalidates;
boundaries and everything above are unaffected.

---

## Animation integration

A panel animating size (collapse, resize) is the worst-case
scenario — its bounds change every frame for ~200 ms. Naïvely this
would force re-layout every frame, invalidating all cached children
layouts.

### The pattern

The animation driver measures **once at start** and **once at end**,
then lerps bounds between the two cached layouts for intermediate
frames:

```cpp
void Panel::tick(float dtSec) {
    if (m_animating) {
        m_animProgress += dtSec / m_animDuration;
        float t = easeOut(std::min(m_animProgress, 1.0f));
        Rect r = lerpRect(m_animStartBounds, m_animEndBounds, t);
        // This layout call matches cached bounds? No — bounds
        // changes per frame. But children's MEASURE cache stays
        // valid because we're not asking them to re-measure.
        layout(r, ctx);
    }
}
```

The trick: `layout()` with different bounds invalidates the layout
cache but NOT the measure cache. Children get laid out in new
positions, but their measured sizes (and thus their own children's
measure caches) stay valid.

For performance, animating widgets should be relayout boundaries —
otherwise their parent's measure cache gets bumped every frame too.
Panels typically are boundaries (window-driven size), so this works
cleanly.

### Alternatives

- **Lerp bounds directly without re-measure**: widgets that can
  compute layout from parent bounds only (FlexBox, Stack) handle
  this naturally. Widgets with content-driven internal layout may
  need re-measure on large bound changes.
- **Clamp animation frequency**: if performance is marginal,
  reduce animation framerate (lerp only every other frame).

---

## Performance targets

| Workload | Target (on a mid-range machine) |
|---|---|
| Idle UI (no widgets changed) | < 0.1 ms per frame on measure+layout |
| Scroll (only offset changed) | < 0.3 ms per frame (paint culling dominates) |
| Single widget invalidation | < 0.1 ms for the one widget's re-measure |
| Panel animation (no measure change) | < 0.5 ms per frame |
| DPI change | < 20 ms one-time cost (full widget re-measure) |
| Theme swap | < 5 ms one-time (no measure changes typically) |

Measured via the debug overlay's per-frame timing.

---

## Debug overlay

Key binding `Ctrl+Shift+D` toggles a diagnostic overlay rendered
over the main UI. Shows:

### Per-widget annotations

Widgets colorized by cache state:
- **Green** — measure + layout served from cache.
- **Yellow** — measure hit, layout missed this frame.
- **Orange** — measure missed, layout consequently missed.
- **Red** — relayout boundary propagation broken (boundary marked,
  but measure result differs between successive frames — indicates
  a bug).
- **Magenta** — widget painted but not in any hit-test tree
  (orphan paint).

### Per-widget text overlay

When hovering a widget:
- Name (from `setName`) + class type.
- Bounds (x, y, w, h).
- Local version + last constraints seen.
- Count of re-measures this frame / total.

### Aggregate stats

Top-right corner shows:
- Frame time breakdown: measure / layout / paint / hit-test ms.
- Widgets in tree: total.
- Widgets re-measured this frame.
- Widgets re-laid-out this frame.
- Layer stack: count per layer.

### Trace mode

Right-click the debug overlay to open trace controls:
- "Dump widget tree" — writes tree + cache state to a file.
- "Record measure/layout calls" — logs next N frames of measure/
  layout calls for profiling.

---

## Common pitfalls

1. **Forgetting to invalidate after setter.** Widget authors using
   provided helpers (`setLabel`, `setPadding`, etc.) are fine.
   Custom setters must call `invalidate()` explicitly if they change
   size-affecting state.

2. **Measuring with different constraints in a loop.** Some
   containers (adaptive grids) might measure children twice with
   different constraints to decide final layout. Each measure is a
   cache miss → O(N²) worst case. If identified, use a dedicated
   size-negotiation pass or relax to single-pass.

3. **Relayout boundary set but measure isn't stable.** A widget
   flagged as boundary whose `onMeasure` returns different values
   for different internal states lies to the cache. Debug overlay's
   red flag catches this; unit tests should verify.

4. **Animations triggering measure cascades.** Forgot to mark the
   animating widget as a boundary → every frame invalidates its
   parent's measure. Symptom: debug overlay shows parent re-measuring
   every frame during animation.

5. **Unbounded constraints reaching content-intrinsic widgets.**
   A Label inside an unbounded-horizontal FlexBox returns full text
   width with no wrap. If that's unintended, wrap in a container
   that provides a max-width.

---

## Testing the cache

Unit tests in `tests/test_fw2_MeasureLayoutCache.cpp`:

1. `MeasureFirstCallComputes` — first measure() calls onMeasure.
2. `MeasureSecondCallCached` — second measure() with same
   constraints returns cached, doesn't call onMeasure.
3. `DifferentConstraintsRecompute` — changing constraints triggers
   re-measure.
4. `InvalidateBumpsVersion` — invalidate() increments localVersion.
5. `InvalidateBubblesThroughNonBoundary` — invalidate on a grandchild
   bumps localVersion on parent (not boundary) and grandparent
   (not boundary).
6. `InvalidateStopsAtBoundary` — invalidate() doesn't bubble past
   a boundary widget.
7. `GlobalEpochBumpInvalidatesAll` — ctx.bumpEpoch() makes all
   measure caches miss.
8. `LayoutCacheByBounds` — layout(sameRect) reuses cache.
9. `LayoutCacheByMeasureVersion` — measure change bumps layout
   cache too.
10. `AutoDetectBoundaryForFixedSize` — widget with fixed size
    policy auto-becomes boundary.
11. `OptInBoundary` — setRelayoutBoundary(true) forces boundary.
12. `AnimationOnlyLayoutInvalidates` — animating bounds invalidates
    layout but not measure.
13. `RepeatedMeasureWithSameInputs` — measurable widget's
    onMeasure count stays at 1 across many measure() calls with
    same constraints.
14. `AncestorUnaffectedByBoundary` — parent of a boundary widget
    doesn't re-measure when boundary's child invalidates.

---

## Open questions

1. **Multi-slot measure cache?** Rare containers (adaptive grid)
   alternate between two constraint values. Current single-slot
   cache causes re-measure on each alternation. Observed impact:
   small. Defer multi-slot until measured perf issue.

2. **Parallel measure across siblings?** Measuring is pure;
   theoretically siblings could be measured in parallel threads.
   Complexity vs payoff: not obviously worth it for desktop UI
   scales we hit. Defer.

3. **Incremental re-layout for large widget trees?** Breaking
   up a big re-layout into multiple frames to avoid a >16 ms hit
   during DPI change. Would require layout to be interruptible.
   Defer — DPI change is rare and 20 ms is acceptable.

4. **Cache statistics in production?** Hit rate would be useful to
   monitor in real-world use. Could be behind a developer-mode flag.

5. **Constraint coercion on invalid input?** Constraints with
   `minW > maxW` are currently UB. Assert in debug, clamp in
   release? Currently assert.
