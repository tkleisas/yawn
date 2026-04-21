// UI v2 — FwFader tests.
//
// Covers the 14-test surface listed in docs/widgets/fader.md plus a
// couple of extras that cover the gesture state machine path. The
// critical regression test is DragRespectsDPI — the v1 bug was that
// drag speed halved at 2× DPI because of physical-vs-logical pixel
// confusion.

#include <gtest/gtest.h>

#include "ui/framework/v2/Fader.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

// Shared harness — installs a fresh UIContext for each test.
class FaderHarness : public ::testing::Test {
protected:
    void SetUp() override {
        UIContext::setGlobal(&ctx);
        ctx.setDpiScale(1.0f);
    }
    void TearDown() override {
        UIContext::setGlobal(nullptr);
    }
    UIContext ctx;
};

// ─── Helpers ─────────────────────────────────────────────────────────

namespace {

// Simulate a drag from (sx0, sy0) to (sx1, sy1) on the given fader.
// Runs the gesture state machine end-to-end: press → move → release.
// Dispatches via the widget's dispatch* methods (not raw callbacks),
// so the base-class state machine fires.
void simulateDrag(FwFader& f, float sx0, float sy0,
                   float sx1, float sy1,
                   uint16_t mods = 0) {
    // Press.
    MouseEvent down{};
    down.x = sx0; down.y = sy0;
    down.lx = sx0 - f.bounds().x; down.ly = sy0 - f.bounds().y;
    down.button = MouseButton::Left;
    down.modifiers = mods;
    down.timestampMs = 1000;
    f.dispatchMouseDown(down);

    // Move past threshold into drag.
    // Split into two moves so gesture state machine detects threshold.
    // First small move — still under threshold = no drag yet.
    if (std::abs(sx1 - sx0) > 1.0f || std::abs(sy1 - sy0) > 1.0f) {
        MouseMoveEvent m1{};
        m1.x = sx0 + 1.5f; m1.y = sy0 + 1.5f;   // below 3px threshold
        m1.lx = m1.x - f.bounds().x; m1.ly = m1.y - f.bounds().y;
        m1.dx = 1.5f; m1.dy = 1.5f;
        m1.buttonMask = 1u << int(MouseButton::Left);
        m1.modifiers = mods;
        m1.timestampMs = 1010;
        f.dispatchMouseMove(m1);
    }

    // Main move to final position (crosses threshold, triggers drag).
    MouseMoveEvent m2{};
    m2.x = sx1; m2.y = sy1;
    m2.lx = sx1 - f.bounds().x; m2.ly = sy1 - f.bounds().y;
    m2.dx = sx1 - sx0; m2.dy = sy1 - sy0;
    m2.buttonMask = 1u << int(MouseButton::Left);
    m2.modifiers = mods;
    m2.timestampMs = 1020;
    f.dispatchMouseMove(m2);

    // Release.
    MouseEvent up{};
    up.x = sx1; up.y = sy1;
    up.lx = sx1 - f.bounds().x; up.ly = sy1 - f.bounds().y;
    up.button = MouseButton::Left;
    up.modifiers = mods;
    up.timestampMs = 1030;
    f.dispatchMouseUp(up);
}

// Simulate a simple click (down + up, no movement).
void simulateClick(FwFader& f, float sx, float sy,
                    MouseButton btn = MouseButton::Left,
                    uint64_t startTimeMs = 1000) {
    MouseEvent down{};
    down.x = sx; down.y = sy;
    down.lx = sx - f.bounds().x; down.ly = sy - f.bounds().y;
    down.button = btn;
    down.timestampMs = startTimeMs;
    f.dispatchMouseDown(down);

    MouseEvent up{};
    up.x = sx; up.y = sy;
    up.lx = down.lx; up.ly = down.ly;
    up.button = btn;
    up.timestampMs = startTimeMs + 20;
    f.dispatchMouseUp(up);
}

} // anon

// ─── Measure ────────────────────────────────────────────────────────

TEST_F(FaderHarness, MeasurePreferredSize) {
    FwFader f;
    Size s = f.measure(Constraints::loose(200, 200), ctx);
    // Width = handleWidth = 20, Height = preferredHeight = 120 (defaults).
    EXPECT_FLOAT_EQ(s.w, 20.0f);
    EXPECT_FLOAT_EQ(s.h, 120.0f);
}

TEST_F(FaderHarness, MeasureClampedByConstraints) {
    FwFader f;
    // maxH=50 should clamp preferred 120 down to 50.
    Size s = f.measure(Constraints::loose(200, 50), ctx);
    EXPECT_FLOAT_EQ(s.h, 50.0f);
}

