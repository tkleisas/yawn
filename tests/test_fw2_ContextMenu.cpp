// UI v2 — ContextMenu tests.
//
// Exercises the manager's state machine without rendering. Uses a
// real LayerStack so push/pop lifecycle is observable, and leaves
// the painter null (no crash, state still advances).

#include <gtest/gtest.h>

#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class ContextMenuHarness : public ::testing::Test {
protected:
    void SetUp() override {
        ctx.layerStack = &stack;
        ctx.viewport   = {0, 0, 1024, 768};
        UIContext::setGlobal(&ctx);
        ContextMenuManager::instance()._testResetAll();
    }
    void TearDown() override {
        ContextMenuManager::instance()._testResetAll();
        UIContext::setGlobal(nullptr);
    }
    UIContext  ctx;
    LayerStack stack;
};

// ─── Entry factories ───────────────────────────────────────────────

TEST_F(ContextMenuHarness, FactoryHelpersProduceCorrectKinds) {
    EXPECT_EQ(Menu::item("x", []{}).kind,        MenuEntryKind::Item);
    EXPECT_EQ(Menu::submenu("x", {}).kind,       MenuEntryKind::Submenu);
    EXPECT_EQ(Menu::separator().kind,            MenuEntryKind::Separator);
    EXPECT_EQ(Menu::header("x").kind,            MenuEntryKind::Header);
    EXPECT_EQ(Menu::checkable("x", true, []{}).kind, MenuEntryKind::Checkable);
    EXPECT_EQ(Menu::radio("g", "x", false, []{}).kind, MenuEntryKind::Radio);
}

TEST_F(ContextMenuHarness, SeparatorIsNotEnabled) {
    EXPECT_FALSE(Menu::separator().enabled);
    EXPECT_FALSE(Menu::header("x").enabled);
}

// ─── show / close lifecycle ───────────────────────────────────────

TEST_F(ContextMenuHarness, ShowPushesOverlay) {
    EXPECT_FALSE(ContextMenu::isOpen());
    ContextMenu::show({Menu::item("A", []{})}, Point{100, 100});
    EXPECT_TRUE(ContextMenu::isOpen());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 1);
    EXPECT_EQ(ContextMenuManager::instance().levelCount(), 1);
}

TEST_F(ContextMenuHarness, CloseRemovesOverlay) {
    ContextMenu::show({Menu::item("A", []{})}, Point{100, 100});
    ContextMenu::close();
    EXPECT_FALSE(ContextMenu::isOpen());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 0);
}

TEST_F(ContextMenuHarness, ShowReplacesExistingMenu) {
    ContextMenu::show({Menu::item("A", []{})}, Point{100, 100});
    ContextMenu::show({Menu::item("B", []{}), Menu::item("C", []{})}, Point{200, 200});
    EXPECT_TRUE(ContextMenu::isOpen());
    EXPECT_EQ(ContextMenuManager::instance().levelCount(), 1);
    EXPECT_EQ(ContextMenuManager::instance().level(0).entries.size(), 2u);
}

TEST_F(ContextMenuHarness, EmptyEntriesIsNoOp) {
    ContextMenu::show({}, Point{100, 100});
    EXPECT_FALSE(ContextMenu::isOpen());
}

TEST_F(ContextMenuHarness, NoLayerStackSilentlyFailsToShow) {
    UIContext bare;
    UIContext::setGlobal(&bare);
    ContextMenu::show({Menu::item("A", []{})}, Point{100, 100});
    EXPECT_FALSE(ContextMenu::isOpen());
    UIContext::setGlobal(&ctx);
}

// ─── Geometry ─────────────────────────────────────────────────────

TEST_F(ContextMenuHarness, RootShiftsLeftWhenClippedRightEdge) {
    // Anchor near the right edge; menu would overflow → shift left.
    ContextMenu::show({Menu::item("A long item label", []{})}, Point{1020, 100});
    const Rect& b = ContextMenuManager::instance().level(0).bounds;
    EXPECT_LE(b.x + b.w, 1024.0f + 0.01f);
}

TEST_F(ContextMenuHarness, RootShiftsUpWhenClippedBottomEdge) {
    ContextMenu::show({Menu::item("A", []{}), Menu::item("B", []{}),
                        Menu::item("C", []{}), Menu::item("D", []{})},
                       Point{100, 760});
    const Rect& b = ContextMenuManager::instance().level(0).bounds;
    EXPECT_LE(b.y + b.h, 768.0f + 0.01f);
}

// ─── Activation ───────────────────────────────────────────────────

