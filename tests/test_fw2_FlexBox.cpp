// UI v2 — FlexBox tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/FlexBox.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

namespace {

// Test widget with manual preferred size — avoids font dependency.
class FixedBox : public Widget {
public:
    Size preferred;
    FixedBox(float w, float h) : preferred{w, h} {
        // Default SizePolicy (flexWeight=0) auto-boundary.
    }
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain(preferred);
    }
};

// Flex-weighted test widget.
class FlexBox2Box : public Widget {
public:
    Size preferred{0, 0};
    FlexBox2Box(float w, float h, float weight) : preferred{w, h} {
        setSizePolicy(SizePolicy::flex(weight));
    }
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain(preferred);
    }
};

class FlexHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

} // anon

// ─── Row layout ─────────────────────────────────────────────────────

TEST_F(FlexHarness, RowFixedChildrenSideBySide) {
    FlexBox box(Direction::Row);
    FixedBox a(30, 20), b(50, 20), c(20, 20);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    box.measure(Constraints::loose(400, 200), ctx);
    box.layout(Rect{0, 0, 400, 200}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(a.bounds().w, 30);
    EXPECT_FLOAT_EQ(b.bounds().x, 30);
    EXPECT_FLOAT_EQ(b.bounds().w, 50);
    EXPECT_FLOAT_EQ(c.bounds().x, 80);
    EXPECT_FLOAT_EQ(c.bounds().w, 20);
}

TEST_F(FlexHarness, RowWithGap) {
    FlexBox box(Direction::Row);
    box.setGap(10);
    FixedBox a(30, 20), b(30, 20);
    box.addChild(&a); box.addChild(&b);

    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(b.bounds().x, 40);  // 30 + 10
}

// ─── Column layout ──────────────────────────────────────────────────

TEST_F(FlexHarness, ColumnStacksVertically) {
    FlexBox box(Direction::Column);
    FixedBox a(100, 30), b(100, 50), c(100, 20);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().y, 0);
    EXPECT_FLOAT_EQ(a.bounds().h, 30);
    EXPECT_FLOAT_EQ(b.bounds().y, 30);
    EXPECT_FLOAT_EQ(b.bounds().h, 50);
    EXPECT_FLOAT_EQ(c.bounds().y, 80);
    EXPECT_FLOAT_EQ(c.bounds().h, 20);
}

// ─── Flex distribution ──────────────────────────────────────────────

TEST_F(FlexHarness, SingleFlexFillsRemaining) {
    FlexBox box(Direction::Row);
    FixedBox a(50, 30);
    FlexBox2Box flex(0, 30, 1);
    FixedBox b(50, 30);
    box.addChild(&a); box.addChild(&flex); box.addChild(&b);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().w, 50);
    EXPECT_FLOAT_EQ(flex.bounds().w, 200);   // 300 - 50 - 50
    EXPECT_FLOAT_EQ(b.bounds().w, 50);
}

TEST_F(FlexHarness, MultipleFlexSplitsByWeight) {
    FlexBox box(Direction::Row);
    FlexBox2Box a(0, 30, 1);
    FlexBox2Box b(0, 30, 2);
    FlexBox2Box c(0, 30, 1);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    box.measure(Constraints::loose(400, 100), ctx);
    box.layout(Rect{0, 0, 400, 100}, ctx);

    // 1:2:1 split of 400 = 100 / 200 / 100
    EXPECT_FLOAT_EQ(a.bounds().w, 100);
    EXPECT_FLOAT_EQ(b.bounds().w, 200);
    EXPECT_FLOAT_EQ(c.bounds().w, 100);
}

// ─── Justify ────────────────────────────────────────────────────────

TEST_F(FlexHarness, JustifyStart) {
    FlexBox box(Direction::Row);
    box.setJustify(Justify::Start);
    FixedBox a(50, 30), b(50, 30);
    box.addChild(&a); box.addChild(&b);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(b.bounds().x, 50);
}

