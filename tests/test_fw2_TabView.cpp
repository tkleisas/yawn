// UI v2 — TabView tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/TabView.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

namespace {
class Pane : public Widget {
public:
    Size preferred;
    Pane(float w, float h) : preferred{w, h} {}
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain(preferred);
    }
};
} // anon

class TabViewHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

// ─── Adding / removing tabs ───────────────────────────────────────

TEST_F(TabViewHarness, EmptyHasNoActive) {
    TabView tv;
    EXPECT_EQ(tv.tabCount(), 0);
    EXPECT_EQ(tv.activeTabIndex(), -1);
    EXPECT_EQ(tv.activeTabId(), "");
}

TEST_F(TabViewHarness, FirstTabAutoActivates) {
    TabView tv;
    Pane p(100, 100);
    int fires = 0;
    std::string lastId;
    tv.setOnActivated([&](const std::string& id) { ++fires; lastId = id; });
    tv.addTab("audio", "Audio", &p);
    EXPECT_EQ(tv.tabCount(), 1);
    EXPECT_EQ(tv.activeTabIndex(), 0);
    EXPECT_EQ(tv.activeTabId(), "audio");
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastId, "audio");
}

TEST_F(TabViewHarness, AddingDoesNotChangeActive) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    int fires = 0;
    tv.setOnActivated([&](const std::string&) { ++fires; });
    tv.addTab("b", "B", &b);
    EXPECT_EQ(tv.activeTabId(), "a");
    EXPECT_EQ(fires, 0);
}

TEST_F(TabViewHarness, SetActiveByIdFires) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    int fires = 0;
    std::string lastId;
    tv.setOnActivated([&](const std::string& id) { ++fires; lastId = id; });
    tv.setActiveTab("b");
    EXPECT_EQ(tv.activeTabId(), "b");
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastId, "b");
}

TEST_F(TabViewHarness, SetActiveUnknownIgnored) {
    TabView tv;
    Pane a(100, 100);
    tv.addTab("a", "A", &a);
    int fires = 0;
    tv.setOnActivated([&](const std::string&) { ++fires; });
    tv.setActiveTab("nope");
    EXPECT_EQ(tv.activeTabId(), "a");
    EXPECT_EQ(fires, 0);
}

TEST_F(TabViewHarness, SetActiveSameNoCallback) {
    TabView tv;
    Pane a(100, 100);
    tv.addTab("a", "A", &a);
    int fires = 0;
    tv.setOnActivated([&](const std::string&) { ++fires; });
    tv.setActiveTab("a");
    EXPECT_EQ(fires, 0);
}

TEST_F(TabViewHarness, AutomationSuppressesCallback) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    int fires = 0;
    tv.setOnActivated([&](const std::string&) { ++fires; });
    tv.setActiveTab("b", ValueChangeSource::Automation);
    EXPECT_EQ(tv.activeTabId(), "b");
    EXPECT_EQ(fires, 0);
}

// ─── Removal cascades ────────────────────────────────────────────

TEST_F(TabViewHarness, RemoveNonActiveNoCascade) {
    TabView tv;
    Pane a(100, 100), b(100, 100), c(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    tv.addTab("c", "C", &c);
    tv.setActiveTab("b");
    int fires = 0;
    tv.setOnActivated([&](const std::string&) { ++fires; });
    tv.removeTab("c");
    EXPECT_EQ(tv.activeTabId(), "b");
    EXPECT_EQ(fires, 0);
}

TEST_F(TabViewHarness, RemoveActiveCascadesToNext) {
    TabView tv;
    Pane a(100, 100), b(100, 100), c(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    tv.addTab("c", "C", &c);
    tv.setActiveTab("b");
    int fires = 0;
    std::string lastId;
    tv.setOnActivated([&](const std::string& id) { ++fires; lastId = id; });
    tv.removeTab("b");
    EXPECT_EQ(tv.tabCount(), 2);
    EXPECT_EQ(tv.activeTabId(), "c");
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastId, "c");
}

TEST_F(TabViewHarness, RemoveLastActiveCascadesToPrev) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    tv.setActiveTab("b");
    tv.removeTab("b");
    EXPECT_EQ(tv.activeTabId(), "a");
}

TEST_F(TabViewHarness, RemoveOnlyTabNoActive) {
    TabView tv;
    Pane a(100, 100);
    tv.addTab("a", "A", &a);
    tv.removeTab("a");
    EXPECT_EQ(tv.tabCount(), 0);
    EXPECT_EQ(tv.activeTabId(), "");
    EXPECT_EQ(tv.activeTabIndex(), -1);
}

TEST_F(TabViewHarness, ClearTabsResets) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    tv.clearTabs();
    EXPECT_EQ(tv.tabCount(), 0);
    EXPECT_EQ(tv.activeTabIndex(), -1);
}

// ─── Navigation ──────────────────────────────────────────────────

TEST_F(TabViewHarness, SelectNextWraps) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    tv.setActiveTab("b");
    tv.selectNextTab();
    EXPECT_EQ(tv.activeTabId(), "a");
}

