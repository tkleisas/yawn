#include <gtest/gtest.h>
#include "ui/Theme.h"

using namespace yawn::ui;

// Helper to restore Theme globals after each test
class ThemeScaleTest : public ::testing::Test {
protected:
    void TearDown() override {
        Theme::scaleFactor = 1.0f;
        Theme::userScaleOverride = 0.0f;
    }
};

TEST_F(ThemeScaleTest, ScaleFactorDefault) {
    EXPECT_FLOAT_EQ(Theme::scaleFactor, 1.0f);
}

TEST_F(ThemeScaleTest, EffectiveScaleAutoDetect) {
    Theme::userScaleOverride = 0.0f;
    Theme::scaleFactor = 2.0f;
    EXPECT_FLOAT_EQ(Theme::effectiveScale(), 2.0f);
}

TEST_F(ThemeScaleTest, EffectiveScaleUserOverride) {
    Theme::userScaleOverride = 1.5f;
    Theme::scaleFactor = 2.0f;
    EXPECT_FLOAT_EQ(Theme::effectiveScale(), 1.5f);
}

TEST_F(ThemeScaleTest, ScaledHelper) {
    Theme::scaleFactor = 2.0f;
    Theme::userScaleOverride = 0.0f;
    EXPECT_FLOAT_EQ(Theme::scaled(100.0f), 200.0f);
}

TEST_F(ThemeScaleTest, ScaledUsesEffectiveScale) {
    Theme::scaleFactor = 2.0f;
    Theme::userScaleOverride = 1.5f;
    EXPECT_FLOAT_EQ(Theme::scaled(100.0f), 150.0f);
}

TEST_F(ThemeScaleTest, ScaledAtUnity) {
    EXPECT_FLOAT_EQ(Theme::scaled(42.0f), 42.0f);
}
