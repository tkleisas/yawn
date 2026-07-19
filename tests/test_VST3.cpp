#include <gtest/gtest.h>
#include "util/Base64.h"

using namespace yawn;

TEST(Base64, EmptyData) {
    std::vector<uint8_t> data;
    EXPECT_EQ(base64Encode(data), "");
    EXPECT_TRUE(base64Decode("").empty());
}

TEST(Base64, SingleByte) {
    std::vector<uint8_t> data = {0x41}; // 'A'
    auto encoded = base64Encode(data);
    EXPECT_EQ(encoded, "QQ==");
    auto decoded = base64Decode(encoded);
    EXPECT_EQ(decoded, data);
}

TEST(Base64, TwoBytes) {
    std::vector<uint8_t> data = {0x41, 0x42};
    auto encoded = base64Encode(data);
    EXPECT_EQ(encoded, "QUI=");
    auto decoded = base64Decode(encoded);
    EXPECT_EQ(decoded, data);
}

TEST(Base64, ThreeBytes) {
    std::vector<uint8_t> data = {0x41, 0x42, 0x43};
    auto encoded = base64Encode(data);
    EXPECT_EQ(encoded, "QUJD");
    auto decoded = base64Decode(encoded);
    EXPECT_EQ(decoded, data);
}

TEST(Base64, Roundtrip256Bytes) {
    std::vector<uint8_t> data(256);
    for (int i = 0; i < 256; ++i) data[i] = static_cast<uint8_t>(i);
    auto encoded = base64Encode(data);
    auto decoded = base64Decode(encoded);
    EXPECT_EQ(decoded, data);
}

TEST(Base64, RoundtripBinaryState) {
    // Simulate a typical VST3 state blob
    std::vector<uint8_t> data = {
        0x00, 0xFF, 0x80, 0x7F, 0x01, 0xFE, 0x40, 0xBF,
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE
    };
    auto encoded = base64Encode(data);
    auto decoded = base64Decode(encoded);
    EXPECT_EQ(decoded, data);
}

#ifdef YAWN_HAS_VST3
#include "vst3/VST3Scanner.h"
#include <fstream>
#include <filesystem>
#include <unordered_set>

TEST(VST3Scanner, CacheRoundtrip) {
    using namespace yawn::vst3;

    // Create a scanner with some test data
    VST3Scanner scanner;

    // We can't scan real plugins in tests, but we can test cache roundtrip
    auto tempPath = std::filesystem::temp_directory_path() / "yawn_test_vst3cache.json";

    // Save empty cache
    EXPECT_TRUE(scanner.saveCache(tempPath.string()));
    EXPECT_TRUE(scanner.loadCache(tempPath.string()));
    EXPECT_EQ(scanner.plugins().size(), 0u);

    std::filesystem::remove(tempPath);
}

TEST(VST3Scanner, SearchPathsNotEmpty) {
    auto paths = yawn::vst3::VST3Scanner::getSearchPaths();
    EXPECT_FALSE(paths.empty());
}

// ── v2 resumable scan (out-of-process crash isolation) ───────────

namespace {

// Fake per-module scan function factory: records called paths and
// returns plugins/failure per scripted behavior.
struct FakeScan {
    std::vector<std::string> called;
    std::unordered_set<std::string> failOn;
    int pluginsPerModule = 1;

    yawn::vst3::VST3Scanner::ModuleScanFn fn() {
        return [this](const std::string& path,
                      std::vector<yawn::vst3::VST3PluginInfo>& out) -> bool {
            called.push_back(path);
            if (failOn.count(path)) return false;
            for (int i = 0; i < pluginsPerModule; ++i) {
                yawn::vst3::VST3PluginInfo info;
                info.name = "Plug" + std::to_string(i) + "@" + path;
                info.classIDString = "ID" + std::to_string(called.size());
                info.modulePath = path;
                info.isInstrument = (i % 2 == 0);
                out.push_back(std::move(info));
            }
            return true;
        };
    }
};

std::filesystem::path tempCache(const char* name) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(p);
    return p;
}

} // namespace

