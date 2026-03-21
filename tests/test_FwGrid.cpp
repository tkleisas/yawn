#include <gtest/gtest.h>
#include "ui/framework/FwGrid.h"

using namespace yawn::ui::fw;

TEST(FwGridTest, EmptyGridMeasuresZero) {
    FwGrid grid;
    UIContext ctx;
    Size s = grid.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.w, 0.0f);
    EXPECT_FLOAT_EQ(s.h, 0.0f);
}

TEST(FwGridTest, SingleChildLayout) {
    FwGrid grid;
    grid.setCellSize(64, 70);
    grid.setMaxRows(2);
    grid.setPadding(8);

    Widget child;
    grid.addChild(&child);

    UIContext ctx;
    Size s = grid.measure(Constraints::loose(400, 400), ctx);
    // 1 child → 1 col, 1 row: padding*2 + cellW = 16 + 64 = 80
    EXPECT_FLOAT_EQ(s.w, 80.0f);
    EXPECT_FLOAT_EQ(s.h, 86.0f); // 16 + 70

    grid.layout(Rect{0, 0, 80, 86}, ctx);
    // Child should be at (8, 8) with cell size
    EXPECT_FLOAT_EQ(child.bounds().x, 8.0f);
    EXPECT_FLOAT_EQ(child.bounds().y, 8.0f);
    EXPECT_FLOAT_EQ(child.bounds().w, 64.0f);
    EXPECT_FLOAT_EQ(child.bounds().h, 70.0f);
}

TEST(FwGridTest, RowMajorLayout) {
    FwGrid grid;
    grid.setCellSize(64, 70);
    grid.setMaxRows(2);
    grid.setPadding(0);
    grid.setGap(0, 0);

    Widget c0, c1, c2, c3;
    grid.addChild(&c0);
    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);

    UIContext ctx;
    Size s = grid.measure(Constraints::loose(400, 400), ctx);
    // 4 children, maxRows=2 → cols=2, rows=2
    EXPECT_FLOAT_EQ(s.w, 128.0f); // 2 * 64
    EXPECT_FLOAT_EQ(s.h, 140.0f); // 2 * 70

    grid.layout(Rect{0, 0, 128, 140}, ctx);

    // Row-major: [c0, c1] / [c2, c3]
    EXPECT_FLOAT_EQ(c0.bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(c0.bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(c1.bounds().x, 64.0f);
    EXPECT_FLOAT_EQ(c1.bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(c2.bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(c2.bounds().y, 70.0f);
    EXPECT_FLOAT_EQ(c3.bounds().x, 64.0f);
    EXPECT_FLOAT_EQ(c3.bounds().y, 70.0f);
}

TEST(FwGridTest, GapSpacing) {
    FwGrid grid;
    grid.setCellSize(40, 50);
    grid.setMaxRows(2);
    grid.setPadding(0);
    grid.setGap(10, 5);

    Widget c0, c1, c2, c3;
    grid.addChild(&c0);
    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);

    UIContext ctx;
    grid.measure(Constraints::loose(400, 400), ctx);
    grid.layout(Rect{0, 0, 400, 400}, ctx);

    // Col 1 starts at x = 40 + 10 = 50
    EXPECT_FLOAT_EQ(c1.bounds().x, 50.0f);
    // Row 1 starts at y = 50 + 5 = 55
    EXPECT_FLOAT_EQ(c2.bounds().y, 55.0f);
}

TEST(FwGridTest, OddChildCount) {
    FwGrid grid;
    grid.setCellSize(64, 70);
    grid.setMaxRows(2);
    grid.setPadding(0);
    grid.setGap(0, 0);

    Widget c0, c1, c2;
    grid.addChild(&c0);
    grid.addChild(&c1);
    grid.addChild(&c2);

    UIContext ctx;
    Size s = grid.measure(Constraints::loose(400, 400), ctx);
    // 3 children, maxRows=2 → cols=ceil(3/2)=2, rows=2
    EXPECT_FLOAT_EQ(s.w, 128.0f);
    EXPECT_FLOAT_EQ(s.h, 140.0f);
}

TEST(FwGridTest, CellSizeAccessors) {
    FwGrid grid;
    grid.setCellSize(48, 60);
    EXPECT_FLOAT_EQ(grid.cellWidth(), 48.0f);
    EXPECT_FLOAT_EQ(grid.cellHeight(), 60.0f);

    grid.setMaxRows(3);
    EXPECT_EQ(grid.maxRows(), 3);
}
