// UI v2 — FwGrid tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/FwGrid.h"
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

class FwGridHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

// ─── Measure ───────────────────────────────────────────────────────

TEST_F(FwGridHarness, EmptyGridIsZero) {
    FwGrid g;
    Size s = g.measure(Constraints::loose(1000, 1000), ctx);
    EXPECT_FLOAT_EQ(s.w, 0.0f);
    EXPECT_FLOAT_EQ(s.h, 0.0f);
    EXPECT_EQ(g.currentRows(), 0);
    EXPECT_EQ(g.currentCols(), 0);
}

TEST_F(FwGridHarness, SingleChildMeasure) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(0, 0);
    g.setGridPadding(0);
    FixedBox a(50, 40);
    g.addChild(&a);

    Size s = g.measure(Constraints::loose(1000, 1000), ctx);
    EXPECT_FLOAT_EQ(s.w, 50.0f);
    EXPECT_FLOAT_EQ(s.h, 40.0f);
    EXPECT_EQ(g.currentRows(), 1);
    EXPECT_EQ(g.currentCols(), 1);
}

TEST_F(FwGridHarness, EightChildrenTwoRowsFourCols) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(0, 0);
    g.setGridPadding(0);
    g.setMaxRows(2);
    std::vector<std::unique_ptr<FixedBox>> kids;
    for (int i = 0; i < 8; ++i) {
        kids.push_back(std::make_unique<FixedBox>(50, 40));
        g.addChild(kids.back().get());
    }

    Size s = g.measure(Constraints::loose(1000, 1000), ctx);
    EXPECT_EQ(g.currentRows(), 2);
    EXPECT_EQ(g.currentCols(), 4);
    EXPECT_FLOAT_EQ(s.w, 50 * 4);
    EXPECT_FLOAT_EQ(s.h, 40 * 2);
}

TEST_F(FwGridHarness, PaddingAndGapInMeasure) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(5, 10);
    g.setGridPadding(8);
    g.setMaxRows(2);
    std::vector<std::unique_ptr<FixedBox>> kids;
    for (int i = 0; i < 4; ++i) {
        kids.push_back(std::make_unique<FixedBox>(50, 40));
        g.addChild(kids.back().get());
    }

    Size s = g.measure(Constraints::loose(1000, 1000), ctx);
    // 4 children, maxRows=2 → 2 rows × 2 cols.
    // w = 2*pad + 2*cellW + 1*gapX = 16 + 100 + 5 = 121
    // h = 2*pad + 2*cellH + 1*gapY = 16 +  80 + 10 = 106
    EXPECT_FLOAT_EQ(s.w, 121.0f);
    EXPECT_FLOAT_EQ(s.h, 106.0f);
}

// ─── Layout ────────────────────────────────────────────────────────

TEST_F(FwGridHarness, RowMajorPlacement) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(0, 0);
    g.setGridPadding(0);
    g.setMaxRows(2);
    std::vector<std::unique_ptr<FixedBox>> kids;
    for (int i = 0; i < 8; ++i) {
        kids.push_back(std::make_unique<FixedBox>(50, 40));
        g.addChild(kids.back().get());
    }

    g.measure(Constraints::loose(1000, 1000), ctx);
    g.layout(Rect{0, 0, 200, 80}, ctx);

    // 8 kids, 4 cols × 2 rows. Row-major: indices 0,1,2,3 in row 0;
    // 4,5,6,7 in row 1.
    EXPECT_FLOAT_EQ(kids[0]->bounds().x, 0);
    EXPECT_FLOAT_EQ(kids[0]->bounds().y, 0);
    EXPECT_FLOAT_EQ(kids[3]->bounds().x, 150);
    EXPECT_FLOAT_EQ(kids[3]->bounds().y, 0);
    EXPECT_FLOAT_EQ(kids[4]->bounds().x, 0);
    EXPECT_FLOAT_EQ(kids[4]->bounds().y, 40);
    EXPECT_FLOAT_EQ(kids[7]->bounds().x, 150);
    EXPECT_FLOAT_EQ(kids[7]->bounds().y, 40);
}