TEST_F(TabViewHarness, SelectPrevWraps) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    tv.selectPrevTab();
    EXPECT_EQ(tv.activeTabId(), "b");
}

// ─── Layout ──────────────────────────────────────────────────────

TEST_F(TabViewHarness, LayoutPositionsContentBelowStrip) {
    TabView tv;
    Pane a(100, 100);
    tv.addTab("a", "A", &a);

    tv.measure(Constraints::loose(400, 300), ctx);
    tv.layout(Rect{0, 0, 400, 300}, ctx);

    const float stripH = tv.tabStripHeight();
    EXPECT_FLOAT_EQ(a.bounds().y, stripH);
    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(a.bounds().w, 400);
    EXPECT_FLOAT_EQ(a.bounds().h, 300 - stripH);
}

TEST_F(TabViewHarness, InactiveContentHiddenAndZeroBounds) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);

    tv.measure(Constraints::loose(400, 300), ctx);
    tv.layout(Rect{0, 0, 400, 300}, ctx);

    EXPECT_TRUE(a.isVisible());
    EXPECT_FALSE(b.isVisible());
    // Inactive pane has zero-size bounds.
    EXPECT_FLOAT_EQ(b.bounds().w, 0);
    EXPECT_FLOAT_EQ(b.bounds().h, 0);
}

TEST_F(TabViewHarness, ActivationSwapsVisibility) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);

    tv.measure(Constraints::loose(400, 300), ctx);
    tv.layout(Rect{0, 0, 400, 300}, ctx);
    EXPECT_TRUE(a.isVisible());
    EXPECT_FALSE(b.isVisible());

    tv.setActiveTab("b");
    tv.measure(Constraints::loose(400, 300), ctx);
    tv.layout(Rect{0, 0, 400, 300}, ctx);
    EXPECT_FALSE(a.isVisible());
    EXPECT_TRUE(b.isVisible());
}

TEST_F(TabViewHarness, TabStripLaysOutHorizontally) {
    TabView tv;
    Pane a(100, 100), b(100, 100), c(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "Bb",   &b);
    tv.addTab("c", "Cccc", &c);
    tv.measure(Constraints::loose(600, 300), ctx);
    tv.layout(Rect{10, 20, 600, 300}, ctx);

    const auto& tabs = tv.tabs();
    EXPECT_FLOAT_EQ(tabs[0].stripRect.x, 10);
    EXPECT_GT(tabs[1].stripRect.x, tabs[0].stripRect.x);
    EXPECT_GT(tabs[2].stripRect.x, tabs[1].stripRect.x);
    // All at the same y (strip top).
    EXPECT_FLOAT_EQ(tabs[1].stripRect.y, 20);
    EXPECT_FLOAT_EQ(tabs[2].stripRect.y, 20);
}

// ─── Click + keyboard ────────────────────────────────────────────

TEST_F(TabViewHarness, ClickOnTabActivates) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);
    tv.measure(Constraints::loose(600, 300), ctx);
    tv.layout(Rect{0, 0, 600, 300}, ctx);

    int fires = 0;
    std::string lastId;
    tv.setOnActivated([&](const std::string& id) { ++fires; lastId = id; });

    const Rect& bRect = tv.tabs()[1].stripRect;
    MouseEvent down{};
    down.x = bRect.x + bRect.w * 0.5f;
    down.y = bRect.y + bRect.h * 0.5f;
    down.lx = down.x; down.ly = down.y;
    down.timestampMs = 100;
    tv.dispatchMouseDown(down);

    // Click-only widgets fire on press; but TabView isn't marked
    // click-only — it reads the raw onMouseDown. Still, onMouseDown
    // activates immediately.
    EXPECT_EQ(tv.activeTabId(), "b");
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastId, "b");
}

TEST_F(TabViewHarness, CtrlTabSelectsNext) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);

    KeyEvent e{};
    e.key = Key::Tab;
    e.modifiers = ModifierKey::Ctrl;
    tv.dispatchKeyDown(e);
    EXPECT_EQ(tv.activeTabId(), "b");
}

TEST_F(TabViewHarness, CtrlShiftTabSelectsPrev) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);

    KeyEvent e{};
    e.key = Key::Tab;
    e.modifiers = ModifierKey::Ctrl | ModifierKey::Shift;
    tv.dispatchKeyDown(e);
    EXPECT_EQ(tv.activeTabId(), "b");   // wraps
}

TEST_F(TabViewHarness, PlainTabNotHandled) {
    TabView tv;
    Pane a(100, 100), b(100, 100);
    tv.addTab("a", "A", &a);
    tv.addTab("b", "B", &b);

    KeyEvent e{};
    e.key = Key::Tab;   // no ctrl
    tv.dispatchKeyDown(e);
    EXPECT_EQ(tv.activeTabId(), "a");   // unchanged
}

// ─── Misc ────────────────────────────────────────────────────────

TEST_F(TabViewHarness, SetTabLabelUpdatesStrip) {
    TabView tv;
    Pane a(100, 100);
    tv.addTab("a", "Old", &a);
    tv.setTabLabel("a", "Renamed");
    EXPECT_EQ(tv.tabs()[0].label, "Renamed");
}
