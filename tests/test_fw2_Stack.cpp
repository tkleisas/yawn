// UI v2 — Stack tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/Stack.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

namespace {
class FixedBox : public Widget {
public:
    Size preferred;
    FixedBox(float w, float h) : preferred{w, h} {}
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain(preferred);
    }
};
} // anon

class StackHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

// ─── Measure ───────────────────────────────────────────────────────

TEST_F(StackHarness, EmptyStackIsZero) {
    Stack s;
    Size sz = s.measure(Constraints::loose(500, 500), ctx);
    EXPECT_FLOAT_EQ(sz.w, 0.0f);
    EXPECT_FLOAT_EQ(sz.h, 0.0f);
}

TEST_F(StackHarness, LargestChildDrivesSize) {
    Stack s;
    FixedBox a(50, 40);
    FixedBox b(80, 30);
    FixedBox c(40, 70);
    s.addChild(&a, StackAlign::Center);
    s.addChild(&b, StackAlign::Center);
    s.addChild(&c, StackAlign::Center);

    Size sz = s.measure(Constraints::loose(500, 500), ctx);
    EXPECT_FLOAT_EQ(sz.w, 80.0f);
    EXPECT_FLOAT_EQ(sz.h, 70.0f);
}

TEST_F(StackHarness, PreferredSizeOverrides) {
    Stack s;
    FixedBox a(50, 40);
    s.addChild(&a, StackAlign::Fill);
    s.setPreferredSize({200, 100});

    Size sz = s.measure(Constraints::loose(500, 500), ctx);
    EXPECT_FLOAT_EQ(sz.w, 200.0f);
    EXPECT_FLOAT_EQ(sz.h, 100.0f);
}

// ─── Layout ────────────────────────────────────────────────────────

TEST_F(StackHarness, FillChildExpandsToStack) {
    Stack s;
    FixedBox a(50, 40);   // intrinsic smaller than the stack
    s.addChild(&a, StackAlign::Fill);

    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{10, 20, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 10);
    EXPECT_FLOAT_EQ(a.bounds().y, 20);
    EXPECT_FLOAT_EQ(a.bounds().w, 200);
    EXPECT_FLOAT_EQ(a.bounds().h, 100);
}

TEST_F(StackHarness, CenterAlignsChild) {
    Stack s;
    FixedBox a(60, 40);
    s.addChild(&a, StackAlign::Center);

    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{0, 0, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, (200 - 60) * 0.5f);
    EXPECT_FLOAT_EQ(a.bounds().y, (100 - 40) * 0.5f);
    EXPECT_FLOAT_EQ(a.bounds().w, 60);
    EXPECT_FLOAT_EQ(a.bounds().h, 40);
}

TEST_F(StackHarness, TopLeftAligns) {
    Stack s;
    FixedBox a(60, 40);
    s.addChild(&a, StackAlign::TopLeft);

    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{0, 0, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(a.bounds().y, 0);
}

TEST_F(StackHarness, BottomRightAligns) {
    Stack s;
    FixedBox a(60, 40);
    s.addChild(&a, StackAlign::BottomRight);

    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{0, 0, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 200 - 60);
    EXPECT_FLOAT_EQ(a.bounds().y, 100 - 40);
}

TEST_F(StackHarness, InsetsShrinkContainer) {
    Stack s;
    FixedBox a(60, 40);
    s.addChild(&a, StackAlign::TopLeft);
    s.setInsets(&a, Insets{10});   // 10 px all sides

    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{0, 0, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 10);
    EXPECT_FLOAT_EQ(a.bounds().y, 10);
}

TEST_F(StackHarness, FillWithInsets) {
    Stack s;
    FixedBox a(50, 50);
    s.addChild(&a, StackAlign::Fill);
    s.setInsets(&a, Insets{10});

    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{0, 0, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 10);
    EXPECT_FLOAT_EQ(a.bounds().y, 10);
    EXPECT_FLOAT_EQ(a.bounds().w, 200 - 20);
    EXPECT_FLOAT_EQ(a.bounds().h, 100 - 20);
}

// ─── Paint order ──────────────────────────────────────────────────

TEST_F(StackHarness, ChildOrderPreserved) {
    Stack s;
    FixedBox a(10, 10), b(10, 10), c(10, 10);
    s.addChild(&a, StackAlign::TopLeft);
    s.addChild(&b, StackAlign::TopLeft);
    s.addChild(&c, StackAlign::TopLeft);

    const auto& kids = s.children();
    ASSERT_EQ(static_cast<int>(kids.size()), 3);
    EXPECT_EQ(kids[0], &a);
    EXPECT_EQ(kids[1], &b);
    EXPECT_EQ(kids[2], &c);
}

TEST_F(StackHarness, SetAlignmentUpdatesPlacement) {
    Stack s;
    FixedBox a(60, 40);
    s.addChild(&a, StackAlign::TopLeft);

    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{0, 0, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 0);

    s.setAlignment(&a, StackAlign::Center);
    s.measure(Constraints::loose(200, 100), ctx);
    s.layout(Rect{0, 0, 200, 100}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, (200 - 60) * 0.5f);
}
