// UI v2 — FwRadioButton + FwRadioGroup tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/RadioGroup.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class RadioHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

namespace {
void clickButton(FwRadioButton& btn, uint64_t t = 1000) {
    const Rect& b = btn.bounds();
    const float cx = b.x + b.w * 0.5f;
    const float cy = b.y + b.h * 0.5f;
    MouseEvent down{};
    down.x = cx; down.y = cy;
    down.lx = cx - b.x; down.ly = cy - b.y;
    down.timestampMs = t;
    btn.dispatchMouseDown(down);

    MouseEvent up{};
    up.x = cx; up.y = cy;
    up.lx = down.lx; up.ly = down.ly;
    up.timestampMs = t + 10;
    btn.dispatchMouseUp(up);
}
} // anon

// ─── Standalone FwRadioButton ──────────────────────────────────────

TEST_F(RadioHarness, StandaloneStartsDeselected) {
    FwRadioButton b("opt");
    EXPECT_FALSE(b.isSelected());
}

TEST_F(RadioHarness, StandaloneClickToggles) {
    FwRadioButton b("opt");
    b.measure(Constraints::loose(200, 30), ctx);
    b.layout(Rect{0, 0, 100, 28}, ctx);

    int fires = 0;
    bool last = false;
    b.setOnChange([&](bool s) { ++fires; last = s; });
    clickButton(b);
    EXPECT_TRUE(b.isSelected());
    EXPECT_EQ(fires, 1);
    EXPECT_TRUE(last);

    clickButton(b, 1500);
    EXPECT_FALSE(b.isSelected());
    EXPECT_EQ(fires, 2);
}

// ─── Group exclusivity ────────────────────────────────────────────

TEST_F(RadioHarness, AddOptionCreatesButton) {
    FwRadioGroup g;
    g.addOption("A");
    g.addOption("B");
    EXPECT_EQ(g.optionCount(), 2);
    EXPECT_EQ(g.button(0)->label(), "A");
    EXPECT_EQ(g.button(1)->label(), "B");
}

TEST_F(RadioHarness, ConstructorAddsOptions) {
    FwRadioGroup g({"A", "B", "C"});
    EXPECT_EQ(g.optionCount(), 3);
    EXPECT_EQ(g.selectedIndex(), -1);
}

TEST_F(RadioHarness, SetSelectedIndexProgrammatic) {
    FwRadioGroup g({"A", "B", "C"});
    int fires = 0;
    int lastIdx = -2;
    std::string lastLabel;
    g.setOnChange([&](int idx, const std::string& l) {
        ++fires; lastIdx = idx; lastLabel = l;
    });
    g.setSelectedIndex(1);
    EXPECT_EQ(g.selectedIndex(), 1);
    EXPECT_TRUE(g.button(1)->isSelected());
    EXPECT_FALSE(g.button(0)->isSelected());
    EXPECT_FALSE(g.button(2)->isSelected());
    // Programmatic src still fires (not Automation) — matches other v2 widgets.
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastIdx, 1);
    EXPECT_EQ(lastLabel, "B");
}

TEST_F(RadioHarness, SetSelectedSameNoOp) {
    FwRadioGroup g({"A", "B"});
    g.setSelectedIndex(0);
    int fires = 0;
    g.setOnChange([&](int, const std::string&) { ++fires; });
    g.setSelectedIndex(0);
    EXPECT_EQ(fires, 0);
}

TEST_F(RadioHarness, AutomationSourceSuppressesCallback) {
    FwRadioGroup g({"A", "B"});
    int fires = 0;
    g.setOnChange([&](int, const std::string&) { ++fires; });
    g.setSelectedIndex(1, ValueChangeSource::Automation);
    EXPECT_EQ(g.selectedIndex(), 1);
    EXPECT_EQ(fires, 0);
}