TEST(VST3Scanner, V2CacheContainsEveryModuleAndCompletes) {
    using namespace yawn::vst3;
    auto cache = tempCache("yawn_test_v2_a.json");

    VST3Scanner scanner;
    FakeScan fake;
    ASSERT_TRUE(scanner.scanWith(cache.string(), fake.fn()));
    EXPECT_TRUE(scanner.scanComplete());
    // Every fake-scanned module's plugins are visible via the
    // flattened plugin list.
    EXPECT_EQ(scanner.plugins().size(), fake.called.size());
    // Cache file exists and round-trips with the complete flag.
    VST3Scanner s2;
    ASSERT_TRUE(s2.loadCache(cache.string()));
    EXPECT_TRUE(s2.scanComplete());
    EXPECT_EQ(s2.plugins().size(), fake.called.size());
    std::filesystem::remove(cache);
}

TEST(VST3Scanner, V2ResumeSkipsAlreadyScannedModules) {
    using namespace yawn::vst3;
    auto cache = tempCache("yawn_test_v2_b.json");

    // First pass: complete scan (whatever modules the machine has).
    {
        VST3Scanner scanner;
        FakeScan fake;
        ASSERT_TRUE(scanner.scanWith(cache.string(), fake.fn()));
    }

    // Second pass: nothing left to scan — fn must never fire.
    {
        VST3Scanner scanner;
        ASSERT_TRUE(scanner.loadCache(cache.string()));
        FakeScan fake2;
        EXPECT_TRUE(scanner.scanWith(cache.string(), fake2.fn()));
        EXPECT_TRUE(fake2.called.empty())
            << "completed cache must not rescan any module";
    }
    std::filesystem::remove(cache);
}

TEST(VST3Scanner, V2FailedEntriesAreNotRetried) {
    using namespace yawn::vst3;
    auto cache = tempCache("yawn_test_v2_c.json");

    // Fail on the first module path we see (if any), succeed elsewhere.
    FakeScan fake1;
    std::string firstPath;
    {
        VST3Scanner scanner;
        // Wrap the fake to capture the first path and fail on it.
        auto baseFn = fake1.fn();
        auto fn = [&](const std::string& path,
                      std::vector<VST3PluginInfo>& out) -> bool {
            if (firstPath.empty()) firstPath = path;
            if (path == firstPath) { fake1.called.push_back(path); return false; }
            return baseFn(path, out);
        };
        ASSERT_TRUE(scanner.scanWith(cache.string(), fn));
        EXPECT_TRUE(scanner.scanComplete())
            << "failed entries count as done (no crash loop)";
    }

    // Reload: the failed entry must not be retried.
    {
        VST3Scanner scanner;
        ASSERT_TRUE(scanner.loadCache(cache.string()));
        FakeScan fake2;
        EXPECT_TRUE(scanner.scanWith(cache.string(), fake2.fn()));
        EXPECT_TRUE(fake2.called.empty())
            << "failed entries are terminal — not retried next launch";
    }
    std::filesystem::remove(cache);
}

TEST(VST3Scanner, V1LegacyCacheStillLoads) {
    using namespace yawn::vst3;
    auto cache = tempCache("yawn_test_v1.json");

    // Hand-write a legacy v1 array cache.
    {
        std::ofstream out(cache.string());
        out << R"([{"name":"Old","vendor":"V","version":"1","category":"Audio Module Class","subcategories":"Fx","classID":"ABC","modulePath":"/x/y.vst3","isInstrument":false}])";
    }

    VST3Scanner scanner;
    ASSERT_TRUE(scanner.loadCache(cache.string()));
    EXPECT_FALSE(scanner.scanComplete())
        << "v1 has no per-module provenance — new modules may rescan";
    ASSERT_EQ(scanner.plugins().size(), 1u);
    EXPECT_EQ(scanner.plugins()[0].name, "Old");
    EXPECT_EQ(scanner.effects().size(), 1u);
    EXPECT_EQ(scanner.instruments().size(), 0u);
    std::filesystem::remove(cache);
}

// ── Plugin state hardening ──

TEST(VST3PluginInstance, OverCapStateRejectedBeforePluginAccess) {
    using namespace yawn::vst3;
    // An instance with no plugin loaded: the size cap must fire
    // BEFORE any plugin interaction — a corrupt project's arbitrary
    // base64 blob must never reach plugin code.
    VST3PluginInstance inst;
    std::vector<uint8_t> blob(kMaxPluginStateBytes + 1, 0xAB);
    EXPECT_FALSE(inst.setProcessorState(blob));
    EXPECT_FALSE(inst.setControllerState(blob));
    // Under-cap input takes the normal path (here: no component).
    std::vector<uint8_t> small(64, 0x01);
    EXPECT_FALSE(inst.setProcessorState(small));
    EXPECT_FALSE(inst.setControllerState(small));
}
#endif
