// fw2::DeviceHeaderWidget — migration regression tests.
//
// Mirrors the coverage of the old test_DeviceHeaderWidget.cpp but
// targets the fw2 class directly. The v1 wrapper is pure delegation and
// doesn't need its own coverage.

#include <gtest/gtest.h>

#include "ui/framework/v2/DeviceHeaderWidget.h"
#include "ui/framework/v2/UIContext.h"

using ::yawn::ui::fw2::DeviceHeaderWidget;
using ::yawn::ui::fw2::UIContext;
using ::yawn::ui::fw2::MouseEvent;
using ::yawn::ui::fw2::MouseButton;
using ::yawn::ui::fw::Constraints;
using ::yawn::ui::fw::Size;
using ::yawn::ui::fw::Rect;

static void seatHeader(DeviceHeaderWidget& hdr, float w = 300.0f) {
    UIContext ctx;
    hdr.measure(Constraints::loose(w, w), ctx);
    hdr.layout(Rect{0, 0, w, DeviceHeaderWidget::kHeaderH}, ctx);
}

TEST(Fw2DeviceHeaderTest, DefaultState) {
    DeviceHeaderWidget hdr;
    EXPECT_FALSE(hdr.isBypassed());
    EXPECT_TRUE(hdr.isExpanded());
}

TEST(Fw2DeviceHeaderTest, MeasureFixedHeight) {
    DeviceHeaderWidget hdr;
    UIContext ctx;
    Size s = hdr.measure(Constraints::loose(400, 400), ctx);
    EXPECT_FLOAT_EQ(s.h, DeviceHeaderWidget::kHeaderH);
    EXPECT_FLOAT_EQ(s.w, 400.0f);
}

TEST(Fw2DeviceHeaderTest, ExpandToggle) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("Delay");
    seatHeader(hdr);

    bool toggled = false;
    bool newState = true;
    hdr.setOnExpandToggle([&](bool expanded) {
        toggled = true;
        newState = expanded;
    });

    MouseEvent e;
    e.x = 12; e.y = 12;
    e.button = MouseButton::Left;
    hdr.dispatchMouseDown(e);

    EXPECT_TRUE(toggled);
    EXPECT_FALSE(newState);            // was expanded → now collapsed
    EXPECT_FALSE(hdr.isExpanded());
}

TEST(Fw2DeviceHeaderTest, BypassToggle) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("EQ");
    seatHeader(hdr);

    bool toggled = false;
    hdr.setOnBypassToggle([&](bool) { toggled = true; });

    MouseEvent e;
    e.x = 30; e.y = 12;
    e.button = MouseButton::Left;
    hdr.dispatchMouseDown(e);

    EXPECT_TRUE(toggled);
    EXPECT_TRUE(hdr.isBypassed());
}

TEST(Fw2DeviceHeaderTest, RemoveButton) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("Compressor");
    hdr.setRemovable(true);
    seatHeader(hdr);

    bool removed = false;
    hdr.setOnRemove([&]() { removed = true; });

    MouseEvent e;
    e.x = 288; e.y = 12;
    e.button = MouseButton::Left;
    hdr.dispatchMouseDown(e);

    EXPECT_TRUE(removed);
}

TEST(Fw2DeviceHeaderTest, DeviceTypeColors) {
    DeviceHeaderWidget hdr;
    using ::yawn::ui::Color;

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

TEST(Fw2DeviceHeaderTest, SetBypassedDirectly) {
    DeviceHeaderWidget hdr;
    hdr.setBypassed(true);
    EXPECT_TRUE(hdr.isBypassed());
    hdr.setBypassed(false);
    EXPECT_FALSE(hdr.isBypassed());
}

TEST(Fw2DeviceHeaderTest, SetExpandedDirectly) {
    DeviceHeaderWidget hdr;
    hdr.setExpanded(false);
    EXPECT_FALSE(hdr.isExpanded());
    hdr.setExpanded(true);
    EXPECT_TRUE(hdr.isExpanded());
}

TEST(Fw2DeviceHeaderTest, PresetButtonFiresCallback) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("Synth");
    hdr.setPresetName("Init");
    seatHeader(hdr, 400.0f);

    bool called = false;
    float cbX = -1, cbY = -1;
    hdr.setOnPresetClick([&](float x, float y) {
        called = true; cbX = x; cbY = y;
    });

    // Click at the center of the preset rect.
    Rect pr = hdr.presetBtnRect();
    ASSERT_GT(pr.w, 0.0f);
    MouseEvent e;
    e.x = pr.x + pr.w * 0.5f;
    e.y = pr.y + pr.h * 0.5f;
    e.button = MouseButton::Left;
    hdr.dispatchMouseDown(e);

    EXPECT_TRUE(called);
    EXPECT_GT(cbX, 0.0f);
    EXPECT_GT(cbY, 0.0f);
}

TEST(Fw2DeviceHeaderTest, ClickOutsideButtonsTriggersDragStart) {
    DeviceHeaderWidget hdr;
    hdr.setDeviceName("Reverb");
    seatHeader(hdr);

    bool dragStarted = false;
    hdr.setOnDragStart([&]() { dragStarted = true; });

    // Click somewhere in the header's dead zone near the right edge but
    // before the remove button, outside any hit rect.
    MouseEvent e;
    e.x = 270; e.y = 20;
    e.button = MouseButton::Left;
    hdr.dispatchMouseDown(e);

    EXPECT_TRUE(dragStarted);
}