TEST_F(FlexHarness, JustifyCenter) {
    FlexBox box(Direction::Row);
    box.setJustify(Justify::Center);
    FixedBox a(50, 30), b(50, 30);
    box.addChild(&a); box.addChild(&b);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    // Total content = 100; viewport 300; extra 200 → 100 start offset
    EXPECT_FLOAT_EQ(a.bounds().x, 100);
    EXPECT_FLOAT_EQ(b.bounds().x, 150);
}

TEST_F(FlexHarness, JustifyEnd) {
    FlexBox box(Direction::Row);
    box.setJustify(Justify::End);
    FixedBox a(50, 30), b(50, 30);
    box.addChild(&a); box.addChild(&b);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 200);
    EXPECT_FLOAT_EQ(b.bounds().x, 250);
}

TEST_F(FlexHarness, JustifySpaceBetween) {
    FlexBox box(Direction::Row);
    box.setJustify(Justify::SpaceBetween);
    FixedBox a(50, 30), b(50, 30), c(50, 30);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    // 3 children, 150 content + 150 remaining = 75 between each pair
    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(b.bounds().x, 125);  // 50 + 75
    EXPECT_FLOAT_EQ(c.bounds().x, 250);  // 50 + 75 + 50 + 75
}

// ─── Cross-axis alignment ───────────────────────────────────────────

TEST_F(FlexHarness, AlignStretch) {
    FlexBox box(Direction::Row);
    box.setAlign(Align::Stretch);
    FixedBox a(30, 20);
    box.addChild(&a);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    // Stretch to full cross height
    EXPECT_FLOAT_EQ(a.bounds().h, 100);
}

TEST_F(FlexHarness, AlignCenter) {
    FlexBox box(Direction::Row);
    box.setAlign(Align::Center);
    FixedBox a(30, 20);
    box.addChild(&a);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().h, 20);
    EXPECT_FLOAT_EQ(a.bounds().y, 40);  // centered: (100-20)/2
}

TEST_F(FlexHarness, AlignStart) {
    FlexBox box(Direction::Row);
    box.setAlign(Align::Start);
    FixedBox a(30, 20);
    box.addChild(&a);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().y, 0);
    EXPECT_FLOAT_EQ(a.bounds().h, 20);
}

// ─── Padding ────────────────────────────────────────────────────────

TEST_F(FlexHarness, PaddingReducesChildArea) {
    FlexBox box(Direction::Row);
    box.setPadding(Insets{10});
    FixedBox a(30, 30);
    box.addChild(&a);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 10);
    EXPECT_FLOAT_EQ(a.bounds().y, 10);
}

// ─── Visibility ─────────────────────────────────────────────────────

TEST_F(FlexHarness, InvisibleChildrenSkipped) {
    FlexBox box(Direction::Row);
    FixedBox a(50, 30), b(50, 30), c(50, 30);
    b.setVisible(false);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    // b invisible, c starts where a ends (no gap since gap=0)
    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(c.bounds().x, 50);
}

// ─── Measure result ─────────────────────────────────────────────────

TEST_F(FlexHarness, RowMeasureSumsChildren) {
    FlexBox box(Direction::Row);
    FixedBox a(30, 20), b(50, 30);
    box.addChild(&a); box.addChild(&b);

    Size s = box.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 80);   // 30 + 50
    EXPECT_FLOAT_EQ(s.h, 30);   // max(20, 30)
}

TEST_F(FlexHarness, ColumnMeasureSumsChildren) {
    FlexBox box(Direction::Column);
    FixedBox a(30, 20), b(50, 30);
    box.addChild(&a); box.addChild(&b);

    Size s = box.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 50);   // max(30, 50)
    EXPECT_FLOAT_EQ(s.h, 50);   // 20 + 30
}

// ─── Cache ──────────────────────────────────────────────────────────

TEST_F(FlexHarness, RepeatedLayoutIsIdempotent) {
    FlexBox box(Direction::Row);
    FixedBox a(50, 30), b(50, 30);
    box.addChild(&a); box.addChild(&b);

    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);
    const Rect aFirst = a.bounds();
    box.layout(Rect{0, 0, 300, 100}, ctx);
    EXPECT_EQ(a.bounds(), aFirst);
}
