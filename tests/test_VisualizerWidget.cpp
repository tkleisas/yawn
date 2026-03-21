#include <gtest/gtest.h>
#include "ui/framework/VisualizerWidget.h"

using namespace yawn::ui::fw;

TEST(VisualizerWidgetTest, DefaultMode) {
    VisualizerWidget viz;
    EXPECT_EQ(viz.mode(), VisualizerWidget::Mode::Oscilloscope);
}

TEST(VisualizerWidgetTest, SetMode) {
    VisualizerWidget viz;
    viz.setMode(VisualizerWidget::Mode::Spectrum);
    EXPECT_EQ(viz.mode(), VisualizerWidget::Mode::Spectrum);
}

TEST(VisualizerWidgetTest, SetData) {
    VisualizerWidget viz;
    float data[] = {0.1f, 0.5f, -0.3f, 0.0f};
    viz.setData(data, 4);
    // No crash — data is stored internally
}

TEST(VisualizerWidgetTest, SetNullData) {
    VisualizerWidget viz;
    viz.setData(nullptr, 0);
    // No crash
}

TEST(VisualizerWidgetTest, MeasureDefaultHeight) {
    VisualizerWidget viz;
    UIContext ctx;
    Size s = viz.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.h, 120.0f);
    EXPECT_FLOAT_EQ(s.w, 400.0f);
}

TEST(VisualizerWidgetTest, MeasureConstrainedHeight) {
    VisualizerWidget viz;
    UIContext ctx;
    Size s = viz.measure(Constraints::tight(200, 80), ctx);
    EXPECT_FLOAT_EQ(s.h, 80.0f);
    EXPECT_FLOAT_EQ(s.w, 200.0f);
}

TEST(VisualizerWidgetTest, LayoutStoresBounds) {
    VisualizerWidget viz;
    UIContext ctx;
    viz.layout(Rect{10, 20, 300, 100}, ctx);
    EXPECT_FLOAT_EQ(viz.bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(viz.bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(viz.bounds().w, 300.0f);
    EXPECT_FLOAT_EQ(viz.bounds().h, 100.0f);
}
