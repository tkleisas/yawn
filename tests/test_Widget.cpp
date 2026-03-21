#include <gtest/gtest.h>
#include "ui/framework/Widget.h"

using namespace yawn::ui::fw;

// ─── Test widget that tracks calls ──────────────────────────────────────────

class TestWidget : public Widget {
public:
    Size preferredSize{50, 30};
    int paintCount = 0;
    int mouseDownCount = 0;
    bool consumeMouseDown = false;

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain(preferredSize);
    }

    void paint(UIContext&) override { ++paintCount; }

    bool onMouseDown(MouseEvent& e) override {
        ++mouseDownCount;
        if (consumeMouseDown) e.consume();
        return consumeMouseDown;
    }
};

// ─── Tree management ────────────────────────────────────────────────────────

TEST(WidgetTree, AddChild) {
    Widget parent;
    TestWidget child;
    parent.addChild(&child);
    EXPECT_EQ(parent.childCount(), 1);
    EXPECT_EQ(parent.childAt(0), &child);
    EXPECT_EQ(child.parent(), &parent);
}

TEST(WidgetTree, RemoveChild) {
    Widget parent;
    TestWidget child;
    parent.addChild(&child);
    parent.removeChild(&child);
    EXPECT_EQ(parent.childCount(), 0);
    EXPECT_EQ(child.parent(), nullptr);
}

TEST(WidgetTree, RemoveAllChildren) {
    Widget parent;
    TestWidget a, b, c;
    parent.addChild(&a);
    parent.addChild(&b);
    parent.addChild(&c);
    EXPECT_EQ(parent.childCount(), 3);
    parent.removeAllChildren();
    EXPECT_EQ(parent.childCount(), 0);
    EXPECT_EQ(a.parent(), nullptr);
    EXPECT_EQ(b.parent(), nullptr);
    EXPECT_EQ(c.parent(), nullptr);
}

TEST(WidgetTree, ReparentMovesChild) {
    Widget parent1, parent2;
    TestWidget child;
    parent1.addChild(&child);
    EXPECT_EQ(parent1.childCount(), 1);

    parent2.addChild(&child);
    EXPECT_EQ(parent1.childCount(), 0);
    EXPECT_EQ(parent2.childCount(), 1);
    EXPECT_EQ(child.parent(), &parent2);
}

TEST(WidgetTree, AddSelfIsNoOp) {
    Widget w;
    w.addChild(&w);
    EXPECT_EQ(w.childCount(), 0);
}

TEST(WidgetTree, ChildAtOutOfRange) {
    Widget w;
    EXPECT_EQ(w.childAt(-1), nullptr);
    EXPECT_EQ(w.childAt(0), nullptr);
}

// ─── Geometry ───────────────────────────────────────────────────────────────

TEST(WidgetGeometry, DefaultBounds) {
    Widget w;
    EXPECT_EQ(w.bounds(), Rect());
}

TEST(WidgetGeometry, LayoutSetsBounds) {
    Widget w;
    UIContext ctx;
    w.layout(Rect{10, 20, 100, 50}, ctx);
    EXPECT_EQ(w.bounds(), Rect(10, 20, 100, 50));
}

TEST(WidgetGeometry, GlobalBoundsNoParent) {
    Widget w;
    UIContext ctx;
    w.layout(Rect{10, 20, 100, 50}, ctx);
    EXPECT_EQ(w.globalBounds(), Rect(10, 20, 100, 50));
}

TEST(WidgetGeometry, GlobalBoundsWithParent) {
    Widget parent;
    Widget child;
    parent.addChild(&child);

    UIContext ctx;
    parent.layout(Rect{100, 200, 400, 300}, ctx);
    child.layout(Rect{10, 20, 50, 30}, ctx);

    auto gb = child.globalBounds();
    EXPECT_FLOAT_EQ(gb.x, 110.0f);
    EXPECT_FLOAT_EQ(gb.y, 220.0f);
    EXPECT_FLOAT_EQ(gb.w, 50.0f);
    EXPECT_FLOAT_EQ(gb.h, 30.0f);
}

TEST(WidgetGeometry, GlobalBoundsDeepNesting) {
    Widget root, mid, leaf;
    root.addChild(&mid);
    mid.addChild(&leaf);

    UIContext ctx;
    root.layout(Rect{10, 10, 500, 500}, ctx);
    mid.layout(Rect{20, 30, 200, 200}, ctx);
    leaf.layout(Rect{5, 5, 50, 50}, ctx);

    auto gb = leaf.globalBounds();
    EXPECT_FLOAT_EQ(gb.x, 35.0f);  // 10 + 20 + 5
    EXPECT_FLOAT_EQ(gb.y, 45.0f);  // 10 + 30 + 5
}

