// VideoImporter tests — focused on the pieces that don't require
// ffmpeg to actually run: hash determinism and the Result struct.
// Live import end-to-end is covered interactively in the app.

#include <gtest/gtest.h>
#include "visual/VideoImporter.h"

#include <fstream>
#include <filesystem>

using namespace yawn::visual;
namespace fs = std::filesystem;

namespace {
fs::path writeTempFile(const std::string& name, size_t bytes) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary);
    std::string buf(bytes, 'x');
    f.write(buf.data(), buf.size());
    return p;
}
}

TEST(VideoImporterTest, ShortHashDeterministic) {
    auto p = writeTempFile("yawn_test_hash_a.bin", 1024);
    std::string h1 = VideoImporter::shortHash(p.string());
    std::string h2 = VideoImporter::shortHash(p.string());
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.size(), 16u);  // 64-bit FNV-1a → 16 hex chars
    fs::remove(p);
}

TEST(VideoImporterTest, ShortHashDiffersOnPath) {
    auto a = writeTempFile("yawn_test_hash_a.bin", 1024);
    auto b = writeTempFile("yawn_test_hash_b.bin", 1024);
    std::string h1 = VideoImporter::shortHash(a.string());
    std::string h2 = VideoImporter::shortHash(b.string());
    EXPECT_NE(h1, h2);
    fs::remove(a);
    fs::remove(b);
}

TEST(VideoImporterTest, ShortHashDiffersOnSize) {
    auto a = writeTempFile("yawn_test_hash_size.bin", 1024);
    // Rewrite with different size.
    auto a_big = a;
    fs::remove(a_big);
    a_big = writeTempFile("yawn_test_hash_size.bin", 4096);
    std::string h2 = VideoImporter::shortHash(a_big.string());

    fs::remove(a_big);
    auto a_small = writeTempFile("yawn_test_hash_size.bin", 1024);
    std::string h1 = VideoImporter::shortHash(a_small.string());

    EXPECT_NE(h1, h2);
    fs::remove(a_small);
}

TEST(VideoImporterTest, InitialStateIsIdle) {
    VideoImporter imp;
    EXPECT_EQ(imp.state(), VideoImporter::State::Idle);
    EXPECT_FLOAT_EQ(imp.progress(), 0.0f);
    EXPECT_TRUE(imp.error().empty());
}

TEST(VideoImporterTest, MissingSourceFails) {
    VideoImporter imp;
    auto tmp = fs::temp_directory_path() / "yawn_import_test";
    bool ok = imp.start("/definitely/does/not/exist.mp4", tmp);
    EXPECT_FALSE(ok);
    EXPECT_EQ(imp.state(), VideoImporter::State::Failed);
    EXPECT_FALSE(imp.error().empty());
}
