// fw2::VisualizerWidget — migration regression tests.
//
// Mirrors tests/test_VisualizerWidget.cpp but targets the fw2 class
// directly (the old v1 tests go through the v1→fw2 wrapper). Both suites
// should pass; when DeviceWidget migrates and the wrapper retires, the
// v1 suite can be deleted.

#include <gtest/gtest.h>

#include "ui/framework/v2/VisualizerWidget.h"
#include "ui/framework/v2/UIContext.h"

using ::yawn::ui::fw2::VisualizerWidget;
using ::yawn::ui::fw2::UIContext;
using ::yawn::ui::fw::Constraints;
using ::yawn::ui::fw::Size;
using ::yawn::ui::fw::Rect;

TEST(Fw2VisualizerWidgetTest, DefaultMode) {
    VisualizerWidget viz;
    EXPECT_EQ(viz.mode(), VisualizerWidget::Mode::Oscilloscope);
}

TEST(Fw2VisualizerWidgetTest, ConstructWithMode) {
    VisualizerWidget viz(VisualizerWidget::Mode::Tuner);
    EXPECT_EQ(viz.mode(), VisualizerWidget::Mode::Tuner);
}

TEST(Fw2VisualizerWidgetTest, SetMode) {
    VisualizerWidget viz;
    viz.setMode(VisualizerWidget::Mode::Spectrum);
    EXPECT_EQ(viz.mode(), VisualizerWidget::Mode::Spectrum);
}

TEST(Fw2VisualizerWidgetTest, SetDataStores) {
    VisualizerWidget viz;
    const float samples[] = {0.1f, 0.5f, -0.3f, 0.0f};
    viz.setData(samples, 4);
    ASSERT_EQ(viz.data().size(), 4u);
    EXPECT_FLOAT_EQ(viz.data()[0], 0.1f);
    EXPECT_FLOAT_EQ(viz.data()[2], -0.3f);
}

TEST(Fw2VisualizerWidgetTest, SetNullClears) {
    VisualizerWidget viz;
    const float samples[] = {1.0f, 2.0f};
    viz.setData(samples, 2);
    EXPECT_EQ(viz.data().size(), 2u);
    viz.setData(nullptr, 0);
    EXPECT_TRUE(viz.data().empty());
}

TEST(Fw2VisualizerWidgetTest, MeasureDefaultHeight) {
    VisualizerWidget viz;
    UIContext ctx;
    Size s = viz.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.h, 120.0f);
    EXPECT_FLOAT_EQ(s.w, 400.0f);
}

TEST(Fw2VisualizerWidgetTest, MeasureTightConstraint) {
    VisualizerWidget viz;
    UIContext ctx;
    Size s = viz.measure(Constraints::tight(200, 80), ctx);
    EXPECT_FLOAT_EQ(s.h, 80.0f);
    EXPECT_FLOAT_EQ(s.w, 200.0f);
}

TEST(Fw2VisualizerWidgetTest, LayoutStoresBounds) {
    VisualizerWidget viz;
    UIContext ctx;
    viz.measure(Constraints::tight(300, 100), ctx);
    viz.layout(Rect{10, 20, 300, 100}, ctx);
    EXPECT_FLOAT_EQ(viz.bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(viz.bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(viz.bounds().w, 300.0f);
    EXPECT_FLOAT_EQ(viz.bounds().h, 100.0f);
}

TEST(Fw2VisualizerWidgetTest, RenderNoOpUnderTestBuild) {
    // Under YAWN_TEST_BUILD the render() body is empty — this test just
    // verifies it's safe to call with no renderer wired up.
    VisualizerWidget viz;
    UIContext ctx;  // renderer + textMetrics both null
    viz.measure(Constraints::tight(200, 120), ctx);
    viz.layout(Rect{0, 0, 200, 120}, ctx);
    viz.render(ctx);  // must not crash
    SUCCEED();
}