TEST(WidgetGeometry, HitTestLocal) {
    Widget w;
    UIContext ctx;
    w.layout(Rect{0, 0, 100, 50}, ctx);
    EXPECT_TRUE(w.hitTest(0, 0));
    EXPECT_TRUE(w.hitTest(50, 25));
    EXPECT_TRUE(w.hitTest(99.9f, 49.9f));
    EXPECT_FALSE(w.hitTest(-1, 0));
    EXPECT_FALSE(w.hitTest(100, 0));
    EXPECT_FALSE(w.hitTest(0, 50));
}

TEST(WidgetGeometry, HitTestGlobal) {
    Widget parent;
    Widget child;
    parent.addChild(&child);

    UIContext ctx;
    parent.layout(Rect{100, 100, 400, 300}, ctx);
    child.layout(Rect{10, 10, 50, 50}, ctx);

    EXPECT_TRUE(child.hitTestGlobal(115, 115));
    EXPECT_FALSE(child.hitTestGlobal(100, 100));
}

TEST(WidgetGeometry, ToLocalAndBack) {
    Widget parent;
    Widget child;
    parent.addChild(&child);

    UIContext ctx;
    parent.layout(Rect{100, 200, 400, 300}, ctx);
    child.layout(Rect{10, 20, 50, 30}, ctx);

    auto local = child.toLocal(120, 230);
    EXPECT_FLOAT_EQ(local.x, 10.0f);
    EXPECT_FLOAT_EQ(local.y, 10.0f);

    auto global = child.toGlobal(10, 10);
    EXPECT_FLOAT_EQ(global.x, 120.0f);
    EXPECT_FLOAT_EQ(global.y, 230.0f);
}

// ─── Measure ────────────────────────────────────────────────────────────────

TEST(WidgetMeasure, DefaultReturnsZero) {
    Widget w;
    UIContext ctx;
    auto s = w.measure(Constraints::loose(200, 200), ctx);
    EXPECT_FLOAT_EQ(s.w, 0.0f);
    EXPECT_FLOAT_EQ(s.h, 0.0f);
}

TEST(WidgetMeasure, TestWidgetReturnsPreferred) {
    TestWidget w;
    w.preferredSize = {80, 40};
    UIContext ctx;
    auto s = w.measure(Constraints::loose(200, 200), ctx);
    EXPECT_FLOAT_EQ(s.w, 80.0f);
    EXPECT_FLOAT_EQ(s.h, 40.0f);
}

TEST(WidgetMeasure, ConstrainedSize) {
    TestWidget w;
    w.preferredSize = {200, 100};
    UIContext ctx;
    auto s = w.measure(Constraints(0, 0, 150, 80), ctx);
    EXPECT_FLOAT_EQ(s.w, 150.0f);
    EXPECT_FLOAT_EQ(s.h, 80.0f);
}

// ─── Render ─────────────────────────────────────────────────────────────────

TEST(WidgetRender, PaintCalledWhenVisible) {
    TestWidget w;
    UIContext ctx;
    w.render(ctx);
    EXPECT_EQ(w.paintCount, 1);
}

TEST(WidgetRender, NotPaintedWhenHidden) {
    TestWidget w;
    w.setVisible(false);
    UIContext ctx;
    w.render(ctx);
    EXPECT_EQ(w.paintCount, 0);
}

TEST(WidgetRender, ChildrenRendered) {
    Widget parent;
    TestWidget a, b;
    parent.addChild(&a);
    parent.addChild(&b);

    UIContext ctx;
    parent.render(ctx);
    EXPECT_EQ(a.paintCount, 1);
    EXPECT_EQ(b.paintCount, 1);
}

// ─── Event dispatch ─────────────────────────────────────────────────────────

TEST(WidgetEvents, MouseDownHitsChild) {
    Widget root;
    TestWidget child;
    child.consumeMouseDown = true;
    root.addChild(&child);

    UIContext ctx;
    root.layout(Rect{0, 0, 200, 200}, ctx);
    child.layout(Rect{10, 10, 50, 50}, ctx);

    MouseEvent e;
    e.x = 25;  // Inside child (global: 10+10=20..60, so 25 is inside)
    e.y = 25;
    e.button = MouseButton::Left;

    Widget* hit = root.dispatchMouseDown(e);
    EXPECT_EQ(hit, &child);
    EXPECT_EQ(child.mouseDownCount, 1);
}

