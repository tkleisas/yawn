#include <gtest/gtest.h>
#include "ui/framework/FlexBox.h"

using namespace yawn::ui::fw;

class FixedWidget : public Widget {
public:
    Size preferred;
    FixedWidget(float w, float h) : preferred{w, h} {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain(preferred);
    }
};

// ─── Column layout ──────────────────────────────────────────────────────────

TEST(FlexBoxColumn, FixedChildrenStackVertically) {
    FlexBox box(Direction::Column);
    FixedWidget a(100, 30), b(100, 50), c(100, 20);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    UIContext ctx;
    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().y, 0);
    EXPECT_FLOAT_EQ(a.bounds().h, 30);
    EXPECT_FLOAT_EQ(b.bounds().y, 30);
    EXPECT_FLOAT_EQ(b.bounds().h, 50);
    EXPECT_FLOAT_EQ(c.bounds().y, 80);
    EXPECT_FLOAT_EQ(c.bounds().h, 20);
}

TEST(FlexBoxColumn, GapBetweenChildren) {
    FlexBox box(Direction::Column);
    box.setGap(10);
    FixedWidget a(100, 30), b(100, 30);
    box.addChild(&a); box.addChild(&b);

    UIContext ctx;
    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().y, 0);
    EXPECT_FLOAT_EQ(b.bounds().y, 40);  // 30 + 10 gap
}

TEST(FlexBoxColumn, FlexChildFillsRemaining) {
    FlexBox box(Direction::Column);
    FixedWidget fixed(100, 50);
    FixedWidget flexible(100, 0);
    flexible.setSizePolicy(SizePolicy::flex(1.0f));
    box.addChild(&fixed);
    box.addChild(&flexible);

    UIContext ctx;
    box.measure(Constraints::loose(200, 300), ctx);
    box.layout(Rect{0, 0, 200, 300}, ctx);

    EXPECT_FLOAT_EQ(fixed.bounds().h, 50);
    EXPECT_FLOAT_EQ(flexible.bounds().y, 50);
    EXPECT_FLOAT_EQ(flexible.bounds().h, 250);  // 300 - 50
}

TEST(FlexBoxColumn, TwoFlexChildrenSplitEvenly) {
    FlexBox box(Direction::Column);
    FixedWidget a(100, 0), b(100, 0);
    a.setSizePolicy(SizePolicy::flex(1.0f));
    b.setSizePolicy(SizePolicy::flex(1.0f));
    box.addChild(&a); box.addChild(&b);

    UIContext ctx;
    box.measure(Constraints::loose(200, 300), ctx);
    box.layout(Rect{0, 0, 200, 300}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().h, 150);
    EXPECT_FLOAT_EQ(b.bounds().h, 150);
}

TEST(FlexBoxColumn, FlexWeightsProportional) {
    FlexBox box(Direction::Column);
    FixedWidget a(100, 0), b(100, 0);
    a.setSizePolicy(SizePolicy::flex(1.0f));
    b.setSizePolicy(SizePolicy::flex(3.0f));
    box.addChild(&a); box.addChild(&b);

    UIContext ctx;
    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().h, 100);  // 1/4 of 400
    EXPECT_FLOAT_EQ(b.bounds().h, 300);  // 3/4 of 400
}

TEST(FlexBoxColumn, StretchAlign) {
    FlexBox box(Direction::Column);
    box.setAlign(Align::Stretch);
    FixedWidget child(50, 30);
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().w, 200);  // Stretched to full width
}

TEST(FlexBoxColumn, CenterAlign) {
    FlexBox box(Direction::Column);
    box.setAlign(Align::Center);
    FixedWidget child(50, 30);
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 75);  // (200 - 50) / 2
    EXPECT_FLOAT_EQ(child.bounds().w, 50);
}

TEST(FlexBoxColumn, EndAlign) {
    FlexBox box(Direction::Column);
    box.setAlign(Align::End);
    FixedWidget child(50, 30);
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 150);  // 200 - 50
}

// ─── Row layout ─────────────────────────────────────────────────────────────

