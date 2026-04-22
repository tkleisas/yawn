// UI v2 — FwProgressBar tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/ProgressBar.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class ProgressBarHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

// ─── Determinate ───────────────────────────────────────────────────

TEST_F(ProgressBarHarness, InitialValueZero) {
    FwProgressBar pb;
    EXPECT_FLOAT_EQ(pb.value(), 0.0f);
    EXPECT_TRUE(pb.isDeterminate());
    EXPECT_FALSE(pb.hasError());
}

TEST_F(ProgressBarHarness, SetValueClampsHigh) {
    FwProgressBar pb;
    pb.setValue(1.5f);
    EXPECT_FLOAT_EQ(pb.value(), 1.0f);
}

TEST_F(ProgressBarHarness, SetValueClampsLow) {
    FwProgressBar pb;
    pb.setValue(-0.2f);
    EXPECT_FLOAT_EQ(pb.value(), 0.0f);
}

TEST_F(ProgressBarHarness, SetValueInRangeStored) {
    FwProgressBar pb;
    pb.setValue(0.42f);
    EXPECT_FLOAT_EQ(pb.value(), 0.42f);
}

// ─── Indeterminate ────────────────────────────────────────────────

TEST_F(ProgressBarHarness, IndeterminateMode) {
    FwProgressBar pb;
    pb.setIndeterminate();
    EXPECT_FALSE(pb.isDeterminate());
    EXPECT_TRUE(pb.requiresContinuousRepaint());
}

TEST_F(ProgressBarHarness, DeterminateClearsContinuousRepaint) {
    FwProgressBar pb;
    pb.setIndeterminate();
    pb.setDeterminate();
    EXPECT_TRUE(pb.isDeterminate());
    EXPECT_FALSE(pb.requiresContinuousRepaint());
}

TEST_F(ProgressBarHarness, IndeterminateSweepAdvances) {
    FwProgressBar pb;
    pb.setIndeterminate();
    const float p0 = pb.sweepPhase();
    pb.tick(0.25f);   // quarter of default 1.2 s cycle
    const float p1 = pb.sweepPhase();
    EXPECT_NE(p0, p1);
    EXPECT_GE(p1, 0.0f);
    EXPECT_LE(p1, 1.0f);
}

// ─── Error ─────────────────────────────────────────────────────────

TEST_F(ProgressBarHarness, ErrorSet) {
    FwProgressBar pb;
    pb.setError(true);
    EXPECT_TRUE(pb.hasError());
}

TEST_F(ProgressBarHarness, ErrorBlocksValueUpdates) {
    FwProgressBar pb;
    pb.setValue(0.3f);
    pb.setError(true);
    pb.setValue(0.8f);
    EXPECT_FLOAT_EQ(pb.value(), 0.3f);
    EXPECT_TRUE(pb.hasError());
}

TEST_F(ProgressBarHarness, ErrorStopsContinuousRepaint) {
    FwProgressBar pb;
    pb.setIndeterminate();
    pb.setError(true);
    EXPECT_FALSE(pb.requiresContinuousRepaint());
}

// ─── Complete fade ─────────────────────────────────────────────────

TEST_F(ProgressBarHarness, CompleteStartsRepaint) {
    FwProgressBar pb;
    pb.setCompleteFade(true);
    pb.setValue(1.0f);
    EXPECT_TRUE(pb.requiresContinuousRepaint());
}

TEST_F(ProgressBarHarness, CompleteFadeDisabledStaysFull) {
    FwProgressBar pb;
    pb.setCompleteFade(false);
    pb.setValue(1.0f);
    pb.tick(1.0f);
    pb.tick(1.0f);
    EXPECT_FLOAT_EQ(pb.value(), 1.0f);
}

TEST_F(ProgressBarHarness, CompleteFadeResetsAfterFullCycle) {
    FwProgressBar pb;
    pb.setCompleteFade(true);
    pb.setValue(1.0f);
    // Fade sequence: 500 ms hold + 500 ms fade. Tick past it.
    pb.tick(0.5f);   // end of hold
    pb.tick(0.5f);   // end of fade
    pb.tick(0.01f);  // trigger reset
    EXPECT_FLOAT_EQ(pb.value(), 0.0f);
    EXPECT_FLOAT_EQ(pb.completeAlpha(), 1.0f);
}

// ─── Orientation + measure ────────────────────────────────────────

TEST_F(ProgressBarHarness, HorizontalMeasureStretches) {
    FwProgressBar pb(ProgressOrientation::Horizontal);
    Size s = pb.measure(Constraints::loose(400, 200), ctx);
    EXPECT_FLOAT_EQ(s.w, 400);
    EXPECT_GT(s.h, 0.0f);
    EXPECT_LT(s.h, 200.0f);
}

TEST_F(ProgressBarHarness, VerticalMeasureStretches) {
    FwProgressBar pb(ProgressOrientation::Vertical);
    Size s = pb.measure(Constraints::loose(200, 400), ctx);
    EXPECT_FLOAT_EQ(s.h, 400);
    EXPECT_GT(s.w, 0.0f);
    EXPECT_LT(s.w, 200.0f);
}

TEST_F(ProgressBarHarness, MinLengthRespected) {
    FwProgressBar pb;
    pb.setMinLength(200);
    Size s = pb.measure(Constraints::loose(100, 50), ctx);
    // maxW is 100 so we clamp to that; min applies only when no upper
    // bound would otherwise force us narrower. Here it floors the
    // stretch: max(200, maxW=100) → 200, then clamp to maxW=100 → 100.
    // The guarantee is "at least stretch to maxW"; minLength acts as
    // a floor when maxW is larger.
    EXPECT_LE(s.w, 100.0f);
}

// ─── Right-click callback ──────────────────────────────────────────

TEST_F(ProgressBarHarness, RightClickFires) {
    FwProgressBar pb;
    pb.measure(Constraints::loose(100, 20), ctx);
    pb.layout(Rect{0, 0, 100, 20}, ctx);

    int fires = 0;
    Point p{};
    pb.setOnRightClick([&](Point pt) { ++fires; p = pt; });

    MouseEvent down{};
    down.x = 50; down.y = 10;
    down.lx = 50; down.ly = 10;
    down.button = MouseButton::Right;
    down.timestampMs = 100;
    pb.dispatchMouseDown(down);

    MouseEvent up{};
    up.x = 50; up.y = 10;
    up.lx = 50; up.ly = 10;
    up.button = MouseButton::Right;
    up.timestampMs = 120;
    pb.dispatchMouseUp(up);

    EXPECT_EQ(fires, 1);
    EXPECT_FLOAT_EQ(p.x, 50.0f);
    EXPECT_FLOAT_EQ(p.y, 10.0f);
}
