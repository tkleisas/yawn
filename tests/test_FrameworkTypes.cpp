#include <gtest/gtest.h>
#include "ui/framework/Types.h"
#include "ui/framework/EventSystem.h"

using namespace yawn::ui::fw;

// ─── Point tests ────────────────────────────────────────────────────────────

TEST(PointTest, DefaultConstruction) {
    Point p;
    EXPECT_FLOAT_EQ(p.x, 0.0f);
    EXPECT_FLOAT_EQ(p.y, 0.0f);
}

TEST(PointTest, Arithmetic) {
    Point a{3, 4};
    Point b{1, 2};
    auto sum  = a + b;
    auto diff = a - b;
    auto scaled = a * 2.0f;

    EXPECT_FLOAT_EQ(sum.x, 4.0f);
    EXPECT_FLOAT_EQ(sum.y, 6.0f);
    EXPECT_FLOAT_EQ(diff.x, 2.0f);
    EXPECT_FLOAT_EQ(diff.y, 2.0f);
    EXPECT_FLOAT_EQ(scaled.x, 6.0f);
    EXPECT_FLOAT_EQ(scaled.y, 8.0f);
}

TEST(PointTest, Length) {
    Point p{3, 4};
    EXPECT_FLOAT_EQ(p.length(), 5.0f);
}

TEST(PointTest, Distance) {
    Point a{0, 0};
    Point b{3, 4};
    EXPECT_FLOAT_EQ(a.distanceTo(b), 5.0f);
}

TEST(PointTest, Equality) {
    EXPECT_EQ(Point(1, 2), Point(1, 2));
    EXPECT_NE(Point(1, 2), Point(3, 4));
}

// ─── Size tests ─────────────────────────────────────────────────────────────

TEST(SizeTest, DefaultConstruction) {
    Size s;
    EXPECT_FLOAT_EQ(s.w, 0.0f);
    EXPECT_FLOAT_EQ(s.h, 0.0f);
    EXPECT_TRUE(s.isEmpty());
}

TEST(SizeTest, Arithmetic) {
    Size a{10, 20};
    Size b{5, 10};
    auto sum = a + b;
    auto scaled = a * 1.5f;

    EXPECT_FLOAT_EQ(sum.w, 15.0f);
    EXPECT_FLOAT_EQ(sum.h, 30.0f);
    EXPECT_FLOAT_EQ(scaled.w, 15.0f);
    EXPECT_FLOAT_EQ(scaled.h, 30.0f);
}

TEST(SizeTest, IsEmpty) {
    EXPECT_TRUE(Size(0, 10).isEmpty());
    EXPECT_TRUE(Size(10, 0).isEmpty());
    EXPECT_TRUE(Size(-1, 10).isEmpty());
    EXPECT_FALSE(Size(1, 1).isEmpty());
}

// ─── Insets tests ───────────────────────────────────────────────────────────

TEST(InsetsTest, UniformConstruction) {
    Insets i(10);
    EXPECT_FLOAT_EQ(i.top, 10.0f);
    EXPECT_FLOAT_EQ(i.right, 10.0f);
    EXPECT_FLOAT_EQ(i.bottom, 10.0f);
    EXPECT_FLOAT_EQ(i.left, 10.0f);
    EXPECT_FLOAT_EQ(i.horizontal(), 20.0f);
    EXPECT_FLOAT_EQ(i.vertical(), 20.0f);
}

TEST(InsetsTest, TwoAxisConstruction) {
    Insets i(5, 10);
    EXPECT_FLOAT_EQ(i.top, 5.0f);
    EXPECT_FLOAT_EQ(i.right, 10.0f);
    EXPECT_FLOAT_EQ(i.bottom, 5.0f);
    EXPECT_FLOAT_EQ(i.left, 10.0f);
}

TEST(InsetsTest, FourSideConstruction) {
    Insets i(1, 2, 3, 4);
    EXPECT_FLOAT_EQ(i.top, 1.0f);
    EXPECT_FLOAT_EQ(i.right, 2.0f);
    EXPECT_FLOAT_EQ(i.bottom, 3.0f);
    EXPECT_FLOAT_EQ(i.left, 4.0f);
    EXPECT_FLOAT_EQ(i.horizontal(), 6.0f);
    EXPECT_FLOAT_EQ(i.vertical(), 4.0f);
}

// ─── Rect tests ─────────────────────────────────────────────────────────────

TEST(RectTest, DefaultConstruction) {
    Rect r;
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.w, 0.0f);
    EXPECT_FLOAT_EQ(r.h, 0.0f);
    EXPECT_TRUE(r.isEmpty());
}

TEST(RectTest, Accessors) {
    Rect r{10, 20, 100, 50};
    EXPECT_EQ(r.origin(), Point(10, 20));
    EXPECT_EQ(r.size(), Size(100, 50));
    EXPECT_FLOAT_EQ(r.right(), 110.0f);
    EXPECT_FLOAT_EQ(r.bottom(), 70.0f);
    EXPECT_EQ(r.center(), Point(60, 45));
}

