#include "audio/Metronome.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace yawn::audio;

static constexpr double kSR    = 44100.0;
static constexpr int    kBlock = 256;

TEST(Metronome, Init) {
    Metronome met;
    met.init(kSR, kBlock);
    EXPECT_FALSE(met.enabled());
    EXPECT_FLOAT_EQ(met.volume(), 0.7f);
    EXPECT_EQ(met.beatsPerBar(), 4);
    EXPECT_FALSE(met.accentClick().empty());
    EXPECT_FALSE(met.normalClick().empty());
}

TEST(Metronome, NoOutputWhenDisabled) {
    Metronome met;
    met.init(kSR, kBlock);

    std::vector<float> buf(kBlock * 2, 0.0f);
    met.process(buf.data(), kBlock, 2, 120.0, 0, true);

    float sum = 0.0f;
    for (float s : buf) sum += std::abs(s);
    EXPECT_EQ(sum, 0.0f);
}

TEST(Metronome, NoOutputWhenNotPlaying) {
    Metronome met;
    met.init(kSR, kBlock);
    met.setEnabled(true);

    std::vector<float> buf(kBlock * 2, 0.0f);
    met.process(buf.data(), kBlock, 2, 120.0, 0, false);

    float sum = 0.0f;
    for (float s : buf) sum += std::abs(s);
    EXPECT_EQ(sum, 0.0f);
}

TEST(Metronome, ClickAtBeatBoundary) {
    Metronome met;
    met.init(kSR, kBlock);
    met.setEnabled(true);
    met.setVolume(1.0f);

    int bufSize = 1024;
    std::vector<float> buf(bufSize * 2, 0.0f);
    met.process(buf.data(), bufSize, 2, 120.0, 0, true);

    float sum = 0.0f;
    for (float s : buf) sum += std::abs(s);
    EXPECT_GT(sum, 0.0f);
}

TEST(Metronome, AccentOnDownbeat) {
    Metronome met1, met2;
    met1.init(kSR, kBlock);
    met2.init(kSR, kBlock);
    met1.setEnabled(true);  met1.setVolume(1.0f);  met1.setBeatsPerBar(4);
    met2.setEnabled(true);  met2.setVolume(1.0f);  met2.setBeatsPerBar(4);

    double spb = kSR * 60.0 / 120.0; // 22050

    int bufSize = 2048;
    std::vector<float> buf1(bufSize * 2, 0.0f);
    met1.process(buf1.data(), bufSize, 2, 120.0, 0, true);
    float peak1 = *std::max_element(buf1.begin(), buf1.end());

    std::vector<float> buf2(bufSize * 2, 0.0f);
    int64_t beat1Sample = (int64_t)(spb + 0.5);
    met2.process(buf2.data(), bufSize, 2, 120.0, beat1Sample, true);
    float peak2 = *std::max_element(buf2.begin(), buf2.end());

    EXPECT_GT(peak1, peak2);
}

TEST(Metronome, VolumeScaling) {
    Metronome met1, met2;
    met1.init(kSR, kBlock);
    met2.init(kSR, kBlock);
    met1.setEnabled(true);  met1.setVolume(1.0f);
    met2.setEnabled(true);  met2.setVolume(0.5f);

    int bufSize = 1024;
    std::vector<float> buf1(bufSize * 2, 0.0f);
    std::vector<float> buf2(bufSize * 2, 0.0f);
    met1.process(buf1.data(), bufSize, 2, 120.0, 0, true);
    met2.process(buf2.data(), bufSize, 2, 120.0, 0, true);

    float peak1 = *std::max_element(buf1.begin(), buf1.end());
    float peak2 = *std::max_element(buf2.begin(), buf2.end());
    EXPECT_NEAR(peak2 / peak1, 0.5f, 0.01f);
}

TEST(Metronome, ClickAtCorrectFrameOffset) {
    Metronome met;
    met.init(kSR, kBlock);
    met.setEnabled(true);
    met.setVolume(1.0f);

    // At 120 BPM, beat at sample 22050. Start at 22000 → click at frame 50
    int64_t startSample = 22000;
    int bufSize = 1024;
    std::vector<float> buf(bufSize * 2, 0.0f);
    met.process(buf.data(), bufSize, 2, 120.0, startSample, true);

    float silentSum = 0.0f;
    for (int i = 0; i < 50 * 2; ++i) silentSum += std::abs(buf[i]);
    EXPECT_EQ(silentSum, 0.0f);

    float clickSum = 0.0f;
    for (int i = 50 * 2; i < 100 * 2; ++i) clickSum += std::abs(buf[i]);
    EXPECT_GT(clickSum, 0.0f);
}

TEST(Metronome, ClickSpansBuffers) {
    Metronome met;
    met.init(kSR, kBlock);
    met.setEnabled(true);
    met.setVolume(1.0f);

    int bufSize = 64;
    std::vector<float> buf(bufSize * 2, 0.0f);
    met.process(buf.data(), bufSize, 2, 120.0, 0, true);

    float sum1 = 0.0f;
    for (float s : buf) sum1 += std::abs(s);
    EXPECT_GT(sum1, 0.0f);

    std::fill(buf.begin(), buf.end(), 0.0f);
    met.process(buf.data(), bufSize, 2, 120.0, 64, true);

    float sum2 = 0.0f;
    for (float s : buf) sum2 += std::abs(s);
    EXPECT_GT(sum2, 0.0f);
}

TEST(Metronome, BeatsPerBarSetting) {
    Metronome met;
    met.init(kSR, kBlock);
    met.setBeatsPerBar(3);
    EXPECT_EQ(met.beatsPerBar(), 3);
    met.setBeatsPerBar(7);
    EXPECT_EQ(met.beatsPerBar(), 7);
}
