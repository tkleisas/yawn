#include <gtest/gtest.h>
#include "ui/framework/Viewport.h"
#include "ui/framework/ConstraintBox.h"
#include "ui/framework/Overlay.h"
#include "ui/framework/Animator.h"

using namespace yawn::ui::fw;

class SizedWidget : public Widget {
public:
    Size preferred;
    SizedWidget(float w, float h) : preferred{w, h} {}
    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain(preferred);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Viewport tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(Viewport, ContentSizeMeasured) {
    Viewport vp;
    SizedWidget content(500, 800);
    vp.setContent(&content);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);

    EXPECT_FLOAT_EQ(vp.contentSize().w, 500);
    EXPECT_FLOAT_EQ(vp.contentSize().h, 800);
}

TEST(Viewport, ScrollClampedToRange) {
    Viewport vp;
    SizedWidget content(500, 800);
    vp.setContent(&content);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);
    vp.layout(Rect{0, 0, 200, 200}, ctx);

    vp.setScrollY(1000);
    EXPECT_FLOAT_EQ(vp.scrollY(), 600);  // 800 - 200

    vp.setScrollY(-50);
    EXPECT_FLOAT_EQ(vp.scrollY(), 0);
}

TEST(Viewport, ScrollByAccumulates) {
    Viewport vp;
    SizedWidget content(500, 800);
    vp.setContent(&content);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);
    vp.layout(Rect{0, 0, 200, 200}, ctx);

    vp.scrollBy(0, 50);
    EXPECT_FLOAT_EQ(vp.scrollY(), 50);
    vp.scrollBy(0, 100);
    EXPECT_FLOAT_EQ(vp.scrollY(), 150);
}

TEST(Viewport, DisabledAxisIgnored) {
    Viewport vp;
    SizedWidget content(500, 800);
    vp.setContent(&content);
    vp.setScrollHorizontal(false);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);
    vp.layout(Rect{0, 0, 200, 200}, ctx);

    vp.setScrollX(100);
    EXPECT_FLOAT_EQ(vp.scrollX(), 0);  // Disabled
    vp.setScrollY(100);
    EXPECT_FLOAT_EQ(vp.scrollY(), 100);  // Enabled
}

TEST(Viewport, VisibleRegion) {
    Viewport vp;
    SizedWidget content(500, 800);
    vp.setContent(&content);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);
    vp.layout(Rect{0, 0, 200, 200}, ctx);

    vp.setScroll(50, 100);
    Rect vr = vp.visibleRegion();
    EXPECT_FLOAT_EQ(vr.x, 50);
    EXPECT_FLOAT_EQ(vr.y, 100);
    EXPECT_FLOAT_EQ(vr.w, 200);
    EXPECT_FLOAT_EQ(vr.h, 200);
}

TEST(Viewport, MaxScroll) {
    Viewport vp;
    SizedWidget content(500, 800);
    vp.setContent(&content);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);
    vp.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(vp.maxScrollX(), 300);
    EXPECT_FLOAT_EQ(vp.maxScrollY(), 600);
}

TEST(Viewport, ContentSmallerThanViewport) {
    Viewport vp;
    SizedWidget content(100, 100);
    vp.setContent(&content);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);
    vp.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(vp.maxScrollX(), 0);
    EXPECT_FLOAT_EQ(vp.maxScrollY(), 0);
    vp.setScroll(50, 50);
    EXPECT_FLOAT_EQ(vp.scrollX(), 0);
    EXPECT_FLOAT_EQ(vp.scrollY(), 0);
}

TEST(Viewport, ScrollEvent) {
    Viewport vp;
    SizedWidget content(500, 800);
    vp.setContent(&content);

    UIContext ctx;
    vp.measure(Constraints::loose(200, 200), ctx);
    vp.layout(Rect{0, 0, 200, 200}, ctx);

    ScrollEvent e;
    e.dy = -2;  // Scroll down (negative dy = scroll down)
    e.x = 100; e.y = 100;

    bool consumed = vp.onScroll(e);
    EXPECT_TRUE(consumed);
    EXPECT_FLOAT_EQ(vp.scrollY(), 60);  // 2 * 30 (default speed)
}

