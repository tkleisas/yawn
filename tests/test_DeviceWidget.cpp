#include <gtest/gtest.h>
#include "ui/framework/DeviceWidget.h"

using namespace yawn::ui::fw;

TEST(DeviceWidgetTest, DefaultState) {
    DeviceWidget dw;
    EXPECT_TRUE(dw.isExpanded());
    EXPECT_FALSE(dw.isBypassed());
}

TEST(DeviceWidgetTest, SetExpandedCollapsed) {
    DeviceWidget dw;
    dw.setExpanded(false);
    EXPECT_FALSE(dw.isExpanded());
    dw.setExpanded(true);
    EXPECT_TRUE(dw.isExpanded());
}

TEST(DeviceWidgetTest, SetBypassed) {
    DeviceWidget dw;
    dw.setBypassed(true);
    EXPECT_TRUE(dw.isBypassed());
    dw.setBypassed(false);
    EXPECT_FALSE(dw.isBypassed());
}

TEST(DeviceWidgetTest, PreferredWidthCollapsed) {
    DeviceWidget dw;
    dw.setExpanded(false);
    EXPECT_FLOAT_EQ(dw.preferredWidth(), DeviceWidget::kCollapsedW);
}

TEST(DeviceWidgetTest, PreferredWidthMinExpanded) {
    DeviceWidget dw;
    dw.setExpanded(true);
    // No params → should be kMinExpandedW
    EXPECT_GE(dw.preferredWidth(), DeviceWidget::kMinExpandedW);
}

TEST(DeviceWidgetTest, SetParameters) {
    DeviceWidget dw;
    std::vector<DeviceWidget::ParamInfo> params = {
        {0, "Freq", "Hz", 20, 20000, 440, false},
        {1, "Gain", "dB", -24, 24, 0, false},
        {2, "Mix",  "%",  0, 1, 0.5f, false},
        {3, "On",   "",   0, 1, 1, true},
    };
    dw.setParameters(params);
    // Should not crash, knobs created internally
}

TEST(DeviceWidgetTest, PreferredWidthWithParams) {
    DeviceWidget dw;
    std::vector<DeviceWidget::ParamInfo> params;
    for (int i = 0; i < 6; ++i)
        params.push_back({i, "P" + std::to_string(i), "dB", 0, 1, 0.5f, false});
    dw.setParameters(params);
    // 6 params / 2 rows = 3 cols → 3 * 64 + 24 = 216 > kMinExpandedW
    EXPECT_GT(dw.preferredWidth(), DeviceWidget::kMinExpandedW);
}

TEST(DeviceWidgetTest, ParamChangeCallback) {
    DeviceWidget dw;
    int changedIdx = -1;
    float changedVal = -1;
    dw.setOnParamChange([&](int idx, float v) {
        changedIdx = idx;
        changedVal = v;
    });

    std::vector<DeviceWidget::ParamInfo> params = {
        {0, "Vol", "dB", 0, 1, 0.5f, false},
    };
    dw.setParameters(params);
    dw.updateParamValue(0, 0.75f);
    // updateParamValue sets the knob value but doesn't fire callback
    // (callback fires from knob drag, not programmatic set)
}

TEST(DeviceWidgetTest, MeasureLayout) {
    DeviceWidget dw;
    dw.setDeviceName("Reverb");
    UIContext ctx;
    Size s = dw.measure(Constraints::loose(400, 200), ctx);
    EXPECT_GT(s.w, 0);
    EXPECT_GT(s.h, 0);

    dw.layout(Rect{0, 0, s.w, 200}, ctx);
    EXPECT_FLOAT_EQ(dw.bounds().x, 0);
    EXPECT_FLOAT_EQ(dw.bounds().w, s.w);
}

TEST(DeviceWidgetTest, VisualizerMode) {
    DeviceWidget dw;
    dw.setVisualizer(true, "oscilloscope");

    std::vector<DeviceWidget::ParamInfo> params = {
        {0, "Mix", "%", 0, 1, 0.5f, false},
    };
    dw.setParameters(params);
    EXPECT_GE(dw.preferredWidth(), DeviceWidget::kVisualizerMinW);
}

TEST(DeviceWidgetTest, FormatValueHz) {
    // Test the static formatValue method
    std::string s = DeviceWidget::formatValue(440.0f, "Hz", false);
    EXPECT_EQ(s, "440");

    s = DeviceWidget::formatValue(2500.0f, "Hz", false);
    EXPECT_EQ(s, "2.5k");
}

TEST(DeviceWidgetTest, FormatValueDb) {
    std::string s = DeviceWidget::formatValue(-6.5f, "dB", false);
    EXPECT_EQ(s, "-6.5");
}

TEST(DeviceWidgetTest, FormatValuePercent) {
    std::string s = DeviceWidget::formatValue(0.75f, "%", false);
    EXPECT_EQ(s, "75%");
}

TEST(DeviceWidgetTest, FormatValueBoolean) {
    EXPECT_EQ(DeviceWidget::formatValue(1.0f, "", true), "On");
    EXPECT_EQ(DeviceWidget::formatValue(0.0f, "", true), "Off");
}

TEST(DeviceWidgetTest, DestructorCleansUp) {
    // Verify no leaks — create with params and destroy
    auto* dw = new DeviceWidget();
    std::vector<DeviceWidget::ParamInfo> params = {
        {0, "A", "Hz", 0, 100, 50, false},
        {1, "B", "dB", -12, 12, 0, false},
    };
    dw->setParameters(params);
    dw->setVisualizer(true, "spectrum");
    dw->setParameters(params); // rebuild
    delete dw;
    // No crash = success
}
