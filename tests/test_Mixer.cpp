#include <gtest/gtest.h>
#include "audio/Mixer.h"
#include <cmath>
#include <cstring>

using namespace yawn::audio;

class MixerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_mixer.reset();
    }

    // Create a constant-value stereo buffer for a track
    void fillTrackBuffer(float* buf, int numFrames, int numChannels, float valueL, float valueR) {
        for (int i = 0; i < numFrames; ++i) {
            buf[i * numChannels + 0] = valueL;
            if (numChannels > 1)
                buf[i * numChannels + 1] = valueR;
        }
    }

    Mixer m_mixer;
};

TEST_F(MixerTest, DefaultState) {
    EXPECT_FLOAT_EQ(m_mixer.master().volume, 1.0f);
    EXPECT_FLOAT_EQ(m_mixer.master().peakL, 0.0f);
    EXPECT_FLOAT_EQ(m_mixer.master().peakR, 0.0f);
    EXPECT_FALSE(m_mixer.anySoloed());

    const auto& ch = m_mixer.trackChannel(0);
    EXPECT_FLOAT_EQ(ch.volume, 1.0f);
    EXPECT_FLOAT_EQ(ch.pan, 0.0f);
    EXPECT_FALSE(ch.muted);
    EXPECT_FALSE(ch.soloed);
}

TEST_F(MixerTest, SetTrackVolume) {
    m_mixer.setTrackVolume(0, 0.5f);
    EXPECT_FLOAT_EQ(m_mixer.trackChannel(0).volume, 0.5f);
}

TEST_F(MixerTest, VolumeClamp) {
    m_mixer.setTrackVolume(0, -1.0f);
    EXPECT_FLOAT_EQ(m_mixer.trackChannel(0).volume, 0.0f);
    m_mixer.setTrackVolume(0, 5.0f);
    EXPECT_FLOAT_EQ(m_mixer.trackChannel(0).volume, 2.0f);
}

TEST_F(MixerTest, SetTrackPan) {
    m_mixer.setTrackPan(0, -0.5f);
    EXPECT_FLOAT_EQ(m_mixer.trackChannel(0).pan, -0.5f);
    m_mixer.setTrackPan(0, -2.0f);
    EXPECT_FLOAT_EQ(m_mixer.trackChannel(0).pan, -1.0f);
}

TEST_F(MixerTest, MuteTrack) {
    m_mixer.setTrackMute(0, true);
    EXPECT_TRUE(m_mixer.trackChannel(0).muted);
    m_mixer.setTrackMute(0, false);
    EXPECT_FALSE(m_mixer.trackChannel(0).muted);
}

TEST_F(MixerTest, SoloTrack) {
    EXPECT_FALSE(m_mixer.anySoloed());
    m_mixer.setTrackSolo(1, true);
    EXPECT_TRUE(m_mixer.anySoloed());
    EXPECT_TRUE(m_mixer.trackChannel(1).soloed);
    m_mixer.setTrackSolo(1, false);
    EXPECT_FALSE(m_mixer.anySoloed());
}

TEST_F(MixerTest, MasterVolume) {
    m_mixer.setMasterVolume(0.8f);
    EXPECT_FLOAT_EQ(m_mixer.master().volume, 0.8f);
}

TEST_F(MixerTest, SendControls) {
    m_mixer.setSendLevel(0, 0, 0.7f);
    m_mixer.setSendMode(0, 0, SendMode::PreFader);
    m_mixer.setSendEnabled(0, 0, true);

    const auto& send = m_mixer.trackChannel(0).sends[0];
    EXPECT_FLOAT_EQ(send.level, 0.7f);
    EXPECT_EQ(send.mode, SendMode::PreFader);
    EXPECT_TRUE(send.enabled);
}

