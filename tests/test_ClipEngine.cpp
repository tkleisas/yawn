#include <gtest/gtest.h>
#include "audio/ClipEngine.h"
#include <cmath>
#include <vector>

using namespace yawn::audio;

class ClipEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_transport.setSampleRate(44100.0);
        m_transport.setBPM(120.0);
        m_engine.setTransport(&m_transport);
        m_engine.setSampleRate(44100.0);
    }

    // Create a simple clip with a constant value per channel
    std::unique_ptr<Clip> makeClip(int frames, float value = 1.0f) {
        auto clip = std::make_unique<Clip>();
        clip->buffer = std::make_shared<AudioBuffer>(2, frames);
        clip->looping = true;
        clip->gain = 1.0f;
        for (int ch = 0; ch < 2; ++ch) {
            for (int f = 0; f < frames; ++f) {
                clip->buffer->sample(ch, f) = value;
            }
        }
        return clip;
    }

    // Process and return output buffer
    std::vector<float> processFrames(int numFrames, int numChannels = 2) {
        std::vector<float> output(numFrames * numChannels, 0.0f);
        m_engine.process(output.data(), numFrames, numChannels);
        return output;
    }

    Transport m_transport;
    ClipEngine m_engine;
};

TEST_F(ClipEngineTest, InitialStateNoTracksPlaying) {
    for (int i = 0; i < kMaxTracks; ++i) {
        EXPECT_FALSE(m_engine.isTrackPlaying(i));
    }
}

TEST_F(ClipEngineTest, SilenceWhenNoClips) {
    auto output = processFrames(256);
    for (float sample : output) {
        EXPECT_FLOAT_EQ(sample, 0.0f);
    }
}

TEST_F(ClipEngineTest, ScheduleClipImmediateMode) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip = makeClip(44100);
    m_engine.scheduleClip(0, clip.get());

    EXPECT_TRUE(m_engine.isTrackPlaying(0));
}

TEST_F(ClipEngineTest, ScheduleStopImmediateMode) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip = makeClip(44100);
    m_engine.scheduleClip(0, clip.get());
    m_engine.scheduleStop(0);

    EXPECT_TRUE(m_engine.trackState(0).stopping);
}

TEST_F(ClipEngineTest, ProducesAudioWhenPlaying) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip = makeClip(44100, 0.5f);
    m_engine.scheduleClip(0, clip.get());

    auto output = processFrames(256);

    // After fade-in, should produce non-zero output
    bool hasNonZero = false;
    for (float s : output) {
        if (std::abs(s) > 0.0f) {
            hasNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(hasNonZero);
}

TEST_F(ClipEngineTest, FadeInFromZero) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip = makeClip(44100, 1.0f);
    m_engine.scheduleClip(0, clip.get());

    auto output = processFrames(1, 2);
    // First sample should be near-zero due to fade-in starting at 0
    EXPECT_NEAR(output[0], ClipPlayState::kFadeIncrement, 0.01f);
}

TEST_F(ClipEngineTest, MultipleTracks) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip1 = makeClip(44100, 0.3f);
    auto clip2 = makeClip(44100, 0.2f);

    m_engine.scheduleClip(0, clip1.get());
    m_engine.scheduleClip(1, clip2.get());

    EXPECT_TRUE(m_engine.isTrackPlaying(0));
    EXPECT_TRUE(m_engine.isTrackPlaying(1));
}

TEST_F(ClipEngineTest, InvalidTrackIndex) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip = makeClip(100);

    // Should not crash
    m_engine.scheduleClip(-1, clip.get());
    m_engine.scheduleClip(kMaxTracks, clip.get());
    m_engine.scheduleStop(-1);
    m_engine.scheduleStop(kMaxTracks);

    EXPECT_FALSE(m_engine.isTrackPlaying(-1));
    EXPECT_FALSE(m_engine.isTrackPlaying(kMaxTracks));
}

TEST_F(ClipEngineTest, QuantizeModeGetSet) {
    m_engine.setQuantizeMode(QuantizeMode::NextBeat);
    EXPECT_EQ(m_engine.quantizeMode(), QuantizeMode::NextBeat);

    m_engine.setQuantizeMode(QuantizeMode::NextBar);
    EXPECT_EQ(m_engine.quantizeMode(), QuantizeMode::NextBar);
}

TEST_F(ClipEngineTest, QuantizedLaunchPendsUntilBoundary) {
    m_engine.setQuantizeMode(QuantizeMode::NextBar);
    auto clip = makeClip(44100);

    m_transport.play();
    m_transport.setPositionInSamples(100); // mid-bar

    m_engine.scheduleClip(0, clip.get());
    // Should not be active yet (pending quantized launch)
    EXPECT_FALSE(m_engine.isTrackPlaying(0));
}

TEST_F(ClipEngineTest, NonLoopingClipStops) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip = makeClip(64, 1.0f); // very short
    clip->looping = false;

    m_engine.scheduleClip(0, clip.get());

    // Process enough frames to exhaust the clip
    auto output = processFrames(128);
    EXPECT_FALSE(m_engine.isTrackPlaying(0));
}

TEST_F(ClipEngineTest, LoopingClipWraps) {
    m_engine.setQuantizeMode(QuantizeMode::None);
    auto clip = makeClip(64, 1.0f);
    clip->looping = true;

    m_engine.scheduleClip(0, clip.get());

    // Process more frames than the clip length - should still be playing
    processFrames(256);
    EXPECT_TRUE(m_engine.isTrackPlaying(0));
}
