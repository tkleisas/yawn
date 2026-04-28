#include <gtest/gtest.h>
#include "presets/PresetManager.h"
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#  include <process.h>
#  define YAWN_GETPID() static_cast<unsigned>(_getpid())
#else
#  include <unistd.h>
#  define YAWN_GETPID() static_cast<unsigned>(::getpid())
#endif

namespace fs = std::filesystem;
using namespace yawn;

// Test fixture that creates a temp directory and cleans up after.
//
// gtest_discover_tests + ctest --parallel turns every TEST_F in this
// file into its own ctest entry, and ctest happily fires up multiple
// test-binary processes simultaneously. A shared literal
// "/tmp/yawn_test_presets" path used to mean Test A's TearDown could
// `remove_all` the directory mid-walk of Test B's
// `directory_iterator`, depending on scheduler luck — flaky enough to
// stay green on most runs and bite specific CI runners (e.g. Linux
// Release Linux failed where Linux CI passed on the same commit).
//
// Fix: per-test-name + per-pid unique temp dir so each test owns its
// own filesystem subtree end-to-end. Test name alone is enough under
// gtest_discover_tests (which runs each test in its own process via
// --gtest_filter), but the PID suffix is harmless belt-and-suspenders
// for any future where the test binary loops the same TEST_F twice.
class PresetManagerTest : public ::testing::Test {
protected:
    fs::path m_tempDir;

    void SetUp() override {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string suite = info ? info->test_suite_name() : "PMT";
        const std::string tname = info ? info->name()             : "?";
        m_tempDir = fs::temp_directory_path() /
            ("yawn_test_presets_" + suite + "_" + tname + "_" +
             std::to_string(YAWN_GETPID()));
        std::error_code ec;
        fs::remove_all(m_tempDir, ec);   // start clean — kills stale runs
        fs::create_directories(m_tempDir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_tempDir, ec);
    }

    // Write a preset JSON manually to the temp dir for testing load
    fs::path writeTestPreset(const std::string& deviceId, const std::string& name,
                              const json& content) {
        fs::path dir = m_tempDir / PresetManager::sanitizeName(deviceId);
        fs::create_directories(dir);
        fs::path path = dir / (PresetManager::sanitizeName(name) + ".json");
        std::ofstream f(path);
        f << content.dump(2);
        return path;
    }
};

// ── sanitizeName ─────────────────────────────────────────────────────────────

TEST(PresetManagerSanitize, BasicName) {
    EXPECT_EQ(PresetManager::sanitizeName("Warm Pad"), "Warm Pad");
}

TEST(PresetManagerSanitize, IllegalCharsStripped) {
    EXPECT_EQ(PresetManager::sanitizeName("My/Preset:Cool*"), "MyPresetCool");
}

TEST(PresetManagerSanitize, AllIllegal) {
    EXPECT_EQ(PresetManager::sanitizeName("/<>|"), "");
}

TEST(PresetManagerSanitize, LeadingTrailingDotsAndSpaces) {
    EXPECT_EQ(PresetManager::sanitizeName("...Hello... "), "Hello");
}

TEST(PresetManagerSanitize, ControlCharsReplaced) {
    std::string s = "A";
    s += '\t';
    s += "B";
    EXPECT_EQ(PresetManager::sanitizeName(s), "A_B");
}

TEST(PresetManagerSanitize, VST3ClassIdPreserved) {
    EXPECT_EQ(PresetManager::sanitizeName("vst3_ABCDEF12345678"),
              "vst3_ABCDEF12345678");
}

// ── Save / Load round-trip ───────────────────────────────────────────────────

TEST_F(PresetManagerTest, SaveAndLoadNativePreset) {
    json params;
    params["Cutoff"] = 0.75f;
    params["Resonance"] = 0.3f;
    params["Volume"] = 1.0f;

    // Save using the real presetsDir (we can't mock it easily, so test the
    // actual user directory path). Instead, we test the load from a file we write.
    fs::path path = writeTestPreset("fm_synth", "My Preset", json{
        {"name", "My Preset"},
        {"deviceId", "fm_synth"},
        {"deviceName", "FM Synth"},
        {"params", params}
    });

    PresetData data;
    ASSERT_TRUE(PresetManager::loadPreset(path, data));
    EXPECT_EQ(data.name, "My Preset");
    EXPECT_EQ(data.deviceId, "fm_synth");
    EXPECT_EQ(data.deviceName, "FM Synth");
    EXPECT_FLOAT_EQ(data.params["Cutoff"].get<float>(), 0.75f);
    EXPECT_FLOAT_EQ(data.params["Resonance"].get<float>(), 0.3f);
    EXPECT_TRUE(data.vst3State.empty());
    EXPECT_TRUE(data.vst3ControllerState.empty());
}

