#include <gtest/gtest.h>

#include "visual/gltf/M3DSceneScript.h"
#include "visual/gltf/M3DTransform.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace yawn::visual;
namespace fs = std::filesystem;

namespace {

// Write `body` to a unique temp .lua file and return its path. The
// fixture removes it on teardown.
class SceneScriptTest : public ::testing::Test {
protected:
    std::string writeScript(const std::string& body) {
        auto p = fs::temp_directory_path() /
                 ("yawn_scene_test_" + std::to_string(m_counter++) + ".lua");
        std::ofstream f(p);
        f << body;
        f.close();
        m_paths.push_back(p);
        return p.string();
    }
    void TearDown() override {
        std::error_code ec;
        for (auto& p : m_paths) fs::remove(p, ec);
    }
    int m_counter = 0;
    std::vector<fs::path> m_paths;
    M3DSceneScript::Inputs in;   // all zero by default
};

} // namespace

TEST_F(SceneScriptTest, SingleInstanceShorthandWithDefaults) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(writeScript(
        "function tick(ctx) return { position = {1, 2, 3} } end")));

    std::vector<M3DInstance> out;
    ASSERT_TRUE(s.tick(in, out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].position[0], 1.0f);
    EXPECT_FLOAT_EQ(out[0].position[1], 2.0f);
    EXPECT_FLOAT_EQ(out[0].position[2], 3.0f);
    // Appearance defaults: white, no emissive, opaque, model 0, uniform scale 1.
    EXPECT_FLOAT_EQ(out[0].color[0], 1.0f);
    EXPECT_FLOAT_EQ(out[0].emissive, 0.0f);
    EXPECT_FLOAT_EQ(out[0].opacity, 1.0f);
    EXPECT_EQ(out[0].model, 0);
    EXPECT_FLOAT_EQ(out[0].scale, 1.0f);
}

TEST_F(SceneScriptTest, EmptyReturnDrawsNothing) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(writeScript("function tick(ctx) return {} end")));
    std::vector<M3DInstance> out;
    ASSERT_TRUE(s.tick(in, out));
    EXPECT_TRUE(out.empty());   // {} = draw nothing, not one default instance
}

TEST_F(SceneScriptTest, RichPerInstanceFields) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(writeScript(R"LUA(
        function tick(ctx)
          return {
            { model = 2, position = {0, 0, 0}, scale = {2, 3, 4},
              color = {0.1, 0.2, 0.3}, emissive = 0.5, opacity = 0.25,
              anim = { clip = 1, time = 0.75 } },
            { model = 0, scale = 5 },
          }
        end)LUA")));

    std::vector<M3DInstance> out;
    ASSERT_TRUE(s.tick(in, out));
    ASSERT_EQ(out.size(), 2u);

    EXPECT_EQ(out[0].model, 2);
    EXPECT_FLOAT_EQ(out[0].scale3[0], 2.0f);
    EXPECT_FLOAT_EQ(out[0].scale3[1], 3.0f);
    EXPECT_FLOAT_EQ(out[0].scale3[2], 4.0f);
    EXPECT_FLOAT_EQ(out[0].color[1], 0.2f);
    EXPECT_FLOAT_EQ(out[0].emissive, 0.5f);
    EXPECT_FLOAT_EQ(out[0].opacity, 0.25f);
    EXPECT_EQ(out[0].animClip, 1);
    EXPECT_FLOAT_EQ(out[0].animTime, 0.75f);

    // Second instance: numeric scale → uniform; scale3 stays identity.
    EXPECT_EQ(out[1].model, 0);
    EXPECT_FLOAT_EQ(out[1].scale, 5.0f);
    EXPECT_FLOAT_EQ(out[1].scale3[0], 1.0f);
}

TEST_F(SceneScriptTest, CameraFromSecondReturn) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(writeScript(R"LUA(
        function tick(ctx)
          return { { position = {0,0,0} } },
                 { camera = { pos = {1, 2, 9}, target = {0, 1, 0}, fov = 35 } }
        end)LUA")));

    std::vector<M3DInstance> out;
    M3DCamera cam;
    EXPECT_FALSE(cam.explicitCam);
    ASSERT_TRUE(s.tick(in, out, &cam));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_TRUE(cam.explicitCam);
    EXPECT_FLOAT_EQ(cam.pos[2], 9.0f);
    EXPECT_FLOAT_EQ(cam.target[1], 1.0f);
    EXPECT_FLOAT_EQ(cam.fov, 35.0f);
}

TEST_F(SceneScriptTest, NoCameraLeavesItAuto) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(writeScript(
        "function tick(ctx) return { { scale = 1 } } end")));
    std::vector<M3DInstance> out;
    M3DCamera cam;
    ASSERT_TRUE(s.tick(in, out, &cam));
    EXPECT_FALSE(cam.explicitCam);   // no camera returned → auto-frame
}

TEST_F(SceneScriptTest, BackwardCompatListOfTransforms) {
    // The pre-v2 contract: a list of {position, rotation, scale} tables.
    M3DSceneScript s;
    ASSERT_TRUE(s.load(writeScript(R"LUA(
        function tick(ctx)
          local t = {}
          for i = 1, 8 do
            t[i] = { position = {i, 0, 0}, rotation = {0, i * 45, 0}, scale = 0.5 }
          end
          return t
        end)LUA")));

    std::vector<M3DInstance> out;
    ASSERT_TRUE(s.tick(in, out));
    ASSERT_EQ(out.size(), 8u);
    EXPECT_FLOAT_EQ(out[3].position[0], 4.0f);
    EXPECT_FLOAT_EQ(out[3].rotationDeg[1], 180.0f);
    EXPECT_FLOAT_EQ(out[3].scale, 0.5f);
}
