// UI v2 — FwMenuBar tests.
//
// Focus: the open-state sync between FwMenuBar and the singleton
// ContextMenuManager. The menu bar does not get notified when its
// menu chain closes externally (Escape via LayerStack, item
// activation, outside-click dismiss, or a right-click context menu
// replacing it) — a stale m_openIndex used to turn the next title
// click into a phantom "re-click → close" (the swallowed-click bug,
// where a click on a menu title appeared to do nothing).

#include <gtest/gtest.h>

#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/MenuBar.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class MenuBarHarness : public ::testing::Test {
protected:
    void SetUp() override {
        ctx.layerStack = &stack;
        ctx.viewport   = {0, 0, 1024, 768};
        UIContext::setGlobal(&ctx);
        ContextMenuManager::instance()._testResetAll();

        bar.addMenu("File", { Menu::item("New", []{}) });
        bar.addMenu("Edit", { Menu::item("Undo", []{}) });
        bar.measure(Constraints::tight(800, 24), ctx);
        bar.layout(Rect{0, 0, 800, 24}, ctx);
    }
    void TearDown() override {
        ContextMenuManager::instance()._testResetAll();
        UIContext::setGlobal(nullptr);
    }

    // Click (press only — the bar acts on mouse-down) on the bar strip
    // at the given x. Title strips start near x=4 and are ~35-45 px
    // wide each, so x=10 → "File", x=60 → "Edit".
    void clickTitle(float x) {
        MouseEvent e{};
        e.x = x;  e.y = 12.0f;
        e.lx = x; e.ly = 12.0f;
        bar.dispatchMouseDown(e);
    }
    void hoverTitle(float x) {
        MouseMoveEvent e{};
        e.x = x;  e.y = 12.0f;
        e.lx = x; e.ly = 12.0f;
        bar.dispatchMouseMove(e);
    }

    UIContext  ctx;
    LayerStack stack;
    FwMenuBar  bar;
};

TEST_F(MenuBarHarness, ClickOpensAndReclickCloses) {
    clickTitle(10.0f);
    EXPECT_TRUE(ContextMenu::isOpen());
    EXPECT_TRUE(bar.isOpen());
    EXPECT_EQ(bar.openIndex(), 0);

    clickTitle(10.0f);
    EXPECT_FALSE(ContextMenu::isOpen());
    EXPECT_FALSE(bar.isOpen());
}

TEST_F(MenuBarHarness, ReclickAfterExternalCloseOpensAgain) {
    clickTitle(10.0f);
    ASSERT_TRUE(ContextMenu::isOpen());

    // What Escape does: LayerStack calls the entry's onEscape, which
    // closes the manager chain without telling the menu bar.
    ContextMenuManager::instance().close();
    ASSERT_FALSE(ContextMenu::isOpen());
    EXPECT_FALSE(bar.isOpen());
    EXPECT_EQ(bar.openIndex(), -1);

    // The next click on the same title must OPEN, not close.
    clickTitle(10.0f);
    EXPECT_TRUE(ContextMenu::isOpen());
    EXPECT_TRUE(bar.isOpen());
    EXPECT_EQ(bar.openIndex(), 0);
    ASSERT_EQ(ContextMenuManager::instance().levelCount(), 1);
    EXPECT_EQ(ContextMenuManager::instance().level(0).entries[0].label, "New");
}

TEST_F(MenuBarHarness, HoverSwitchSuppressedAfterExternalClose) {
    clickTitle(10.0f);
    ASSERT_TRUE(ContextMenu::isOpen());
    ContextMenuManager::instance().close();

    // Moving over another title must NOT phantom-open it — that was
    // the first half of the swallowed-click bug (the following click
    // then "re-clicked" it closed).
    hoverTitle(60.0f);
    EXPECT_FALSE(ContextMenu::isOpen());
    EXPECT_FALSE(bar.isOpen());
}

TEST_F(MenuBarHarness, HoverSwitchWorksWhileOurMenuOpen) {
    clickTitle(10.0f);
    ASSERT_TRUE(ContextMenu::isOpen());

    hoverTitle(60.0f);   // onto "Edit" while File is open → switch
    EXPECT_TRUE(ContextMenu::isOpen());
    EXPECT_EQ(bar.openIndex(), 1);
    ASSERT_EQ(ContextMenuManager::instance().levelCount(), 1);
    EXPECT_EQ(ContextMenuManager::instance().level(0).entries[0].label, "Undo");
}

TEST_F(MenuBarHarness, ReplacementChainInvalidatesOpenState) {
    clickTitle(10.0f);
    ASSERT_TRUE(ContextMenu::isOpen());

    // A right-click context menu replaces the bar's chain.
    ContextMenu::show({Menu::item("Ctx", []{})}, Point{300, 300});
    EXPECT_FALSE(bar.isOpen());
    EXPECT_EQ(bar.openIndex(), -1);

    // Clicking the title again must OPEN the File menu (not close the
    // context menu and swallow the click).
    clickTitle(10.0f);
    EXPECT_TRUE(ContextMenu::isOpen());
    EXPECT_TRUE(bar.isOpen());
    ASSERT_EQ(ContextMenuManager::instance().levelCount(), 1);
    EXPECT_EQ(ContextMenuManager::instance().level(0).entries[0].label, "New");
}

TEST_F(MenuBarHarness, ItemActivationInvalidatesOpenState) {
    clickTitle(10.0f);
    ASSERT_TRUE(ContextMenu::isOpen());

    // Clicking a menu item fires it and closes the chain externally.
    bool fired = false;
    FwMenuBar bar2;
    bar2.addMenu("File", { Menu::item("New", [&fired]{ fired = true; }) });
    bar2.measure(Constraints::tight(800, 24), ctx);
    bar2.layout(Rect{0, 0, 800, 24}, ctx);
    {
        MouseEvent e{};
        e.x = 10.0f; e.y = 12.0f; e.lx = 10.0f; e.ly = 12.0f;
        bar2.dispatchMouseDown(e);
    }
    ASSERT_TRUE(ContextMenu::isOpen());
    EXPECT_TRUE(ContextMenuManager::instance().activateItemByLabel("New"));
    EXPECT_TRUE(fired);
    EXPECT_FALSE(ContextMenu::isOpen());
    EXPECT_FALSE(bar2.isOpen());
}
