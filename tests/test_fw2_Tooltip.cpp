// UI v2 — TooltipManager tests.
//
// Exercises the state machine without a renderer:
//   • attach / detach / isAttached / clearAll
//   • pointer enter / leave drives m_hover correctly
//   • tick() waits out the show delay before pushing an overlay
//   • entering a second widget resets the timer and hides the current
//   • detaching the hovered widget hides immediately
//   • widget destruction auto-detaches (Widget::~Widget hook)
//
// Uses a real LayerStack on a fresh UIContext so we can observe
// entryCount transitions. No painter is installed, so the overlay's
// paint closure is a no-op — fine for state-only tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Widget.h"

using namespace yawn::ui::fw2;

namespace {
// Minimal concrete Widget for tests — visible, zero onMeasure, manual
// layout bounds.
class DummyWidget : public Widget {
public:
    DummyWidget() = default;
    // Publicly expose layout() to tests (base class already public).
protected:
    Size onMeasure(Constraints c, UIContext&) override {
        return {c.maxW, c.maxH};
    }
};
} // anon

class TooltipHarness : public ::testing::Test {
protected:
    void SetUp() override {
        ctx.layerStack = &stack;
        ctx.viewport   = {0, 0, 1024, 768};
        UIContext::setGlobal(&ctx);
        // Clean any residue from previous tests (manager is a global
        // singleton — tests in the same process share state).
        TooltipManager::instance().clearAll();
        TooltipManager::instance().setShowDelay(0.5f);
    }
    void TearDown() override {
        TooltipManager::instance().clearAll();
        UIContext::setGlobal(nullptr);
    }
    UIContext  ctx;
    LayerStack stack;
};

// ─── Registration ──────────────────────────────────────────────────

TEST_F(TooltipHarness, AttachRegisters) {
    DummyWidget w;
    EXPECT_FALSE(TooltipManager::instance().isAttached(&w));
    Tooltip::attach(&w, "Hi");
    EXPECT_TRUE(TooltipManager::instance().isAttached(&w));
}

TEST_F(TooltipHarness, AttachEmptyTextDetaches) {
    DummyWidget w;
    Tooltip::attach(&w, "Hi");
    Tooltip::attach(&w, "");   // spec: empty == detach
    EXPECT_FALSE(TooltipManager::instance().isAttached(&w));
}

TEST_F(TooltipHarness, DetachRemoves) {
    DummyWidget w;
    Tooltip::attach(&w, "Hi");
    Tooltip::detach(&w);
    EXPECT_FALSE(TooltipManager::instance().isAttached(&w));
}

TEST_F(TooltipHarness, ClearAllEmpties) {
    DummyWidget a, b;
    Tooltip::attach(&a, "A");
    Tooltip::attach(&b, "B");
    TooltipManager::instance().clearAll();
    EXPECT_FALSE(TooltipManager::instance().isAttached(&a));
    EXPECT_FALSE(TooltipManager::instance().isAttached(&b));
}

// ─── Pointer tracking ──────────────────────────────────────────────

TEST_F(TooltipHarness, PointerEnterSetsHover) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hover me");

    TooltipManager::instance().onPointerMoved(50, 30);
    EXPECT_EQ(TooltipManager::instance().currentHover(), &w);
}

TEST_F(TooltipHarness, PointerLeavingClearsHover) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hover me");

    TooltipManager::instance().onPointerMoved(50, 30);
    ASSERT_EQ(TooltipManager::instance().currentHover(), &w);
    TooltipManager::instance().onPointerMoved(500, 500);   // elsewhere
    EXPECT_EQ(TooltipManager::instance().currentHover(), nullptr);
}

TEST_F(TooltipHarness, PointerLeftWindow) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hover me");

    TooltipManager::instance().onPointerMoved(50, 30);
    TooltipManager::instance().onPointerLeft();
    EXPECT_EQ(TooltipManager::instance().currentHover(), nullptr);
    EXPECT_FALSE(TooltipManager::instance().isVisible());
}