TEST_F(FwGridHarness, OffsetByGridBoundsOrigin) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(0, 0);
    g.setGridPadding(0);
    g.setMaxRows(1);
    FixedBox a(50, 40);
    g.addChild(&a);

    g.measure(Constraints::loose(1000, 1000), ctx);
    g.layout(Rect{100, 50, 100, 50}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 100);
    EXPECT_FLOAT_EQ(a.bounds().y, 50);
}

TEST_F(FwGridHarness, PaddingOffsetsFirstCell) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(0, 0);
    g.setGridPadding(10);
    g.setMaxRows(2);
    FixedBox a(50, 40);
    g.addChild(&a);

    g.measure(Constraints::loose(1000, 1000), ctx);
    g.layout(Rect{0, 0, 200, 200}, ctx);
    EXPECT_FLOAT_EQ(a.bounds().x, 10);
    EXPECT_FLOAT_EQ(a.bounds().y, 10);
}

TEST_F(FwGridHarness, GapAddedBetweenCells) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(6, 8);
    g.setGridPadding(0);
    g.setMaxRows(2);
    FixedBox a(50, 40), b(50, 40), c(50, 40), d(50, 40);
    g.addChild(&a);
    g.addChild(&b);
    g.addChild(&c);
    g.addChild(&d);

    g.measure(Constraints::loose(1000, 1000), ctx);
    g.layout(Rect{0, 0, 200, 200}, ctx);
    // 4 kids, maxRows=2 → 2×2. Row 0: [0,1], Row 1: [2,3].
    EXPECT_FLOAT_EQ(a.bounds().x, 0);
    EXPECT_FLOAT_EQ(b.bounds().x, 50 + 6);
    EXPECT_FLOAT_EQ(c.bounds().y, 40 + 8);
}

TEST_F(FwGridHarness, UnfilledBottomRowPlacement) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(0, 0);
    g.setGridPadding(0);
    g.setMaxRows(2);
    std::vector<std::unique_ptr<FixedBox>> kids;
    for (int i = 0; i < 5; ++i) {
        kids.push_back(std::make_unique<FixedBox>(50, 40));
        g.addChild(kids.back().get());
    }
    g.measure(Constraints::loose(1000, 1000), ctx);
    g.layout(Rect{0, 0, 200, 200}, ctx);

    // 5 kids, maxRows=2. rows = min(5,2) = 2. cols = ceil(5/2) = 3.
    // Row 0: indices 0,1,2. Row 1: indices 3,4 (col 2 empty).
    EXPECT_EQ(g.currentRows(), 2);
    EXPECT_EQ(g.currentCols(), 3);
    EXPECT_FLOAT_EQ(kids[0]->bounds().x, 0);
    EXPECT_FLOAT_EQ(kids[2]->bounds().x, 100);
    EXPECT_FLOAT_EQ(kids[3]->bounds().y, 40);
    EXPECT_FLOAT_EQ(kids[4]->bounds().x, 50);
}

TEST_F(FwGridHarness, SetMaxRowsChangesLayout) {
    FwGrid g;
    g.setCellSize(50, 40);
    g.setGap(0, 0);
    g.setGridPadding(0);
    g.setMaxRows(4);
    std::vector<std::unique_ptr<FixedBox>> kids;
    for (int i = 0; i < 4; ++i) {
        kids.push_back(std::make_unique<FixedBox>(50, 40));
        g.addChild(kids.back().get());
    }
    g.measure(Constraints::loose(1000, 1000), ctx);
    g.layout(Rect{0, 0, 100, 200}, ctx);
    EXPECT_EQ(g.currentRows(), 4);
    EXPECT_EQ(g.currentCols(), 1);

    g.setMaxRows(2);
    g.measure(Constraints::loose(1000, 1000), ctx);
    g.layout(Rect{0, 0, 200, 80}, ctx);
    EXPECT_EQ(g.currentRows(), 2);
    EXPECT_EQ(g.currentCols(), 2);
}
