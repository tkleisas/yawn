// UI v2 — FwButton tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class ButtonHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

namespace {

void simulateClick(FwButton& b, float sx, float sy,
                    MouseButton btn = MouseButton::Left,
                    uint64_t startTimeMs = 1000) {
    MouseEvent down{};
    down.x = sx; down.y = sy;
    down.lx = sx - b.bounds().x; down.ly = sy - b.bounds().y;
    down.button = btn;
    down.timestampMs = startTimeMs;
    b.dispatchMouseDown(down);

    MouseEvent up{};
    up.x = sx; up.y = sy;
    up.lx = down.lx; up.ly = down.ly;
    up.button = btn;
    up.timestampMs = startTimeMs + 20;
    b.dispatchMouseUp(up);
}

void simulateDrag(FwButton& b, float sx0, float sy0, float sx1, float sy1) {
    MouseEvent down{};
    down.x = sx0; down.y = sy0;
    down.lx = sx0 - b.bounds().x; down.ly = sy0 - b.bounds().y;
    down.timestampMs = 1000;
    b.dispatchMouseDown(down);

    MouseMoveEvent m{};
    m.x = sx1; m.y = sy1;
    m.lx = sx1 - b.bounds().x; m.ly = sy1 - b.bounds().y;
    m.dx = sx1 - sx0; m.dy = sy1 - sy0;
    m.timestampMs = 1010;
    b.dispatchMouseMove(m);

    MouseEvent up{};
    up.x = sx1; up.y = sy1;
    up.lx = m.lx; up.ly = m.ly;
    up.timestampMs = 1020;
    b.dispatchMouseUp(up);
}

} // anon

// ─── Measure ────────────────────────────────────────────────────────

TEST_F(ButtonHarness, EmptyLabelMeasuresToMinimum) {
    FwButton b;
    Size s = b.measure(Constraints::loose(300, 100), ctx);
    // Content is 0 width + padding (baseUnit × 4 = 16 by default).
    EXPECT_FLOAT_EQ(s.w, 16.0f);
    // Height = controlHeight (default 28).
    EXPECT_FLOAT_EQ(s.h, 28.0f);
}

TEST_F(ButtonHarness, LabelDrivesWidth) {
    FwButton b("Save");   // 4 × 8 = 32 + 16 padding = 48
    Size s = b.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 48.0f);
}

TEST_F(ButtonHarness, FixedWidthOverrides) {
    FwButton b("Save");
    b.setFixedWidth(200);
    Size s = b.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 200.0f);
}

TEST_F(ButtonHarness, MinWidthRespected) {
    FwButton b("Hi");   // 16 + 16 padding = 32
    b.setMinWidth(100);
    Size s = b.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 100.0f);
}

// ─── Click ──────────────────────────────────────────────────────────

TEST_F(ButtonHarness, ClickFires) {
    FwButton b("Save");
    b.layout(Rect{0, 0, 100, 30}, ctx);

    int calls = 0;
    b.setOnClick([&]() { ++calls; });

    simulateClick(b, 50, 15);
    EXPECT_EQ(calls, 1);
}

TEST_F(ButtonHarness, RightClickFires) {
    FwButton b("Save");
    b.layout(Rect{0, 0, 100, 30}, ctx);

    int calls = 0;
    Point pos;
    b.setOnRightClick([&](Point p) { ++calls; pos = p; });

    simulateClick(b, 50, 15, MouseButton::Right);
    EXPECT_EQ(calls, 1);
    EXPECT_FLOAT_EQ(pos.x, 50.0f);
    EXPECT_FLOAT_EQ(pos.y, 15.0f);
}

TEST_F(ButtonHarness, ClickFiresEvenWithJitter) {
    // Buttons are click-only — they don't have a drag behaviour, so
    // any press-release pair should fire onClick regardless of how
    // far the pointer moved between mouseDown and mouseUp. Hand
    // jitter during a press is normal; suppressing the click on
    // mid-press motion made TAP / metronome feel broken in the real
    // app.
    FwButton b("Save");
    b.layout(Rect{0, 0, 100, 30}, ctx);

    int calls = 0;
    b.setOnClick([&]() { ++calls; });

    // Drag past the 3 px click-drag threshold → click STILL fires.
    simulateDrag(b, 50, 15, 60, 15);
    EXPECT_EQ(calls, 1);
}

TEST_F(ButtonHarness, DoubleClickFires) {
    FwButton b("Save");
    b.layout(Rect{0, 0, 100, 30}, ctx);

    int clicks = 0, doubles = 0;
    b.setOnClick([&]() { ++clicks; });
    b.setOnDoubleClick([&]() { ++doubles; });

    // Two clicks within 400ms at same position.
    simulateClick(b, 50, 15, MouseButton::Left, 1000);
    simulateClick(b, 50, 15, MouseButton::Left, 1200);

    EXPECT_EQ(clicks, 2);    // onClick fires for both
    EXPECT_EQ(doubles, 1);   // onDoubleClick fires for the second
}

TEST_F(ButtonHarness, DoubleClickWindowExpires) {
    FwButton b("Save");
    b.layout(Rect{0, 0, 100, 30}, ctx);

    int doubles = 0;
    b.setOnDoubleClick([&]() { ++doubles; });

    // Clicks > 400 ms apart → no double-click.
    simulateClick(b, 50, 15, MouseButton::Left, 1000);
    simulateClick(b, 50, 15, MouseButton::Left, 2000);
    EXPECT_EQ(doubles, 0);
}

// ─── Disabled ───────────────────────────────────────────────────────

TEST_F(ButtonHarness, DisabledSwallowsClick) {
    FwButton b("Save");
    b.layout(Rect{0, 0, 100, 30}, ctx);
    b.setEnabled(false);

    int calls = 0;
    b.setOnClick([&]() { ++calls; });

    simulateClick(b, 50, 15);
    EXPECT_EQ(calls, 0);
}

// ─── Invalidation ───────────────────────────────────────────────────

TEST_F(ButtonHarness, SetLabelInvalidatesMeasure) {
    FwButton b("Save");
    b.measure(Constraints::loose(300, 100), ctx);
    const int v1 = b.measureVersion();
    b.setLabel("Cancel");
    EXPECT_GT(b.measureVersion(), v1);
}

TEST_F(ButtonHarness, SetHighlightedDoesNotInvalidate) {
    FwButton b("Save");
    b.measure(Constraints::loose(300, 100), ctx);
    const int v1 = b.measureVersion();
    b.setHighlighted(true);
    EXPECT_EQ(b.measureVersion(), v1);   // paint-only
}

// ─── Relayout boundary ─────────────────────────────────────────────

TEST_F(ButtonHarness, ButtonIsRelayoutBoundary) {
    FwButton b("Save");
    // Default fixed SizePolicy → auto boundary.
    EXPECT_TRUE(b.isRelayoutBoundary());
}

// ─── Cache ──────────────────────────────────────────────────────────

TEST_F(ButtonHarness, MeasureCacheHit) {
    FwButton b("Save");
    b.measure(Constraints::loose(300, 100), ctx);
    const int c1 = b.measureCallCount();
    b.measure(Constraints::loose(300, 100), ctx);
    b.measure(Constraints::loose(300, 100), ctx);
    EXPECT_EQ(b.measureCallCount(), c1);
}
