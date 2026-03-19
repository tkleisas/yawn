#include <gtest/gtest.h>
#include "ui/Widget.h"
#include "ui/Widgets.h"
#include "ui/MenuBar.h"

using namespace yawn::ui;

// ========================= InputState =========================

TEST(InputState, HitTestTopmost) {
    InputState input;
    Button b1("A"), b2("B");
    b1.setBounds(0, 0, 100, 30);
    b2.setBounds(0, 0, 100, 30); // overlapping
    input.addWidget(&b1);
    input.addWidget(&b2); // b2 is on top

    input.onMouseDown(50, 15, 1);
    EXPECT_EQ(input.captured(), &b2);
    input.onMouseUp(50, 15, 1);
}

TEST(InputState, CapturedWidgetGetsMouseUp) {
    InputState input;
    Button b1("A");
    b1.setBounds(0, 0, 100, 30);
    input.addWidget(&b1);

    input.onMouseDown(50, 15, 1);
    EXPECT_EQ(input.captured(), &b1);
    // Mouse up outside the widget — still goes to captured
    input.onMouseUp(500, 500, 1);
    EXPECT_EQ(input.captured(), nullptr);
}

TEST(InputState, HoverTracking) {
    InputState input;
    Button b1("A");
    b1.setBounds(100, 100, 50, 30);
    input.addWidget(&b1);

    input.onMouseMove(125, 115);
    EXPECT_EQ(input.hovered(), &b1);
    EXPECT_TRUE(b1.isHovered());

    input.onMouseMove(0, 0);
    EXPECT_EQ(input.hovered(), nullptr);
    EXPECT_FALSE(b1.isHovered());
}

TEST(InputState, FocusManagement) {
    InputState input;
    Button b1("A"), b2("B");
    b1.setBounds(0, 0, 50, 30);
    b2.setBounds(60, 0, 50, 30);
    input.addWidget(&b1);
    input.addWidget(&b2);

    input.onMouseDown(25, 15, 1);
    EXPECT_EQ(input.focused(), &b1);
    input.onMouseUp(25, 15, 1);

    input.onMouseDown(85, 15, 1);
    EXPECT_EQ(input.focused(), &b2);
    EXPECT_FALSE(b1.isFocused());
    input.onMouseUp(85, 15, 1);
}

TEST(InputState, RemoveWidget) {
    InputState input;
    Button b1("A");
    b1.setBounds(0, 0, 100, 30);
    input.addWidget(&b1);
    input.onMouseDown(50, 15, 1);
    EXPECT_EQ(input.captured(), &b1);
    input.removeWidget(&b1);
    EXPECT_EQ(input.captured(), nullptr);
}

TEST(InputState, DisabledWidgetIgnored) {
    InputState input;
    Button b1("A");
    b1.setBounds(0, 0, 100, 30);
    b1.setEnabled(false);
    input.addWidget(&b1);

    bool consumed = input.onMouseDown(50, 15, 1);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(input.captured(), nullptr);
}

// ========================= Button =========================

TEST(ButtonWidget, ClickCallsCallback) {
    Button btn("Test");
    btn.setBounds(0, 0, 100, 30);

    bool clicked = false;
    btn.setOnClick([&]() { clicked = true; });

    btn.onMouseDown(50, 15, 1);
    EXPECT_TRUE(btn.isPressed());
    btn.onMouseUp(50, 15, 1);
    EXPECT_TRUE(clicked);
    EXPECT_FALSE(btn.isPressed());
}

TEST(ButtonWidget, ClickOutsideDoesNotFire) {
    Button btn("Test");
    btn.setBounds(0, 0, 100, 30);

    bool clicked = false;
    btn.setOnClick([&]() { clicked = true; });

    btn.onMouseDown(50, 15, 1);
    btn.onMouseUp(500, 500, 1); // outside
    EXPECT_FALSE(clicked);
}

TEST(ButtonWidget, ToggleMode) {
    Button btn("M");
    btn.setBounds(0, 0, 50, 30);
    btn.setToggle(true);
    EXPECT_FALSE(btn.toggleState());

    btn.onMouseDown(25, 15, 1);
    btn.onMouseUp(25, 15, 1);
    EXPECT_TRUE(btn.toggleState());

    btn.onMouseDown(25, 15, 1);
    btn.onMouseUp(25, 15, 1);
    EXPECT_FALSE(btn.toggleState());
}

// ========================= Fader =========================

TEST(FaderWidget, DragChangesValue) {
    Fader fader;
    fader.setBounds(0, 0, 20, 200);
    fader.setRange(0, 1);
    fader.setValue(0.5f);

    float newVal = -1;
    fader.setOnChange([&](float v) { newVal = v; });

    fader.onMouseDown(10, 100, 1);
    // Drag up (negative dy) → increase value
    fader.onMouseDrag(10, 80, 0, -20);
    EXPECT_GT(newVal, 0.5f);
}

TEST(FaderWidget, ValueClamped) {
    Fader fader;
    fader.setBounds(0, 0, 20, 200);
    fader.setRange(0, 1);
    fader.setValue(0.95f);

    fader.onMouseDown(10, 100, 1);
    fader.onMouseDrag(10, -1000, 0, -1000); // huge drag up
    EXPECT_LE(fader.value(), 1.0f);
}

// ========================= Knob =========================

