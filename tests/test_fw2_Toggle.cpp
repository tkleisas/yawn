// UI v2 — FwToggle tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class ToggleHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

namespace {
void simulateClick(FwToggle& t, float sx, float sy,
                    MouseButton btn = MouseButton::Left,
                    uint64_t startMs = 1000) {
    MouseEvent down{};
    down.x = sx; down.y = sy;
    down.lx = sx - t.bounds().x; down.ly = sy - t.bounds().y;
    down.button = btn;
    down.timestampMs = startMs;
    t.dispatchMouseDown(down);

    MouseEvent up{};
    up.x = sx; up.y = sy;
    up.lx = down.lx; up.ly = down.ly;
    up.button = btn;
    up.timestampMs = startMs + 20;
    t.dispatchMouseUp(up);
}
} // anon

// ─── Default state ─────────────────────────────────────────────────

TEST_F(ToggleHarness, DefaultsOff) {
    FwToggle tg;
    EXPECT_FALSE(tg.state());
    EXPECT_EQ(tg.variant(), ToggleVariant::Button);
    EXPECT_FALSE(tg.scrollFlipsState());
}

TEST_F(ToggleHarness, ConstructWithLabel) {
    FwToggle tg("Enable");
    EXPECT_EQ(tg.label(), "Enable");
    EXPECT_FALSE(tg.state());
}

// ─── setState ──────────────────────────────────────────────────────

TEST_F(ToggleHarness, SetStateFiresCallbackForProgrammatic) {
    FwToggle tg;
    int fires = 0;
    bool last = false;
    tg.setOnChange([&](bool v) { ++fires; last = v; });
    tg.setState(true);
    EXPECT_TRUE(tg.state());
    EXPECT_EQ(fires, 1);
    EXPECT_TRUE(last);
}

TEST_F(ToggleHarness, SetStateSkipsCallbackForAutomation) {
    FwToggle tg;
    int fires = 0;
    tg.setOnChange([&](bool) { ++fires; });
    tg.setState(true, ValueChangeSource::Automation);
    EXPECT_TRUE(tg.state());
    EXPECT_EQ(fires, 0);
}

TEST_F(ToggleHarness, SetStateNoOpSameValue) {
    FwToggle tg;
    tg.setState(true);
    int fires = 0;
    tg.setOnChange([&](bool) { ++fires; });
    tg.setState(true);
    EXPECT_EQ(fires, 0);
}

// ─── Click toggles ──────────────────────────────────────────────────

TEST_F(ToggleHarness, ClickTogglesOnFiresCallback) {
    FwToggle tg("On");
    tg.layout(Rect{0, 0, 100, 28}, ctx);
    int fires = 0;
    bool lastVal = false;
    tg.setOnChange([&](bool v) { ++fires; lastVal = v; });

    simulateClick(tg, 50, 14);
    EXPECT_TRUE(tg.state());
    EXPECT_EQ(fires, 1);
    EXPECT_TRUE(lastVal);

    simulateClick(tg, 50, 14, MouseButton::Left, 2000);
    EXPECT_FALSE(tg.state());
    EXPECT_EQ(fires, 2);
    EXPECT_FALSE(lastVal);
}

TEST_F(ToggleHarness, DisabledSwallowsClick) {
    FwToggle tg("Off");
    tg.layout(Rect{0, 0, 100, 28}, ctx);
    tg.setEnabled(false);
    int fires = 0;
    tg.setOnChange([&](bool) { ++fires; });
    simulateClick(tg, 50, 14);
    EXPECT_FALSE(tg.state());
    EXPECT_EQ(fires, 0);
}

// ─── Right-click ───────────────────────────────────────────────────

TEST_F(ToggleHarness, RightClickFiresWithoutToggling) {
    FwToggle tg("x");
    tg.layout(Rect{0, 0, 100, 28}, ctx);
    int leftFires = 0;
    int rightFires = 0;
    tg.setOnChange([&](bool) { ++leftFires; });
    tg.setOnRightClick([&](Point) { ++rightFires; });

    simulateClick(tg, 50, 14, MouseButton::Right);
    EXPECT_FALSE(tg.state());
    EXPECT_EQ(leftFires, 0);
    EXPECT_EQ(rightFires, 1);
}

// ─── Keyboard ──────────────────────────────────────────────────────

TEST_F(ToggleHarness, SpaceTogglesState) {
    FwToggle tg;
    int fires = 0;
    tg.setOnChange([&](bool) { ++fires; });
    KeyEvent k; k.key = Key::Space;
    tg.dispatchKeyDown(k);
    EXPECT_TRUE(tg.state());
    EXPECT_EQ(fires, 1);
}

