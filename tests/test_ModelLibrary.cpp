#include <gtest/gtest.h>

#include "visual/ModelLibrary.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace yawn::visual;
namespace fs = std::filesystem;

namespace {
class ModelLibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_root = fs::temp_directory_path() / "yawn_modellib_test";
        fs::remove_all(m_root);
        m_bundled = m_root / "bundled";
        m_user    = m_root / "user";
        fs::create_directories(m_bundled);
        fs::create_directories(m_user);
    }
    void TearDown() override { std::error_code ec; fs::remove_all(m_root, ec); }
    void touch(const fs::path& p) { std::ofstream f(p); f << "x"; }

    fs::path m_root, m_bundled, m_user;
};
} // namespace

TEST_F(ModelLibraryTest, ScansGlbGltfAndIgnoresOthers) {
    touch(m_bundled / "Duck.glb");
    touch(m_bundled / "Scene.GLTF");      // case-insensitive ext
    touch(m_bundled / "readme.txt");      // ignored
    touch(m_user / "robot.glb");

    ModelLibrary lib;
    lib.setDirectories({ m_bundled.string(), m_user.string() }, /*bundledCount*/1);
    lib.refresh();

    const auto& e = lib.entries();
    ASSERT_EQ(e.size(), 3u);              // 2 bundled + 1 user, no .txt
    // Bundled entries sort first.
    EXPECT_TRUE(e[0].bundled);
    EXPECT_TRUE(e[1].bundled);
    EXPECT_FALSE(e[2].bundled);
    EXPECT_EQ(e[2].name, "robot");
}

TEST_F(ModelLibraryTest, FilterByName) {
    touch(m_bundled / "Duck.glb");
    touch(m_bundled / "Fox.glb");
    touch(m_user / "DuckDuckGoose.glb");

    ModelLibrary lib;
    lib.setDirectories({ m_bundled.string(), m_user.string() }, 1);
    lib.refresh();

    auto ducks = lib.filtered("duck");    // case-insensitive substring
    EXPECT_EQ(ducks.size(), 2u);
    EXPECT_TRUE(lib.filtered("").size() == 3u);   // empty = all
    EXPECT_TRUE(lib.filtered("zzz").empty());
}

TEST_F(ModelLibraryTest, MissingDirIsSkipped) {
    touch(m_user / "a.glb");
    ModelLibrary lib;
    lib.setDirectories({ (m_root / "does_not_exist").string(), m_user.string() }, 1);
    lib.refresh();
    EXPECT_EQ(lib.entries().size(), 1u);
}