TEST_F(TooltipHarness, UnattachedWidgetsDoNotRegisterHover) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    // No attach().

    TooltipManager::instance().onPointerMoved(50, 30);
    EXPECT_EQ(TooltipManager::instance().currentHover(), nullptr);
}

// ─── Show delay ────────────────────────────────────────────────────

TEST_F(TooltipHarness, TickBeforeDelayDoesNotShow) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hi");
    TooltipManager::instance().onPointerMoved(50, 30);

    TooltipManager::instance().tick(0.1f);
    EXPECT_FALSE(TooltipManager::instance().isVisible());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Tooltip), 0);
}

TEST_F(TooltipHarness, TickPastDelayShowsOverlay) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hi");
    TooltipManager::instance().onPointerMoved(50, 30);

    TooltipManager::instance().tick(0.6f);   // > 0.5s delay
    EXPECT_TRUE(TooltipManager::instance().isVisible());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Tooltip), 1);
    EXPECT_EQ(TooltipManager::instance().currentText(), "Hi");
}

TEST_F(TooltipHarness, TickAccumulatesAcrossCalls) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hi");
    TooltipManager::instance().onPointerMoved(50, 30);

    // 4 × 0.1s = 0.4s — clearly under the 0.5s show delay.
    for (int i = 0; i < 4; ++i) TooltipManager::instance().tick(0.1f);
    EXPECT_FALSE(TooltipManager::instance().isVisible());
    // One more tick pushes us well past the threshold.
    TooltipManager::instance().tick(0.11f);
    EXPECT_TRUE(TooltipManager::instance().isVisible());
}

TEST_F(TooltipHarness, MovingToAnotherAttachedWidgetResetsTimer) {
    DummyWidget a, b;
    a.layout(Rect{10, 10, 100, 40}, ctx);
    b.layout(Rect{200, 10, 100, 40}, ctx);
    Tooltip::attach(&a, "A");
    Tooltip::attach(&b, "B");

    TooltipManager::instance().onPointerMoved(50, 30);
    TooltipManager::instance().tick(0.6f);
    ASSERT_TRUE(TooltipManager::instance().isVisible());
    EXPECT_EQ(TooltipManager::instance().currentText(), "A");

    // Move to widget B.
    TooltipManager::instance().onPointerMoved(250, 30);
    EXPECT_FALSE(TooltipManager::instance().isVisible());  // hidden on transition
    EXPECT_EQ(TooltipManager::instance().currentHover(), &b);

    // New delay starts.
    TooltipManager::instance().tick(0.3f);
    EXPECT_FALSE(TooltipManager::instance().isVisible());
    TooltipManager::instance().tick(0.3f);
    EXPECT_TRUE(TooltipManager::instance().isVisible());
    EXPECT_EQ(TooltipManager::instance().currentText(), "B");
}

// ─── Dismissal ─────────────────────────────────────────────────────

TEST_F(TooltipHarness, PointerLeaveWhileVisibleHides) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hi");
    TooltipManager::instance().onPointerMoved(50, 30);
    TooltipManager::instance().tick(0.6f);
    ASSERT_TRUE(TooltipManager::instance().isVisible());

    TooltipManager::instance().onPointerMoved(500, 500);
    EXPECT_FALSE(TooltipManager::instance().isVisible());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Tooltip), 0);
}

TEST_F(TooltipHarness, DetachingHoveredWidgetHides) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hi");
    TooltipManager::instance().onPointerMoved(50, 30);
    TooltipManager::instance().tick(0.6f);
    ASSERT_TRUE(TooltipManager::instance().isVisible());

    Tooltip::detach(&w);
    EXPECT_FALSE(TooltipManager::instance().isVisible());
    EXPECT_EQ(TooltipManager::instance().currentHover(), nullptr);
}

TEST_F(TooltipHarness, InvisibleWidgetNoLongerRegistersHover) {
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, ctx);
    Tooltip::attach(&w, "Hi");
    w.setVisible(false);

    TooltipManager::instance().onPointerMoved(50, 30);
    EXPECT_EQ(TooltipManager::instance().currentHover(), nullptr);
}

