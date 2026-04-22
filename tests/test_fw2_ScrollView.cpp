// UI v2 — ScrollView tests.

#include <gtest/gtest.h>

#include "ui/framework/v2/ScrollView.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

namespace {
// Content widget with a controllable intrinsic size.
class FixedBox : public Widget {
public:
    Size preferred;
    FixedBox(float w, float h) : preferred{w, h} {}
    Size onMeasure(Constraints c, UIContext&) override {
        // Honor the caller's constraints: if tight on an axis, use
        // that; otherwise report preferred (up to max).
        float w = preferred.w, h = preferred.h;
        if (c.minW == c.maxW) w = c.maxW;
        else                  w = std::min(preferred.w, c.maxW);
        if (c.minH == c.maxH) h = c.maxH;
        else                  h = std::min(preferred.h, c.maxH);
        return {w, h};
    }
};
} // anon

class ScrollViewHarness : public ::testing::Test {
protected:
    void SetUp() override    { UIContext::setGlobal(&ctx); }
    void TearDown() override { UIContext::setGlobal(nullptr); }
    UIContext ctx;
};

// ─── Layout ────────────────────────────────────────────────────────

TEST_F(ScrollViewHarness, EmptyScrollView) {
    ScrollView sv;
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);
    EXPECT_FALSE(sv.showVerticalBar());
    EXPECT_FALSE(sv.showHorizontalBar());
    EXPECT_FLOAT_EQ(sv.scrollOffset().x, 0);
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 0);
}

TEST_F(ScrollViewHarness, ContentSmallerThanViewportNoBars) {
    ScrollView sv;
    FixedBox content(200, 150);
    sv.setContent(&content);

    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    EXPECT_FALSE(sv.showVerticalBar());
    EXPECT_FALSE(sv.showHorizontalBar());
    EXPECT_FLOAT_EQ(content.bounds().x, 0);
    EXPECT_FLOAT_EQ(content.bounds().y, 0);
}

TEST_F(ScrollViewHarness, VerticalOverflowShowsVerticalBar) {
    ScrollView sv;
    FixedBox content(200, 800);
    sv.setContent(&content);

    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    EXPECT_TRUE(sv.showVerticalBar());
    EXPECT_FALSE(sv.showHorizontalBar());
    EXPECT_FLOAT_EQ(sv.contentSize().h, 800);
}

TEST_F(ScrollViewHarness, AlwaysOverflowAlwaysShowsBar) {
    ScrollView sv;
    FixedBox content(50, 50);
    sv.setContent(&content);
    sv.setVerticalOverflow(ScrollOverflow::Always);

    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    EXPECT_TRUE(sv.showVerticalBar());
}

TEST_F(ScrollViewHarness, NeverOverflowHidesBar) {
    ScrollView sv;
    FixedBox content(50, 800);
    sv.setContent(&content);
    sv.setVerticalOverflow(ScrollOverflow::Never);

    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    EXPECT_FALSE(sv.showVerticalBar());
}

// ─── Scroll offset ─────────────────────────────────────────────────

TEST_F(ScrollViewHarness, SetScrollOffsetClampsHigh) {
    ScrollView sv;
    FixedBox content(400, 800);
    sv.setContent(&content);

    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    sv.setScrollOffset({0, 9999});
    // max y = contentH - viewportH = 800 - (300 - barT)
    const Point m = sv.maxScrollOffset();
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, m.y);
    EXPECT_GT(m.y, 0.0f);
}

TEST_F(ScrollViewHarness, SetScrollOffsetClampsNegative) {
    ScrollView sv;
    FixedBox content(400, 800);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    sv.setScrollOffset({0, -50});
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 0);
}

TEST_F(ScrollViewHarness, ScrollByAccumulates) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    sv.scrollBy({0, 100});
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 100);
    sv.scrollBy({0, 50});
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 150);
}

TEST_F(ScrollViewHarness, ScrollToTopZeros) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);
    sv.setScrollOffset({0, 500});
    sv.scrollToTop();
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 0);
}

TEST_F(ScrollViewHarness, ScrollToBottomMaxesY) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    sv.scrollToBottom();
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, sv.maxScrollOffset().y);
}

