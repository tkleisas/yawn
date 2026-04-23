// UI v2 — ContentGrid divider drag + child event forwarding tests.
//
// Regression coverage for the 2×2 grid:
//   - Clicking a divider marks it "dragging" on mouseDown.
//   - A mouseMove during drag resizes the children immediately
//     (reflowChildren) — not on some later frame, and not skipped by
//     the fw2 layout cache.
//   - A captured child receives subsequent mouse moves + mouseUp
//     forwarded by the grid (scrollbar / fader drag keep working even
//     though the child isn't in a v2 parent tree above the grid).

#include <gtest/gtest.h>

// NB: ContentGridWrapper (v1) transitively pulls in heavy UI / SDL
// includes that the tests target doesn't link against, so these tests
// exercise the fw2 ContentGrid directly rather than via the wrapper.
// The regression bug was in App.cpp dispatch (unconditional browser
// panel consume) — not in ContentGrid itself — so direct-dispatch
// coverage is what protects against its re-introduction.

#include "ui/framework/v2/ContentGrid.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Widget.h"

using namespace yawn::ui::fw2;

namespace {

// Minimal fw2 widget that records the bounds handed to it in onLayout
// and counts mouseMove / mouseUp dispatches.
class RecordingChild : public Widget {
public:
    Rect lastBounds{};
    int  layoutCalls = 0;
    int  mouseMoves  = 0;
    int  mouseUps    = 0;

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }
    void onLayout(Rect bounds, UIContext&) override {
        lastBounds = bounds;
        ++layoutCalls;
    }
    bool onMouseMove(MouseMoveEvent&) override { ++mouseMoves; return true; }
    bool onMouseUp(MouseEvent&) override       { ++mouseUps;   return true; }
};

