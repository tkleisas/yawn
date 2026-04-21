// UI v2 — Label tests.
//
// Without a real Font, Label falls back to 8-logical-px-per-char for
// measurement. That's what these tests exercise.

#include <gtest/gtest.h>

#include "ui/framework/v2/Label.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class LabelHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;   // font = nullptr → fallback 8 px/char
};

// ─── Measure ────────────────────────────────────────────────────────

TEST_F(LabelHarness, EmptyLabelMeasuresToZeroWidth) {
    Label lbl;
    Size s = lbl.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 0.0f);
    // Height is line height — with no Font, fontSize × 1.2 = 13 × 1.2
    EXPECT_GT(s.h, 0.0f);
}

TEST_F(LabelHarness, MeasureExactTextViaFallback) {
    Label lbl("Volume");   // 6 chars × 8 = 48 px
    Size s = lbl.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 48.0f);
}

TEST_F(LabelHarness, MinWidthEnforced) {
    Label lbl("Hi");       // 2 × 8 = 16
    lbl.setMinWidth(100.0f);
    Size s = lbl.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 100.0f);
}

TEST_F(LabelHarness, MaxWidthClampsBeforeConstraint) {
    Label lbl("This is a very long label");  // 25 × 8 = 200
    lbl.setMaxWidth(80.0f);
    Size s = lbl.measure(Constraints::loose(300, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 80.0f);
}

TEST_F(LabelHarness, ConstraintClampsWidth) {
    Label lbl("A very long label");  // > constraint
    Size s = lbl.measure(Constraints::loose(50, 100), ctx);
    EXPECT_FLOAT_EQ(s.w, 50.0f);
}

// ─── Truncation ─────────────────────────────────────────────────────

TEST_F(LabelHarness, EllipsisAppliedWhenOverflow) {
    // 10 chars × 8 = 80 px; available = 40 px.
    Label lbl("0123456789");
    std::string truncated = lbl.effectiveText(40.0f, ctx);
    // Ellipsis is 3 bytes UTF-8. With 40 px = 5 chars budget, minus
    // ellipsis (8px, 1 "logical char"), we fit 4 prefix chars + "…".
    // So: "0123…"
    EXPECT_EQ(truncated, "0123\xE2\x80\xA6");
}

TEST_F(LabelHarness, NoTruncationWhenFits) {
    Label lbl("hello");
    std::string shown = lbl.effectiveText(100.0f, ctx);
    EXPECT_EQ(shown, "hello");
}

TEST_F(LabelHarness, TruncationNoneLeavesText) {
    Label lbl("0123456789");
    lbl.setTruncation(Truncation::None);
    std::string shown = lbl.effectiveText(20.0f, ctx);
    EXPECT_EQ(shown, "0123456789");
}

TEST_F(LabelHarness, TruncationClipLeavesText) {
    Label lbl("0123456789");
    lbl.setTruncation(Truncation::Clip);
    std::string shown = lbl.effectiveText(20.0f, ctx);
    // Clip doesn't modify text; widget bounds clip at paint time.
    EXPECT_EQ(shown, "0123456789");
}

// ─── Invalidation ──────────────────────────────────────────────────

TEST_F(LabelHarness, SetTextInvalidatesMeasure) {
    Label lbl("hello");
    lbl.measure(Constraints::loose(300, 100), ctx);
    const int v1 = lbl.measureVersion();
    lbl.setText("world");
    EXPECT_GT(lbl.measureVersion(), v1);
}

TEST_F(LabelHarness, SetColorDoesNotInvalidate) {
    Label lbl("hello");
    lbl.measure(Constraints::loose(300, 100), ctx);
    const int v1 = lbl.measureVersion();
    lbl.setColor(Color{255, 0, 0});
    EXPECT_EQ(lbl.measureVersion(), v1);   // paint-only
}

TEST_F(LabelHarness, SetFontScaleInvalidates) {
    Label lbl("hello");
    lbl.measure(Constraints::loose(300, 100), ctx);
    const int v1 = lbl.measureVersion();
    lbl.setFontScale(1.5f);
    EXPECT_GT(lbl.measureVersion(), v1);
}

TEST_F(LabelHarness, SetAlignDoesNotInvalidate) {
    Label lbl("hello");
    lbl.measure(Constraints::loose(300, 100), ctx);
    const int v1 = lbl.measureVersion();
    lbl.setAlign(TextAlign::Center, VerticalAlign::Top);
    EXPECT_EQ(lbl.measureVersion(), v1);
}

// ─── Relayout boundary ─────────────────────────────────────────────

TEST_F(LabelHarness, LabelIsRelayoutBoundary) {
    // Labels default to flex=0 (fixed-size policy) → auto boundary.
    Label lbl("hello");
    EXPECT_TRUE(lbl.isRelayoutBoundary());
}

// ─── Cache hit ─────────────────────────────────────────────────────

TEST_F(LabelHarness, MeasureCacheHit) {
    Label lbl("hello");
    lbl.measure(Constraints::loose(300, 100), ctx);
    const int calls1 = lbl.measureCallCount();
    lbl.measure(Constraints::loose(300, 100), ctx);
    lbl.measure(Constraints::loose(300, 100), ctx);
    EXPECT_EQ(lbl.measureCallCount(), calls1);   // no re-measure
}
