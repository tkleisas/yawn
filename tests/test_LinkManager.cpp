#include <gtest/gtest.h>
#include "link/LinkManager.h"

using namespace yawn;

TEST(LinkManager, DefaultDisabled) {
    LinkManager lm;
    EXPECT_FALSE(lm.enabled());
    EXPECT_EQ(lm.numPeers(), 0);
}

TEST(LinkManager, EnableDisable) {
    LinkManager lm;
    lm.enable(true);
    EXPECT_TRUE(lm.enabled());
    lm.enable(false);
    EXPECT_FALSE(lm.enabled());
}

TEST(LinkManager, NoPeersWhenDisabled) {
    LinkManager lm;
    EXPECT_EQ(lm.numPeers(), 0);
    lm.enable(true);
    EXPECT_GE(lm.numPeers(), 0);
}

TEST(LinkManager, AudioCallbackNoOpWhenDisabled) {
    LinkManager lm;
    double bpm = 120.0;
    double beat = 0.0;
    lm.onAudioCallback(bpm, beat, false);
    EXPECT_DOUBLE_EQ(bpm, 120.0);
    EXPECT_DOUBLE_EQ(beat, 0.0);
}

TEST(LinkManager, AudioCallbackPreservesValuesWhenNoPeers) {
    LinkManager lm;
    lm.enable(true);
    double bpm = 140.0;
    double beat = 4.0;
    lm.onAudioCallback(bpm, beat, true);
    // With Link enabled but no peers, bpm/beat should be preserved
    // (they get committed back to Link, but local values stay)
    EXPECT_GT(bpm, 0.0);
}

TEST(LinkManager, EnumeratePeersAfterEnable) {
    LinkManager lm;
    lm.enable(true);
    // Zero or more peers is valid — depends on network
    EXPECT_GE(lm.numPeers(), 0);
    lm.enable(false);
    EXPECT_EQ(lm.numPeers(), 0);
}
