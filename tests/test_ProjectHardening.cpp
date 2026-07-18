#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

#include "app/Project.h"
#include "audio/AudioEngine.h"
#include "util/ProjectSerializer.h"
#include "util/FileIO.h"
#include "instruments/Multisampler.h"

using namespace yawn;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Hardening tests: a corrupt / hostile project.json must never crash
// or terminate the app, and file-supplied counts/indices must never
// reach engine arrays or audio-thread buffer reads unchecked.

namespace {

class ProjectHardeningTest : public ::testing::Test {
protected:
    fs::path dir;
    void SetUp() override {
        dir = fs::temp_directory_path() /
              ("yawn_hardening_" +
               std::string(::testing::UnitTest::GetInstance()
                               ->current_test_info()->name()) + ".yawn");
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override { fs::remove_all(dir); }

    void writeProjectJson(const std::string& body) {
        std::ofstream out(dir / "project.json");
        out << body;
    }

    bool load(Project& p, audio::AudioEngine& e) {
        return ProjectSerializer::loadFromFolder(dir, p, e,
                                                 nullptr, nullptr);
    }
};

} // namespace

TEST_F(ProjectHardeningTest, TruncatedJsonReturnsFalse) {
    writeProjectJson(R"({"formatVersion": 1, "tracks": [{"name": "A)");
    Project p;
    audio::AudioEngine e;
    EXPECT_FALSE(load(p, e));
}

TEST_F(ProjectHardeningTest, TypeConfusedTrackSkippedOthersLoad) {
    writeProjectJson(R"({
        "formatVersion": 1,
        "tracks": [
            {"name": "Bad", "volume": "loud"},
            {"name": "Good", "volume": 0.5}
        ],
        "scenes": [{"name": "1"}]
    })");
    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    // Track 0 threw on the type-confused volume and the rest of the
    // track was skipped — fields read before the throw survive,
    // everything after (volume, chains, instrument) stays default.
    EXPECT_EQ(p.track(0).name, "Bad");
    EXPECT_FLOAT_EQ(p.track(0).volume, 1.0f);
    // Track 1 loaded fine.
    EXPECT_EQ(p.track(1).name, "Good");
    EXPECT_FLOAT_EQ(p.track(1).volume, 0.5f);
}

TEST_F(ProjectHardeningTest, NonNumericMacroKeysSkipped) {
    writeProjectJson(R"({
        "formatVersion": 1,
        "tracks": [{"name": "A", "macros": {"values": {"x": 0.5, "2": 0.9}}}],
        "scenes": [{"name": "1"}]
    })");
    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    EXPECT_FLOAT_EQ(p.track(0).macros.values[2], 0.9f);
}

TEST_F(ProjectHardeningTest, MalformedClipKeySkipped) {
    writeProjectJson(R"({
        "formatVersion": 1,
        "tracks": [{"name": "A"}],
        "scenes": [{"name": "1"}],
        "clips": {
            "x:0": {"type": "audio"},
            "0:y": {"type": "audio"},
            "0:0": {"type": "audio", "name": "NoSample"}
        }
    })");
    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    auto* slot = p.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(slot->audioClip, nullptr);
    EXPECT_EQ(slot->audioClip->name, "NoSample");
}

TEST_F(ProjectHardeningTest, HugeTrackCountClamped) {
    json root;
    root["formatVersion"] = 1;
    root["tracks"] = json::array();
    for (int i = 0; i < 200; ++i)
        root["tracks"].push_back({{"name", "T" + std::to_string(i)}});
    root["scenes"] = json::array({{"name", "1"}});
    writeProjectJson(root.dump());

    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    EXPECT_EQ(p.numTracks(), kMaxTracks);
    // The engine's fixed arrays were never indexed past their bounds
    // (this is the OOB write the clamp prevents).
}

TEST_F(ProjectHardeningTest, NonArrayTracksFallsBackToDefaults) {
    writeProjectJson(R"({
        "formatVersion": 1,
        "tracks": 42,
        "scenes": "many"
    })");
    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    EXPECT_EQ(p.numTracks(), kDefaultNumTracks);
    EXPECT_EQ(p.numScenes(), kDefaultNumScenes);
}

TEST_F(ProjectHardeningTest, ClipLoopPointsClampedToBuffer) {
    // Write a real sample the clip deserializer can load.
    fs::create_directories(dir / "samples");
    std::vector<float> data(1000, 0.25f);
    ASSERT_TRUE(FileIO::saveAudioFile((dir / "samples" / "c.wav").string(),
                                      data.data(), 1000, 1, 48000));

    json root;
    root["formatVersion"] = 1;
    root["tracks"] = json::array({{"name", "A"}});
    root["scenes"] = json::array({{"name", "1"}});
    root["clips"] = {
        {"0:0", {{"type", "audio"}, {"sampleFile", "samples/c.wav"},
                 {"loopStart", 1000000000}, {"loopEnd", 2000000000},
                 {"looping", true}}}
    };
    writeProjectJson(root.dump());

    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    auto* clip = p.getClip(0, 0);
    ASSERT_NE(clip, nullptr);
    EXPECT_LE(clip->loopStart, clip->buffer->numFrames() - 1);
    EXPECT_GE(clip->loopStart, 0);
    EXPECT_LE(clip->loopEnd, clip->buffer->numFrames());
    EXPECT_GT(clip->loopEnd, clip->loopStart);
}

TEST_F(ProjectHardeningTest, MultisamplerAddZoneClampsLoop) {
    instruments::Multisampler ms;
    instruments::Multisampler::Zone z;
    z.sampleData.assign(500, 0.1f);
    z.sampleFrames = 500;
    z.loop = true;
    z.loopStart = 490;
    z.loopEnd = 999999;   // hostile
    const int idx = ms.addZone(z);
    ASSERT_GE(idx, 0);
    EXPECT_EQ(ms.zone(idx)->loopEnd, 500);
    EXPECT_EQ(ms.zone(idx)->loopStart, 490);
}

TEST_F(ProjectHardeningTest, SaveLeavesNoTmpFileAndRoundTrips) {
    Project p;
    p.init(2, 2);
    p.track(0).name = "Keep";
    audio::AudioEngine e;
    ASSERT_TRUE(ProjectSerializer::saveToFolder(dir, p, e));

    EXPECT_TRUE(fs::exists(dir / "project.json"));
    EXPECT_FALSE(fs::exists(dir / "project.json.tmp"))
        << "temp file must be renamed away after a successful save";

    Project p2;
    audio::AudioEngine e2;
    ASSERT_TRUE(load(p2, e2));
    EXPECT_EQ(p2.track(0).name, "Keep");
}

TEST_F(ProjectHardeningTest, TypeConfusedClipEntrySkipped) {
    writeProjectJson(R"({
        "formatVersion": 1,
        "tracks": [{"name": "A"}],
        "scenes": [{"name": "1"}, {"name": "2"}],
        "clips": {
            "0:0": {"type": "audio", "gain": "lots"},
            "0:1": {"type": "audio", "name": "Fine"}
        }
    })");
    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    // The type-confused gain entry was skipped; the clean one loaded.
    auto* clip = p.getClip(0, 1);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->name, "Fine");
}

TEST_F(ProjectHardeningTest, NonObjectClipsSectionIgnored) {
    writeProjectJson(R"({
        "formatVersion": 1,
        "tracks": [{"name": "A"}],
        "scenes": [{"name": "1"}],
        "clips": [1, 2, 3]
    })");
    Project p;
    audio::AudioEngine e;
    ASSERT_TRUE(load(p, e));
    EXPECT_EQ(p.track(0).name, "A");
}
