#include <gtest/gtest.h>
#include "ui/Widget.h"
#include "ui/MenuBar.h"

using namespace yawn::ui;

class StubWidget : public Widget {
public:
    void render(Renderer2D&, Font&) override {}
    bool onMouseDown(float, float, int) override { return true; }
    bool onMouseUp(float, float, int) override { return true; }
};

// ========================= InputState =========================

TEST(InputState, HitTestTopmost) {
    InputState input;
    StubWidget w1, w2;
    w1.setBounds(0, 0, 100, 30);
    w2.setBounds(0, 0, 100, 30);
    input.addWidget(&w1);
    input.addWidget(&w2);

    input.onMouseDown(50, 15, 1);
    EXPECT_EQ(input.captured(), &w2);
    input.onMouseUp(50, 15, 1);
}

TEST(InputState, CapturedWidgetGetsMouseUp) {
    InputState input;
    StubWidget w1;
    w1.setBounds(0, 0, 100, 30);
    input.addWidget(&w1);

    input.onMouseDown(50, 15, 1);
    EXPECT_EQ(input.captured(), &w1);
    input.onMouseUp(500, 500, 1);
    EXPECT_EQ(input.captured(), nullptr);
}

TEST(InputState, HoverTracking) {
    InputState input;
    StubWidget w1;
    w1.setBounds(100, 100, 50, 30);
    input.addWidget(&w1);

    input.onMouseMove(125, 115);
    EXPECT_EQ(input.hovered(), &w1);
    EXPECT_TRUE(w1.isHovered());

    input.onMouseMove(0, 0);
    EXPECT_EQ(input.hovered(), nullptr);
    EXPECT_FALSE(w1.isHovered());
}

TEST(InputState, FocusManagement) {
    InputState input;
    StubWidget w1, w2;
    w1.setBounds(0, 0, 50, 30);
    w2.setBounds(60, 0, 50, 30);
    input.addWidget(&w1);
    input.addWidget(&w2);

    input.onMouseDown(25, 15, 1);
    EXPECT_EQ(input.focused(), &w1);
    input.onMouseUp(25, 15, 1);

    input.onMouseDown(85, 15, 1);
    EXPECT_EQ(input.focused(), &w2);
    EXPECT_FALSE(w1.isFocused());
    input.onMouseUp(85, 15, 1);
}

TEST(InputState, RemoveWidget) {
    InputState input;
    StubWidget w1;
    w1.setBounds(0, 0, 100, 30);
    input.addWidget(&w1);
    input.onMouseDown(50, 15, 1);
    EXPECT_EQ(input.captured(), &w1);
    input.removeWidget(&w1);
    EXPECT_EQ(input.captured(), nullptr);
}

TEST(InputState, DisabledWidgetIgnored) {
    InputState input;
    StubWidget w1;
    w1.setBounds(0, 0, 100, 30);
    w1.setEnabled(false);
    input.addWidget(&w1);

    bool consumed = input.onMouseDown(50, 15, 1);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(input.captured(), nullptr);
}

// ========================= MenuBar =========================

TEST(MenuBar, ClickOpensMenu) {
    MenuBar bar;
    bar.addMenu("File", {
        {"New", "Ctrl+N", nullptr},
        {"Open", "Ctrl+O", nullptr},
    });

    bool consumed = bar.handleClick(20, 12);
    EXPECT_TRUE(consumed);
    EXPECT_TRUE(bar.isOpen());
}

TEST(MenuBar, ClickItemFiresAction) {
    MenuBar bar;
    bool fired = false;
    bar.addMenu("File", {
        {"New", "Ctrl+N", [&]() { fired = true; }},
    });

    bar.handleClick(20, 12);
    bar.handleClick(20, MenuBar::kMenuBarHeight + 12);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(bar.isOpen());
}

TEST(MenuBar, ClickOutsideCloses) {
    MenuBar bar;
    bar.addMenu("File", {{"New", "", nullptr}});

    bar.handleClick(20, 12);
    EXPECT_TRUE(bar.isOpen());
    bar.handleClick(500, 500);
    EXPECT_FALSE(bar.isOpen());
}

TEST(MenuBar, HoverSwitchesMenu) {
    MenuBar bar;
    bar.addMenu("File", {{"New", "", nullptr}});
    bar.addMenu("Edit", {{"Undo", "", nullptr}});

    bar.handleClick(20, 12);
    bar.handleMouseMove(100, 12);
}
