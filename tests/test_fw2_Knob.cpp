// UI v2 — FwKnob tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class KnobHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

namespace {

// Simulate a vertical drag: mouseDown, mouseMove beyond threshold,
// then mouseUp.
void simulateVerticalDrag(FwKnob& k, float fromY, float toY,
                           uint16_t mods = 0,
                           uint64_t startMs = 1000) {
    const float x = k.bounds().x + k.bounds().w * 0.5f;
    MouseEvent down{};
    down.x = x; down.y = fromY;
    down.lx = x - k.bounds().x; down.ly = fromY - k.bounds().y;
    down.button = MouseButton::Left;
    down.modifiers = mods;
    down.timestampMs = startMs;
    k.dispatchMouseDown(down);

    // Drive the move past the click-vs-drag threshold (3 px).
    MouseMoveEvent m{};
    m.x = x; m.y = toY;
    m.lx = x - k.bounds().x; m.ly = toY - k.bounds().y;
    m.dx = 0; m.dy = toY - fromY;
    m.modifiers = mods;
    m.timestampMs = startMs + 10;
    k.dispatchMouseMove(m);

    MouseEvent up{};
    up.x = x; up.y = toY;
    up.lx = m.lx; up.ly = m.ly;
    up.button = MouseButton::Left;
    up.modifiers = mods;
    up.timestampMs = startMs + 20;
    k.dispatchMouseUp(up);
}

void simulateRightClick(FwKnob& k, float sx, float sy) {
    MouseEvent down{};
    down.x = sx; down.y = sy;
    down.lx = sx - k.bounds().x; down.ly = sy - k.bounds().y;
    down.button = MouseButton::Right;
    down.timestampMs = 1000;
    k.dispatchMouseDown(down);

    MouseEvent up{};
    up.x = sx; up.y = sy;
    up.lx = down.lx; up.ly = down.ly;
    up.button = MouseButton::Right;
    up.timestampMs = 1020;
    k.dispatchMouseUp(up);
}

} // anon

// ─── Defaults ──────────────────────────────────────────────────────

TEST_F(KnobHarness, DefaultRangeAndValue) {
    FwKnob k;
    EXPECT_FLOAT_EQ(k.min(), 0.0f);
    EXPECT_FLOAT_EQ(k.max(), 1.0f);
    EXPECT_FLOAT_EQ(k.value(), 0.5f);
    EXPECT_FALSE(k.bipolar());
}

// ─── Range + value ─────────────────────────────────────────────────

TEST_F(KnobHarness, SetRangeClampsValue) {
    FwKnob k;
    k.setValue(0.9f);
    k.setRange(0.0f, 0.5f);
    EXPECT_FLOAT_EQ(k.value(), 0.5f);
}

TEST_F(KnobHarness, SetRangeSwapsIfReversed) {
    FwKnob k;
    k.setRange(10.0f, 1.0f);
    EXPECT_FLOAT_EQ(k.min(), 1.0f);
    EXPECT_FLOAT_EQ(k.max(), 10.0f);
}

TEST_F(KnobHarness, SetValueClampsToRange) {
    FwKnob k;
    k.setRange(0.0f, 1.0f);
    k.setValue(2.0f);
    EXPECT_FLOAT_EQ(k.value(), 1.0f);
    k.setValue(-1.0f);
    EXPECT_FLOAT_EQ(k.value(), 0.0f);
}

TEST_F(KnobHarness, SetValueFiresOnChangeForUser) {
    FwKnob k;
    int fires = 0;
    float last = -1.0f;
    k.setOnChange([&](float v) { ++fires; last = v; });
    k.setValue(0.7f, ValueChangeSource::User);
    EXPECT_EQ(fires, 1);
    EXPECT_FLOAT_EQ(last, 0.7f);
}

TEST_F(KnobHarness, SetValueSuppressesCallbackForAutomation) {
    FwKnob k;
    int fires = 0;
    k.setOnChange([&](float) { ++fires; });
    k.setValue(0.7f, ValueChangeSource::Automation);
    EXPECT_FLOAT_EQ(k.value(), 0.7f);
    EXPECT_EQ(fires, 0);
}

TEST_F(KnobHarness, SetValueNoOpWhenUnchanged) {
    FwKnob k;
    k.setValue(0.5f);
    int fires = 0;
    k.setOnChange([&](float) { ++fires; });
    k.setValue(0.5f);
    EXPECT_EQ(fires, 0);
}

