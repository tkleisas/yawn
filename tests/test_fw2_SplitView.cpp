// UI v2 — SplitView tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/SplitView.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

namespace {
class Pane : public Widget {
public:
    Size preferred{100, 100};
    Pane() = default;
    explicit Pane(Size p) : preferred(p) {}
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain(preferred);
    }
};
} // anon

class SplitViewHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

// ─── Basics ────────────────────────────────────────────────────────

TEST_F(SplitViewHarness, DefaultsToHorizontalRatioHalf) {
    SplitView sv;
    EXPECT_EQ(sv.orientation(), SplitOrientation::Horizontal);
    EXPECT_EQ(sv.splitMode(), SplitMode::Ratio);
    EXPECT_FLOAT_EQ(sv.dividerRatio(), 0.5f);
}

TEST_F(SplitViewHarness, EmptyMeasuresToConstraint) {
    SplitView sv;
    Size s = sv.measure(Constraints::loose(400, 200), ctx);
    EXPECT_FLOAT_EQ(s.w, 400);
    EXPECT_FLOAT_EQ(s.h, 200);
}

TEST_F(SplitViewHarness, PanesAreAddedAsChildren) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    // Replacing panes correctly unparents the old one.
    Pane c;
    sv.setFirst(&c);
    EXPECT_EQ(a.parent(), nullptr);
    EXPECT_EQ(c.parent(), &sv);
    EXPECT_EQ(b.parent(), &sv);
}

// ─── Layout: horizontal ratio ─────────────────────────────────────

TEST_F(SplitViewHarness, HorizontalRatioHalf) {
    SplitView sv(SplitOrientation::Horizontal);
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);

    sv.measure(Constraints::loose(406, 200), ctx);
    sv.layout(Rect{0, 0, 406, 200}, ctx);

    // Available = 400; ratio 0.5 → first = 200, second = 200.
    EXPECT_FLOAT_EQ(a.bounds().w, 200);
    EXPECT_FLOAT_EQ(b.bounds().w, 200);
    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(b.bounds().x, 206);
    // Divider rect.
    EXPECT_FLOAT_EQ(sv.dividerRect().x, 200);
    EXPECT_FLOAT_EQ(sv.dividerRect().w, 6);
}

TEST_F(SplitViewHarness, HorizontalRatioThirty) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setRatio(0.3f);

    sv.measure(Constraints::loose(406, 200), ctx);
    sv.layout(Rect{0, 0, 406, 200}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().w, 120);   // 0.3 * 400
    EXPECT_FLOAT_EQ(b.bounds().w, 280);
}

// ─── Layout: vertical ratio ────────────────────────────────────────

TEST_F(SplitViewHarness, VerticalRatioHalf) {
    SplitView sv(SplitOrientation::Vertical);
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);

    sv.measure(Constraints::loose(400, 206), ctx);
    sv.layout(Rect{0, 0, 400, 206}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().h, 100);
    EXPECT_FLOAT_EQ(b.bounds().h, 100);
    EXPECT_FLOAT_EQ(b.bounds().y, 106);
}

// ─── Fixed modes ──────────────────────────────────────────────────

TEST_F(SplitViewHarness, FixedStartMode) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setFirstPaneSize(80);
    EXPECT_EQ(sv.splitMode(), SplitMode::FixedStart);

    // 300 wide: first = 80, second = 300 - 6 - 80 = 214.
    sv.measure(Constraints::loose(300, 100), ctx);
    sv.layout(Rect{0, 0, 300, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().w, 80);
    EXPECT_FLOAT_EQ(b.bounds().w, 214);

    // Grow the SplitView: first stays 80, second absorbs delta.
    sv.measure(Constraints::loose(500, 100), ctx);
    sv.layout(Rect{0, 0, 500, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().w, 80);
    EXPECT_FLOAT_EQ(b.bounds().w, 500 - 6 - 80);
}

TEST_F(SplitViewHarness, FixedEndMode) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setSecondPaneSize(100);
    EXPECT_EQ(sv.splitMode(), SplitMode::FixedEnd);

    sv.measure(Constraints::loose(300, 100), ctx);
    sv.layout(Rect{0, 0, 300, 100}, ctx);
    EXPECT_FLOAT_EQ(b.bounds().w, 100);
    EXPECT_FLOAT_EQ(a.bounds().w, 300 - 6 - 100);

    // Grow: second stays 100, first absorbs delta.
    sv.measure(Constraints::loose(500, 100), ctx);
    sv.layout(Rect{0, 0, 500, 100}, ctx);
    EXPECT_FLOAT_EQ(b.bounds().w, 100);
    EXPECT_FLOAT_EQ(a.bounds().w, 500 - 6 - 100);
}

// ─── Constraints ──────────────────────────────────────────────────

TEST_F(SplitViewHarness, FirstMinEnforced) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setConstraints(SplitPane::First, {50, 9999});

    sv.setRatio(0.05f);   // would give ~15 px
    sv.measure(Constraints::loose(306, 100), ctx);
    sv.layout(Rect{0, 0, 306, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().w, 50);
}