TEST(FlexBoxRow, FixedChildrenArrangeHorizontally) {
    FlexBox box(Direction::Row);
    FixedWidget a(40, 100), b(60, 100), c(30, 100);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    UIContext ctx;
    box.measure(Constraints::loose(400, 200), ctx);
    box.layout(Rect{0, 0, 400, 200}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(a.bounds().w, 40);
    EXPECT_FLOAT_EQ(b.bounds().x, 40);
    EXPECT_FLOAT_EQ(b.bounds().w, 60);
    EXPECT_FLOAT_EQ(c.bounds().x, 100);
    EXPECT_FLOAT_EQ(c.bounds().w, 30);
}

TEST(FlexBoxRow, FlexFillsHorizontal) {
    FlexBox box(Direction::Row);
    FixedWidget fixed(50, 100);
    FixedWidget flex(0, 100);
    flex.setSizePolicy(SizePolicy::flex(1.0f));
    box.addChild(&fixed); box.addChild(&flex);

    UIContext ctx;
    box.measure(Constraints::loose(300, 100), ctx);
    box.layout(Rect{0, 0, 300, 100}, ctx);

    EXPECT_FLOAT_EQ(fixed.bounds().w, 50);
    EXPECT_FLOAT_EQ(flex.bounds().x, 50);
    EXPECT_FLOAT_EQ(flex.bounds().w, 250);
}

TEST(FlexBoxRow, GapBetweenChildren) {
    FlexBox box(Direction::Row);
    box.setGap(5);
    FixedWidget a(40, 100), b(40, 100), c(40, 100);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    UIContext ctx;
    box.measure(Constraints::loose(400, 200), ctx);
    box.layout(Rect{0, 0, 400, 200}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(b.bounds().x, 45);  // 40 + 5
    EXPECT_FLOAT_EQ(c.bounds().x, 90);  // 45 + 40 + 5
}

// ─── Justify ────────────────────────────────────────────────────────────────

TEST(FlexBoxJustify, Center) {
    FlexBox box(Direction::Row);
    box.setJustify(Justify::Center);
    FixedWidget child(100, 50);
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(400, 200), ctx);
    box.layout(Rect{0, 0, 400, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 150);  // (400 - 100) / 2
}

TEST(FlexBoxJustify, End) {
    FlexBox box(Direction::Row);
    box.setJustify(Justify::End);
    FixedWidget child(100, 50);
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(400, 200), ctx);
    box.layout(Rect{0, 0, 400, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 300);  // 400 - 100
}

TEST(FlexBoxJustify, SpaceBetween) {
    FlexBox box(Direction::Row);
    box.setJustify(Justify::SpaceBetween);
    FixedWidget a(50, 50), b(50, 50), c(50, 50);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    UIContext ctx;
    box.measure(Constraints::loose(350, 200), ctx);
    box.layout(Rect{0, 0, 350, 200}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    // Remaining: 350 - 150 = 200, split 2 gaps = 100 each
    EXPECT_FLOAT_EQ(b.bounds().x, 150);  // 50 + 100
    EXPECT_FLOAT_EQ(c.bounds().x, 300);  // 150 + 50 + 100
}

// ─── Padding ────────────────────────────────────────────────────────────────

TEST(FlexBoxPadding, PaddingOffsetsChildren) {
    FlexBox box(Direction::Column);
    box.setPadding(Insets(10, 20, 10, 20));
    FixedWidget child(100, 50);
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(400, 400), ctx);
    box.layout(Rect{0, 0, 400, 400}, ctx);

    // Child should be inside padding: x=20 (left padding), y=10 (top padding)
    EXPECT_FLOAT_EQ(child.bounds().x, 20);
    EXPECT_FLOAT_EQ(child.bounds().y, 10);
}

TEST(FlexBoxPadding, FlexAccountsForPadding) {
    FlexBox box(Direction::Column);
    box.setPadding(Insets(10));
    FixedWidget child(100, 0);
    child.setSizePolicy(SizePolicy::flex(1.0f));
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    // Flex fills inner height: 200 - 10 - 10 = 180
    EXPECT_FLOAT_EQ(child.bounds().h, 180);
}

// ─── Visibility ─────────────────────────────────────────────────────────────

TEST(FlexBoxVisibility, HiddenChildSkipped) {
    FlexBox box(Direction::Column);
    FixedWidget a(100, 30), b(100, 40), c(100, 50);
    b.setVisible(false);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    UIContext ctx;
    box.measure(Constraints::loose(200, 400), ctx);
    box.layout(Rect{0, 0, 200, 400}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().y, 0);
    EXPECT_FLOAT_EQ(a.bounds().h, 30);
    EXPECT_FLOAT_EQ(c.bounds().y, 30);  // b is skipped
    EXPECT_FLOAT_EQ(c.bounds().h, 50);
}

// ─── Min/Max constraints ────────────────────────────────────────────────────

TEST(FlexBoxMinMax, FlexChildRespectsMinSize) {
    FlexBox box(Direction::Column);
    FixedWidget a(100, 0), b(100, 0);
    a.setSizePolicy(SizePolicy::flexMin(1.0f, 100.0f));  // Min 100
    b.setSizePolicy(SizePolicy::flex(1.0f));
    box.addChild(&a); box.addChild(&b);

    UIContext ctx;
    box.measure(Constraints::loose(200, 150), ctx);
    box.layout(Rect{0, 0, 200, 150}, ctx);

    // 150 / 2 = 75, but a has min 100
    EXPECT_GE(a.bounds().h, 100.0f);
}

// ─── Measure returns correct size ───────────────────────────────────────────

TEST(FlexBoxMeasure, ColumnMeasureSumsHeights) {
    FlexBox box(Direction::Column);
    FixedWidget a(100, 30), b(80, 50);
    box.addChild(&a); box.addChild(&b);

    UIContext ctx;
    Size s = box.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.h, 80);   // 30 + 50
    EXPECT_FLOAT_EQ(s.w, 100);  // max width
}

TEST(FlexBoxMeasure, RowMeasureSumsWidths) {
    FlexBox box(Direction::Row);
    FixedWidget a(40, 100), b(60, 80);
    box.addChild(&a); box.addChild(&b);

    UIContext ctx;
    Size s = box.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.w, 100);  // 40 + 60
    EXPECT_FLOAT_EQ(s.h, 100);  // max height
}

TEST(FlexBoxMeasure, GapIncludedInMeasure) {
    FlexBox box(Direction::Column);
    box.setGap(10);
    FixedWidget a(100, 30), b(100, 30), c(100, 30);
    box.addChild(&a); box.addChild(&b); box.addChild(&c);

    UIContext ctx;
    Size s = box.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.h, 110);  // 30 + 10 + 30 + 10 + 30
}

// ─── Bounds offset ──────────────────────────────────────────────────────────

TEST(FlexBoxBounds, NonZeroOrigin) {
    FlexBox box(Direction::Column);
    FixedWidget child(100, 50);
    box.addChild(&child);

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{100, 200, 200, 200}, ctx);

    // Child should be positioned at (100, 200) since box starts there
    EXPECT_FLOAT_EQ(child.bounds().x, 100);
    EXPECT_FLOAT_EQ(child.bounds().y, 200);
}
