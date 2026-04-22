// UI v2 — FwCheckbox tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/Checkbox.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class CheckboxHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

namespace {
void simulateClick(FwCheckbox& c, float sx, float sy,
                    MouseButton btn = MouseButton::Left,
                    uint64_t startMs = 1000) {
    MouseEvent down{};
    down.x = sx; down.y = sy;
    down.lx = sx - c.bounds().x; down.ly = sy - c.bounds().y;
    down.button = btn;
    down.timestampMs = startMs;
    c.dispatchMouseDown(down);

    MouseEvent up{};
    up.x = sx; up.y = sy;
    up.lx = down.lx; up.ly = down.ly;
    up.button = btn;
    up.timestampMs = startMs + 20;
    c.dispatchMouseUp(up);
}
} // anon

// ─── Default state ─────────────────────────────────────────────────

TEST_F(CheckboxHarness, DefaultsOff) {
    FwCheckbox cb;
    EXPECT_EQ(cb.state(), CheckState::Off);
    EXPECT_FALSE(cb.isChecked());
}

TEST_F(CheckboxHarness, ConstructWithLabel) {
    FwCheckbox cb("Remember me");
    EXPECT_EQ(cb.label(), "Remember me");
}

// ─── setChecked / setIndeterminate ─────────────────────────────────

TEST_F(CheckboxHarness, SetCheckedTrue) {
    FwCheckbox cb;
    int fires = 0;
    CheckState last = CheckState::Off;
    cb.setOnChange([&](CheckState s) { ++fires; last = s; });
    cb.setChecked(true);
    EXPECT_EQ(cb.state(), CheckState::On);
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(last, CheckState::On);
}

TEST_F(CheckboxHarness, SetIndeterminate) {
    FwCheckbox cb;
    int fires = 0;
    cb.setOnChange([&](CheckState) { ++fires; });
    cb.setIndeterminate();
    EXPECT_EQ(cb.state(), CheckState::Indeterminate);
    EXPECT_FALSE(cb.isChecked());
    EXPECT_EQ(fires, 1);
}

TEST_F(CheckboxHarness, AutomationSuppressesCallback) {
    FwCheckbox cb;
    int fires = 0;
    cb.setOnChange([&](CheckState) { ++fires; });
    cb.setChecked(true, ValueChangeSource::Automation);
    EXPECT_EQ(cb.state(), CheckState::On);
    EXPECT_EQ(fires, 0);
}

TEST_F(CheckboxHarness, SetCheckedNoOpSameValue) {
    FwCheckbox cb;
    cb.setChecked(true);
    int fires = 0;
    cb.setOnChange([&](CheckState) { ++fires; });
    cb.setChecked(true);
    EXPECT_EQ(fires, 0);
}

// ─── Click behaviour ───────────────────────────────────────────────

TEST_F(CheckboxHarness, ClickOffGoesOn) {
    FwCheckbox cb("Enable");
    cb.layout(Rect{0, 0, 150, 28}, ctx);
    int fires = 0;
    CheckState last = CheckState::Off;
    cb.setOnChange([&](CheckState s) { ++fires; last = s; });

    simulateClick(cb, 14, 14);
    EXPECT_EQ(cb.state(), CheckState::On);
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(last, CheckState::On);
}

TEST_F(CheckboxHarness, ClickOnGoesOff) {
    FwCheckbox cb("x");
    cb.layout(Rect{0, 0, 150, 28}, ctx);
    cb.setChecked(true);
    int fires = 0;
    cb.setOnChange([&](CheckState) { ++fires; });

    simulateClick(cb, 14, 14);
    EXPECT_EQ(cb.state(), CheckState::Off);
    EXPECT_EQ(fires, 1);
}

TEST_F(CheckboxHarness, ClickIndeterminateGoesOn) {
    FwCheckbox cb("x");
    cb.layout(Rect{0, 0, 150, 28}, ctx);
    cb.setIndeterminate();
    int fires = 0;
    CheckState last = CheckState::Off;
    cb.setOnChange([&](CheckState s) { ++fires; last = s; });

    simulateClick(cb, 14, 14);
    EXPECT_EQ(cb.state(), CheckState::On);
    EXPECT_EQ(last, CheckState::On);
    EXPECT_EQ(fires, 1);
}

