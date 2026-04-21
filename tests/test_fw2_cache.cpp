// UI v2 — Measure / Layout cache correctness tests.
//
// Verifies the contract in docs/ui-v2-measure-layout.md:
//   - single-slot measure cache (constraints + version + epoch)
//   - single-slot layout cache (bounds + version + epoch)
//   - invalidate() bubbles through non-boundary ancestors
//   - invalidate() stops at relayout boundaries
//   - global epoch bump invalidates all caches
//   - auto-detected boundaries (flexWeight=0) work
//   - opt-in boundaries override auto-detect

#include <gtest/gtest.h>

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

// Test widget with counter of onMeasure calls and manual size override.
class CountingWidget : public Widget {
public:
    Size preferred{50, 30};
    int  measureCalls = 0;

    Size onMeasure(Constraints c, UIContext&) override {
        ++measureCalls;
        return c.constrain(preferred);
    }
};

class Harness {
public:
    Harness() { UIContext::setGlobal(&ctx); }
    ~Harness() { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

// ─── Basic cache behavior ─────────────────────────────────────────────

TEST(Fw2Cache, MeasureFirstCallComputes) {
    Harness h;
    CountingWidget w;
    w.measure(Constraints::loose(100, 100), h.ctx);
    EXPECT_EQ(w.measureCalls, 1);
}

TEST(Fw2Cache, MeasureSecondCallCached) {
    Harness h;
    CountingWidget w;
    w.measure(Constraints::loose(100, 100), h.ctx);
    w.measure(Constraints::loose(100, 100), h.ctx);
    EXPECT_EQ(w.measureCalls, 1);
}

TEST(Fw2Cache, DifferentConstraintsRecompute) {
    Harness h;
    CountingWidget w;
    w.measure(Constraints::loose(100, 100), h.ctx);
    w.measure(Constraints::loose(200, 100), h.ctx);
    EXPECT_EQ(w.measureCalls, 2);
}

TEST(Fw2Cache, RepeatedMeasureWithSameInputs) {
    // Even 100 calls with identical state → one onMeasure call.
    Harness h;
    CountingWidget w;
    for (int i = 0; i < 100; ++i) {
        w.measure(Constraints::loose(200, 200), h.ctx);
    }
    EXPECT_EQ(w.measureCalls, 1);
}

// ─── Invalidation ─────────────────────────────────────────────────────

TEST(Fw2Cache, InvalidateBumpsVersion) {
    Harness h;
    CountingWidget w;
    w.measure(Constraints::loose(100, 100), h.ctx);
    const int v1 = w.measureVersion();
    w.invalidate();
    EXPECT_GT(w.measureVersion(), v1);
    // Next measure should recompute.
    w.measure(Constraints::loose(100, 100), h.ctx);
    EXPECT_EQ(w.measureCalls, 2);
}

TEST(Fw2Cache, InvalidateBubblesThroughNonBoundary) {
    Harness h;
    // Parent is a non-boundary: flexWeight > 0.
    CountingWidget parent;
    parent.setSizePolicy(SizePolicy::flex(1.0f));
    parent.setRelayoutBoundary(false);   // explicitly not a boundary

    CountingWidget child;
    child.setSizePolicy(SizePolicy::flex(1.0f));
    child.setRelayoutBoundary(false);

    parent.addChild(&child);

    parent.measure(Constraints::loose(100, 100), h.ctx);
    child.measure(Constraints::loose(100, 100), h.ctx);
    const int parentVBefore = parent.measureVersion();

    child.invalidate();   // should bubble up

    EXPECT_GT(parent.measureVersion(), parentVBefore);
}

TEST(Fw2Cache, InvalidateStopsAtBoundary) {
    Harness h;
    CountingWidget parent;
    parent.setSizePolicy(SizePolicy::flex(1.0f));
    parent.setRelayoutBoundary(false);

    // Middle widget is a boundary.
    CountingWidget boundary;
    boundary.setRelayoutBoundary(true);

    CountingWidget grandchild;
    grandchild.setSizePolicy(SizePolicy::flex(1.0f));
    grandchild.setRelayoutBoundary(false);

    parent.addChild(&boundary);
    boundary.addChild(&grandchild);

    parent.measure(Constraints::loose(100, 100), h.ctx);
    boundary.measure(Constraints::loose(100, 100), h.ctx);
    grandchild.measure(Constraints::loose(100, 100), h.ctx);
    const int parentVBefore = parent.measureVersion();

    grandchild.invalidate();   // should reach boundary but NOT parent

    EXPECT_EQ(parent.measureVersion(), parentVBefore);
    // Boundary's own version DOES bump (grandchild.invalidate first bumps
    // grandchild, then bubbles one step to boundary). Implementation
    // note: the bubble reads "bubble up until hits boundary, then stop";
    // the boundary itself is the last hop that gets bumped.
}

// ─── Global epoch ─────────────────────────────────────────────────────

TEST(Fw2Cache, GlobalEpochBumpInvalidatesAll) {
    Harness h;
    CountingWidget w;
    w.measure(Constraints::loose(100, 100), h.ctx);
    EXPECT_EQ(w.measureCalls, 1);

    h.ctx.bumpEpoch();
    w.measure(Constraints::loose(100, 100), h.ctx);
    EXPECT_EQ(w.measureCalls, 2);
}

// ─── Layout cache ─────────────────────────────────────────────────────

TEST(Fw2Cache, LayoutCacheByBounds) {
    // Repeated layout with same bounds skips work (same onLayout would
    // be called once). We measure via measureCalls; layout caching can
    // be inferred by checking whether bounds were stored without
    // redundant re-layout — since base onLayout is a no-op we use a
    // subclass that counts.

    Harness h;
    class LayoutCountingWidget : public Widget {
    public:
        int layoutCalls = 0;
        Size onMeasure(Constraints c, UIContext&) override {
            return c.constrain({50, 30});
        }
        void onLayout(Rect, UIContext&) override {
            ++layoutCalls;
        }
    } w;

    w.measure(Constraints::loose(100, 100), h.ctx);
    w.layout(Rect{0, 0, 50, 30}, h.ctx);
    w.layout(Rect{0, 0, 50, 30}, h.ctx);
    EXPECT_EQ(w.layoutCalls, 1);
}

TEST(Fw2Cache, DifferentBoundsRelayout) {
    Harness h;
    class LayoutCountingWidget : public Widget {
    public:
        int layoutCalls = 0;
        Size onMeasure(Constraints c, UIContext&) override {
            return c.constrain({50, 30});
        }
        void onLayout(Rect, UIContext&) override { ++layoutCalls; }
    } w;

    w.measure(Constraints::loose(100, 100), h.ctx);
    w.layout(Rect{0, 0, 50, 30}, h.ctx);
    w.layout(Rect{10, 10, 50, 30}, h.ctx);
    EXPECT_EQ(w.layoutCalls, 2);
}

// ─── Auto-detect boundary ─────────────────────────────────────────────

TEST(Fw2Cache, AutoDetectBoundaryForFixedSize) {
    // Widget with default SizePolicy (flexWeight=0) auto-becomes a
    // boundary without calling setRelayoutBoundary.
    CountingWidget w;
    // Default SizePolicy has flexWeight=0 → isRelayoutBoundary should
    // return true without an explicit override.
    EXPECT_EQ(w.sizePolicy().flexWeight, 0.0f);
    EXPECT_TRUE(w.isRelayoutBoundary());
}

TEST(Fw2Cache, FlexWidgetNotBoundaryByDefault) {
    CountingWidget w;
    w.setSizePolicy(SizePolicy::flex(1.0f));
    EXPECT_FALSE(w.isRelayoutBoundary());
}

TEST(Fw2Cache, OptInBoundary) {
    CountingWidget w;
    w.setSizePolicy(SizePolicy::flex(1.0f));
    EXPECT_FALSE(w.isRelayoutBoundary());
    w.setRelayoutBoundary(true);
    EXPECT_TRUE(w.isRelayoutBoundary());
}

TEST(Fw2Cache, OptOutBoundary) {
    CountingWidget w;   // would auto be a boundary
    EXPECT_TRUE(w.isRelayoutBoundary());
    w.setRelayoutBoundary(false);
    EXPECT_FALSE(w.isRelayoutBoundary());
}

TEST(Fw2Cache, ClearOverrideRestoresAutoDetect) {
    CountingWidget w;
    w.setRelayoutBoundary(false);
    EXPECT_FALSE(w.isRelayoutBoundary());
    w.clearRelayoutBoundaryOverride();
    EXPECT_TRUE(w.isRelayoutBoundary());   // auto-detect kicks back in
}

// ─── Measure cache survives layout changes ───────────────────────────

TEST(Fw2Cache, LayoutBoundsChangeDoesNotInvalidateMeasure) {
    // Changing bounds (parent re-laying-out) re-runs onLayout but NOT
    // onMeasure.
    Harness h;
    class W : public Widget {
    public:
        int measureCalls = 0;
        int layoutCalls = 0;
        Size onMeasure(Constraints c, UIContext&) override {
            ++measureCalls;
            return c.constrain({50, 30});
        }
        void onLayout(Rect, UIContext&) override { ++layoutCalls; }
    } w;

    w.measure(Constraints::loose(100, 100), h.ctx);
    w.layout(Rect{0, 0, 50, 30}, h.ctx);
    // Now parent re-lays-out with different bounds but same measure.
    w.layout(Rect{5, 5, 50, 30}, h.ctx);
    EXPECT_EQ(w.measureCalls, 1);
    EXPECT_EQ(w.layoutCalls, 2);
}