// ─── Drag ──────────────────────────────────────────────────────────

TEST_F(KnobHarness, DragUpIncreasesValue) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.5f);
    int fires = 0;
    k.setOnChange([&](float) { ++fires; });

    // Drag 40 pixels up (negative dy) → 0.4 of range → value goes 0.5→0.9
    simulateVerticalDrag(k, 30, -10);
    EXPECT_NEAR(k.value(), 0.9f, 0.001f);
    EXPECT_GE(fires, 1);
}

TEST_F(KnobHarness, DragDownDecreasesValue) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.5f);

    // Drag 40 down (positive dy) → -0.4 → value 0.5→0.1
    simulateVerticalDrag(k, 30, 70);
    EXPECT_NEAR(k.value(), 0.1f, 0.001f);
}

TEST_F(KnobHarness, DragRespectsDPI) {
    // Higher DPI should not make drag "faster" in logical pixels.
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.5f);
    ctx.setDpiScale(2.0f);

    // 40 physical pixels up = 20 logical pixels → 0.2 delta → 0.7.
    simulateVerticalDrag(k, 30, -10);
    EXPECT_NEAR(k.value(), 0.7f, 0.001f);
}

TEST_F(KnobHarness, ShiftFineDragDividesDelta) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.5f);

    simulateVerticalDrag(k, 30, -10, ModifierKey::Shift);
    // 40 px up × 0.1 fine = 0.04 delta → 0.54
    EXPECT_NEAR(k.value(), 0.54f, 0.001f);
}

TEST_F(KnobHarness, CtrlCoarseDragMultipliesDelta) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.5f);

    simulateVerticalDrag(k, 30, 35, ModifierKey::Ctrl);
    // 5 px down × 10 coarse = -0.5 → 0.0 (clamped; tolerance for
    // accumulated FP error through the delta chain)
    EXPECT_NEAR(k.value(), 0.0f, 1e-5f);
}

TEST_F(KnobHarness, DragEndCallbackFiresWithStartAndEnd) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.5f);
    float startV = -1, endV = -1;
    int ends = 0;
    k.setOnDragEnd([&](float s, float e) { ++ends; startV = s; endV = e; });
    simulateVerticalDrag(k, 30, 20);
    EXPECT_EQ(ends, 1);
    EXPECT_FLOAT_EQ(startV, 0.5f);
    EXPECT_GT(endV, 0.5f);
}

// ─── Scroll wheel ──────────────────────────────────────────────────

TEST_F(KnobHarness, ScrollUpIncreasesByStep) {
    FwKnob k;
    k.setValue(0.5f);
    k.setStep(0.1f);
    ScrollEvent s;
    s.dy = 1.0f;
    k.dispatchScroll(s);
    EXPECT_NEAR(k.value(), 0.6f, 0.001f);
}

TEST_F(KnobHarness, ScrollDownDecreasesByStep) {
    FwKnob k;
    k.setValue(0.5f);
    k.setStep(0.1f);
    ScrollEvent s;
    s.dy = -1.0f;
    k.dispatchScroll(s);
    EXPECT_NEAR(k.value(), 0.4f, 0.001f);
}

TEST_F(KnobHarness, ScrollShiftFine) {
    FwKnob k;
    k.setValue(0.5f);
    k.setStep(0.1f);
    ScrollEvent s;
    s.dy = 1.0f;
    s.modifiers = ModifierKey::Shift;
    k.dispatchScroll(s);
    EXPECT_NEAR(k.value(), 0.51f, 0.001f);
}

TEST_F(KnobHarness, ScrollCtrlCoarse) {
    FwKnob k;
    k.setValue(0.5f);
    k.setStep(0.05f);
    ScrollEvent s;
    s.dy = 1.0f;
    s.modifiers = ModifierKey::Ctrl;
    k.dispatchScroll(s);
    EXPECT_NEAR(k.value(), 1.0f, 0.001f);   // 0.5 + 0.5 = 1.0 clamped
}

TEST_F(KnobHarness, ScrollDisabledNoOp) {
    FwKnob k;
    k.setValue(0.5f);
    k.setEnabled(false);
    ScrollEvent s;
    s.dy = 1.0f;
    EXPECT_FALSE(k.dispatchScroll(s));
    EXPECT_FLOAT_EQ(k.value(), 0.5f);
}

// ─── Right-click resets to default ─────────────────────────────────

