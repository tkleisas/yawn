#include <gtest/gtest.h>
#include "audio/AudioBuffer.h"

using namespace yawn::audio;

TEST(AudioBufferTest, DefaultConstructor) {
    AudioBuffer buf;
    EXPECT_EQ(buf.numChannels(), 0);
    EXPECT_EQ(buf.numFrames(), 0);
    EXPECT_EQ(buf.totalSamples(), 0u);
    EXPECT_TRUE(buf.isEmpty());
}

TEST(AudioBufferTest, ParameterizedConstructor) {
    AudioBuffer buf(2, 1024);
    EXPECT_EQ(buf.numChannels(), 2);
    EXPECT_EQ(buf.numFrames(), 1024);
    EXPECT_EQ(buf.totalSamples(), 2048u);
    EXPECT_FALSE(buf.isEmpty());
}

TEST(AudioBufferTest, InitializedToZero) {
    AudioBuffer buf(2, 512);
    for (int ch = 0; ch < 2; ++ch) {
        for (int f = 0; f < 512; ++f) {
            EXPECT_FLOAT_EQ(buf.sample(ch, f), 0.0f);
        }
    }
}

TEST(AudioBufferTest, SampleReadWrite) {
    AudioBuffer buf(2, 100);
    buf.sample(0, 50) = 0.5f;
    buf.sample(1, 99) = -0.75f;

    EXPECT_FLOAT_EQ(buf.sample(0, 50), 0.5f);
    EXPECT_FLOAT_EQ(buf.sample(1, 99), -0.75f);
    EXPECT_FLOAT_EQ(buf.sample(0, 0), 0.0f); // untouched
}

TEST(AudioBufferTest, ChannelDataPointer) {
    AudioBuffer buf(2, 100);
    buf.sample(0, 0) = 1.0f;
    buf.sample(1, 0) = 2.0f;

    const float* ch0 = buf.channelData(0);
    const float* ch1 = buf.channelData(1);
    EXPECT_FLOAT_EQ(ch0[0], 1.0f);
    EXPECT_FLOAT_EQ(ch1[0], 2.0f);

    // Non-interleaved: channel 1 data starts after all channel 0 frames
    EXPECT_EQ(ch1 - ch0, 100);
}

TEST(AudioBufferTest, Resize) {
    AudioBuffer buf;
    buf.resize(1, 256);
    EXPECT_EQ(buf.numChannels(), 1);
    EXPECT_EQ(buf.numFrames(), 256);
    EXPECT_FALSE(buf.isEmpty());

    buf.resize(4, 512);
    EXPECT_EQ(buf.numChannels(), 4);
    EXPECT_EQ(buf.numFrames(), 512);
    EXPECT_EQ(buf.totalSamples(), 2048u);
}

TEST(AudioBufferTest, Clear) {
    AudioBuffer buf(1, 10);
    for (int i = 0; i < 10; ++i) {
        buf.sample(0, i) = static_cast<float>(i);
    }
    buf.clear();
    for (int i = 0; i < 10; ++i) {
        EXPECT_FLOAT_EQ(buf.sample(0, i), 0.0f);
    }
}

TEST(AudioBufferTest, RawDataPointer) {
    AudioBuffer buf(2, 4);
    float* raw = buf.data();
    ASSERT_NE(raw, nullptr);

    // Write via raw pointer (non-interleaved: ch0 frames first, then ch1)
    raw[0] = 1.0f;  // ch0, frame0
    raw[4] = 2.0f;  // ch1, frame0

    EXPECT_FLOAT_EQ(buf.sample(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(buf.sample(1, 0), 2.0f);
}

TEST(AudioBufferTest, ConstAccess) {
    AudioBuffer buf(1, 5);
    buf.sample(0, 3) = 42.0f;

    const AudioBuffer& cref = buf;
    EXPECT_FLOAT_EQ(cref.sample(0, 3), 42.0f);
    EXPECT_NE(cref.channelData(0), nullptr);
    EXPECT_NE(cref.data(), nullptr);
}
