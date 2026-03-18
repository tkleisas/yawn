#include <gtest/gtest.h>
#include "app/Project.h"

using namespace yawn;

TEST(ProjectTest, DefaultUninitialized) {
    Project p;
    EXPECT_EQ(p.numTracks(), 0);
    EXPECT_EQ(p.numScenes(), 0);
}

TEST(ProjectTest, InitDefault) {
    Project p;
    p.init();
    EXPECT_EQ(p.numTracks(), kDefaultNumTracks);
    EXPECT_EQ(p.numScenes(), kDefaultNumScenes);
}

TEST(ProjectTest, InitCustomSize) {
    Project p;
    p.init(4, 6);
    EXPECT_EQ(p.numTracks(), 4);
    EXPECT_EQ(p.numScenes(), 6);
}

TEST(ProjectTest, TrackNames) {
    Project p;
    p.init(3, 2);
    EXPECT_EQ(p.track(0).name, "Track 1");
    EXPECT_EQ(p.track(1).name, "Track 2");
    EXPECT_EQ(p.track(2).name, "Track 3");
}

TEST(ProjectTest, TrackColorIndex) {
    Project p;
    p.init(4, 1);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(p.track(i).colorIndex, i);
    }
}

TEST(ProjectTest, SceneNames) {
    Project p;
    p.init(1, 3);
    EXPECT_EQ(p.scene(0).name, "1");
    EXPECT_EQ(p.scene(1).name, "2");
    EXPECT_EQ(p.scene(2).name, "3");
}

TEST(ProjectTest, GetClipEmptySlot) {
    Project p;
    p.init(2, 2);
    EXPECT_EQ(p.getClip(0, 0), nullptr);
    EXPECT_EQ(p.getClip(1, 1), nullptr);
}

TEST(ProjectTest, GetClipOutOfBounds) {
    Project p;
    p.init(2, 2);
    EXPECT_EQ(p.getClip(-1, 0), nullptr);
    EXPECT_EQ(p.getClip(0, -1), nullptr);
    EXPECT_EQ(p.getClip(5, 0), nullptr);
    EXPECT_EQ(p.getClip(0, 5), nullptr);
}

TEST(ProjectTest, SetAndGetClip) {
    Project p;
    p.init(2, 2);

    auto clip = std::make_unique<audio::Clip>();
    clip->name = "test_clip";
    clip->gain = 0.5f;

    auto* ptr = p.setClip(0, 1, std::move(clip));
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->name, "test_clip");
    EXPECT_FLOAT_EQ(ptr->gain, 0.5f);

    // Get should return the same pointer
    EXPECT_EQ(p.getClip(0, 1), ptr);
}

TEST(ProjectTest, SetClipOutOfBounds) {
    Project p;
    p.init(2, 2);
    auto clip = std::make_unique<audio::Clip>();
    EXPECT_EQ(p.setClip(-1, 0, std::move(clip)), nullptr);
}

TEST(ProjectTest, ReplaceClip) {
    Project p;
    p.init(1, 1);

    auto clip1 = std::make_unique<audio::Clip>();
    clip1->name = "first";
    p.setClip(0, 0, std::move(clip1));

    auto clip2 = std::make_unique<audio::Clip>();
    clip2->name = "second";
    auto* ptr = p.setClip(0, 0, std::move(clip2));

    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->name, "second");
    EXPECT_EQ(p.getClip(0, 0)->name, "second");
}

TEST(ProjectTest, AddTrack) {
    Project p;
    p.init(2, 3);
    p.addTrack();
    EXPECT_EQ(p.numTracks(), 3);
    EXPECT_EQ(p.track(2).name, "Track 3");
    // New track should have empty clip slots
    for (int s = 0; s < 3; ++s) {
        EXPECT_EQ(p.getClip(2, s), nullptr);
    }
}

TEST(ProjectTest, AddScene) {
    Project p;
    p.init(2, 2);
    p.addScene();
    EXPECT_EQ(p.numScenes(), 3);
    EXPECT_EQ(p.scene(2).name, "3");
    // Existing tracks should have empty slots for new scene
    for (int t = 0; t < 2; ++t) {
        EXPECT_EQ(p.getClip(t, 2), nullptr);
    }
}

TEST(ProjectTest, TrackDefaultValues) {
    Project p;
    p.init(1, 1);
    const auto& t = p.track(0);
    EXPECT_FLOAT_EQ(t.volume, 1.0f);
    EXPECT_FALSE(t.muted);
    EXPECT_FALSE(t.soloed);
}

TEST(ProjectTest, ConstAccess) {
    Project p;
    p.init(2, 2);
    const Project& cp = p;
    EXPECT_EQ(cp.numTracks(), 2);
    EXPECT_EQ(cp.getClip(0, 0), nullptr);
    EXPECT_EQ(cp.track(0).name, "Track 1");
    EXPECT_EQ(cp.scene(0).name, "1");
}