TEST_F(KnobHarness, RightClickResetsToDefault) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setValue(0.9f);
    k.setDefaultValue(0.3f);
    int rightFires = 0;
    k.setOnRightClick([&](Point) { ++rightFires; });
    int changes = 0;
    k.setOnChange([&](float) { ++changes; });

    simulateRightClick(k, 20, 20);
    EXPECT_FLOAT_EQ(k.value(), 0.3f);
    EXPECT_EQ(rightFires, 1);
    EXPECT_EQ(changes, 1);
}

TEST_F(KnobHarness, RightClickNoDefaultKeepsValue) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setValue(0.9f);
    // No default set.
    int rightFires = 0;
    k.setOnRightClick([&](Point) { ++rightFires; });
    simulateRightClick(k, 20, 20);
    EXPECT_FLOAT_EQ(k.value(), 0.9f);
    EXPECT_EQ(rightFires, 1);
}

// ─── Bipolar ───────────────────────────────────────────────────────

TEST_F(KnobHarness, SetBipolarIsPaintOnly) {
    FwKnob k;
    k.measure(Constraints::loose(200, 200), ctx);
    const int v1 = k.measureVersion();
    k.setBipolar(true);
    EXPECT_EQ(k.measureVersion(), v1);   // paint-only — no re-measure
    EXPECT_TRUE(k.bipolar());
}

// ─── Modulation overlay ────────────────────────────────────────────

TEST_F(KnobHarness, SetModulatedValueClampsToRange) {
    FwKnob k;
    k.setRange(0.0f, 1.0f);
    k.setModulatedValue(1.5f);
    ASSERT_TRUE(k.modulatedValue().has_value());
    EXPECT_FLOAT_EQ(*k.modulatedValue(), 1.0f);
}

TEST_F(KnobHarness, ClearModulation) {
    FwKnob k;
    k.setModulatedValue(0.5f);
    EXPECT_TRUE(k.modulatedValue().has_value());
    k.setModulatedValue(std::nullopt);
    EXPECT_FALSE(k.modulatedValue().has_value());
}

// ─── Value formatter ───────────────────────────────────────────────

TEST_F(KnobHarness, DefaultFormatterRendersTwoDecimal) {
    FwKnob k;
    k.setValue(0.5f);
    EXPECT_EQ(k.formattedValue(), "0.50");
}

TEST_F(KnobHarness, CustomFormatter) {
    FwKnob k;
    k.setValue(0.75f);
    k.setValueFormatter([](float v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0f %%", v * 100.0f);
        return std::string(buf);
    });
    EXPECT_EQ(k.formattedValue(), "75 %");
}

// ─── Measure ───────────────────────────────────────────────────────

TEST_F(KnobHarness, MeasureDefaultDisc) {
    FwKnob k;
    Size s = k.measure(Constraints::loose(200, 200), ctx);
    // Diameter defaults to controlHeight * 1.6 = 28 * 1.6 = 44.8.
    // Plus label (default "" — skipped) + value ("0.50", small font).
    EXPECT_GT(s.w, 30.0f);
    EXPECT_GT(s.h, 44.0f);
}

TEST_F(KnobHarness, SetDiameterOverridesMeasure) {
    FwKnob k;
    k.setDiameter(80);
    k.setShowLabel(false);
    k.setShowValue(false);
    Size s = k.measure(Constraints::loose(200, 200), ctx);
    EXPECT_FLOAT_EQ(s.w, 80.0f);
    EXPECT_FLOAT_EQ(s.h, 80.0f);
}

TEST_F(KnobHarness, SetLabelInvalidatesMeasure) {
    FwKnob k;
    k.measure(Constraints::loose(200, 200), ctx);
    const int v1 = k.measureVersion();
    k.setLabel("Gain");
    EXPECT_GT(k.measureVersion(), v1);
}

// ─── Relayout boundary ────────────────────────────────────────────

TEST_F(KnobHarness, IsRelayoutBoundary) {
    FwKnob k;
    EXPECT_TRUE(k.isRelayoutBoundary());
}

// ─── Inline edit mode ─────────────────────────────────────────────
//
// Double-click opens an edit buffer seeded with the current formatted
// value. Digits / ± / '.' feed the buffer (via takeTextInput — this is
// what the host wires from SDL_EVENT_TEXT_INPUT). Enter commits (parse
// float, clamp, setValue as User), Escape / endEdit(false) cancels,
// Backspace pops one char.

