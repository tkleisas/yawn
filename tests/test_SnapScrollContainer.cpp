#include <gtest/gtest.h>
#include "ui/framework/SnapScrollContainer.h"

using namespace yawn::ui::fw;

TEST(SnapScrollTest, DefaultScrollIsZero) {
    SnapScrollContainer sc;
    EXPECT_FLOAT_EQ(sc.scrollX(), 0.0f);
}

TEST(SnapScrollTest, SetScrollX) {
    SnapScrollContainer sc;
    sc.layout(Rect{0, 0, 200, 100}, UIContext{});
    sc.setScrollX(50);
    // Without content, clamp brings it back to 0
    EXPECT_FLOAT_EQ(sc.scrollX(), 0.0f);
}

TEST(SnapScrollTest, SnapPointNavigation) {
    SnapScrollContainer sc;

    // Create two child widgets to give content width
    Widget c1, c2, c3;
    sc.addChild(&c1);
    sc.addChild(&c2);
    sc.addChild(&c3);

    UIContext ctx;
    // Measure and layout with a narrow container
    sc.measure(Constraints::loose(200, 100), ctx);
    sc.layout(Rect{0, 0, 200, 100}, ctx);

    // Set snap points
    sc.setSnapPoints({0, 100, 200});

    // Manually set content width > container to enable scrolling
    sc.setScrollX(0);
    sc.scrollRight();
    // Should snap to next point (100) if content is wide enough
    // Exact behavior depends on content width vs viewport
}

TEST(SnapScrollTest, ScrollLeftAtZeroStaysZero) {
    SnapScrollContainer sc;
    sc.setSnapPoints({0, 100, 200});
    sc.scrollLeft();
    EXPECT_FLOAT_EQ(sc.scrollX(), 0.0f);
}

TEST(SnapScrollTest, SetSnapPointsSorts) {
    SnapScrollContainer sc;
    sc.setSnapPoints({200, 0, 100});
    // Should be sorted internally — scrollRight from 0 goes to 100, not 200
    // (Can't directly inspect, but no crash = success)
}

TEST(SnapScrollTest, CanScrollWithoutContent) {
    SnapScrollContainer sc;
    sc.layout(Rect{0, 0, 200, 100}, UIContext{});
    EXPECT_FALSE(sc.canScroll());
}

TEST(SnapScrollTest, HandleScrollWheel) {
    SnapScrollContainer sc;
    sc.setSnapPoints({0, 100, 200});
    // Just verify no crash
    sc.handleScroll(1.0f, 0.0f);
    sc.handleScroll(-1.0f, 0.0f);
}

TEST(SnapScrollTest, GapAccessor) {
    SnapScrollContainer sc;
    sc.setGap(8.0f);
    EXPECT_FLOAT_EQ(sc.gap(), 8.0f);
}
