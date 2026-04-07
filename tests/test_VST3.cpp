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
#endif