TEST_F(KnobHarness, BeginEditSeedsBufferWithFormattedValue) {
    FwKnob k;
    k.setValue(0.75f);
    EXPECT_FALSE(k.isEditing());
    k.beginEdit();
    EXPECT_TRUE(k.isEditing());
    EXPECT_EQ(k.editBuffer(), "0.75");
}

TEST_F(KnobHarness, BeginEditIdempotent) {
    FwKnob k;
    k.setValue(0.25f);
    k.beginEdit();
    EXPECT_EQ(k.editBuffer(), "0.25");
    // Second call must not reseed the buffer (user's partial typing
    // would be clobbered if it did).
    k.takeTextInput("9");
    k.beginEdit();
    EXPECT_EQ(k.editBuffer(), "0.259");
}

TEST_F(KnobHarness, EndEditCommitParsesBuffer) {
    FwKnob k;
    k.setRange(0.0f, 10.0f);
    k.setValue(1.0f);
    int fires = 0;
    k.setOnChange([&](float) { ++fires; });
    k.beginEdit();
    // Replace the buffer contents by clearing and feeding fresh text.
    // The simplest way from the public API is endEdit(false) + begin
    // again, but that wouldn't exercise typed input — so we use
    // Backspace-style pops via the test's editBuffer semantics: feed
    // a new buffer in one shot.
    // (The knob's own onKeyDown path exercises Backspace; here we
    //  just prove the commit parser works.)
    // Reset the buffer by sending backspace via onKeyDown until empty,
    // then type a new value.
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.takeTextInput("7.5");
    k.endEdit(/*commit*/true);
    EXPECT_FALSE(k.isEditing());
    EXPECT_FLOAT_EQ(k.value(), 7.5f);
    EXPECT_EQ(fires, 1);
}

TEST_F(KnobHarness, EndEditCancelLeavesValueUnchanged) {
    FwKnob k;
    k.setValue(0.5f);
    k.beginEdit();
    k.takeTextInput("9");
    int fires = 0;
    k.setOnChange([&](float) { ++fires; });
    k.endEdit(/*commit*/false);
    EXPECT_FALSE(k.isEditing());
    EXPECT_FLOAT_EQ(k.value(), 0.5f);
    EXPECT_EQ(fires, 0);
    EXPECT_TRUE(k.editBuffer().empty());
}

TEST_F(KnobHarness, EndEditEmptyBufferIsNoCommit) {
    FwKnob k;
    k.setValue(0.5f);
    k.beginEdit();
    // Drop every seeded character.
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.endEdit(/*commit*/true);
    EXPECT_FLOAT_EQ(k.value(), 0.5f);  // unchanged — no parseable value
}

TEST_F(KnobHarness, EndEditCommitClampsToRange) {
    FwKnob k;
    k.setRange(0.0f, 1.0f);
    k.setValue(0.5f);
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.takeTextInput("42");
    k.endEdit(true);
    EXPECT_FLOAT_EQ(k.value(), 1.0f);
}

TEST_F(KnobHarness, TakeTextInputFiltersNonDigits) {
    FwKnob k;
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.takeTextInput("a1b2c3!");
    EXPECT_EQ(k.editBuffer(), "123");
}

TEST_F(KnobHarness, TakeTextInputAllowsLeadingMinus) {
    FwKnob k;
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.takeTextInput("-");
    k.takeTextInput("4");
    k.takeTextInput("2");
    EXPECT_EQ(k.editBuffer(), "-42");
}

TEST_F(KnobHarness, TakeTextInputRejectsMinusAfterDigit) {
    FwKnob k;
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.takeTextInput("5");
    k.takeTextInput("-");
    EXPECT_EQ(k.editBuffer(), "5");
}

TEST_F(KnobHarness, TakeTextInputAllowsSingleDecimalPoint) {
    FwKnob k;
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.takeTextInput("1.5");
    k.takeTextInput(".");   // second '.' — rejected
    k.takeTextInput("3");
    EXPECT_EQ(k.editBuffer(), "1.53");
}

TEST_F(KnobHarness, TakeTextInputIgnoredWhenNotEditing) {
    FwKnob k;
    EXPECT_FALSE(k.isEditing());
    k.takeTextInput("123");
    EXPECT_EQ(k.editBuffer(), "");
}