TEST_F(CheckboxHarness, DisabledSwallowsClick) {
    FwCheckbox cb("x");
    cb.layout(Rect{0, 0, 150, 28}, ctx);
    cb.setEnabled(false);
    int fires = 0;
    cb.setOnChange([&](CheckState) { ++fires; });
    simulateClick(cb, 14, 14);
    EXPECT_EQ(cb.state(), CheckState::Off);
    EXPECT_EQ(fires, 0);
}

// ─── Right-click ───────────────────────────────────────────────────

TEST_F(CheckboxHarness, RightClickFiresWithoutChanging) {
    FwCheckbox cb("x");
    cb.layout(Rect{0, 0, 150, 28}, ctx);
    int left = 0, right = 0;
    cb.setOnChange([&](CheckState) { ++left; });
    cb.setOnRightClick([&](Point) { ++right; });
    simulateClick(cb, 14, 14, MouseButton::Right);
    EXPECT_EQ(cb.state(), CheckState::Off);
    EXPECT_EQ(left, 0);
    EXPECT_EQ(right, 1);
}

// ─── Keyboard ──────────────────────────────────────────────────────

TEST_F(CheckboxHarness, SpaceTogglesState) {
    FwCheckbox cb;
    int fires = 0;
    cb.setOnChange([&](CheckState) { ++fires; });
    KeyEvent k; k.key = Key::Space;
    cb.dispatchKeyDown(k);
    EXPECT_EQ(cb.state(), CheckState::On);
    EXPECT_EQ(fires, 1);
}

TEST_F(CheckboxHarness, EnterTogglesState) {
    FwCheckbox cb;
    KeyEvent k; k.key = Key::Enter;
    cb.dispatchKeyDown(k);
    EXPECT_EQ(cb.state(), CheckState::On);
}

// ─── Measure ───────────────────────────────────────────────────────

TEST_F(CheckboxHarness, MeasureIncludesBoxAndLabel) {
    FwCheckbox cb("Hello");   // 5 × 8 = 40
    Size s = cb.measure(Constraints::loose(300, 100), ctx);
    // boxSize = controlHeight * 0.6 = 28 * 0.6 = 16.8; + gap 4 + 40 label
    EXPECT_GT(s.w, 40.0f);
    EXPECT_LT(s.w, 300.0f);
}

TEST_F(CheckboxHarness, EmptyLabelMeasuresBoxOnly) {
    FwCheckbox cb;
    Size s = cb.measure(Constraints::loose(300, 100), ctx);
    // No label → no gap or label width.
    EXPECT_LE(s.w, 30.0f);
}

TEST_F(CheckboxHarness, MinWidthRespected) {
    FwCheckbox cb("x");
    cb.setMinWidth(100);
    Size s = cb.measure(Constraints::loose(300, 100), ctx);
    EXPECT_GE(s.w, 100.0f);
}

// ─── Invalidation ──────────────────────────────────────────────────

TEST_F(CheckboxHarness, SetLabelInvalidatesMeasure) {
    FwCheckbox cb("A");
    cb.measure(Constraints::loose(300, 100), ctx);
    const int v1 = cb.measureVersion();
    cb.setLabel("Longer label");
    EXPECT_GT(cb.measureVersion(), v1);
}

TEST_F(CheckboxHarness, SetCheckedDoesNotInvalidateMeasure) {
    FwCheckbox cb;
    cb.measure(Constraints::loose(300, 100), ctx);
    const int v1 = cb.measureVersion();
    cb.setChecked(true);
    EXPECT_EQ(cb.measureVersion(), v1);
}

// ─── Relayout boundary ────────────────────────────────────────────

TEST_F(CheckboxHarness, IsRelayoutBoundary) {
    FwCheckbox cb;
    EXPECT_TRUE(cb.isRelayoutBoundary());
}