TEST_F(ContextMenuHarness, KeyboardEnterFiresItem) {
    int calls = 0;
    ContextMenu::show({
        Menu::item("A", [&]{ ++calls; }),
        Menu::item("B", [&]{ calls = 999; }),
    }, Point{100, 100});

    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);   // highlight moves to A
    KeyEvent en; en.key = Key::Enter;
    stack.dispatchKey(en);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(ContextMenu::isOpen());
}

TEST_F(ContextMenuHarness, DisabledItemNotActivated) {
    int calls = 0;
    MenuEntry disabled;
    disabled.kind = MenuEntryKind::Item;
    disabled.label = "Disabled";
    disabled.onClick = [&]{ ++calls; };
    disabled.enabled = false;
    ContextMenu::show({disabled, Menu::item("OK", [&]{ calls = 100; })},
                       Point{100, 100});

    // Down should skip the disabled entry and land on "OK".
    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);
    EXPECT_EQ(ContextMenuManager::instance().level(0).highlighted, 1);
}

TEST_F(ContextMenuHarness, SeparatorSkippedInKeyboardNav) {
    int calls = 0;
    ContextMenu::show({
        Menu::item("A", [&]{ calls = 1; }),
        Menu::separator(),
        Menu::item("B", [&]{ calls = 2; }),
    }, Point{100, 100});

    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);
    EXPECT_EQ(ContextMenuManager::instance().level(0).highlighted, 0);
    stack.dispatchKey(down);
    EXPECT_EQ(ContextMenuManager::instance().level(0).highlighted, 2);  // jumped over separator
}

// ─── Escape ──────────────────────────────────────────────────────

TEST_F(ContextMenuHarness, EscapeClosesRootMenu) {
    ContextMenu::show({Menu::item("A", []{})}, Point{100, 100});
    KeyEvent esc; esc.key = Key::Escape;
    stack.dispatchKey(esc);
    EXPECT_FALSE(ContextMenu::isOpen());
}

// ─── Submenu ─────────────────────────────────────────────────────

TEST_F(ContextMenuHarness, RightArrowOnSubmenuRowOpensSubmenu) {
    int inner = 0;
    ContextMenu::show({
        Menu::submenu("More", {
            Menu::item("Inner", [&]{ ++inner; }),
        }),
    }, Point{100, 100});

    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);   // highlight = 0 (the submenu row)
    KeyEvent right; right.key = Key::Right;
    stack.dispatchKey(right);
    EXPECT_EQ(ContextMenuManager::instance().levelCount(), 2);
}

TEST_F(ContextMenuHarness, LeftArrowClosesTopSubmenuKeepsRoot) {
    ContextMenu::show({
        Menu::submenu("More", {Menu::item("Inner", []{})}),
    }, Point{100, 100});

    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);
    KeyEvent right; right.key = Key::Right;
    stack.dispatchKey(right);
    ASSERT_EQ(ContextMenuManager::instance().levelCount(), 2);

    KeyEvent left; left.key = Key::Left;
    stack.dispatchKey(left);
    EXPECT_EQ(ContextMenuManager::instance().levelCount(), 1);
    EXPECT_TRUE(ContextMenu::isOpen());
}

TEST_F(ContextMenuHarness, EscapeOnSubmenuPopsOneLevel) {
    ContextMenu::show({
        Menu::submenu("More", {Menu::item("Inner", []{})}),
    }, Point{100, 100});
    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);
    KeyEvent right; right.key = Key::Right;
    stack.dispatchKey(right);
    ASSERT_EQ(ContextMenuManager::instance().levelCount(), 2);

    KeyEvent esc; esc.key = Key::Escape;
    stack.dispatchKey(esc);
    EXPECT_EQ(ContextMenuManager::instance().levelCount(), 1);

    stack.dispatchKey(esc);
    EXPECT_FALSE(ContextMenu::isOpen());
}

TEST_F(ContextMenuHarness, SubmenuPositionsRightOfParent) {
    ContextMenu::show({
        Menu::submenu("More", {Menu::item("Inner", []{})}),
    }, Point{100, 100});
    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);
    KeyEvent right; right.key = Key::Right;
    stack.dispatchKey(right);
    const Rect& parent = ContextMenuManager::instance().level(0).bounds;
    const Rect& sub    = ContextMenuManager::instance().level(1).bounds;
    // Submenu should be to the right of parent (with overlap of ~1 px).
    EXPECT_GE(sub.x, parent.x + parent.w - 2.0f);
}

TEST_F(ContextMenuHarness, SubmenuFlipsLeftNearRightEdge) {
    // Root near the right edge so submenu has no room to the right.
    ContextMenu::show({
        Menu::submenu("More", {Menu::item("Inner item text", []{})}),
    }, Point{1000, 100});
    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);
    KeyEvent right; right.key = Key::Right;
    stack.dispatchKey(right);
    const Rect& parent = ContextMenuManager::instance().level(0).bounds;
    const Rect& sub    = ContextMenuManager::instance().level(1).bounds;
    // Submenu to the left of parent.
    EXPECT_LE(sub.x + sub.w, parent.x + 2.0f);
}