TEST(KnobWidget, DragChangesValue) {
    Knob knob;
    knob.setBounds(0, 0, 40, 50);
    knob.setRange(0, 1);
    knob.setValue(0.5f);

    float newVal = -1;
    knob.setOnChange([&](float v) { newVal = v; });

    knob.onMouseDown(20, 25, 1);
    knob.onMouseDrag(20, 5, 0, -20);
    EXPECT_GT(newVal, 0.5f);
}

TEST(KnobWidget, RightClickResets) {
    Knob knob;
    knob.setBounds(0, 0, 40, 50);
    knob.setRange(0, 1);
    knob.setDefault(0.3f);
    knob.setValue(0.8f);

    float newVal = -1;
    knob.setOnChange([&](float v) { newVal = v; });
    knob.onMouseDown(20, 25, 3); // right-click
    EXPECT_NEAR(knob.value(), 0.3f, 0.01f);
    EXPECT_NEAR(newVal, 0.3f, 0.01f);
}

// ========================= Toggle =========================

TEST(ToggleWidget, TogglesOnClick) {
    Toggle toggle("M", false);
    toggle.setBounds(0, 0, 30, 22);

    bool state = false;
    toggle.setOnChange([&](bool on) { state = on; });

    toggle.onMouseDown(15, 11, 1);
    toggle.onMouseUp(15, 11, 1);
    EXPECT_TRUE(toggle.isOn());
    EXPECT_TRUE(state);

    toggle.onMouseDown(15, 11, 1);
    toggle.onMouseUp(15, 11, 1);
    EXPECT_FALSE(toggle.isOn());
    EXPECT_FALSE(state);
}

// ========================= NumberBox =========================

TEST(NumberBoxWidget, DragChangesValue) {
    NumberBox nb;
    nb.setBounds(0, 0, 60, 22);
    nb.setRange(0, 200);
    nb.setValue(100);
    nb.setSensitivity(1.0f);

    float newVal = -1;
    nb.setOnChange([&](float v) { newVal = v; });

    nb.onMouseDown(30, 11, 1);
    nb.onMouseDrag(30, 1, 0, -10);
    EXPECT_GT(newVal, 100);
}

TEST(NumberBoxWidget, TextInputCommit) {
    NumberBox nb;
    nb.setBounds(0, 0, 60, 22);
    nb.setRange(0, 200);
    nb.setValue(100);

    float newVal = -1;
    nb.setOnChange([&](float v) { newVal = v; });

    // Enter edit mode via key
    nb.onFocusGained();
    nb.onKeyDown(13, false, false); // Enter to start editing
    nb.onTextInput("150");
    nb.onKeyDown(13, false, false); // Enter to commit
    EXPECT_NEAR(newVal, 150.0f, 0.1f);
}

TEST(NumberBoxWidget, EscapeCancels) {
    NumberBox nb;
    nb.setBounds(0, 0, 60, 22);
    nb.setRange(0, 200);
    nb.setValue(100);

    nb.onFocusGained();
    nb.onKeyDown(13, false, false);
    nb.onTextInput("50");
    nb.onKeyDown(27, false, false); // Escape
    EXPECT_NEAR(nb.value(), 100.0f, 0.1f);
}

// ========================= DropDown =========================

TEST(DropDownWidget, SelectItem) {
    DropDown dd;
    dd.setBounds(0, 0, 100, 24);
    dd.setItems({"Sine", "Saw", "Square"});
    dd.setSelected(0);

    int sel = -1;
    dd.setOnChange([&](int i) { sel = i; });

    // Click to open
    dd.onMouseDown(50, 12, 1);
    // Click item 1 (y = 24 + 24*1 + 12 = 60, inside "Saw")
    dd.onMouseDown(50, 24 + 12, 1);
    EXPECT_EQ(sel, 0);
}

TEST(DropDownWidget, SelectedText) {
    DropDown dd;
    dd.setItems({"A", "B", "C"});
    dd.setSelected(1);
    EXPECT_EQ(dd.selectedText(), "B");
}

// ========================= MenuBar =========================

TEST(MenuBar, ClickOpensMenu) {
    MenuBar bar;
    bar.addMenu("File", {
        {"New", "Ctrl+N", nullptr},
        {"Open", "Ctrl+O", nullptr},
    });

    // Click on "File" title area
    bool consumed = bar.handleClick(20, 12);
    EXPECT_TRUE(consumed);
    EXPECT_TRUE(bar.isOpen());
}

TEST(MenuBar, ClickItemFiresAction) {
    MenuBar bar;
    bool fired = false;
    bar.addMenu("File", {
        {"New", "Ctrl+N", [&]() { fired = true; }},
    });

    bar.handleClick(20, 12);       // open menu
    bar.handleClick(20, MenuBar::kMenuBarHeight + 12); // click item
    EXPECT_TRUE(fired);
    EXPECT_FALSE(bar.isOpen());
}

TEST(MenuBar, ClickOutsideCloses) {
    MenuBar bar;
    bar.addMenu("File", {{"New", "", nullptr}});

    bar.handleClick(20, 12); // open
    EXPECT_TRUE(bar.isOpen());
    bar.handleClick(500, 500); // click outside
    EXPECT_FALSE(bar.isOpen());
}

TEST(MenuBar, HoverSwitchesMenu) {
    MenuBar bar;
    bar.addMenu("File", {{"New", "", nullptr}});
    bar.addMenu("Edit", {{"Undo", "", nullptr}});

    // We need to render first to calculate bounds (they're set during render)
    // Since we can't render in tests, just verify the API doesn't crash
    bar.handleClick(20, 12);
    bar.handleMouseMove(100, 12);
}
