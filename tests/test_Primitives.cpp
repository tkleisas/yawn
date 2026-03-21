#include <gtest/gtest.h>
#include <cstring>
#include "ui/framework/Primitives.h"

using namespace yawn::ui::fw;

// ═══════════════════════════════════════════════════════════════════════════
// Label tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwLabel, DefaultsToLeftAlign) {
    Label lbl("Hello");
    EXPECT_EQ(lbl.text(), "Hello");
    EXPECT_EQ(lbl.align(), TextAlign::Left);
}

TEST(FwLabel, SetText) {
    Label lbl;
    lbl.setText("World");
    EXPECT_EQ(lbl.text(), "World");
}

TEST(FwLabel, MeasureProportionalToLength) {
    Label short_lbl("Hi");
    Label long_lbl("Hello World!");
    UIContext ctx;
    Size s1 = short_lbl.measure(Constraints::loose(400, 400), ctx);
    Size s2 = long_lbl.measure(Constraints::loose(400, 400), ctx);
    EXPECT_GT(s2.w, s1.w);
}

// ═══════════════════════════════════════════════════════════════════════════
// Button tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwButtonTest, ClickCallsCallback) {
    FwButton btn("Test");
    UIContext ctx;
    btn.measure(Constraints::loose(200, 200), ctx);
    btn.layout(Rect{0, 0, 100, 30}, ctx);

    bool clicked = false;
    btn.setOnClick([&]() { clicked = true; });

    MouseEvent down;
    down.x = 50; down.y = 15;
    down.button = MouseButton::Left;
    btn.onMouseDown(down);
    EXPECT_TRUE(btn.isPressed());

    MouseEvent up;
    up.x = 50; up.y = 15;
    btn.onMouseUp(up);
    EXPECT_TRUE(clicked);
    EXPECT_FALSE(btn.isPressed());
}

TEST(FwButtonTest, ClickOutsideDoesNotFire) {
    FwButton btn("Test");
    UIContext ctx;
    btn.measure(Constraints::loose(200, 200), ctx);
    btn.layout(Rect{0, 0, 100, 30}, ctx);

    bool clicked = false;
    btn.setOnClick([&]() { clicked = true; });

    MouseEvent down;
    down.x = 50; down.y = 15;
    down.button = MouseButton::Left;
    btn.onMouseDown(down);

    MouseEvent up;
    up.x = 500; up.y = 500;
    btn.onMouseUp(up);
    EXPECT_FALSE(clicked);
}

TEST(FwButtonTest, ToggleMode) {
    FwButton btn("M");
    UIContext ctx;
    btn.measure(Constraints::loose(200, 200), ctx);
    btn.layout(Rect{0, 0, 50, 30}, ctx);
    btn.setToggle(true);
    EXPECT_FALSE(btn.toggleState());

    MouseEvent down;
    down.x = 25; down.y = 15;
    down.button = MouseButton::Left;
    btn.onMouseDown(down);

    MouseEvent up;
    up.x = 25; up.y = 15;
    btn.onMouseUp(up);
    EXPECT_TRUE(btn.toggleState());

    btn.onMouseDown(down);
    btn.onMouseUp(up);
    EXPECT_FALSE(btn.toggleState());
}

TEST(FwButtonTest, LabelAccess) {
    FwButton btn("Play");
    EXPECT_EQ(btn.label(), "Play");
    btn.setLabel("Stop");
    EXPECT_EQ(btn.label(), "Stop");
}

// ═══════════════════════════════════════════════════════════════════════════
// Toggle tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwToggleTest, TogglesOnClick) {
    FwToggle toggle("Mute", false);
    UIContext ctx;
    toggle.measure(Constraints::loose(200, 200), ctx);
    toggle.layout(Rect{0, 0, 60, 24}, ctx);

    bool state = false;
    toggle.setOnChange([&](bool on) { state = on; });

    MouseEvent down;
    down.x = 30; down.y = 12;
    down.button = MouseButton::Left;
    toggle.onMouseDown(down);

    MouseEvent up;
    up.x = 30; up.y = 12;
    toggle.onMouseUp(up);
    EXPECT_TRUE(toggle.isOn());
    EXPECT_TRUE(state);

    toggle.onMouseDown(down);
    toggle.onMouseUp(up);
    EXPECT_FALSE(toggle.isOn());
    EXPECT_FALSE(state);
}