// ═══════════════════════════════════════════════════════════════════════════
// ConstraintBox tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ConstraintBox, LeftAndRightAnchors) {
    ConstraintBox box;
    SizedWidget child(50, 30);
    box.addChild(&child);
    box.setConstraints(&child, EdgeConstraint::anchored({}, 10.0f, {}, 10.0f));

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 10);
    EXPECT_FLOAT_EQ(child.bounds().w, 180);  // 200 - 10 - 10
}

TEST(ConstraintBox, TopAndBottomAnchors) {
    ConstraintBox box;
    SizedWidget child(50, 30);
    box.addChild(&child);
    box.setConstraints(&child, EdgeConstraint::anchored(20.0f, {}, 30.0f, {}));

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().y, 20);
    EXPECT_FLOAT_EQ(child.bounds().h, 150);  // 200 - 20 - 30
}

TEST(ConstraintBox, LeftOnlyUsesPreferredWidth) {
    ConstraintBox box;
    SizedWidget child(50, 30);
    box.addChild(&child);
    box.setConstraints(&child, EdgeConstraint::anchored({}, {}, {}, 15.0f));

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 15);
    EXPECT_FLOAT_EQ(child.bounds().w, 50);  // Preferred
}

TEST(ConstraintBox, RightOnlyAlignsRight) {
    ConstraintBox box;
    SizedWidget child(50, 30);
    box.addChild(&child);
    box.setConstraints(&child, EdgeConstraint::anchored({}, 10.0f, {}, {}));

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 140);  // 200 - 10 - 50
    EXPECT_FLOAT_EQ(child.bounds().w, 50);
}

TEST(ConstraintBox, CenterHorizontal) {
    ConstraintBox box;
    SizedWidget child(60, 40);
    box.addChild(&child);
    box.setConstraints(&child, EdgeConstraint::centered(true, false));

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 70);  // (200 - 60) / 2
    EXPECT_FLOAT_EQ(child.bounds().w, 60);
}

TEST(ConstraintBox, CenterVertical) {
    ConstraintBox box;
    SizedWidget child(60, 40);
    box.addChild(&child);
    box.setConstraints(&child, EdgeConstraint::centered(false, true));

    UIContext ctx;
    box.measure(Constraints::loose(200, 200), ctx);
    box.layout(Rect{0, 0, 200, 200}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().y, 80);  // (200 - 40) / 2
}

TEST(ConstraintBox, AllFourAnchors) {
    ConstraintBox box;
    SizedWidget child(0, 0);
    box.addChild(&child);
    box.setConstraints(&child, EdgeConstraint::anchored(10.0f, 20.0f, 30.0f, 40.0f));

    UIContext ctx;
    box.measure(Constraints::loose(300, 300), ctx);
    box.layout(Rect{0, 0, 300, 300}, ctx);

    EXPECT_FLOAT_EQ(child.bounds().x, 40);
    EXPECT_FLOAT_EQ(child.bounds().y, 10);
    EXPECT_FLOAT_EQ(child.bounds().w, 240);  // 300 - 40 - 20
    EXPECT_FLOAT_EQ(child.bounds().h, 260);  // 300 - 10 - 30
}

// ═══════════════════════════════════════════════════════════════════════════
// Overlay tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(Overlay, ShowAndHide) {
    Overlay overlay;
    SizedWidget popup(100, 50);

    overlay.show(&popup);
    EXPECT_FALSE(overlay.isEmpty());
    EXPECT_EQ(overlay.childCount(), 1);

    overlay.hide(&popup);
    EXPECT_TRUE(overlay.isEmpty());
    EXPECT_EQ(overlay.childCount(), 0);
}

TEST(Overlay, ModalFlag) {
    Overlay overlay;
    SizedWidget dialog(200, 100);

    overlay.show(&dialog, true);
    EXPECT_TRUE(overlay.hasModal());

    overlay.hide(&dialog);
    EXPECT_FALSE(overlay.hasModal());
}

TEST(Overlay, HideAll) {
    Overlay overlay;
    SizedWidget a(50, 50), b(50, 50);
    overlay.show(&a);
    overlay.show(&b, true);
    EXPECT_EQ(overlay.childCount(), 2);

    overlay.hideAll();
    EXPECT_TRUE(overlay.isEmpty());
}

