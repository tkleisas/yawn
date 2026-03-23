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

TEST(ClipTest, CloneSharesBuffer) {
    Clip clip;
    clip.name = "TestClip";
    clip.buffer = std::make_shared<AudioBuffer>(2, 1000);
    clip.loopStart = 100;
    clip.loopEnd = 500;
    clip.looping = false;
    clip.gain = 0.5f;

    auto cloned = clip.clone();
    EXPECT_EQ(cloned->name, "TestClip");
    EXPECT_EQ(cloned->buffer, clip.buffer);  // shared_ptr, same buffer
    EXPECT_EQ(cloned->loopStart, 100);
    EXPECT_EQ(cloned->loopEnd, 500);
    EXPECT_FALSE(cloned->looping);
    EXPECT_FLOAT_EQ(cloned->gain, 0.5f);
}

TEST(ClipTest, CloneIndependent) {
    Clip clip;
    clip.name = "Original";
    clip.buffer = std::make_shared<AudioBuffer>(1, 100);
    auto cloned = clip.clone();

    // Modifying clone doesn't affect original
    cloned->name = "Modified";
    cloned->loopStart = 50;
    EXPECT_EQ(clip.name, "Original");
    EXPECT_EQ(clip.loopStart, 0);
}