TEST_F(ToggleHarness, EnterTogglesState) {
    FwToggle tg;
    KeyEvent k; k.key = Key::Enter;
    tg.dispatchKeyDown(k);
    EXPECT_TRUE(tg.state());
}

TEST_F(ToggleHarness, SwitchHomeEndSetOffAndOn) {
    FwToggle tg;
    tg.setVariant(ToggleVariant::Switch);
    tg.setState(true);
    KeyEvent home; home.key = Key::Home;
    tg.dispatchKeyDown(home);
    EXPECT_FALSE(tg.state());
    KeyEvent end; end.key = Key::End;
    tg.dispatchKeyDown(end);
    EXPECT_TRUE(tg.state());
}

TEST_F(ToggleHarness, ButtonVariantIgnoresHomeEnd) {
    FwToggle tg;
    // Button variant (default) — Home/End shouldn't touch state.
    KeyEvent home; home.key = Key::Home;
    tg.dispatchKeyDown(home);
    EXPECT_FALSE(tg.state());
}

// ─── Scroll ────────────────────────────────────────────────────────

TEST_F(ToggleHarness, ScrollFlipsStateWhenEnabled) {
    FwToggle tg;
    tg.setScrollFlipsState(true);
    ScrollEvent s;
    s.dy = 1.0f;
    tg.dispatchScroll(s);
    EXPECT_TRUE(tg.state());
    s.dy = -1.0f;
    tg.dispatchScroll(s);
    EXPECT_FALSE(tg.state());
}

TEST_F(ToggleHarness, ScrollIgnoredWhenDisabled) {
    FwToggle tg;
    // Default: scrollFlipsState=false.
    ScrollEvent s;
    s.dy = 1.0f;
    EXPECT_FALSE(tg.dispatchScroll(s));
    EXPECT_FALSE(tg.state());
}

// ─── Variant + measure ─────────────────────────────────────────────

TEST_F(ToggleHarness, ButtonVariantMeasureDrivenByLabel) {
    FwToggle tg("HelloWorld");   // 10 chars × 8 = 80; + 16 pad = 96
    Size s = tg.measure(Constraints::loose(300, 100), ctx);
    EXPECT_GE(s.w, 80.0f);
}

TEST_F(ToggleHarness, SwitchVariantHasFixedWidth) {
    FwToggle tg;
    tg.setVariant(ToggleVariant::Switch);
    Size s = tg.measure(Constraints::loose(300, 100), ctx);
    // baseUnit 4 × 10 = 40
    EXPECT_FLOAT_EQ(s.w, 40.0f);
}

TEST_F(ToggleHarness, FixedWidthOverridesLabel) {
    FwToggle tg("Short");
    tg.setFixedWidth(200);
    Size s = tg.measure(Constraints::loose(400, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 200.0f);
}

TEST_F(ToggleHarness, MinWidthRespected) {
    FwToggle tg("x");
    tg.setMinWidth(150);
    Size s = tg.measure(Constraints::loose(400, 100), ctx);
    EXPECT_GE(s.w, 150.0f);
}

// ─── Invalidation discipline ───────────────────────────────────────

TEST_F(ToggleHarness, SetLabelInvalidatesMeasure) {
    FwToggle tg("A");
    tg.measure(Constraints::loose(300, 100), ctx);
    const int v1 = tg.measureVersion();
    tg.setLabel("Longer label");
    EXPECT_GT(tg.measureVersion(), v1);
}

TEST_F(ToggleHarness, SetStateDoesNotInvalidateMeasure) {
    FwToggle tg;
    tg.measure(Constraints::loose(300, 100), ctx);
    const int v1 = tg.measureVersion();
    tg.setState(true);
    EXPECT_EQ(tg.measureVersion(), v1);   // paint-only
}

TEST_F(ToggleHarness, SetVariantInvalidatesMeasure) {
    FwToggle tg;
    tg.measure(Constraints::loose(300, 100), ctx);
    const int v1 = tg.measureVersion();
    tg.setVariant(ToggleVariant::Switch);
    EXPECT_GT(tg.measureVersion(), v1);   // Switch has different size
}

// ─── Relayout boundary ────────────────────────────────────────────

TEST_F(ToggleHarness, IsRelayoutBoundary) {
    FwToggle tg;
    EXPECT_TRUE(tg.isRelayoutBoundary());
}