TEST_F(MixerTest, ReturnBusControls) {
    m_mixer.setReturnVolume(0, 0.6f);
    m_mixer.setReturnPan(0, -0.3f);
    m_mixer.setReturnMute(0, true);

    const auto& rb = m_mixer.returnBus(0);
    EXPECT_FLOAT_EQ(rb.volume, 0.6f);
    EXPECT_FLOAT_EQ(rb.pan, -0.3f);
    EXPECT_TRUE(rb.muted);
}

TEST_F(MixerTest, OutOfRangeSafe) {
    // Should not crash
    m_mixer.setTrackVolume(-1, 0.5f);
    m_mixer.setTrackVolume(kMixerMaxTracks, 0.5f);
    m_mixer.setTrackPan(-1, 0.5f);
    m_mixer.setSendLevel(-1, 0, 0.5f);
    m_mixer.setSendLevel(0, -1, 0.5f);
    m_mixer.setSendLevel(0, kMaxReturnBuses, 0.5f);
    m_mixer.setReturnVolume(-1, 0.5f);
    m_mixer.setReturnVolume(kMaxReturnBuses, 0.5f);
}

TEST_F(MixerTest, ProcessSilence) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc] = {};
    float output[nf * nc] = {};
    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    m_mixer.process(ptrs, 1, output, nf, nc);

    for (int i = 0; i < nf * nc; ++i) {
        EXPECT_FLOAT_EQ(output[i], 0.0f);
    }
}

TEST_F(MixerTest, ProcessUnityGain) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 0.5f, 0.5f);

    float output[nf * nc] = {};
    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    // Volume=1, pan=center, master=1 → constant-power pan gives ~0.707 per side
    m_mixer.process(ptrs, 1, output, nf, nc);

    float expected = 0.5f * std::cos(0.25f * 3.14159265f); // ~0.354
    for (int i = 0; i < nf; ++i) {
        EXPECT_NEAR(output[i * nc + 0], expected, 0.01f);
        EXPECT_NEAR(output[i * nc + 1], expected, 0.01f);
    }
}

TEST_F(MixerTest, MutedTrackProducesSilence) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 1.0f, 1.0f);

    float output[nf * nc] = {};
    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    m_mixer.setTrackMute(0, true);
    m_mixer.process(ptrs, 1, output, nf, nc);

    for (int i = 0; i < nf * nc; ++i) {
        EXPECT_FLOAT_EQ(output[i], 0.0f);
    }
}

TEST_F(MixerTest, SoloMutesOtherTracks) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf0[nf * nc], trackBuf1[nf * nc];
    fillTrackBuffer(trackBuf0, nf, nc, 0.5f, 0.5f);
    fillTrackBuffer(trackBuf1, nf, nc, 0.3f, 0.3f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf0;
    ptrs[1] = trackBuf1;

    // Solo track 1 → track 0 should be silent
    m_mixer.setTrackSolo(1, true);

    float output[nf * nc] = {};
    m_mixer.process(ptrs, 2, output, nf, nc);

    // Output should only contain track 1's contribution
    float expectedFromTrack1 = 0.3f * std::cos(0.25f * 3.14159265f);
    EXPECT_NEAR(output[0], expectedFromTrack1, 0.01f);
}

TEST_F(MixerTest, PanLeftProducesMoreLeft) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 1.0f, 1.0f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    m_mixer.setTrackPan(0, -1.0f); // hard left
    float output[nf * nc] = {};
    m_mixer.process(ptrs, 1, output, nf, nc);

    EXPECT_GT(std::abs(output[0]), 0.5f);  // Left has signal
    EXPECT_NEAR(output[1], 0.0f, 0.01f);   // Right is near-silent
}

TEST_F(MixerTest, MasterVolumeScalesOutput) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 1.0f, 1.0f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    // Get baseline at master=1.0
    float output1[nf * nc] = {};
    m_mixer.process(ptrs, 1, output1, nf, nc);
    float baseline = output1[0];

    // Now with master=0.5
    m_mixer.reset();
    m_mixer.setMasterVolume(0.5f);
    float output2[nf * nc] = {};
    m_mixer.process(ptrs, 1, output2, nf, nc);

    EXPECT_NEAR(output2[0], baseline * 0.5f, 0.01f);
}