TEST(RectTest, Contains) {
    Rect r{10, 10, 100, 100};
    EXPECT_TRUE(r.contains(10, 10));
    EXPECT_TRUE(r.contains(50, 50));
    EXPECT_TRUE(r.contains(109.9f, 109.9f));
    EXPECT_FALSE(r.contains(110, 110));
    EXPECT_FALSE(r.contains(9.9f, 50));
    EXPECT_FALSE(r.contains(50, 9.9f));
}

TEST(RectTest, ContainsPoint) {
    Rect r{0, 0, 50, 50};
    EXPECT_TRUE(r.contains(Point{25, 25}));
    EXPECT_FALSE(r.contains(Point{-1, 25}));
}

TEST(RectTest, Intersects) {
    Rect a{0, 0, 100, 100};
    Rect b{50, 50, 100, 100};
    Rect c{200, 200, 10, 10};

    EXPECT_TRUE(a.intersects(b));
    EXPECT_TRUE(b.intersects(a));
    EXPECT_FALSE(a.intersects(c));
}

TEST(RectTest, Intersection) {
    Rect a{0, 0, 100, 100};
    Rect b{50, 50, 100, 100};
    auto i = a.intersection(b);
    EXPECT_FLOAT_EQ(i.x, 50.0f);
    EXPECT_FLOAT_EQ(i.y, 50.0f);
    EXPECT_FLOAT_EQ(i.w, 50.0f);
    EXPECT_FLOAT_EQ(i.h, 50.0f);
}

TEST(RectTest, IntersectionNoOverlap) {
    Rect a{0, 0, 10, 10};
    Rect b{20, 20, 10, 10};
    auto i = a.intersection(b);
    EXPECT_TRUE(i.isEmpty());
}

TEST(RectTest, United) {
    Rect a{10, 10, 20, 20};
    Rect b{50, 50, 30, 30};
    auto u = a.united(b);
    EXPECT_FLOAT_EQ(u.x, 10.0f);
    EXPECT_FLOAT_EQ(u.y, 10.0f);
    EXPECT_FLOAT_EQ(u.w, 70.0f);
    EXPECT_FLOAT_EQ(u.h, 70.0f);
}

TEST(RectTest, UnitedWithEmpty) {
    Rect a{10, 10, 20, 20};
    Rect empty;
    EXPECT_EQ(a.united(empty), a);
    EXPECT_EQ(empty.united(a), a);
}

TEST(RectTest, Inset) {
    Rect r{10, 10, 100, 100};
    auto shrunk = r.inset(Insets(5));
    EXPECT_FLOAT_EQ(shrunk.x, 15.0f);
    EXPECT_FLOAT_EQ(shrunk.y, 15.0f);
    EXPECT_FLOAT_EQ(shrunk.w, 90.0f);
    EXPECT_FLOAT_EQ(shrunk.h, 90.0f);
}

TEST(RectTest, Outset) {
    Rect r{10, 10, 100, 100};
    auto expanded = r.outset(Insets(5));
    EXPECT_FLOAT_EQ(expanded.x, 5.0f);
    EXPECT_FLOAT_EQ(expanded.y, 5.0f);
    EXPECT_FLOAT_EQ(expanded.w, 110.0f);
    EXPECT_FLOAT_EQ(expanded.h, 110.0f);
}

TEST(RectTest, Translated) {
    Rect r{10, 20, 30, 40};
    auto moved = r.translated(5, -10);
    EXPECT_EQ(moved, Rect(15, 10, 30, 40));
}

// ─── SizePolicy tests ───────────────────────────────────────────────────────

TEST(SizePolicyTest, Fixed) {
    auto p = SizePolicy::fixed();
    EXPECT_FLOAT_EQ(p.flexWeight, 0.0f);
}

TEST(SizePolicyTest, Flex) {
    auto p = SizePolicy::flex(2.0f);
    EXPECT_FLOAT_EQ(p.flexWeight, 2.0f);
}

TEST(SizePolicyTest, FlexMin) {
    auto p = SizePolicy::flexMin(1.0f, 50.0f);
    EXPECT_FLOAT_EQ(p.flexWeight, 1.0f);
    EXPECT_FLOAT_EQ(p.minSize, 50.0f);
}

// ─── ScaleContext tests ─────────────────────────────────────────────────────

TEST(ScaleContextTest, DefaultScale) {
    ScaleContext sc;
    EXPECT_FLOAT_EQ(sc.dp(10.0f), 10.0f);
    EXPECT_FLOAT_EQ(sc.fromPx(10.0f), 10.0f);
}

TEST(ScaleContextTest, Scale2x) {
    ScaleContext sc{2.0f};
    EXPECT_FLOAT_EQ(sc.dp(10.0f), 20.0f);
    EXPECT_FLOAT_EQ(sc.fromPx(20.0f), 10.0f);
}

TEST(ScaleContextTest, ScaleSize) {
    ScaleContext sc{1.5f};
    auto s = sc.dp(Size{100, 200});
    EXPECT_FLOAT_EQ(s.w, 150.0f);
    EXPECT_FLOAT_EQ(s.h, 300.0f);
}