TEST_F(KnobHarness, OnKeyDownEnterCommits) {
    FwKnob k;
    k.setRange(0.0f, 10.0f);
    k.setValue(1.0f);
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    k.takeTextInput("5");
    KeyEvent enter;
    enter.key = Key::Enter;
    EXPECT_TRUE(k.dispatchKeyDown(enter));
    EXPECT_FALSE(k.isEditing());
    EXPECT_FLOAT_EQ(k.value(), 5.0f);
}

TEST_F(KnobHarness, OnKeyDownEscapeCancels) {
    FwKnob k;
    k.setValue(0.5f);
    k.beginEdit();
    k.takeTextInput("9");
    KeyEvent esc;
    esc.key = Key::Escape;
    EXPECT_TRUE(k.dispatchKeyDown(esc));
    EXPECT_FALSE(k.isEditing());
    EXPECT_FLOAT_EQ(k.value(), 0.5f);
}

TEST_F(KnobHarness, OnKeyDownBackspacePopsChar) {
    FwKnob k;
    k.setValue(0.5f);
    k.beginEdit();
    EXPECT_EQ(k.editBuffer(), "0.50");
    KeyEvent bs;
    bs.key = Key::Backspace;
    EXPECT_TRUE(k.dispatchKeyDown(bs));
    EXPECT_EQ(k.editBuffer(), "0.5");
    EXPECT_TRUE(k.dispatchKeyDown(bs));
    EXPECT_EQ(k.editBuffer(), "0.");
}

TEST_F(KnobHarness, OnKeyDownBackspaceOnEmptyIsNoop) {
    FwKnob k;
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    EXPECT_TRUE(k.dispatchKeyDown(bs));
    EXPECT_EQ(k.editBuffer(), "");
}

TEST_F(KnobHarness, OnKeyDownDigitFallback) {
    // Hosts that don't fire SDL_EVENT_TEXT_INPUT still get digit entry
    // via the Num0..Num9 key fallback.
    FwKnob k;
    k.beginEdit();
    KeyEvent bs;
    bs.key = Key::Backspace;
    while (!k.editBuffer().empty()) k.dispatchKeyDown(bs);
    KeyEvent d;
    d.key = Key::Num7;
    EXPECT_TRUE(k.dispatchKeyDown(d));
    EXPECT_EQ(k.editBuffer(), "7");
}

TEST_F(KnobHarness, OnKeyDownNotEditingReturnsFalse) {
    FwKnob k;
    KeyEvent enter;
    enter.key = Key::Enter;
    EXPECT_FALSE(k.dispatchKeyDown(enter));
}

TEST_F(KnobHarness, DoubleClickOpensEdit) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setValue(0.5f);
    // Synthesize a double-click by down/up/down/up with a short gap
    // on the same spot.
    auto fire = [&](uint64_t t) {
        MouseEvent d{};
        d.x = 20; d.y = 20; d.lx = 20; d.ly = 20;
        d.button = MouseButton::Left;
        d.timestampMs = t;
        k.dispatchMouseDown(d);
        MouseEvent u = d;
        u.timestampMs = t + 5;
        k.dispatchMouseUp(u);
    };
    fire(1000);
    fire(1100);
    EXPECT_TRUE(k.isEditing());
}

TEST_F(KnobHarness, DoubleClickDisabledDoesNotOpen) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setEnabled(false);
    auto fire = [&](uint64_t t) {
        MouseEvent d{};
        d.x = 20; d.y = 20; d.lx = 20; d.ly = 20;
        d.button = MouseButton::Left;
        d.timestampMs = t;
        k.dispatchMouseDown(d);
        MouseEvent u = d;
        u.timestampMs = t + 5;
        k.dispatchMouseUp(u);
    };
    fire(1000);
    fire(1100);
    EXPECT_FALSE(k.isEditing());
}

// ─── Detents (rolled-in FwDentedKnob behaviour) ───────────────────
//
// addDetent(value, snapRangeFrac) installs a "sticky" value. While the
// raw drag position lies within snapRangeFrac × range of the detent,
// the displayed value locks to it. Drag past the threshold and the
// displayed value resumes tracking the raw position.