TEST(WidgetEvents, MouseDownMissesChild) {
    Widget root;
    TestWidget child;
    child.consumeMouseDown = true;
    root.addChild(&child);

    UIContext ctx;
    root.layout(Rect{0, 0, 200, 200}, ctx);
    child.layout(Rect{10, 10, 50, 50}, ctx);

    MouseEvent e;
    e.x = 100;  // Outside child
    e.y = 100;
    e.button = MouseButton::Left;

    Widget* hit = root.dispatchMouseDown(e);
    // Root doesn't consume, so nullptr
    EXPECT_EQ(hit, nullptr);
}

TEST(WidgetEvents, TopmostChildWins) {
    Widget root;
    TestWidget bottom, top;
    bottom.consumeMouseDown = true;
    top.consumeMouseDown = true;
    root.addChild(&bottom);
    root.addChild(&top);  // top is last = topmost

    UIContext ctx;
    root.layout(Rect{0, 0, 200, 200}, ctx);
    bottom.layout(Rect{10, 10, 100, 100}, ctx);
    top.layout(Rect{10, 10, 100, 100}, ctx);  // Overlapping

    MouseEvent e;
    e.x = 50; e.y = 50;
    e.button = MouseButton::Left;

    Widget* hit = root.dispatchMouseDown(e);
    EXPECT_EQ(hit, &top);
    EXPECT_EQ(top.mouseDownCount, 1);
    EXPECT_EQ(bottom.mouseDownCount, 0);
}

TEST(WidgetEvents, DisabledWidgetIgnored) {
    Widget root;
    TestWidget child;
    child.consumeMouseDown = true;
    child.setEnabled(false);
    root.addChild(&child);

    UIContext ctx;
    root.layout(Rect{0, 0, 200, 200}, ctx);
    child.layout(Rect{10, 10, 50, 50}, ctx);

    MouseEvent e;
    e.x = 25; e.y = 25;
    e.button = MouseButton::Left;

    Widget* hit = root.dispatchMouseDown(e);
    EXPECT_EQ(hit, nullptr);
}

TEST(WidgetEvents, HiddenWidgetIgnored) {
    Widget root;
    TestWidget child;
    child.consumeMouseDown = true;
    child.setVisible(false);
    root.addChild(&child);

    UIContext ctx;
    root.layout(Rect{0, 0, 200, 200}, ctx);
    child.layout(Rect{10, 10, 50, 50}, ctx);

    MouseEvent e;
    e.x = 25; e.y = 25;
    e.button = MouseButton::Left;

    Widget* hit = root.dispatchMouseDown(e);
    EXPECT_EQ(hit, nullptr);
}

TEST(WidgetEvents, FindWidgetAt) {
    Widget root;
    TestWidget a, b;
    root.addChild(&a);
    root.addChild(&b);

    UIContext ctx;
    root.layout(Rect{0, 0, 200, 200}, ctx);
    a.layout(Rect{0, 0, 100, 100}, ctx);
    b.layout(Rect{50, 50, 100, 100}, ctx);

    // In overlap region — b is topmost (added last)
    Widget* hit = root.findWidgetAt(75, 75);
    EXPECT_EQ(hit, &b);

    // Only in a
    hit = root.findWidgetAt(10, 10);
    EXPECT_EQ(hit, &a);

    // Only in root
    hit = root.findWidgetAt(180, 180);
    EXPECT_EQ(hit, &root);

    // Outside
    hit = root.findWidgetAt(300, 300);
    EXPECT_EQ(hit, nullptr);
}

// ─── State ──────────────────────────────────────────────────────────────────

TEST(WidgetState, DefaultState) {
    Widget w;
    EXPECT_TRUE(w.isVisible());
    EXPECT_TRUE(w.isEnabled());
    EXPECT_FALSE(w.isFocused());
    EXPECT_FALSE(w.isHovered());
}

TEST(WidgetState, SizePolicy) {
    Widget w;
    EXPECT_FLOAT_EQ(w.sizePolicy().flexWeight, 0.0f);
    w.setSizePolicy(SizePolicy::flex(2.0f));
    EXPECT_FLOAT_EQ(w.sizePolicy().flexWeight, 2.0f);
}

TEST(WidgetState, Padding) {
    Widget w;
    w.setPadding(Insets(5, 10));
    EXPECT_FLOAT_EQ(w.padding().top, 5.0f);
    EXPECT_FLOAT_EQ(w.padding().right, 10.0f);
}

TEST(WidgetState, Name) {
    Widget w;
    w.setName("TransportBar");
    EXPECT_EQ(w.name(), "TransportBar");
}
