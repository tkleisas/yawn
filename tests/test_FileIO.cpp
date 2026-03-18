#include <gtest/gtest.h>
#include "util/FileIO.h"
#include <cmath>

using namespace yawn;

TEST(FileIOTest, LoadNonExistentFileReturnsNull) {
    auto buf = util::loadAudioFile("this_file_does_not_exist.wav");
    EXPECT_EQ(buf, nullptr);
}

TEST(FileIOTest, LoadEmptyPathReturnsNull) {
    auto buf = util::loadAudioFile("");
    EXPECT_EQ(buf, nullptr);
}

TEST(FileIOTest, ResampleIdenticalRate) {
    // Create a simple mono buffer with a known pattern
    audio::AudioBuffer src(1, 100);
    for (int i = 0; i < 100; ++i) {
        src.sample(0, i) = static_cast<float>(i) / 100.0f;
    }

    auto result = util::resampleBuffer(src, 44100.0, 44100.0);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->numFrames(), 100);
    EXPECT_EQ(result->numChannels(), 1);

    for (int i = 0; i < 100; ++i) {
        EXPECT_NEAR(result->sample(0, i), src.sample(0, i), 0.001f);
    }
}

TEST(FileIOTest, ResampleUpsample) {
    // Upsample 2x: 22050 -> 44100 should double the frame count
    audio::AudioBuffer src(1, 100);
    for (int i = 0; i < 100; ++i) {
        src.sample(0, i) = std::sin(2.0f * 3.14159f * i / 100.0f);
    }

    auto result = util::resampleBuffer(src, 22050.0, 44100.0);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->numFrames(), 200);
    EXPECT_EQ(result->numChannels(), 1);
}

TEST(FileIOTest, ResampleDownsample) {
    // Downsample 2x: 88200 -> 44100 should halve the frame count
    audio::AudioBuffer src(1, 200);
    for (int i = 0; i < 200; ++i) {
        src.sample(0, i) = static_cast<float>(i);
    }

    auto result = util::resampleBuffer(src, 88200.0, 44100.0);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->numFrames(), 100);
}

TEST(FileIOTest, ResamplePreservesChannels) {
    audio::AudioBuffer src(2, 100);
    for (int i = 0; i < 100; ++i) {
        src.sample(0, i) = 1.0f;
        src.sample(1, i) = -1.0f;
    }

    auto result = util::resampleBuffer(src, 22050.0, 44100.0);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->numChannels(), 2);

    // All channel 0 samples should be near 1.0, channel 1 near -1.0
    // (constant signal stays constant through linear interpolation)
    for (int i = 0; i < result->numFrames(); ++i) {
        EXPECT_NEAR(result->sample(0, i), 1.0f, 0.01f) << "ch0 at frame " << i;
        EXPECT_NEAR(result->sample(1, i), -1.0f, 0.01f) << "ch1 at frame " << i;
    }
}

TEST(FileIOTest, LoadAudioFileWithInfo) {
    // Test that the info struct is populated even on failure
    util::AudioFileInfo info;
    auto buf = util::loadAudioFile("nonexistent.wav", &info);
    EXPECT_EQ(buf, nullptr);
    // info should remain at defaults
    EXPECT_EQ(info.sampleRate, 0);
    EXPECT_EQ(info.channels, 0);
    EXPECT_EQ(info.frames, 0);
}
