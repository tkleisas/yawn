// UI v2 — FwDropDown tests.
//
// Exercises the first real consumer of LayerStack. Tests use a real
// LayerStack instance attached to UIContext so we can observe the
// popup push/pop lifecycle. No painters are registered — the popup
// paint hook stays null and paint closures are no-ops, which is
// exactly what yawn_tests can handle (no GL).

#include <gtest/gtest.h>

#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class DropDownHarness : public ::testing::Test {
protected:
    void SetUp() override {
        ctx.layerStack = &stack;
        ctx.viewport   = {0, 0, 1024, 768};
        UIContext::setGlobal(&ctx);
    }
    void TearDown() override {
        UIContext::setGlobal(nullptr);
    }
    UIContext  ctx;
    LayerStack stack;
};

// ─── Construction / items ───────────────────────────────────────────

TEST_F(DropDownHarness, EmptyDefault) {
    FwDropDown dd;
    EXPECT_EQ(dd.itemCount(), 0);
    EXPECT_EQ(dd.selectedIndex(), -1);
    EXPECT_EQ(dd.selectedLabel(), "");
    EXPECT_FALSE(dd.isOpen());
}

TEST_F(DropDownHarness, ConstructWithItems) {
    FwDropDown dd({"Alpha", "Beta", "Gamma"});
    EXPECT_EQ(dd.itemCount(), 3);
    EXPECT_EQ(dd.itemLabel(0), "Alpha");
    EXPECT_EQ(dd.itemLabel(2), "Gamma");
}

TEST_F(DropDownHarness, AddItemGrows) {
    FwDropDown dd;
    dd.addItem("first");
    dd.addItem("second");
    EXPECT_EQ(dd.itemCount(), 2);
    EXPECT_EQ(dd.itemLabel(1), "second");
}

TEST_F(DropDownHarness, ClearItemsResetsSelection) {
    FwDropDown dd({"x", "y"});
    dd.setSelectedIndex(1);
    dd.clearItems();
    EXPECT_EQ(dd.itemCount(), 0);
    EXPECT_EQ(dd.selectedIndex(), -1);
}

TEST_F(DropDownHarness, SetItemsClampsInvalidSelection) {
    FwDropDown dd({"a", "b", "c"});
    dd.setSelectedIndex(2);
    dd.setItems(std::vector<std::string>{"x"});
    EXPECT_EQ(dd.selectedIndex(), -1);
}

// ─── Selection ──────────────────────────────────────────────────────

TEST_F(DropDownHarness, SetSelectedIndexBasic) {
    FwDropDown dd({"a", "b", "c"});
    int fires = 0;
    std::string lastLabel;
    dd.setOnChange([&](int, const std::string& l) {
        ++fires;
        lastLabel = l;
    });

    dd.setSelectedIndex(1, ValueChangeSource::User);
    EXPECT_EQ(dd.selectedIndex(), 1);
    EXPECT_EQ(dd.selectedLabel(), "b");
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastLabel, "b");
}

TEST_F(DropDownHarness, OnChangeSuppressedForAutomation) {
    FwDropDown dd({"a", "b"});
    int fires = 0;
    dd.setOnChange([&](int, const std::string&) { ++fires; });
    dd.setSelectedIndex(1, ValueChangeSource::Automation);
    EXPECT_EQ(dd.selectedIndex(), 1);
    EXPECT_EQ(fires, 0);
}

TEST_F(DropDownHarness, SetSelectedIdempotent) {
    FwDropDown dd({"a", "b"});
    dd.setSelectedIndex(1);
    int fires = 0;
    dd.setOnChange([&](int, const std::string&) { ++fires; });
    dd.setSelectedIndex(1);
    EXPECT_EQ(fires, 0);
}

TEST_F(DropDownHarness, SetSelectedSkipsDisabledItem) {
    FwDropDown dd({"a", "b", "c"});
    dd.setItemEnabled(1, false);
    dd.setSelectedIndex(1);
    EXPECT_EQ(dd.selectedIndex(), -1);   // refused
}