TEST_F(RadioHarness, ClickingButtonSelectsAndDeselectsSiblings) {
    FwRadioGroup g({"A", "B", "C"});
    // Layout the group so buttons have real bounds.
    g.measure(Constraints::loose(400, 50), ctx);
    g.layout(Rect{0, 0, 400, 50}, ctx);

    int fires = 0;
    int lastIdx = -1;
    g.setOnChange([&](int idx, const std::string&) { ++fires; lastIdx = idx; });

    clickButton(*g.button(1));
    EXPECT_EQ(g.selectedIndex(), 1);
    EXPECT_FALSE(g.button(0)->isSelected());
    EXPECT_TRUE(g.button(1)->isSelected());
    EXPECT_FALSE(g.button(2)->isSelected());
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastIdx, 1);

    clickButton(*g.button(2), 2000);
    EXPECT_EQ(g.selectedIndex(), 2);
    EXPECT_FALSE(g.button(1)->isSelected());
    EXPECT_TRUE(g.button(2)->isSelected());
    EXPECT_EQ(fires, 2);
}

TEST_F(RadioHarness, AllowDeselectFalseIgnoresSameClick) {
    FwRadioGroup g({"A", "B"});
    g.measure(Constraints::loose(400, 50), ctx);
    g.layout(Rect{0, 0, 400, 50}, ctx);
    g.setSelectedIndex(0);

    int fires = 0;
    g.setOnChange([&](int, const std::string&) { ++fires; });
    clickButton(*g.button(0));
    EXPECT_EQ(g.selectedIndex(), 0);
    EXPECT_EQ(fires, 0);
}

TEST_F(RadioHarness, AllowDeselectTrueDeselectsOnSameClick) {
    FwRadioGroup g({"A", "B"});
    g.setAllowDeselect(true);
    g.measure(Constraints::loose(400, 50), ctx);
    g.layout(Rect{0, 0, 400, 50}, ctx);
    g.setSelectedIndex(0);

    int fires = 0;
    int lastIdx = -99;
    g.setOnChange([&](int idx, const std::string&) { ++fires; lastIdx = idx; });
    clickButton(*g.button(0));
    EXPECT_EQ(g.selectedIndex(), -1);
    EXPECT_FALSE(g.button(0)->isSelected());
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastIdx, -1);
}

TEST_F(RadioHarness, ButtonOnChangeSuppressedWhenGrouped) {
    FwRadioGroup g({"A", "B"});
    g.measure(Constraints::loose(400, 50), ctx);
    g.layout(Rect{0, 0, 400, 50}, ctx);

    int btnFires = 0;
    g.button(0)->setOnChange([&](bool) { ++btnFires; });

    clickButton(*g.button(0));
    EXPECT_TRUE(g.button(0)->isSelected());
    EXPECT_EQ(btnFires, 0);   // group intercepts
}

// ─── Layout ────────────────────────────────────────────────────────

TEST_F(RadioHarness, HorizontalLayoutStacksLeftToRight) {
    FwRadioGroup g({"A", "B", "C"});
    g.setOrientation(RadioOrientation::Horizontal);
    g.measure(Constraints::loose(400, 50), ctx);
    g.layout(Rect{0, 0, 400, 50}, ctx);

    const float ax = g.button(0)->bounds().x;
    const float bx = g.button(1)->bounds().x;
    const float cx = g.button(2)->bounds().x;
    EXPECT_LT(ax, bx);
    EXPECT_LT(bx, cx);
    // All at the same y baseline.
    EXPECT_FLOAT_EQ(g.button(0)->bounds().y, g.button(1)->bounds().y);
}

TEST_F(RadioHarness, VerticalLayoutStacksTopToBottom) {
    FwRadioGroup g({"A", "B"});
    g.setOrientation(RadioOrientation::Vertical);
    g.measure(Constraints::loose(400, 200), ctx);
    g.layout(Rect{10, 20, 400, 200}, ctx);

    const float ay = g.button(0)->bounds().y;
    const float by = g.button(1)->bounds().y;
    EXPECT_LT(ay, by);
    // Both at the same x.
    EXPECT_FLOAT_EQ(g.button(0)->bounds().x, 10);
    EXPECT_FLOAT_EQ(g.button(1)->bounds().x, 10);
}

TEST_F(RadioHarness, ClearOptionsResetsState) {
    FwRadioGroup g({"A", "B"});
    g.setSelectedIndex(1);
    g.clearOptions();
    EXPECT_EQ(g.optionCount(), 0);
    EXPECT_EQ(g.selectedIndex(), -1);
}
