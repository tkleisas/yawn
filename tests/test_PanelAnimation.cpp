#include <gtest/gtest.h>
#include "ui/panels/DetailPanelWidget.h"

using namespace yawn::ui::fw2;

// ─── DetailPanelWidget animation tests ──────────────────────────────────────

TEST(DetailPanelAnim, InitiallyCollapsed) {
    DetailPanelWidget panel;
    EXPECT_FALSE(panel.isOpen());
    EXPECT_FLOAT_EQ(panel.height(), DetailPanelWidget::kCollapsedHeight);
}

TEST(DetailPanelAnim, ToggleSetsTarget) {
    DetailPanelWidget panel;
    panel.toggle();
    EXPECT_TRUE(panel.isOpen());
    // Immediately after toggle, height should still be near collapsed
    // (animation hasn't run yet)
    EXPECT_LT(panel.height(), DetailPanelWidget::kDefaultPanelHeight);
}

TEST(DetailPanelAnim, AnimationConverges) {
    DetailPanelWidget panel;
    panel.setOpen(true);

    UIContext ctx{};  // null renderer/font — fine for measure-only
    Constraints c = Constraints::tight(800, 600);

    // Run enough frames for exponential approach to converge.
    // tick() advances the height animation; measure() reports the
    // current (animating) height.
    for (int i = 0; i < 60; ++i) {
        panel.tick();
        panel.measure(c, ctx);
    }

    EXPECT_FLOAT_EQ(panel.height(), DetailPanelWidget::kDefaultPanelHeight);
}

TEST(DetailPanelAnim, CloseAnimationConverges) {
    DetailPanelWidget panel;
    panel.setOpen(true);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // First converge to open
    for (int i = 0; i < 60; ++i) {
        panel.tick();
        panel.measure(c, ctx);
    }
    EXPECT_FLOAT_EQ(panel.height(), DetailPanelWidget::kDefaultPanelHeight);

    // Now close
    panel.setOpen(false);
    for (int i = 0; i < 60; ++i) {
        panel.tick();
        panel.measure(c, ctx);
    }

    EXPECT_FLOAT_EQ(panel.height(), DetailPanelWidget::kCollapsedHeight);
}

TEST(DetailPanelAnim, HeightChangesGradually) {
    DetailPanelWidget panel;
    panel.setOpen(true);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // After one frame, height should be between collapsed and open
    panel.tick();
    panel.measure(c, ctx);
    float h = panel.height();
    EXPECT_GT(h, DetailPanelWidget::kCollapsedHeight);
    EXPECT_LT(h, DetailPanelWidget::kDefaultPanelHeight);
}

// Note: PianoRollPanel uses the identical animation pattern but
// depends on OpenGL headers (Renderer.h, Font.h) unavailable in
// the test build, so its animation is verified visually only.