TEST_F(DropDownHarness, SetSelectedSkipsSeparator) {
    FwDropDown dd({"a", "sep", "b"});
    dd.setItemSeparator(1, true);
    dd.setSelectedIndex(1);
    EXPECT_EQ(dd.selectedIndex(), -1);
}

// ─── Open / close ──────────────────────────────────────────────────

TEST_F(DropDownHarness, OpenPushesOverlay) {
    FwDropDown dd({"a", "b"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);

    EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 0);
    dd.open();
    EXPECT_TRUE(dd.isOpen());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 1);
}

TEST_F(DropDownHarness, CloseRemovesOverlay) {
    FwDropDown dd({"a", "b"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.open();
    ASSERT_TRUE(dd.isOpen());

    dd.close();
    EXPECT_FALSE(dd.isOpen());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 0);
}

TEST_F(DropDownHarness, ToggleOpensThenCloses) {
    FwDropDown dd({"a", "b"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);

    dd.toggle();
    EXPECT_TRUE(dd.isOpen());
    dd.toggle();
    EXPECT_FALSE(dd.isOpen());
}

TEST_F(DropDownHarness, OpenNoOpWhenEmpty) {
    FwDropDown dd;
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.open();
    EXPECT_FALSE(dd.isOpen());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 0);
}

TEST_F(DropDownHarness, DestructorClosesOpenPopup) {
    {
        FwDropDown dd({"a", "b"});
        dd.layout(Rect{10, 10, 100, 28}, ctx);
        dd.open();
        EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 1);
    }
    EXPECT_EQ(stack.entryCount(OverlayLayer::Overlay), 0);
}

TEST_F(DropDownHarness, OpenWithoutLayerStackIsNoop) {
    UIContext localCtx;   // no layerStack
    UIContext::setGlobal(&localCtx);
    FwDropDown dd({"a", "b"});
    dd.layout(Rect{10, 10, 100, 28}, localCtx);
    dd.open();
    EXPECT_FALSE(dd.isOpen());
    UIContext::setGlobal(&ctx);   // restore
}

// ─── Popup direction & clamp ───────────────────────────────────────

