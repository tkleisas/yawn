#include <gtest/gtest.h>
#include "audio/Transport.h"

using namespace yawn::audio;

TEST(TransportTest, DefaultState) {
    Transport t;
    EXPECT_FALSE(t.isPlaying());
    EXPECT_DOUBLE_EQ(t.bpm(), Transport::kDefaultBPM);
    EXPECT_EQ(t.positionInSamples(), 0);
    EXPECT_DOUBLE_EQ(t.sampleRate(), Transport::kDefaultSampleRate);
}

TEST(TransportTest, PlayStop) {
    Transport t;
    t.play();
    EXPECT_TRUE(t.isPlaying());
    t.stop();
    EXPECT_FALSE(t.isPlaying());
}

TEST(TransportTest, SetBPM) {
    Transport t;
    t.setBPM(140.0);
    EXPECT_DOUBLE_EQ(t.bpm(), 140.0);
    t.setBPM(80.0);
    EXPECT_DOUBLE_EQ(t.bpm(), 80.0);
}

TEST(TransportTest, SetSampleRate) {
    Transport t;
    t.setSampleRate(48000.0);
    EXPECT_DOUBLE_EQ(t.sampleRate(), 48000.0);
}

TEST(TransportTest, AdvanceWhilePlaying) {
    Transport t;
    t.play();
    t.advance(256);
    EXPECT_EQ(t.positionInSamples(), 256);
    t.advance(512);
    EXPECT_EQ(t.positionInSamples(), 768);
}

TEST(TransportTest, AdvanceWhileStoppedDoesNothing) {
    Transport t;
    t.advance(256);
    EXPECT_EQ(t.positionInSamples(), 0);
}

TEST(TransportTest, SetPosition) {
    Transport t;
    t.setPositionInSamples(44100);
    EXPECT_EQ(t.positionInSamples(), 44100);
}

TEST(TransportTest, PositionInBeats) {
    Transport t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    // At 120 BPM, 1 beat = 0.5 seconds = 22050 samples
    t.setPositionInSamples(22050);
    EXPECT_NEAR(t.positionInBeats(), 1.0, 0.001);
}

TEST(TransportTest, PositionInBeatsAtZero) {
    Transport t;
    EXPECT_DOUBLE_EQ(t.positionInBeats(), 0.0);
}

TEST(TransportTest, SamplesPerBeat) {
    Transport t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    // 44100 * 60 / 120 = 22050
    EXPECT_DOUBLE_EQ(t.samplesPerBeat(), 22050.0);
}

TEST(TransportTest, SamplesPerBar) {
    Transport t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    // 22050 * 4 = 88200
    EXPECT_DOUBLE_EQ(t.samplesPerBar(), 88200.0);
}

TEST(TransportTest, Reset) {
    Transport t;
    t.play();
    t.setBPM(180.0);
    t.setPositionInSamples(99999);
    t.reset();

    EXPECT_FALSE(t.isPlaying());
    EXPECT_EQ(t.positionInSamples(), 0);
    EXPECT_DOUBLE_EQ(t.bpm(), Transport::kDefaultBPM);
}

TEST(TransportTest, SamplesPerBeatWithZeroBPM) {
    Transport t;
    t.setBPM(0.0);
    EXPECT_DOUBLE_EQ(t.samplesPerBeat(), 0.0);
}

TEST(TransportTest, PositionInBeatsWithZeroSampleRate) {
    Transport t;
    t.setSampleRate(0.0);
    t.setPositionInSamples(1000);
    EXPECT_DOUBLE_EQ(t.positionInBeats(), 0.0);
}
