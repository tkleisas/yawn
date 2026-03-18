#include <gtest/gtest.h>
#include "audio/Clip.h"

using namespace yawn::audio;

TEST(ClipTest, DefaultValues) {
    Clip clip;
    EXPECT_TRUE(clip.name.empty());
    EXPECT_EQ(clip.buffer, nullptr);
    EXPECT_EQ(clip.loopStart, 0);
    EXPECT_EQ(clip.loopEnd, -1);
    EXPECT_TRUE(clip.looping);
    EXPECT_FLOAT_EQ(clip.gain, 1.0f);
}

TEST(ClipTest, EffectiveLoopEndNoBuffer) {
    Clip clip;
    EXPECT_EQ(clip.effectiveLoopEnd(), 0);
}

TEST(ClipTest, EffectiveLoopEndDefault) {
    Clip clip;
    clip.buffer = std::make_shared<AudioBuffer>(2, 44100);
    EXPECT_EQ(clip.effectiveLoopEnd(), 44100);
}

TEST(ClipTest, EffectiveLoopEndExplicit) {
    Clip clip;
    clip.buffer = std::make_shared<AudioBuffer>(2, 44100);
    clip.loopEnd = 22050;
    EXPECT_EQ(clip.effectiveLoopEnd(), 22050);
}

TEST(ClipTest, LengthInFrames) {
    Clip clip;
    clip.buffer = std::make_shared<AudioBuffer>(1, 10000);
    clip.loopStart = 1000;
    clip.loopEnd = 5000;
    EXPECT_EQ(clip.lengthInFrames(), 4000);
}

TEST(ClipTest, LengthInFramesFullBuffer) {
    Clip clip;
    clip.buffer = std::make_shared<AudioBuffer>(1, 8000);
    // loopEnd = -1 means use full buffer
    EXPECT_EQ(clip.lengthInFrames(), 8000);
}

// --- ClipPlayState tests ---

TEST(ClipPlayStateTest, DefaultValues) {
    ClipPlayState state;
    EXPECT_EQ(state.clip, nullptr);
    EXPECT_EQ(state.playPosition, 0);
    EXPECT_FALSE(state.active);
    EXPECT_FALSE(state.stopping);
    EXPECT_FLOAT_EQ(state.fadeGain, 1.0f);
}

TEST(ClipPlayStateTest, FadeIncrement) {
    // kFadeIncrement should be a small positive value
    EXPECT_GT(ClipPlayState::kFadeIncrement, 0.0f);
    EXPECT_LT(ClipPlayState::kFadeIncrement, 0.1f);
}