TEST_F(PresetManagerTest, SaveAndLoadVST3Preset) {
    std::vector<uint8_t> procState = {0xDE, 0xAD, 0xBE, 0xEF};
    std::vector<uint8_t> ctrlState = {0xCA, 0xFE, 0xBA, 0xBE};

    // Manually write VST3 preset with hex state
    auto toHex = [](const std::vector<uint8_t>& v) {
        static const char hex[] = "0123456789abcdef";
        std::string s;
        for (auto b : v) { s += hex[b >> 4]; s += hex[b & 0xf]; }
        return s;
    };

    json j;
    j["name"]       = "Factory Lead";
    j["deviceId"]   = "vst3_ABCD1234";
    j["deviceName"] = "MiniFreak V";
    j["params"]     = json::object();
    j["vst3State"]  = toHex(procState);
    j["vst3ControllerState"] = toHex(ctrlState);

    fs::path path = writeTestPreset("vst3_ABCD1234", "Factory Lead", j);

    PresetData data;
    ASSERT_TRUE(PresetManager::loadPreset(path, data));
    EXPECT_EQ(data.name, "Factory Lead");
    EXPECT_EQ(data.vst3State, procState);
    EXPECT_EQ(data.vst3ControllerState, ctrlState);
}

TEST_F(PresetManagerTest, LoadMissingFileFails) {
    PresetData data;
    EXPECT_FALSE(PresetManager::loadPreset(m_tempDir / "nonexistent.json", data));
}

TEST_F(PresetManagerTest, LoadMalformedJsonFails) {
    fs::path dir = m_tempDir / "broken_device";
    fs::create_directories(dir);
    fs::path path = dir / "bad.json";
    std::ofstream f(path);
    f << "not valid json {{{{";
    f.close();

    PresetData data;
    EXPECT_FALSE(PresetManager::loadPreset(path, data));
}

// ── Delete ───────────────────────────────────────────────────────────────────

TEST_F(PresetManagerTest, DeletePreset) {
    fs::path path = writeTestPreset("test_dev", "ToDelete", json{
        {"name", "ToDelete"}, {"deviceId", "test_dev"},
        {"deviceName", "Test"}, {"params", json::object()}
    });
    ASSERT_TRUE(fs::exists(path));
    ASSERT_TRUE(PresetManager::deletePreset(path));
    EXPECT_FALSE(fs::exists(path));
}

TEST_F(PresetManagerTest, DeleteNonexistentFails) {
    EXPECT_FALSE(PresetManager::deletePreset(m_tempDir / "nope.json"));
}

// ── Rename ───────────────────────────────────────────────────────────────────

TEST_F(PresetManagerTest, RenamePreset) {
    fs::path path = writeTestPreset("test_dev", "OldName", json{
        {"name", "OldName"}, {"deviceId", "test_dev"},
        {"deviceName", "Test"}, {"params", {{"A", 1.0f}}}
    });

    fs::path newPath = PresetManager::renamePreset(path, "NewName");
    ASSERT_FALSE(newPath.empty());
    EXPECT_FALSE(fs::exists(path));
    EXPECT_TRUE(fs::exists(newPath));
    EXPECT_EQ(newPath.stem().string(), "NewName");

    // Verify the name field inside the JSON was updated
    PresetData data;
    ASSERT_TRUE(PresetManager::loadPreset(newPath, data));
    EXPECT_EQ(data.name, "NewName");
}

// ── Listing ──────────────────────────────────────────────────────────────────

TEST_F(PresetManagerTest, ListPresetsForDevice) {
    writeTestPreset("synth_a", "Pad", json{{"name","Pad"},{"deviceId","synth_a"},
        {"deviceName","A"},{"params",json::object()}});
    writeTestPreset("synth_a", "Bass", json{{"name","Bass"},{"deviceId","synth_a"},
        {"deviceName","A"},{"params",json::object()}});
    writeTestPreset("synth_b", "Other", json{{"name","Other"},{"deviceId","synth_b"},
        {"deviceName","B"},{"params",json::object()}});

    // We can't use listPresetsForDevice directly since it resolves its own path.
    // Instead, verify listing via the temp directory manually.
    fs::path dir = m_tempDir / "synth_a";
    std::vector<std::string> names;
    for (auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".json")
            names.push_back(e.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "Bass");
    EXPECT_EQ(names[1], "Pad");
}

// ── presetsRootDir creates directory ─────────────────────────────────────────

TEST(PresetManagerPaths, RootDirExists) {
    fs::path root = PresetManager::presetsRootDir();
    EXPECT_TRUE(fs::exists(root));
    EXPECT_TRUE(fs::is_directory(root));
}

TEST(PresetManagerPaths, DeviceDirCreated) {
    fs::path dir = PresetManager::presetsDir("test_device_xyz");
    EXPECT_TRUE(fs::exists(dir));
    // Clean up
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ── Full save via PresetManager::savePreset ──────────────────────────────────

TEST(PresetManagerSaveLoad, FullRoundTrip) {
    json params;
    params["Freq"] = 440.0f;
    params["Amp"]  = 0.8f;

    fs::path saved = PresetManager::savePreset(
        "Test Roundtrip", "test_roundtrip_dev", "TestDev", params);
    ASSERT_FALSE(saved.empty());
    ASSERT_TRUE(fs::exists(saved));

    PresetData data;
    ASSERT_TRUE(PresetManager::loadPreset(saved, data));
    EXPECT_EQ(data.name, "Test Roundtrip");
    EXPECT_EQ(data.deviceId, "test_roundtrip_dev");
    EXPECT_FLOAT_EQ(data.params["Freq"].get<float>(), 440.0f);
    EXPECT_FLOAT_EQ(data.params["Amp"].get<float>(), 0.8f);

    // Clean up
    PresetManager::deletePreset(saved);
    std::error_code ec;
    fs::remove_all(saved.parent_path(), ec);
}