// ─── Value ──────────────────────────────────────────────────────────

TEST_F(FaderHarness, SetValueClamps) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setValue(100.0f);
    EXPECT_FLOAT_EQ(f.value(), 1.0f);
    f.setValue(-5.0f);
    EXPECT_FLOAT_EQ(f.value(), 0.0f);
}

TEST_F(FaderHarness, SetValueNoChangeNoCallback) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setValue(0.5f, ValueChangeSource::Programmatic);

    int callbackCount = 0;
    f.setOnChange([&](float) { ++callbackCount; });

    f.setValue(0.5f, ValueChangeSource::Programmatic);  // same value
    EXPECT_EQ(callbackCount, 0);
}

TEST_F(FaderHarness, OnChangeNotFiredForAutomation) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setValue(0.0f);

    int count = 0;
    f.setOnChange([&](float) { ++count; });
    f.setValue(0.7f, ValueChangeSource::Automation);
    EXPECT_EQ(count, 0);
    EXPECT_FLOAT_EQ(f.value(), 0.7f);
}

TEST_F(FaderHarness, OnChangeFiresForUserSource) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setValue(0.0f);

    int count = 0;
    f.setOnChange([&](float) { ++count; });
    f.setValue(0.5f, ValueChangeSource::User);
    EXPECT_EQ(count, 1);
}

// ─── Drag math (the critical tests) ──────────────────────────────────

TEST_F(FaderHarness, DragChangesValue) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setPixelsPerFullRange(200.0f);
    f.setValue(0.5f);
    // Place the fader at (0, 0) with some size.
    f.layout(Rect{0, 0, 20, 200}, ctx);

    // Drag up by 100 logical px → value should increase by 0.5 (half range).
    simulateDrag(f, 10, 100, 10, 0);  // dy = -100

    EXPECT_NEAR(f.value(), 1.0f, 1e-5f);
}

TEST_F(FaderHarness, DragDownDecreasesValue) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setPixelsPerFullRange(200.0f);
    f.setValue(0.5f);
    f.layout(Rect{0, 0, 20, 200}, ctx);

    // Drag down by 100 → value -0.5.
    simulateDrag(f, 10, 0, 10, 100);

    EXPECT_NEAR(f.value(), 0.0f, 1e-5f);
}

TEST_F(FaderHarness, DragRespectsDPI) {
    // The key regression test for the v1 bug.
    // At 2x DPI, the physical pixel deltas are doubled but our
    // framework divides by dpiScale so LOGICAL deltas are identical.
    // Same drag at 1x vs 2x DPI → same value change.
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setPixelsPerFullRange(200.0f);

    // At 1x DPI, drag 100 logical px.
    ctx.setDpiScale(1.0f);
    f.setValue(0.5f);
    f.layout(Rect{0, 0, 20, 200}, ctx);
    simulateDrag(f, 10, 100, 10, 0);  // screen dy = -100 physical
    float value1x = f.value();

    // At 2x DPI, drag 200 physical px (= 100 logical px).
    ctx.setDpiScale(2.0f);
    f.setValue(0.5f);
    simulateDrag(f, 10, 200, 10, 0);  // screen dy = -200 physical
    float value2x = f.value();

    // Both should move the value the SAME amount.
    EXPECT_NEAR(value1x, value2x, 1e-5f);
}

TEST_F(FaderHarness, ShiftFineMode) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setPixelsPerFullRange(200.0f);
    f.setValue(0.5f);
    f.layout(Rect{0, 0, 20, 200}, ctx);

    // Drag up by 100 logical px with Shift — should be 0.1× delta.
    simulateDrag(f, 10, 100, 10, 0, ModifierKey::Shift);

    // Without Shift: +0.5. With Shift: +0.05.
    EXPECT_NEAR(f.value(), 0.55f, 1e-4f);
}

TEST_F(FaderHarness, CtrlCoarseMode) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setPixelsPerFullRange(200.0f);
    f.setValue(0.5f);
    f.layout(Rect{0, 0, 20, 200}, ctx);

    // Drag up by 10 logical px with Ctrl — should be 10× delta.
    simulateDrag(f, 10, 60, 10, 50, ModifierKey::Ctrl);  // dy=-10

    // Without Ctrl: 10 / 200 = +0.05. With Ctrl: +0.5.
    EXPECT_NEAR(f.value(), 1.0f, 1e-4f);  // clamped at 1.0 from 0.5+0.5
}