// ─── Hover-to-open timing ────────────────────────────────────────

TEST_F(ContextMenuHarness, HoverOnSubmenuRowOpensAfterDelay) {
    ContextMenu::show({
        Menu::item("Item1",  []{}),
        Menu::submenu("More", {Menu::item("Inner", []{})}),
    }, Point{100, 100});
    auto& mgr = ContextMenuManager::instance();
    mgr.setSubmenuHoverDelay(0.2f);

    // Move pointer over row 1 (the submenu).
    const Rect& L = mgr.level(0).bounds;
    MouseMoveEvent m;
    m.x = L.x + L.w * 0.5f;
    m.y = L.y + ContextMenuManager::rowYOffset(mgr.level(0).entries, 1) +
          ContextMenuManager::rowHeight() * 0.5f;
    stack.dispatchMouseMove(m);

    mgr.tick(0.1f);
    EXPECT_EQ(mgr.levelCount(), 1);   // still waiting
    mgr.tick(0.2f);
    EXPECT_EQ(mgr.levelCount(), 2);   // opened
}

TEST_F(ContextMenuHarness, HoverMovingOffSubmenuRowResetsTimer) {
    ContextMenu::show({
        Menu::item("Item1",  []{}),
        Menu::submenu("More", {Menu::item("Inner", []{})}),
    }, Point{100, 100});
    auto& mgr = ContextMenuManager::instance();
    mgr.setSubmenuHoverDelay(0.2f);

    const Rect& L = mgr.level(0).bounds;
    MouseMoveEvent m1;
    m1.x = L.x + L.w * 0.5f;
    m1.y = L.y + ContextMenuManager::rowYOffset(mgr.level(0).entries, 1) +
           ContextMenuManager::rowHeight() * 0.5f;
    stack.dispatchMouseMove(m1);
    mgr.tick(0.1f);

    // Move to row 0 (non-submenu) — resets timer.
    MouseMoveEvent m2;
    m2.x = L.x + L.w * 0.5f;
    m2.y = L.y + ContextMenuManager::rowYOffset(mgr.level(0).entries, 0) +
           ContextMenuManager::rowHeight() * 0.5f;
    stack.dispatchMouseMove(m2);

    mgr.tick(0.3f);   // enough time to have opened, but we're not on submenu row
    EXPECT_EQ(mgr.levelCount(), 1);
}

// ─── Mouse click on item ──────────────────────────────────────────

TEST_F(ContextMenuHarness, MouseClickOnItemFiresAndCloses) {
    int calls = 0;
    ContextMenu::show({
        Menu::item("Click me", [&]{ ++calls; }),
    }, Point{100, 100});
    const Rect& L = ContextMenuManager::instance().level(0).bounds;
    MouseEvent e;
    e.x = L.x + L.w * 0.5f;
    e.y = L.y + ContextMenuManager::rowYOffset(
        ContextMenuManager::instance().level(0).entries, 0) +
          ContextMenuManager::rowHeight() * 0.5f;
    e.button = MouseButton::Left;
    stack.dispatchMouseDown(e);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(ContextMenu::isOpen());
}

TEST_F(ContextMenuHarness, OutsideClickClosesChain) {
    ContextMenu::show({
        Menu::submenu("More", {Menu::item("Inner", []{})}),
    }, Point{100, 100});
    KeyEvent down; down.key = Key::Down;
    stack.dispatchKey(down);
    KeyEvent right; right.key = Key::Right;
    stack.dispatchKey(right);
    ASSERT_EQ(ContextMenuManager::instance().levelCount(), 2);

    MouseEvent e;
    e.x = 900; e.y = 700;   // way outside
    stack.dispatchMouseDown(e);
    EXPECT_FALSE(ContextMenu::isOpen());
}

TEST_F(ContextMenuHarness, ClickOnSubmenuRowOpensImmediately) {
    ContextMenu::show({
        Menu::submenu("More", {Menu::item("Inner", []{})}),
    }, Point{100, 100});
    auto& mgr = ContextMenuManager::instance();
    const Rect& L = mgr.level(0).bounds;
    MouseEvent e;
    e.x = L.x + L.w * 0.5f;
    e.y = L.y + ContextMenuManager::rowYOffset(mgr.level(0).entries, 0) +
          ContextMenuManager::rowHeight() * 0.5f;
    e.button = MouseButton::Left;
    stack.dispatchMouseDown(e);
    EXPECT_EQ(mgr.levelCount(), 2);
}