TEST_F(DropDownHarness, PopupOpensDownWhenRoomBelow) {
    FwDropDown dd({"a", "b", "c"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.open();
    EXPECT_FALSE(dd.popupOpensUpward());
    const Rect& pr = dd.popupBounds();
    EXPECT_GE(pr.y, 10.0f + 28.0f);   // below the button
}

TEST_F(DropDownHarness, PopupFlipsUpwardNearBottom) {
    // Button near the bottom of a 768-high viewport.
    FwDropDown dd({"a", "b", "c", "d", "e"});
    dd.layout(Rect{10, 740, 100, 28}, ctx);
    dd.open();
    EXPECT_TRUE(dd.popupOpensUpward());
    const Rect& pr = dd.popupBounds();
    EXPECT_LT(pr.y + pr.h, 740.0f + 0.01f);   // above button's top edge
}

TEST_F(DropDownHarness, PopupHorizontalClampRightEdge) {
    FwDropDown dd({"xxxxxxxxxxxxxxxxxxxxxxxx"});   // long item
    dd.layout(Rect{1000, 10, 100, 28}, ctx);       // near right edge
    dd.open();
    const Rect& pr = dd.popupBounds();
    EXPECT_LE(pr.x + pr.w, 1024.0f + 0.01f);
}

// ─── Mouse on popup ────────────────────────────────────────────────

TEST_F(DropDownHarness, PopupClickOnItemSelectsAndCloses) {
    FwDropDown dd({"a", "b", "c"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    int fires = 0;
    int lastIdx = -1;
    dd.setOnChange([&](int i, const std::string&) { ++fires; lastIdx = i; });

    dd.open();
    const Rect& pr = dd.popupBounds();

    MouseEvent e;
    // Click the second row (index 1). itemHeight defaults to
    // controlHeight=28; popup has ~2px top padding.
    e.x = pr.x + 20;
    e.y = pr.y + 2 + 28 + 14;
    EXPECT_TRUE(stack.dispatchMouseDown(e));
    EXPECT_EQ(dd.selectedIndex(), 1);
    EXPECT_FALSE(dd.isOpen());
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(lastIdx, 1);
}

TEST_F(DropDownHarness, OutsideClickDismisses) {
    FwDropDown dd({"a", "b"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.open();
    ASSERT_TRUE(dd.isOpen());

    MouseEvent e;
    e.x = 900;
    e.y = 500;   // far from popup
    stack.dispatchMouseDown(e);
    EXPECT_FALSE(dd.isOpen());
}

TEST_F(DropDownHarness, MouseMoveSetsHighlight) {
    FwDropDown dd({"a", "b", "c"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.open();
    const Rect& pr = dd.popupBounds();

    MouseMoveEvent m;
    m.x = pr.x + 20;
    m.y = pr.y + 2 + 28 + 14;   // row 1
    stack.dispatchMouseMove(m);
    EXPECT_EQ(dd.highlightedIndex(), 1);
}

// ─── Keyboard ──────────────────────────────────────────────────────

TEST_F(DropDownHarness, ClosedEnterOpens) {
    FwDropDown dd({"a", "b"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    KeyEvent k;
    k.key = Key::Enter;
    dd.dispatchKeyDown(k);
    EXPECT_TRUE(dd.isOpen());
}

TEST_F(DropDownHarness, ClosedDownChangesSelection) {
    FwDropDown dd({"a", "b", "c"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.setSelectedIndex(0);
    KeyEvent k;
    k.key = Key::Down;
    dd.dispatchKeyDown(k);
    EXPECT_EQ(dd.selectedIndex(), 1);
    dd.dispatchKeyDown(k);
    EXPECT_EQ(dd.selectedIndex(), 2);
}

TEST_F(DropDownHarness, ClosedDownWrapsAtEnd) {
    FwDropDown dd({"a", "b"});
    dd.setSelectedIndex(1);
    KeyEvent k;
    k.key = Key::Down;
    dd.dispatchKeyDown(k);
    EXPECT_EQ(dd.selectedIndex(), 0);   // wrapped
}

TEST_F(DropDownHarness, OpenUpDownMovesHighlight) {
    FwDropDown dd({"a", "b", "c"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.setSelectedIndex(0);
    dd.open();
    EXPECT_EQ(dd.highlightedIndex(), 0);

    KeyEvent k;
    k.key = Key::Down;
    stack.dispatchKey(k);
    EXPECT_EQ(dd.highlightedIndex(), 1);

    KeyEvent k2;
    k2.key = Key::Up;
    stack.dispatchKey(k2);
    EXPECT_EQ(dd.highlightedIndex(), 0);
    // Up when at top wraps.
    stack.dispatchKey(k2);
    EXPECT_EQ(dd.highlightedIndex(), 2);
}

TEST_F(DropDownHarness, OpenEnterCommitsAndCloses) {
    FwDropDown dd({"a", "b", "c"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.open();
    // Highlight row 2 by pressing Down twice.
    KeyEvent dn; dn.key = Key::Down;
    stack.dispatchKey(dn);
    stack.dispatchKey(dn);
    EXPECT_EQ(dd.highlightedIndex(), 2);

    KeyEvent en; en.key = Key::Enter;
    stack.dispatchKey(en);
    EXPECT_EQ(dd.selectedIndex(), 2);
    EXPECT_FALSE(dd.isOpen());
}

TEST_F(DropDownHarness, OpenEscapeClosesWithoutCommit) {
    FwDropDown dd({"a", "b", "c"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.setSelectedIndex(0);
    dd.open();

    KeyEvent dn; dn.key = Key::Down;
    stack.dispatchKey(dn);          // highlight row 1
    KeyEvent esc; esc.key = Key::Escape;
    stack.dispatchKey(esc);
    EXPECT_FALSE(dd.isOpen());
    EXPECT_EQ(dd.selectedIndex(), 0);   // unchanged
}

// ─── Scroll ────────────────────────────────────────────────────────

TEST_F(DropDownHarness, ScrollOnClosedChangesSelection) {
    FwDropDown dd({"a", "b", "c"});
    dd.setSelectedIndex(1);
    ScrollEvent s;
    s.dy = -1.0f;   // wheel down → next
    dd.dispatchScroll(s);
    EXPECT_EQ(dd.selectedIndex(), 2);
}

TEST_F(DropDownHarness, ScrollOptOutLeavesSelection) {
    FwDropDown dd({"a", "b", "c"});
    dd.setSelectedIndex(1);
    dd.setScrollChangesSelection(false);
    ScrollEvent s;
    s.dy = -1.0f;
    EXPECT_FALSE(dd.dispatchScroll(s));
    EXPECT_EQ(dd.selectedIndex(), 1);
}

TEST_F(DropDownHarness, ScrollOnOpenPopupScrollsList) {
    FwDropDown dd;
    std::vector<std::string> many;
    for (int i = 0; i < 20; ++i) many.push_back("item" + std::to_string(i));
    dd.setItems(many);
    dd.setMaxVisibleItems(5);
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    dd.open();
    EXPECT_EQ(dd.scrollOffset(), 0);

    ScrollEvent s;
    s.dy = -1.0f;
    s.x = dd.popupBounds().x + 10;
    s.y = dd.popupBounds().y + 10;
    stack.dispatchScroll(s);
    EXPECT_EQ(dd.scrollOffset(), 1);
}

// ─── Right-click ───────────────────────────────────────────────────

TEST_F(DropDownHarness, RightClickFiresCallback) {
    FwDropDown dd({"a"});
    dd.layout(Rect{10, 10, 100, 28}, ctx);
    int fires = 0;
    Point seenPos{};
    dd.setOnRightClick([&](Point p) { ++fires; seenPos = p; });

    MouseEvent down, up;
    down.x = up.x = 50;
    down.y = up.y = 24;
    down.lx = up.lx = 40;
    down.ly = up.ly = 14;
    down.button = up.button = MouseButton::Right;
    down.timestampMs = 1000;
    up.timestampMs   = 1020;
    dd.dispatchMouseDown(down);
    dd.dispatchMouseUp(up);
    EXPECT_EQ(fires, 1);
    EXPECT_FLOAT_EQ(seenPos.x, 50.0f);
}

// ─── Measure ───────────────────────────────────────────────────────

TEST_F(DropDownHarness, MeasureUsesLongestItem) {
    // Fallback: 8 px per char. Longest = "BarBarBar" = 9 chars = 72.
    // Plus glyph (~13) + 2x padding (8x2=16) → ~101 px.
    FwDropDown dd({"A", "BarBarBar", "Short"});
    Size s = dd.measure(Constraints::loose(500, 100), ctx);
    EXPECT_GE(s.w, 72.0f);
}

TEST_F(DropDownHarness, PreferredWidthOverrides) {
    FwDropDown dd({"a"});
    dd.setPreferredWidth(200.0f);
    Size s = dd.measure(Constraints::loose(500, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 200.0f);
}

// ─── Placeholder ───────────────────────────────────────────────────

TEST_F(DropDownHarness, PlaceholderShownWhenNoneSelected) {
    FwDropDown dd({"a", "b"});
    dd.setPlaceholder("Pick one");
    EXPECT_EQ(dd.selectedIndex(), -1);
    EXPECT_EQ(dd.placeholder(), "Pick one");
}