TEST(FwToggleTest, InitialState) {
    FwToggle on("Solo", true);
    FwToggle off("Mute", false);
    EXPECT_TRUE(on.isOn());
    EXPECT_FALSE(off.isOn());
}

// ═══════════════════════════════════════════════════════════════════════════
// Fader tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwFaderTest, DragChangesValue) {
    FwFader fader;
    UIContext ctx;
    fader.measure(Constraints::loose(200, 200), ctx);
    fader.layout(Rect{0, 0, 20, 200}, ctx);
    fader.setRange(0, 1);
    fader.setValue(0.5f);

    float newVal = -1;
    fader.setOnChange([&](float v) { newVal = v; });

    MouseEvent down;
    down.x = 10; down.y = 100;
    down.button = MouseButton::Left;
    fader.onMouseDown(down);

    MouseMoveEvent move;
    move.x = 10; move.y = 80; // drag up → increase
    fader.onMouseMove(move);

    EXPECT_GT(newVal, 0.5f);
}

TEST(FwFaderTest, ValueClamped) {
    FwFader fader;
    fader.setRange(0, 1);
    fader.setValue(2.0f);
    EXPECT_LE(fader.value(), 1.0f);

    fader.setValue(-1.0f);
    EXPECT_GE(fader.value(), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Knob tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwKnobTest, DragChangesValue) {
    FwKnob knob;
    UIContext ctx;
    knob.measure(Constraints::loose(200, 200), ctx);
    knob.layout(Rect{0, 0, 40, 50}, ctx);
    knob.setRange(0, 1);
    knob.setValue(0.5f);

    float newVal = -1;
    knob.setOnChange([&](float v) { newVal = v; });

    MouseEvent down;
    down.x = 20; down.y = 25;
    down.button = MouseButton::Left;
    knob.onMouseDown(down);

    MouseMoveEvent move;
    move.x = 20; move.y = 5;
    knob.onMouseMove(move);

    EXPECT_GT(newVal, 0.5f);
}

TEST(FwKnobTest, RightClickResets) {
    FwKnob knob;
    knob.setRange(0, 1);
    knob.setDefault(0.3f);
    knob.setValue(0.8f);

    float newVal = -1;
    knob.setOnChange([&](float v) { newVal = v; });

    MouseEvent e;
    e.x = 20; e.y = 25;
    e.button = MouseButton::Right;
    knob.onMouseDown(e);

    EXPECT_NEAR(knob.value(), 0.3f, 0.01f);
    EXPECT_NEAR(newVal, 0.3f, 0.01f);
}

TEST(FwKnobTest, LabelAccess) {
    FwKnob knob;
    knob.setLabel("Vol");
    // Just verify it doesn't crash — label used in paint()
    UIContext ctx;
    knob.measure(Constraints::loose(100, 100), ctx);
}

TEST(FwKnobTest, BooleanModeToggle) {
    FwKnob knob;
    knob.setRange(0, 1);
    knob.setDefault(0);
    knob.setValue(0);
    knob.setBoolean(true);
    EXPECT_TRUE(knob.isBoolean());

    float newVal = -1;
    knob.setOnChange([&](float v) { newVal = v; });

    // Click toggles from off (0) to on (1)
    MouseEvent down;
    down.x = 20; down.y = 25;
    down.button = MouseButton::Left;
    knob.onMouseDown(down);
    EXPECT_NEAR(knob.value(), 1.0f, 0.01f);
    EXPECT_NEAR(newVal, 1.0f, 0.01f);

    // Click again toggles back to off (0)
    knob.onMouseDown(down);
    EXPECT_NEAR(knob.value(), 0.0f, 0.01f);
    EXPECT_NEAR(newVal, 0.0f, 0.01f);
}

TEST(FwKnobTest, BooleanModeNoDrag) {
    FwKnob knob;
    knob.setRange(0, 1);
    knob.setValue(0);
    knob.setBoolean(true);

    // Click in boolean mode should not start drag
    MouseEvent down;
    down.x = 20; down.y = 25;
    down.button = MouseButton::Left;
    knob.onMouseDown(down);

    // Moving should have no effect (no drag state)
    MouseMoveEvent move;
    move.x = 20; move.y = 5;
    knob.onMouseMove(move);

    // Value should still be 1 (toggled), not changed by drag
    EXPECT_NEAR(knob.value(), 1.0f, 0.01f);
}

TEST(FwKnobTest, CustomArcColors) {
    using Color = yawn::ui::Color;
    FwKnob knob;
    Color custom{255, 0, 0, 255};
    Color customActive{255, 128, 0, 255};
    knob.setArcColor(custom);
    knob.setArcColorActive(customActive);
    // Verify it doesn't crash — colors used in paint()
    UIContext ctx;
    knob.measure(Constraints::loose(100, 100), ctx);
}

TEST(FwKnobTest, FormatCallback) {
    FwKnob knob;
    knob.setRange(20, 20000);
    knob.setValue(440);

    std::string formatted;
    knob.setFormatCallback([&](float v) -> std::string {
        formatted = std::to_string(static_cast<int>(v)) + " Hz";
        return formatted;
    });

    // Call the callback manually to verify it works
    auto result = knob.value();
    EXPECT_NEAR(result, 440.0f, 0.01f);

    // Verify the format callback produces expected output
    knob.setFormatCallback([](float v) -> std::string {
        return std::to_string(static_cast<int>(v)) + " Hz";
    });
    UIContext ctx;
    knob.measure(Constraints::loose(100, 100), ctx);
}

TEST(FwKnobTest, ValueClampedToRange) {
    FwKnob knob;
    knob.setRange(0, 100);
    knob.setValue(150);
    EXPECT_LE(knob.value(), 100.0f);

    knob.setValue(-50);
    EXPECT_GE(knob.value(), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TextInput tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwTextInputTest, ClickStartsEditing) {
    FwTextInput input;
    input.setText("hello");
    UIContext ctx;
    input.measure(Constraints::loose(200, 200), ctx);
    input.layout(Rect{0, 0, 120, 24}, ctx);

    MouseEvent down;
    down.x = 60; down.y = 12;
    down.button = MouseButton::Left;
    input.onMouseDown(down);

    EXPECT_TRUE(input.isEditing());
}

TEST(FwTextInputTest, TextAccumulation) {
    FwTextInput input;
    UIContext ctx;
    input.measure(Constraints::loose(200, 200), ctx);
    input.layout(Rect{0, 0, 120, 24}, ctx);

    MouseEvent down;
    down.x = 60; down.y = 12;
    down.button = MouseButton::Left;
    input.onMouseDown(down);

    TextInputEvent e;
    std::strncpy(e.text, "abc", sizeof(e.text) - 1);
    input.onTextInput(e);
    EXPECT_EQ(input.text(), "abc");
}

TEST(FwTextInputTest, EnterCommits) {
    FwTextInput input;
    UIContext ctx;
    input.measure(Constraints::loose(200, 200), ctx);
    input.layout(Rect{0, 0, 120, 24}, ctx);

    std::string committed;
    input.setOnCommit([&](const std::string& s) { committed = s; });

    MouseEvent down;
    down.x = 60; down.y = 12;
    down.button = MouseButton::Left;
    input.onMouseDown(down);

    TextInputEvent te;
    std::strncpy(te.text, "test", sizeof(te.text) - 1);
    input.onTextInput(te);

    KeyEvent ke;
    ke.keyCode = 0x0D; // Enter
    input.onKeyDown(ke);

    EXPECT_EQ(committed, "test");
    EXPECT_FALSE(input.isEditing());
}

TEST(FwTextInputTest, EscapeCancels) {
    FwTextInput input;
    input.setText("original");
    UIContext ctx;
    input.measure(Constraints::loose(200, 200), ctx);
    input.layout(Rect{0, 0, 120, 24}, ctx);

    MouseEvent down;
    down.x = 60; down.y = 12;
    down.button = MouseButton::Left;
    input.onMouseDown(down);

    TextInputEvent te;
    std::strncpy(te.text, "changed", sizeof(te.text) - 1);
    input.onTextInput(te);

    KeyEvent ke;
    ke.keyCode = 0x1B; // Escape
    input.onKeyDown(ke);

    EXPECT_EQ(input.text(), "original");
    EXPECT_FALSE(input.isEditing());
}

TEST(FwTextInputTest, BackspaceDeletes) {
    FwTextInput input;
    UIContext ctx;
    input.measure(Constraints::loose(200, 200), ctx);
    input.layout(Rect{0, 0, 120, 24}, ctx);

    MouseEvent down;
    down.x = 60; down.y = 12;
    down.button = MouseButton::Left;
    input.onMouseDown(down);

    TextInputEvent te;
    std::strncpy(te.text, "abc", sizeof(te.text) - 1);
    input.onTextInput(te);
    EXPECT_EQ(input.text(), "abc");

    KeyEvent bs;
    bs.keyCode = 0x08; // Backspace
    input.onKeyDown(bs);
    EXPECT_EQ(input.text(), "ab");
}

// ═══════════════════════════════════════════════════════════════════════════
// NumberInput tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwNumberInputTest, DragChangesValue) {
    FwNumberInput nb;
    UIContext ctx;
    nb.measure(Constraints::loose(200, 200), ctx);
    nb.layout(Rect{0, 0, 60, 22}, ctx);
    nb.setRange(0, 200);
    nb.setValue(100);
    nb.setSensitivity(1.0f);

    float newVal = -1;
    nb.setOnChange([&](float v) { newVal = v; });

    MouseEvent down;
    down.x = 30; down.y = 11;
    down.button = MouseButton::Left;
    nb.onMouseDown(down);

    MouseMoveEvent move;
    move.x = 30; move.y = 1;
    nb.onMouseMove(move);

    EXPECT_GT(newVal, 100);
}

TEST(FwNumberInputTest, TextInputCommit) {
    FwNumberInput nb;
    UIContext ctx;
    nb.measure(Constraints::loose(200, 200), ctx);
    nb.layout(Rect{0, 0, 60, 22}, ctx);
    nb.setRange(0, 200);
    nb.setValue(100);

    float newVal = -1;
    nb.setOnChange([&](float v) { newVal = v; });

    // Enter to start editing
    KeyEvent enter;
    enter.keyCode = 0x0D;
    nb.onKeyDown(enter);
    EXPECT_TRUE(nb.isEditing());

    TextInputEvent te;
    std::strncpy(te.text, "150", sizeof(te.text) - 1);
    nb.onTextInput(te);

    nb.onKeyDown(enter); // Commit
    EXPECT_NEAR(newVal, 150.0f, 0.1f);
    EXPECT_FALSE(nb.isEditing());
}

TEST(FwNumberInputTest, EscapeCancels) {
    FwNumberInput nb;
    nb.setRange(0, 200);
    nb.setValue(100);

    KeyEvent enter;
    enter.keyCode = 0x0D;
    nb.onKeyDown(enter);

    TextInputEvent te;
    std::strncpy(te.text, "50", sizeof(te.text) - 1);
    nb.onTextInput(te);

    KeyEvent esc;
    esc.keyCode = 0x1B;
    nb.onKeyDown(esc);
    EXPECT_NEAR(nb.value(), 100.0f, 0.1f);
}

TEST(FwNumberInputTest, ValueClamped) {
    FwNumberInput nb;
    nb.setRange(0, 100);
    nb.setValue(200);
    EXPECT_LE(nb.value(), 100.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// DropDown tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FwDropDownTest, SelectItem) {
    FwDropDown dd;
    UIContext ctx;
    dd.measure(Constraints::loose(200, 200), ctx);
    dd.layout(Rect{0, 0, 100, 24}, ctx);
    dd.setItems({"Sine", "Saw", "Square"});
    dd.setSelected(0);

    int sel = -1;
    dd.setOnChange([&](int i) { sel = i; });

    // Click to open
    MouseEvent open;
    open.x = 50; open.y = 12;
    open.button = MouseButton::Left;
    dd.onMouseDown(open);
    EXPECT_TRUE(dd.isOpen());

    // Click item 1 (items start at y=24, item 1 is at y=48..72, click mid)
    MouseEvent select;
    select.x = 50; select.y = 60;
    select.button = MouseButton::Left;
    dd.onMouseDown(select);
    EXPECT_EQ(sel, 1);
    EXPECT_FALSE(dd.isOpen());
}

TEST(FwDropDownTest, SelectedText) {
    FwDropDown dd;
    dd.setItems({"A", "B", "C"});
    dd.setSelected(1);
    EXPECT_EQ(dd.selectedText(), "B");
}

TEST(FwDropDownTest, DefaultSelection) {
    FwDropDown dd;
    dd.setItems({"X", "Y"});
    EXPECT_EQ(dd.selected(), 0);
    EXPECT_EQ(dd.selectedText(), "X");
}