TEST_F(SplitViewHarness, FirstMaxEnforced) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setConstraints(SplitPane::First, {0, 120});

    sv.setRatio(0.9f);   // would give ~270 px
    sv.measure(Constraints::loose(306, 100), ctx);
    sv.layout(Rect{0, 0, 306, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().w, 120);
}

TEST_F(SplitViewHarness, SecondMinEnforced) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setConstraints(SplitPane::Second, {80, 9999});

    sv.setRatio(0.95f);  // would crush second
    sv.measure(Constraints::loose(306, 100), ctx);
    sv.layout(Rect{0, 0, 306, 100}, ctx);
    EXPECT_FLOAT_EQ(b.bounds().w, 80);
    EXPECT_FLOAT_EQ(a.bounds().w, 300 - 80);
}

// ─── Dragging ─────────────────────────────────────────────────────

TEST_F(SplitViewHarness, DragMovesDivider) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setRatio(0.5f);
    sv.measure(Constraints::loose(406, 200), ctx);
    sv.layout(Rect{0, 0, 406, 200}, ctx);

    // Press on the divider (x between 200 and 206).
    MouseEvent down{};
    down.x = 203; down.y = 100;
    down.lx = 203; down.ly = 100;
    down.timestampMs = 100;
    sv.dispatchMouseDown(down);

    // Move +50 px; base 3 px drag threshold → fires onDragStart + onDrag.
    MouseMoveEvent mv{};
    mv.x = 253; mv.y = 100;
    mv.dx = 50; mv.dy = 0;
    mv.buttonMask = 1u << static_cast<int>(MouseButton::Left);
    mv.timestampMs = 110;
    sv.dispatchMouseMove(mv);

    // Ratio updated during drag — re-layout (what the app does every
    // frame) and verify the first pane grew.
    sv.measure(Constraints::loose(406, 200), ctx);
    sv.layout(Rect{0, 0, 406, 200}, ctx);
    EXPECT_GT(a.bounds().w, 245);
    EXPECT_LT(a.bounds().w, 255);

    MouseEvent up{};
    up.x = 253; up.y = 100;
    up.lx = 253; up.ly = 100;
    up.timestampMs = 120;
    sv.dispatchMouseUp(up);
    EXPECT_FALSE(sv.isDragging());
}

TEST_F(SplitViewHarness, DragOutsideDividerIgnored) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setRatio(0.5f);
    sv.measure(Constraints::loose(406, 200), ctx);
    sv.layout(Rect{0, 0, 406, 200}, ctx);

    // Press outside the divider (in first pane).
    MouseEvent down{};
    down.x = 100; down.y = 100;
    down.lx = 100; down.ly = 100;
    down.timestampMs = 100;
    sv.dispatchMouseDown(down);

    MouseMoveEvent mv{};
    mv.x = 150; mv.y = 100;
    mv.dx = 50; mv.dy = 0;
    mv.buttonMask = 1u << static_cast<int>(MouseButton::Left);
    mv.timestampMs = 110;
    sv.dispatchMouseMove(mv);

    // No divider movement.
    EXPECT_FLOAT_EQ(a.bounds().w, 200);
}

TEST_F(SplitViewHarness, OnDividerMovedFires) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setRatio(0.5f);
    sv.measure(Constraints::loose(406, 200), ctx);
    sv.layout(Rect{0, 0, 406, 200}, ctx);

    int fires = 0;
    float last = -1.0f;
    sv.setOnDividerMoved([&](float v) { ++fires; last = v; });

    MouseEvent down{};
    down.x = 203; down.y = 100;
    down.lx = 203; down.ly = 100;
    down.timestampMs = 100;
    sv.dispatchMouseDown(down);

    MouseMoveEvent mv{};
    mv.x = 253; mv.y = 100;
    mv.dx = 50; mv.dy = 0;
    mv.buttonMask = 1u << static_cast<int>(MouseButton::Left);
    mv.timestampMs = 110;
    sv.dispatchMouseMove(mv);

    EXPECT_GE(fires, 1);
    EXPECT_GT(last, 0.5f);    // Ratio grew past 50 %
    EXPECT_LT(last, 1.0f);
}

// ─── Mode-switch helpers ───────────────────────────────────────────

TEST_F(SplitViewHarness, SetRatioSwitchesMode) {
    SplitView sv;
    sv.setFirstPaneSize(80);
    EXPECT_EQ(sv.splitMode(), SplitMode::FixedStart);
    sv.setRatio(0.25f);
    EXPECT_EQ(sv.splitMode(), SplitMode::Ratio);
    EXPECT_FLOAT_EQ(sv.firstPaneSize(), 0);   // no layout yet
}

TEST_F(SplitViewHarness, DerivedAccessorsMatchLayout) {
    SplitView sv;
    Pane a, b;
    sv.setFirst(&a);
    sv.setSecond(&b);
    sv.setDividerThickness(6);
    sv.setRatio(0.25f);

    sv.measure(Constraints::loose(406, 200), ctx);
    sv.layout(Rect{0, 0, 406, 200}, ctx);
    EXPECT_FLOAT_EQ(sv.firstPaneSize(), 100);
    EXPECT_FLOAT_EQ(sv.secondPaneSize(), 300);
    EXPECT_NEAR(sv.dividerRatio(), 0.25f, 1e-4f);
}