// ─── Lifetime — Widget::~Widget auto-detach ────────────────────────

TEST_F(TooltipHarness, WidgetDestructorAutoDetaches) {
    {
        DummyWidget w;
        w.layout(Rect{10, 10, 100, 40}, ctx);
        Tooltip::attach(&w, "Hi");
        ASSERT_TRUE(TooltipManager::instance().isAttached(&w));
    }
    // w destroyed — manager must have dropped the pointer cleanly.
    // We don't have a raw iterate API to confirm by pointer, but we
    // CAN confirm the hash didn't retain a dangling key by exercising
    // onPointerMoved: it should silently no-op, not crash.
    TooltipManager::instance().onPointerMoved(50, 30);
    EXPECT_EQ(TooltipManager::instance().currentHover(), nullptr);
}

TEST_F(TooltipHarness, DestructorAutoDetachWhileVisible) {
    // Most interesting lifetime case: the hovered + visible widget
    // gets destroyed. Must hide cleanly and forget the pointer.
    {
        DummyWidget w;
        w.layout(Rect{10, 10, 100, 40}, ctx);
        Tooltip::attach(&w, "Hi");
        TooltipManager::instance().onPointerMoved(50, 30);
        TooltipManager::instance().tick(0.6f);
        ASSERT_TRUE(TooltipManager::instance().isVisible());
    }
    EXPECT_FALSE(TooltipManager::instance().isVisible());
    EXPECT_EQ(TooltipManager::instance().currentHover(), nullptr);
    EXPECT_EQ(stack.entryCount(OverlayLayer::Tooltip), 0);
}

// ─── Positioning ──────────────────────────────────────────────────

TEST_F(TooltipHarness, BubblePositionsBelowByDefault) {
    DummyWidget w;
    w.layout(Rect{100, 100, 80, 20}, ctx);
    Tooltip::attach(&w, "Hi");

    TooltipManager::instance().onPointerMoved(140, 110);
    TooltipManager::instance().tick(0.6f);
    ASSERT_TRUE(TooltipManager::instance().isVisible());
    const Rect& b = TooltipManager::instance().currentBounds();
    EXPECT_GT(b.y, 100.0f + 20.0f);   // below the widget
}

TEST_F(TooltipHarness, BubbleFlipsAboveWhenNoRoomBelow) {
    DummyWidget w;
    // Widget at bottom of viewport (768 high).
    w.layout(Rect{100, 750, 80, 15}, ctx);
    Tooltip::attach(&w, "Hi");

    TooltipManager::instance().onPointerMoved(140, 757);
    TooltipManager::instance().tick(0.6f);
    ASSERT_TRUE(TooltipManager::instance().isVisible());
    const Rect& b = TooltipManager::instance().currentBounds();
    EXPECT_LT(b.y + b.h, 750.0f + 0.01f);
}

TEST_F(TooltipHarness, BubbleClampsToViewport) {
    // Long text that would overflow if centred on a narrow anchor.
    DummyWidget w;
    w.layout(Rect{950, 100, 20, 20}, ctx);
    Tooltip::attach(&w, "A very long tooltip string indeed");

    TooltipManager::instance().onPointerMoved(960, 110);
    TooltipManager::instance().tick(0.6f);
    ASSERT_TRUE(TooltipManager::instance().isVisible());
    const Rect& b = TooltipManager::instance().currentBounds();
    EXPECT_LE(b.x + b.w, 1024.0f + 0.01f);  // doesn't poke past right edge
}

// ─── Edge case: no LayerStack ──────────────────────────────────────

TEST_F(TooltipHarness, NoLayerStackNoShow) {
    UIContext bare;   // no layerStack wired
    UIContext::setGlobal(&bare);
    DummyWidget w;
    w.layout(Rect{10, 10, 100, 40}, bare);
    Tooltip::attach(&w, "Hi");

    TooltipManager::instance().onPointerMoved(50, 30);
    TooltipManager::instance().tick(1.0f);
    EXPECT_FALSE(TooltipManager::instance().isVisible());   // silent no-op

    UIContext::setGlobal(&ctx);
}