TEST_F(ScrollViewHarness, ShrinkingContentRetractsScroll) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);
    sv.setScrollOffset({0, 500});

    // Shrink content — re-layout should clamp scroll offset.
    content.preferred = Size{400, 250};   // fits entirely now
    content.invalidate();
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 0);
}

// ─── Input ─────────────────────────────────────────────────────────

TEST_F(ScrollViewHarness, WheelScrollsDown) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    ScrollEvent se{};
    se.x = 200; se.y = 150;
    se.dy = -1.0f;   // wheel-down convention: negative dy
    sv.dispatchScroll(se);
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, sv.scrollWheelMultiplier());
}

TEST_F(ScrollViewHarness, ShiftWheelScrollsHorizontal) {
    ScrollView sv;
    FixedBox content(2000, 300);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    ScrollEvent se{};
    se.x = 200; se.y = 150;
    se.dy = -1.0f;
    se.modifiers = ModifierKey::Shift;
    sv.dispatchScroll(se);
    EXPECT_FLOAT_EQ(sv.scrollOffset().x, sv.scrollWheelMultiplier());
}

TEST_F(ScrollViewHarness, CtrlWheelPassesThrough) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    ScrollEvent se{};
    se.x = 200; se.y = 150;
    se.dy = -1.0f;
    se.modifiers = ModifierKey::Ctrl;
    sv.dispatchScroll(se);
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 0);   // no scroll
}

TEST_F(ScrollViewHarness, ArrowKeysStep) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    KeyEvent down{};
    down.key = Key::Down;
    sv.dispatchKeyDown(down);
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, sv.arrowStep());
}

TEST_F(ScrollViewHarness, HomeEndKeys) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    KeyEvent end{}; end.key = Key::End;
    sv.dispatchKeyDown(end);
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, sv.maxScrollOffset().y);

    KeyEvent home{}; home.key = Key::Home;
    sv.dispatchKeyDown(home);
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 0);
}

TEST_F(ScrollViewHarness, PageDownPagesByPageStep) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.setPageStep(100);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    KeyEvent pd{}; pd.key = Key::PageDown;
    sv.dispatchKeyDown(pd);
    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 100);
}

// ─── Callbacks ─────────────────────────────────────────────────────

TEST_F(ScrollViewHarness, OnScrollFires) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    int fires = 0;
    Point lastOffset{};
    sv.setOnScroll([&](Point o) { ++fires; lastOffset = o; });

    sv.scrollBy({0, 50});
    EXPECT_EQ(fires, 1);
    EXPECT_FLOAT_EQ(lastOffset.y, 50);
}

TEST_F(ScrollViewHarness, OnScrollNotFiredForAutomation) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    int fires = 0;
    sv.setOnScroll([&](Point) { ++fires; });
    sv.setScrollOffset({0, 100}, ValueChangeSource::Automation);
    EXPECT_EQ(fires, 0);
}

TEST_F(ScrollViewHarness, SameOffsetNoOp) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);
    sv.setScrollOffset({0, 100});

    int fires = 0;
    sv.setOnScroll([&](Point) { ++fires; });
    sv.setScrollOffset({0, 100});
    EXPECT_EQ(fires, 0);
}

// ─── Viewport size ────────────────────────────────────────────────

TEST_F(ScrollViewHarness, ViewportSizeSubtractsBars) {
    ScrollView sv;
    FixedBox content(400, 2000);
    sv.setContent(&content);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    // Vertical bar only.
    const Size vp = sv.viewportSize();
    EXPECT_LT(vp.w, 400.0f);   // bar took some width
    EXPECT_FLOAT_EQ(vp.h, 300.0f);
}

TEST_F(ScrollViewHarness, ReplaceContentResetsScroll) {
    ScrollView sv;
    FixedBox oldContent(400, 2000);
    sv.setContent(&oldContent);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);
    sv.setScrollOffset({0, 500});

    FixedBox newContent(400, 200);
    sv.setContent(&newContent);
    sv.measure(Constraints::loose(400, 300), ctx);
    sv.layout(Rect{0, 0, 400, 300}, ctx);

    EXPECT_FLOAT_EQ(sv.scrollOffset().y, 0);
    EXPECT_EQ(sv.content(), &newContent);
}