TEST_F(MixerTest, PeakMeteringUpdates) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 0.8f, 0.6f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    float output[nf * nc] = {};
    m_mixer.process(ptrs, 1, output, nf, nc);

    EXPECT_GT(m_mixer.trackChannel(0).peakL, 0.0f);
    EXPECT_GT(m_mixer.trackChannel(0).peakR, 0.0f);
    EXPECT_GT(m_mixer.master().peakL, 0.0f);
    EXPECT_GT(m_mixer.master().peakR, 0.0f);
}

TEST_F(MixerTest, PreFaderSendBypassesFader) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 1.0f, 1.0f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    // Set track volume to 0 (muted fader) but pre-fader send to return A
    m_mixer.setTrackVolume(0, 0.0f);
    m_mixer.setSendEnabled(0, 0, true);
    m_mixer.setSendLevel(0, 0, 0.5f);
    m_mixer.setSendMode(0, 0, SendMode::PreFader);
    m_mixer.setReturnVolume(0, 1.0f);

    float output[nf * nc] = {};
    m_mixer.process(ptrs, 1, output, nf, nc);

    // Track has zero volume, but return A should have received audio via pre-fader send
    EXPECT_GT(m_mixer.returnBus(0).peakL, 0.0f);
    // Output should have return bus contribution
    bool hasSignal = false;
    for (int i = 0; i < nf * nc; ++i) {
        if (std::abs(output[i]) > 0.01f) { hasSignal = true; break; }
    }
    EXPECT_TRUE(hasSignal);
}

TEST_F(MixerTest, PostFaderSendScalesWithFader) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 1.0f, 1.0f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    // Post-fader send with track volume = 0 → no send signal
    m_mixer.setTrackVolume(0, 0.0f);
    m_mixer.setSendEnabled(0, 0, true);
    m_mixer.setSendLevel(0, 0, 1.0f);
    m_mixer.setSendMode(0, 0, SendMode::PostFader);

    float output[nf * nc] = {};
    m_mixer.process(ptrs, 1, output, nf, nc);

    // Return bus should have received nothing (post-fader, and fader is 0)
    EXPECT_LT(m_mixer.returnBus(0).peakL, 0.01f);
}

TEST_F(MixerTest, MutedReturnProducesNoOutput) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 1.0f, 1.0f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    // Route all audio through send to return A, mute the track direct output
    m_mixer.setTrackVolume(0, 0.0f);
    m_mixer.setSendEnabled(0, 0, true);
    m_mixer.setSendLevel(0, 0, 1.0f);
    m_mixer.setSendMode(0, 0, SendMode::PreFader);
    m_mixer.setReturnMute(0, true);

    float output[nf * nc] = {};
    m_mixer.process(ptrs, 1, output, nf, nc);

    // Return is muted → output should be silent
    for (int i = 0; i < nf * nc; ++i) {
        EXPECT_NEAR(output[i], 0.0f, 0.001f);
    }
}

TEST_F(MixerTest, DecayPeaks) {
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    fillTrackBuffer(trackBuf, nf, nc, 1.0f, 1.0f);

    float* ptrs[kMixerMaxTracks] = {};
    ptrs[0] = trackBuf;

    float output[nf * nc] = {};
    m_mixer.process(ptrs, 1, output, nf, nc);

    float peakBefore = m_mixer.trackChannel(0).peakL;
    EXPECT_GT(peakBefore, 0.0f);

    // Decay several times
    for (int i = 0; i < 100; ++i) m_mixer.decayPeaks();

    EXPECT_LT(m_mixer.trackChannel(0).peakL, peakBefore * 0.01f);
}
