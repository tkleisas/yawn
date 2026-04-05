#include <gtest/gtest.h>
#include "audio/FollowAction.h"
#include "app/Project.h"

using namespace yawn;

// ---------------------------------------------------------------------------
// FollowAction struct basics
// ---------------------------------------------------------------------------

TEST(FollowAction, DefaultValues) {
    FollowAction fa;
    EXPECT_FALSE(fa.enabled);
    EXPECT_EQ(fa.barCount, 1);
    EXPECT_EQ(fa.actionA, FollowActionType::Next);
    EXPECT_EQ(fa.actionB, FollowActionType::None);
    EXPECT_EQ(fa.chanceA, 100);
    EXPECT_EQ(fa.chanceB(), 0);
}

TEST(FollowAction, ChanceBComplement) {
    FollowAction fa;
    fa.chanceA = 70;
    EXPECT_EQ(fa.chanceB(), 30);
    fa.chanceA = 0;
    EXPECT_EQ(fa.chanceB(), 100);
    fa.chanceA = 100;
    EXPECT_EQ(fa.chanceB(), 0);
}

TEST(FollowAction, AllActionTypes) {
    // Verify all enum values are distinct and castable
    EXPECT_EQ(static_cast<int>(FollowActionType::None), 0);
    EXPECT_EQ(static_cast<int>(FollowActionType::Stop), 1);
    EXPECT_EQ(static_cast<int>(FollowActionType::PlayAgain), 2);
    EXPECT_EQ(static_cast<int>(FollowActionType::Next), 3);
    EXPECT_EQ(static_cast<int>(FollowActionType::Previous), 4);
    EXPECT_EQ(static_cast<int>(FollowActionType::First), 5);
    EXPECT_EQ(static_cast<int>(FollowActionType::Last), 6);
    EXPECT_EQ(static_cast<int>(FollowActionType::Random), 7);
    EXPECT_EQ(static_cast<int>(FollowActionType::Any), 8);
}

// ---------------------------------------------------------------------------
// ClipSlot follow action integration
// ---------------------------------------------------------------------------

TEST(FollowAction, ClipSlotDefaultFollowAction) {
    ClipSlot slot;
    EXPECT_FALSE(slot.followAction.enabled);
    EXPECT_EQ(slot.followAction.barCount, 1);
}

TEST(FollowAction, ClipSlotFollowActionPersistence) {
    ClipSlot slot;
    slot.followAction.enabled = true;
    slot.followAction.barCount = 4;
    slot.followAction.actionA = FollowActionType::Random;
    slot.followAction.actionB = FollowActionType::Stop;
    slot.followAction.chanceA = 80;

    EXPECT_TRUE(slot.followAction.enabled);
    EXPECT_EQ(slot.followAction.barCount, 4);
    EXPECT_EQ(slot.followAction.actionA, FollowActionType::Random);
    EXPECT_EQ(slot.followAction.actionB, FollowActionType::Stop);
    EXPECT_EQ(slot.followAction.chanceA, 80);
    EXPECT_EQ(slot.followAction.chanceB(), 20);
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include "audio/AudioEngine.h"
#include "util/ProjectSerializer.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

static fs::path followActionTestDir(const std::string& suffix) {
    return fs::temp_directory_path() / ("yawn_test_followaction_" + suffix + ".yawn");
}

TEST(FollowAction, SerializationRoundTrip) {
    auto dir = followActionTestDir("roundtrip");
    if (fs::exists(dir)) fs::remove_all(dir);

    // Setup project with follow action on a clip slot
    Project project;
    project.init(1, 2);

    auto clip = std::make_unique<audio::Clip>();
    clip->name = "TestClip";
    clip->buffer = std::make_shared<audio::AudioBuffer>(2, 44100);
    clip->looping = true;
    project.setClip(0, 0, std::move(clip));

    auto* slot = project.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    slot->followAction.enabled = true;
    slot->followAction.barCount = 8;
    slot->followAction.actionA = FollowActionType::Next;
    slot->followAction.actionB = FollowActionType::Random;
    slot->followAction.chanceA = 75;

    // Save
    audio::AudioEngine engine;
    EXPECT_TRUE(ProjectSerializer::saveToFolder(dir, project, engine));

    // Verify JSON contains follow action
    std::ifstream in(dir / "project.json");
    ASSERT_TRUE(in.is_open());
    json root = json::parse(in);
    in.close();

    ASSERT_TRUE(root.contains("clips"));
    ASSERT_TRUE(root["clips"].contains("0:0"));
    auto& clipJ = root["clips"]["0:0"];
    ASSERT_TRUE(clipJ.contains("followAction"));
    auto& fa = clipJ["followAction"];
    EXPECT_EQ(fa["enabled"], true);
    EXPECT_EQ(fa["barCount"], 8);
    EXPECT_EQ(fa["actionA"], static_cast<int>(FollowActionType::Next));
    EXPECT_EQ(fa["actionB"], static_cast<int>(FollowActionType::Random));
    EXPECT_EQ(fa["chanceA"], 75);

    // Load into fresh project
    Project project2;
    audio::AudioEngine engine2;
    EXPECT_TRUE(ProjectSerializer::loadFromFolder(dir, project2, engine2));

    auto* loadedSlot = project2.getSlot(0, 0);
    ASSERT_NE(loadedSlot, nullptr);
    EXPECT_TRUE(loadedSlot->followAction.enabled);
    EXPECT_EQ(loadedSlot->followAction.barCount, 8);
    EXPECT_EQ(loadedSlot->followAction.actionA, FollowActionType::Next);
    EXPECT_EQ(loadedSlot->followAction.actionB, FollowActionType::Random);
    EXPECT_EQ(loadedSlot->followAction.chanceA, 75);

    // Cleanup
    if (fs::exists(dir)) fs::remove_all(dir);
}

TEST(FollowAction, DisabledNotSerialized) {
    auto dir = followActionTestDir("disabled");
    if (fs::exists(dir)) fs::remove_all(dir);

    Project project;
    project.init(1, 1);

    auto clip = std::make_unique<audio::Clip>();
    clip->name = "NoFollow";
    clip->buffer = std::make_shared<audio::AudioBuffer>(2, 44100);
    project.setClip(0, 0, std::move(clip));

    // followAction is disabled by default — should not be serialized
    audio::AudioEngine engine;
    EXPECT_TRUE(ProjectSerializer::saveToFolder(dir, project, engine));

    std::ifstream in(dir / "project.json");
    json root = json::parse(in);
    in.close();

    auto& clipJ = root["clips"]["0:0"];
    EXPECT_FALSE(clipJ.contains("followAction"));

    if (fs::exists(dir)) fs::remove_all(dir);
}