TEST_F(KnobHarness, AddDetentAndClear) {
    FwKnob k;
    EXPECT_TRUE(k.detents().empty());
    k.addDetent(0.5f, 0.05f);
    k.addDetent(0.9f);
    ASSERT_EQ(k.detents().size(), 2u);
    EXPECT_FLOAT_EQ(k.detents()[0].value, 0.5f);
    EXPECT_FLOAT_EQ(k.detents()[0].snapRangeFrac, 0.05f);
    EXPECT_FLOAT_EQ(k.detents()[1].value, 0.9f);
    k.clearDetents();
    EXPECT_TRUE(k.detents().empty());
}

TEST_F(KnobHarness, DetentSnapsWhenWithinRange) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.3f);
    k.addDetent(0.5f, 0.05f);   // snap zone = 0.05 × 1.0 = ±0.05
    // Drag up 18 px → raw +0.18 → reaches 0.48. Outside the snap zone
    // (0.45..0.55)? No — 0.48 IS inside. So value snaps to 0.5.
    simulateVerticalDrag(k, 30, 12);
    EXPECT_FLOAT_EQ(k.value(), 0.5f);
}

TEST_F(KnobHarness, DetentReleasesPastThreshold) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setPixelsPerFullRange(100);
    k.setValue(0.45f);
    k.addDetent(0.5f, 0.05f);
    // Drag up 20 px → raw 0.65 → outside 0.45..0.55 snap zone → no snap.
    simulateVerticalDrag(k, 30, 10);
    EXPECT_NEAR(k.value(), 0.65f, 0.001f);
}

TEST_F(KnobHarness, DetentOnlySnapsDuringDrag) {
    // setValue from code doesn't pass through snap logic — programmatic
    // writes are exact. (Snap is a UX affordance; if a formatter / code
    // path sets 0.48 on purpose, we don't want it bumped to 0.5.)
    FwKnob k;
    k.addDetent(0.5f, 0.05f);
    k.setValue(0.48f);
    EXPECT_FLOAT_EQ(k.value(), 0.48f);
}

// ─── Wrap mode (rolled-in FwKnob360 behaviour) ────────────────────

TEST_F(KnobHarness, WrapModeDefaultOff) {
    FwKnob k;
    EXPECT_FALSE(k.wrapMode());
}

TEST_F(KnobHarness, SetWrapModeTogglesAndInvalidates) {
    FwKnob k;
    k.measure(Constraints::loose(200, 200), ctx);
    const int v1 = k.measureVersion();
    k.setWrapMode(true);
    EXPECT_TRUE(k.wrapMode());
    // Paint-and-geometry change → invalidate bumps measureVersion
    // (size_policy unchanged, but paint depends on wrap mode).
    EXPECT_GT(k.measureVersion(), v1);
}

TEST_F(KnobHarness, WrapModeWrapsPastMax) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setRange(0.0f, 1.0f);
    k.setPixelsPerFullRange(100);
    k.setWrapMode(true);
    k.setValue(0.9f);
    // Drag up 30 px → raw +0.3 → raw 1.2 → wraps to 0.2.
    simulateVerticalDrag(k, 30, 0);
    EXPECT_NEAR(k.value(), 0.2f, 0.001f);
}

TEST_F(KnobHarness, WrapModeWrapsPastMin) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setRange(0.0f, 1.0f);
    k.setPixelsPerFullRange(100);
    k.setWrapMode(true);
    k.setValue(0.1f);
    // Drag down 30 px → raw -0.3 → raw -0.2 → wraps to 0.8.
    simulateVerticalDrag(k, 30, 60);
    EXPECT_NEAR(k.value(), 0.8f, 0.001f);
}

TEST_F(KnobHarness, NoWrapModeClampsAtMax) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setRange(0.0f, 1.0f);
    k.setPixelsPerFullRange(100);
    k.setValue(0.9f);
    // Same 30 px up drag without wrap → clamped at 1.0.
    simulateVerticalDrag(k, 30, 0);
    EXPECT_FLOAT_EQ(k.value(), 1.0f);
}

TEST_F(KnobHarness, WrapModeAndDetentsCoexist) {
    FwKnob k;
    k.layout(Rect{0, 0, 40, 60}, ctx);
    k.setRange(0.0f, 1.0f);
    k.setPixelsPerFullRange(100);
    k.setWrapMode(true);
    k.addDetent(0.0f, 0.05f);   // home detent (wrap boundary)
    k.setValue(0.95f);
    // Drag up 6 px → raw 1.01 → wraps to 0.01 → within 0.0's snap zone → 0.0.
    simulateVerticalDrag(k, 30, 24);
    EXPECT_FLOAT_EQ(k.value(), 0.0f);
}
