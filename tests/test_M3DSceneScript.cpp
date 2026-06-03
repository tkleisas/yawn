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

TEST_F(SceneScriptTest, NotesAreVisibleToTheScript) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(writeScript(R"LUA(
        function tick(ctx)
          local out = {}
          for _, n in ipairs(ctx.notes) do
            out[#out + 1] = {
              position = { n.pitch, n.vel, n.age },
              model    = n.track,
              opacity  = 1.0 - n.age,
            }
          end
          return out
        end)LUA")));

    in.notes.push_back({ /*track*/2, /*channel*/0, /*pitch*/60, /*vel*/0.8f, /*age*/0.0f });
    in.notes.push_back({ /*track*/0, /*channel*/9, /*pitch*/36, /*vel*/0.5f, /*age*/1.5f });

    std::vector<M3DInstance> out;
    ASSERT_TRUE(s.tick(in, out));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_FLOAT_EQ(out[0].position[0], 60.0f);   // pitch
    EXPECT_FLOAT_EQ(out[0].position[1], 0.8f);    // vel
    EXPECT_EQ(out[0].model, 2);                   // track
    EXPECT_FLOAT_EQ(out[1].position[0], 36.0f);
    EXPECT_FLOAT_EQ(out[1].opacity, 1.0f - 1.5f); // age-faded
}

#ifdef YAWN_BUNDLED_SCRIPTS_DIR
// Load + run the scripts we actually ship, so a Lua syntax error or a
// drift from the engine contract is caught in CI rather than in the app.
TEST_F(SceneScriptTest, BundledMultiModelOrbitScript) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(std::string(YAWN_BUNDLED_SCRIPTS_DIR) +
                       "/multi_model_orbit.lua")) << s.error();
    M3DSceneScript::Inputs in;
    in.knobs[0] = 0.5f;   // knob A (camera distance)
    std::vector<M3DInstance> out;
    M3DCamera cam;
    ASSERT_TRUE(s.tick(in, out, &cam)) << s.error();

    ASSERT_EQ(out.size(), 3u);                 // SLOTS = 3
    EXPECT_EQ(out[0].model, 0);                // one model per slot
    EXPECT_EQ(out[1].model, 1);
    EXPECT_EQ(out[2].model, 2);
    EXPECT_NEAR(out[0].emissive, 0.15f, 1e-4f); // baseline glow (no kick)
    EXPECT_TRUE(cam.explicitCam);              // returns an orbit camera
    EXPECT_FLOAT_EQ(cam.fov, 50.0f);
}

TEST_F(SceneScriptTest, BundledNoteBurstScript) {
    M3DSceneScript s;
    ASSERT_TRUE(s.load(std::string(YAWN_BUNDLED_SCRIPTS_DIR) +
                       "/note_burst.lua")) << s.error();
    std::vector<M3DInstance> out;

    // No notes → nothing drawn.
    ASSERT_TRUE(s.tick(in, out));
    EXPECT_TRUE(out.empty());

    // A fresh kick + a snare → two instances; an old note past LIFETIME
    // is culled by the script.
    in.notes.push_back({ 0, 9, 36, 1.0f, 0.0f });   // kick, fresh
    in.notes.push_back({ 0, 9, 38, 0.7f, 0.2f });   // snare, recent
    in.notes.push_back({ 0, 9, 42, 0.5f, 3.0f });   // old hat → culled
    ASSERT_TRUE(s.tick(in, out));
    EXPECT_EQ(out.size(), 2u);
    EXPECT_GT(out[0].emissive, 0.0f);               // flashes on the hit
}

TEST_F(SceneScriptTest, BundledWorkflowScenes) {
    std::vector<M3DInstance> out;

    M3DSceneScript bars;
    ASSERT_TRUE(bars.load(std::string(YAWN_BUNDLED_SCRIPTS_DIR) +
                          "/audio_bars.lua")) << bars.error();
    ASSERT_TRUE(bars.tick(in, out));
    EXPECT_EQ(out.size(), 9u);
    // Per-axis scale: bars are tall/thin (scale3, not uniform scale).
    EXPECT_FLOAT_EQ(out[0].scale3[0], 0.25f);
    EXPECT_NE(out[0].scale3[1], out[0].scale3[0]);

    M3DSceneScript helix;
    ASSERT_TRUE(helix.load(std::string(YAWN_BUNDLED_SCRIPTS_DIR) +
                           "/spin_helix.lua")) << helix.error();
    ASSERT_TRUE(helix.tick(in, out));
    EXPECT_EQ(out.size(), 24u);
}

TEST_F(SceneScriptTest, BundledLegacyScriptsStillParse) {
    M3DSceneScript::Inputs in;
    std::vector<M3DInstance> out;

    M3DSceneScript ring;
    ASSERT_TRUE(ring.load(std::string(YAWN_BUNDLED_SCRIPTS_DIR) +
                          "/kick_ring.lua")) << ring.error();
    ASSERT_TRUE(ring.tick(in, out));
    EXPECT_EQ(out.size(), 8u);                 // ring of 8

    M3DSceneScript stat;
    ASSERT_TRUE(stat.load(std::string(YAWN_BUNDLED_SCRIPTS_DIR) +
                          "/static_demo.lua")) << stat.error();
    ASSERT_TRUE(stat.tick(in, out));
    EXPECT_EQ(out.size(), 1u);
}
#endif

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