TEST(ScaleContextTest, ScaleRect) {
    ScaleContext sc{2.0f};
    auto r = sc.dp(Rect{10, 20, 30, 40});
    EXPECT_FLOAT_EQ(r.x, 20.0f);
    EXPECT_FLOAT_EQ(r.y, 40.0f);
    EXPECT_FLOAT_EQ(r.w, 60.0f);
    EXPECT_FLOAT_EQ(r.h, 80.0f);
}

TEST(ScaleContextTest, ScaleInsets) {
    ScaleContext sc{1.5f};
    auto i = sc.dp(Insets{10, 20, 30, 40});
    EXPECT_FLOAT_EQ(i.top, 15.0f);
    EXPECT_FLOAT_EQ(i.right, 30.0f);
    EXPECT_FLOAT_EQ(i.bottom, 45.0f);
    EXPECT_FLOAT_EQ(i.left, 60.0f);
}

// ─── Constraints tests ──────────────────────────────────────────────────────

TEST(ConstraintsTest, Tight) {
    auto c = Constraints::tight(100, 50);
    EXPECT_TRUE(c.isTight());
    EXPECT_FLOAT_EQ(c.minW, 100.0f);
    EXPECT_FLOAT_EQ(c.maxW, 100.0f);
    EXPECT_FLOAT_EQ(c.minH, 50.0f);
    EXPECT_FLOAT_EQ(c.maxH, 50.0f);
}

TEST(ConstraintsTest, Loose) {
    auto c = Constraints::loose(200, 100);
    EXPECT_FLOAT_EQ(c.minW, 0.0f);
    EXPECT_FLOAT_EQ(c.maxW, 200.0f);
    EXPECT_FALSE(c.isTight());
}

TEST(ConstraintsTest, Constrain) {
    auto c = Constraints(50, 50, 200, 200);
    EXPECT_EQ(c.constrain(Size{100, 100}), Size(100, 100));
    EXPECT_EQ(c.constrain(Size{10, 10}), Size(50, 50));
    EXPECT_EQ(c.constrain(Size{300, 300}), Size(200, 200));
}

TEST(ConstraintsTest, Deflate) {
    auto c = Constraints(0, 0, 200, 100);
    auto d = c.deflate(Insets(10, 20));
    EXPECT_FLOAT_EQ(d.maxW, 160.0f);
    EXPECT_FLOAT_EQ(d.maxH, 80.0f);
}

TEST(ConstraintsTest, Unbounded) {
    auto c = Constraints::unbounded();
    EXPECT_TRUE(c.isUnbounded());
}

// ─── Event system tests ─────────────────────────────────────────────────────

TEST(EventTest, Consume) {
    Event e;
    EXPECT_FALSE(e.consumed);
    e.consume();
    EXPECT_TRUE(e.consumed);
}

TEST(MouseEventTest, ButtonIdentification) {
    MouseEvent e;
    e.button = MouseButton::Left;
    EXPECT_TRUE(e.isLeftButton());
    EXPECT_FALSE(e.isRightButton());
    EXPECT_FALSE(e.isMiddleButton());
}

TEST(MouseEventTest, DoubleClick) {
    MouseEvent e;
    e.clickCount = 1;
    EXPECT_FALSE(e.isDoubleClick());
    e.clickCount = 2;
    EXPECT_TRUE(e.isDoubleClick());
}

TEST(KeyEventTest, SpecialKeys) {
    KeyEvent e;

    e.keyCode = 0x1B;
    EXPECT_TRUE(e.isEscape());

    e.keyCode = 0x0D;
    EXPECT_TRUE(e.isEnter());

    e.keyCode = 0x09;
    EXPECT_TRUE(e.isTab());

    e.keyCode = 0x7F;
    EXPECT_TRUE(e.isDelete());

    e.keyCode = 0x08;
    EXPECT_TRUE(e.isBackspace());
}

TEST(ModifiersTest, None) {
    Modifiers m;
    EXPECT_TRUE(m.none());
    m.shift = true;
    EXPECT_FALSE(m.none());
}

TEST(ScrollEventTest, Fields) {
    ScrollEvent e;
    e.x = 100; e.y = 200;
    e.dx = 0; e.dy = 3;
    e.mods.ctrl = true;

    EXPECT_FLOAT_EQ(e.x, 100.0f);
    EXPECT_FLOAT_EQ(e.dy, 3.0f);
    EXPECT_TRUE(e.mods.ctrl);
}

TEST(TextInputEventTest, Storage) {
    TextInputEvent e;
    std::strncpy(e.text, "abc", sizeof(e.text));
    EXPECT_STREQ(e.text, "abc");
}

TEST(DropFileEventTest, Fields) {
    DropFileEvent e;
    const char* path = "test.wav";
    e.path = path;
    e.x = 50; e.y = 100;
    EXPECT_STREQ(e.path, "test.wav");
}