TEST(Overlay, NoDuplicates) {
    Overlay overlay;
    SizedWidget popup(100, 50);
    overlay.show(&popup);
    overlay.show(&popup);  // Should not duplicate
    EXPECT_EQ(overlay.childCount(), 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Animator tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(Animator, AnimateToTarget) {
    Animator animator;
    float value = 0;
    animator.animate(value, 100, 1.0f, Ease::linear);

    EXPECT_TRUE(animator.hasAnimations());

    animator.update(0.5f);
    EXPECT_NEAR(value, 50, 0.1f);

    animator.update(0.5f);
    EXPECT_NEAR(value, 100, 0.1f);
    EXPECT_FALSE(animator.hasAnimations());
}

TEST(Animator, ZeroDurationIsInstant) {
    Animator animator;
    float value = 10;
    animator.animate(value, 50, 0, Ease::linear);
    EXPECT_FLOAT_EQ(value, 50);
    EXPECT_FALSE(animator.hasAnimations());
}

TEST(Animator, CancelAnimation) {
    Animator animator;
    float value = 0;
    animator.animate(value, 100, 1.0f, Ease::linear);
    EXPECT_TRUE(animator.isAnimating(&value));

    animator.cancel(&value);
    EXPECT_FALSE(animator.isAnimating(&value));
}

TEST(Animator, CancelAllAnimations) {
    Animator animator;
    float a = 0, b = 0;
    animator.animate(a, 100, 1.0f, Ease::linear);
    animator.animate(b, 200, 2.0f, Ease::linear);
    EXPECT_EQ(animator.animationCount(), 2);

    animator.cancelAll();
    EXPECT_EQ(animator.animationCount(), 0);
}

TEST(Animator, OnCompleteCallback) {
    Animator animator;
    float value = 0;
    bool completed = false;
    animator.animate(value, 1, 0.5f, Ease::linear, [&]() { completed = true; });

    animator.update(0.5f);
    EXPECT_TRUE(completed);
    EXPECT_FLOAT_EQ(value, 1.0f);
}

TEST(Animator, NewAnimationReplacesOld) {
    Animator animator;
    float value = 0;
    animator.animate(value, 100, 1.0f, Ease::linear);
    animator.animate(value, 50, 0.5f, Ease::linear);  // Replaces
    EXPECT_EQ(animator.animationCount(), 1);

    animator.update(0.5f);
    EXPECT_NEAR(value, 50, 0.1f);
}

TEST(Animator, EaseInOut) {
    Animator animator;
    float value = 0;
    animator.animate(value, 100, 1.0f, Ease::easeInOut);

    animator.update(0.5f);
    // easeInOut at t=0.5 → 0.5 (inflection point)
    EXPECT_NEAR(value, 50, 1.0f);

    animator.update(0.5f);
    EXPECT_NEAR(value, 100, 0.1f);
}

// Easing function basic checks
TEST(Easing, LinearEndpoints) {
    EXPECT_FLOAT_EQ(Ease::linear(0), 0);
    EXPECT_FLOAT_EQ(Ease::linear(1), 1);
}

TEST(Easing, EaseInEndpoints) {
    EXPECT_FLOAT_EQ(Ease::easeIn(0), 0);
    EXPECT_FLOAT_EQ(Ease::easeIn(1), 1);
}

TEST(Easing, EaseOutEndpoints) {
    EXPECT_FLOAT_EQ(Ease::easeOut(0), 0);
    EXPECT_FLOAT_EQ(Ease::easeOut(1), 1);
}

TEST(Easing, EaseInOutEndpoints) {
    EXPECT_FLOAT_EQ(Ease::easeInOut(0), 0);
    EXPECT_FLOAT_EQ(Ease::easeInOut(1), 1);
}

TEST(Easing, EaseOutBounceEndpoints) {
    EXPECT_NEAR(Ease::easeOutBounce(0), 0, 0.001f);
    EXPECT_NEAR(Ease::easeOutBounce(1), 1, 0.001f);
}
