#include <gtest/gtest.h>
#include "ui/framework/DeviceHeaderWidget.h"

using namespace yawn::ui::fw;

TEST(DeviceHeaderTest, DefaultState) {
    DeviceHeaderWidget hdr;
    EXPECT_FALSE(hdr.isBypassed());
    EXPECT_TRUE(hdr.isExpanded());
}

TEST(DeviceHeaderTest, SetDeviceName) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("Reverb");
    // No crash — name used in paint()
}

TEST(DeviceHeaderTest, MeasureFixedHeight) {
    DeviceHeaderWidget hdr;
    UIContext ctx;
    Size s = hdr.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.h, DeviceHeaderWidget::kHeaderH);
    EXPECT_FLOAT_EQ(s.w, 400.0f);
}

TEST(DeviceHeaderTest, ExpandToggle) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("Delay");

    UIContext ctx;
    hdr.measure(Constraints::loose(300, 300), ctx);
    hdr.layout(Rect{0, 0, 300, 24}, ctx);

    bool toggled = false;
    bool newState = false;
    hdr.setOnExpandToggle([&](bool expanded) {
        toggled = true;
        newState = expanded;
    });

    // Click on expand button (at x+4, y+4, 16x16)
    MouseEvent e;
    e.x = 12; e.y = 12;
    e.button = MouseButton::Left;
    hdr.onMouseDown(e);

    EXPECT_TRUE(toggled);
    EXPECT_FALSE(newState);     // was expanded, now collapsed
    EXPECT_FALSE(hdr.isExpanded());
}

TEST(DeviceHeaderTest, BypassToggle) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("EQ");

    UIContext ctx;
    hdr.measure(Constraints::loose(300, 300), ctx);
    hdr.layout(Rect{0, 0, 300, 24}, ctx);

    bool toggled = false;
    hdr.setOnBypassToggle([&](bool bypassed) { toggled = true; });

    // Click on bypass button (at x+22, y+4, 16x16)
    MouseEvent e;
    e.x = 30; e.y = 12;
    e.button = MouseButton::Left;
    hdr.onMouseDown(e);

    EXPECT_TRUE(toggled);
    EXPECT_TRUE(hdr.isBypassed());
}

TEST(DeviceHeaderTest, RemoveButton) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("Compressor");
    hdr.setRemovable(true);

    UIContext ctx;
    hdr.measure(Constraints::loose(300, 300), ctx);
    hdr.layout(Rect{0, 0, 300, 24}, ctx);

    bool removed = false;
    hdr.setOnRemove([&]() { removed = true; });

    // Click on remove button (at x+w-20, y+4, 16x16)
    MouseEvent e;
    e.x = 288; e.y = 12;
    e.button = MouseButton::Left;
    hdr.onMouseDown(e);

    EXPECT_TRUE(removed);
}

TEST(DeviceHeaderTest, DeviceTypeColors) {
    DeviceHeaderWidget hdr;
    using Color = yawn::ui::Color;

    hdr.setDeviceType(DeviceHeaderWidget::DeviceType::Instrument);
    Color c = hdr.deviceColor();
    EXPECT_EQ(static_cast<int>(c.b), 200); // blue dominant

    hdr.setDeviceType(DeviceHeaderWidget::DeviceType::AudioEffect);
    c = hdr.deviceColor();
    EXPECT_EQ(static_cast<int>(c.r), 180); // purple

    hdr.setDeviceType(DeviceHeaderWidget::DeviceType::MidiEffect);
    c = hdr.deviceColor();
    EXPECT_EQ(static_cast<int>(c.g), 160); // yellow

    hdr.setDeviceType(DeviceHeaderWidget::DeviceType::Utility);
    c = hdr.deviceColor();
    EXPECT_EQ(static_cast<int>(c.g), 180); // green
}

TEST(DeviceHeaderTest, SetBypassedDirectly) {
    DeviceHeaderWidget hdr;
    hdr.setBypassed(true);
    EXPECT_TRUE(hdr.isBypassed());
    hdr.setBypassed(false);
    EXPECT_FALSE(hdr.isBypassed());
}

TEST(DeviceHeaderTest, SetExpandedDirectly) {
    DeviceHeaderWidget hdr;
    hdr.setExpanded(false);
    EXPECT_FALSE(hdr.isExpanded());
    hdr.setExpanded(true);
    EXPECT_TRUE(hdr.isExpanded());
}