class Harness {
public:
    Harness() { UIContext::setGlobal(&ctx); }
    ~Harness() { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

} // namespace

// ─── Divider drag ────────────────────────────────────────────────────────

// Lays the grid out at 1000x800; with default ratios 0.70/0.60 the
// vertical divider sits at x = 700 and the horizontal divider at y = 480
// (plus the 4px divider). A horizontal drag from x=700 → x=800 should
// widen the left column and narrow the right column.
TEST(Fw2ContentGrid, VerticalDividerDragResizesColumns) {
    Harness h;
    RecordingChild tl, tr, bl, br;

    ContentGrid grid;
    grid.setChildren(&tl, &tr, &bl, &br);
    grid.measure(Constraints::tight(1000, 800), h.ctx);
    grid.layout(Rect{0, 0, 1000, 800}, h.ctx);

    const float vDivX0 = tl.lastBounds.w;      // left column width after first layout
    const float topH0  = tl.lastBounds.h;

    // Click exactly on the vertical divider (within the 6px hit zone).
    MouseEvent dn{};
    dn.x = vDivX0 + 1;
    dn.y = topH0 * 0.5f;
    dn.button = MouseButton::Left;
    ASSERT_TRUE(grid.dispatchMouseDown(dn));
    ASSERT_TRUE(grid.wantsHorizontalResize());  // m_dragV is true

    // Move 100px to the right. The left column should widen, the right
    // column should narrow, and the new bounds must hit the children
    // *immediately* — not on a later frame.
    MouseMoveEvent mv{};
    mv.x = dn.x + 100;
    mv.y = dn.y;
    ASSERT_TRUE(grid.dispatchMouseMove(mv));

    EXPECT_GT(tl.lastBounds.w, vDivX0)
        << "Top-left should have widened after vertical divider drag.";
    EXPECT_LT(tr.lastBounds.w, 1000 - vDivX0)
        << "Top-right should have narrowed after vertical divider drag.";
    // Heights unchanged.
    EXPECT_FLOAT_EQ(tl.lastBounds.h, topH0);
    EXPECT_FLOAT_EQ(tr.lastBounds.h, topH0);

    MouseEvent up{};
    up.x = mv.x; up.y = mv.y;
    grid.dispatchMouseUp(up);
    EXPECT_FALSE(grid.wantsHorizontalResize());
    EXPECT_FALSE(grid.isDraggingDivider());
}

TEST(Fw2ContentGrid, HorizontalDividerDragResizesRows) {
    Harness h;
    RecordingChild tl, tr, bl, br;

    ContentGrid grid;
    grid.setChildren(&tl, &tr, &bl, &br);
    grid.measure(Constraints::tight(1000, 800), h.ctx);
    grid.layout(Rect{0, 0, 1000, 800}, h.ctx);

    const float hDivY0 = tl.lastBounds.h;      // top row height
    const float leftW0 = tl.lastBounds.w;

    MouseEvent dn{};
    dn.x = leftW0 * 0.5f;
    dn.y = hDivY0 + 1;
    dn.button = MouseButton::Left;
    ASSERT_TRUE(grid.dispatchMouseDown(dn));
    ASSERT_TRUE(grid.wantsVerticalResize());

    MouseMoveEvent mv{};
    mv.x = dn.x;
    mv.y = dn.y + 80;
    ASSERT_TRUE(grid.dispatchMouseMove(mv));

    EXPECT_GT(tl.lastBounds.h, hDivY0);
    EXPECT_LT(bl.lastBounds.h, 800 - hDivY0);
    EXPECT_FLOAT_EQ(tl.lastBounds.w, leftW0);
    EXPECT_FLOAT_EQ(bl.lastBounds.w, leftW0);
}

// ─── Captured-child event forwarding ─────────────────────────────────────

TEST(Fw2ContentGrid, MouseMoveForwardsToCapturedChild) {
    Harness h;
    RecordingChild tl, tr, bl, br;

    ContentGrid grid;
    grid.setChildren(&tl, &tr, &bl, &br);
    grid.measure(Constraints::tight(1000, 800), h.ctx);
    grid.layout(Rect{0, 0, 1000, 800}, h.ctx);

    // Pretend TL captured (e.g. scrollbar thumb drag). The grid must route
    // subsequent moves to it even though no divider drag is active.
    tl.captureMouse();

    MouseMoveEvent mv{};
    mv.x = 400; mv.y = 400;
    grid.dispatchMouseMove(mv);

    EXPECT_GE(tl.mouseMoves, 1)
        << "Captured child should receive the mouseMove forwarded by the grid.";
    EXPECT_EQ(tr.mouseMoves, 0);
    EXPECT_EQ(bl.mouseMoves, 0);
    EXPECT_EQ(br.mouseMoves, 0);

    // Release capture + deliver mouseUp.
    tl.releaseMouse();
    // Re-capture so grid's onMouseUp can forward the event to the right widget.
    tl.captureMouse();
    MouseEvent up{};
    up.x = 400; up.y = 400;
    grid.dispatchMouseUp(up);
    EXPECT_GE(tl.mouseUps, 1);

    tl.releaseMouse();
}

// Regression: a fader / knob / button *inside* one of the four panels
// is not one of m_tl..m_br, but its bounds are visually inside the
// grid. Forwarding must still reach it — otherwise mixer faders and
// return-bus controls stop responding to drags.
TEST(Fw2ContentGrid, MouseMoveForwardsToDeepDescendantByBounds) {
    Harness h;
    RecordingChild tl, tr, bl, br;

    ContentGrid grid;
    grid.setChildren(&tl, &tr, &bl, &br);
    grid.measure(Constraints::tight(1000, 800), h.ctx);
    grid.layout(Rect{0, 0, 1000, 800}, h.ctx);

    // Simulate a control living inside bl (mixer fader, say). It is
    // not one of the grid's direct children but its bounds are inside
    // the grid.
    RecordingChild innerFader;
    innerFader.layout(Rect{20, 550, 30, 120}, h.ctx);

    innerFader.captureMouse();

    MouseMoveEvent mv{};
    mv.x = 35; mv.y = 600;
    grid.dispatchMouseMove(mv);

    EXPECT_GE(innerFader.mouseMoves, 1)
        << "Grid must forward moves to captured descendants, not just direct children.";

    // A captured widget *outside* the grid's area should NOT receive
    // forwarded moves — otherwise the transport panel's dragged knob
    // would get double-dispatched.
    innerFader.releaseMouse();
    RecordingChild outside;
    outside.layout(Rect{100, -200, 30, 20}, h.ctx);   // above grid (y < 0)
    outside.captureMouse();

    int before = outside.mouseMoves;
    grid.dispatchMouseMove(mv);
    EXPECT_EQ(outside.mouseMoves, before)
        << "Widgets outside grid bounds should NOT receive grid-forwarded moves.";

    outside.releaseMouse();
}

// When no divider drag is in progress and no child is captured, an arbitrary
// mouseMove shouldn't reach any child — only hover state updates.
TEST(Fw2ContentGrid, MouseMoveWithoutDragOrCaptureIsInert) {
    Harness h;
    RecordingChild tl, tr, bl, br;

    ContentGrid grid;
    grid.setChildren(&tl, &tr, &bl, &br);
    grid.measure(Constraints::tight(1000, 800), h.ctx);
    grid.layout(Rect{0, 0, 1000, 800}, h.ctx);

    MouseMoveEvent mv{};
    mv.x = 400; mv.y = 400;
    grid.dispatchMouseMove(mv);

    EXPECT_EQ(tl.mouseMoves, 0);
    EXPECT_EQ(tr.mouseMoves, 0);
    EXPECT_EQ(bl.mouseMoves, 0);
    EXPECT_EQ(br.mouseMoves, 0);
}