// ─── Drag end callback ──────────────────────────────────────────────

TEST_F(FaderHarness, OnDragEndFires) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setPixelsPerFullRange(200.0f);
    f.setValue(0.3f);
    f.layout(Rect{0, 0, 20, 200}, ctx);

    int calls = 0;
    float startV = -1, endV = -1;
    f.setOnDragEnd([&](float s, float e) { ++calls; startV = s; endV = e; });

    simulateDrag(f, 10, 100, 10, 50);  // drag up 50 px → value +0.25

    EXPECT_EQ(calls, 1);
    EXPECT_NEAR(startV, 0.3f, 1e-5f);
    EXPECT_NEAR(endV,   0.55f, 1e-4f);
}

// ─── Click ──────────────────────────────────────────────────────────

TEST_F(FaderHarness, ClickFiresOnClick) {
    FwFader f;
    f.layout(Rect{0, 0, 20, 200}, ctx);

    int calls = 0;
    f.setOnClick([&]() { ++calls; });

    simulateClick(f, 10, 100);
    EXPECT_EQ(calls, 1);
}

TEST_F(FaderHarness, RightClickFiresOnRightClick) {
    FwFader f;
    f.layout(Rect{0, 0, 20, 200}, ctx);

    int calls = 0;
    Point gotPos;
    f.setOnRightClick([&](Point p) { ++calls; gotPos = p; });

    simulateClick(f, 15, 100, MouseButton::Right);
    EXPECT_EQ(calls, 1);
    EXPECT_FLOAT_EQ(gotPos.x, 15.0f);
    EXPECT_FLOAT_EQ(gotPos.y, 100.0f);
}

// ─── Relayout boundary ──────────────────────────────────────────────

TEST_F(FaderHarness, RelayoutBoundary) {
    // Faders explicitly set themselves as relayout boundaries in ctor
    // because value changes are paint-only but flex layout needs to
    // not propagate invalidation upward.
    FwFader f;
    EXPECT_TRUE(f.isRelayoutBoundary());
}

TEST_F(FaderHarness, SetValueDoesNotBumpMeasure) {
    // The key perf test: dragging a fader must not invalidate its own
    // measure cache.
    FwFader f;
    f.measure(Constraints::loose(100, 200), ctx);
    const int versionBefore = f.measureVersion();

    f.setValue(0.1f, ValueChangeSource::User);
    f.setValue(0.2f, ValueChangeSource::User);
    f.setValue(0.9f, ValueChangeSource::User);

    EXPECT_EQ(f.measureVersion(), versionBefore);
}

// ─── Cache ──────────────────────────────────────────────────────────

TEST_F(FaderHarness, MeasureCacheHit) {
    FwFader f;
    f.measure(Constraints::loose(100, 200), ctx);
    const int calls1 = f.measureCallCount();
    f.measure(Constraints::loose(100, 200), ctx);
    f.measure(Constraints::loose(100, 200), ctx);
    EXPECT_EQ(f.measureCallCount(), calls1);   // no re-measure
}

TEST_F(FaderHarness, ThemeSwapPreservesMeasureWhenMetricsUnchanged) {
    // Theme swap bumps global epoch → measure cache misses next time.
    // But the measured value should be identical if metrics haven't
    // changed.
    FwFader f;
    Size s1 = f.measure(Constraints::loose(100, 200), ctx);

    Theme newTheme = theme();
    newTheme.palette.accent = Color{255, 0, 255};  // only palette change
    setTheme(std::move(newTheme));

    Size s2 = f.measure(Constraints::loose(100, 200), ctx);
    EXPECT_EQ(s1, s2);
}

// ─── Disabled ───────────────────────────────────────────────────────

TEST_F(FaderHarness, DisabledSwallowsClicks) {
    FwFader f;
    f.setEnabled(false);
    f.layout(Rect{0, 0, 20, 200}, ctx);

    int clicks = 0;
    f.setOnClick([&]() { ++clicks; });
    simulateClick(f, 10, 100);
    EXPECT_EQ(clicks, 0);
}

TEST_F(FaderHarness, DisabledSwallowsDrag) {
    FwFader f;
    f.setRange(0.0f, 1.0f);
    f.setValue(0.5f);
    f.setEnabled(false);
    f.layout(Rect{0, 0, 20, 200}, ctx);

    simulateDrag(f, 10, 100, 10, 0);
    EXPECT_FLOAT_EQ(f.value(), 0.5f);   // unchanged
}
