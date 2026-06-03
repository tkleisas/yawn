// M3DModel smoke tests. Loads box01.glb (shipped inside the vendored
// tinygltf source tree) and sanity-checks the parsed mesh + bounds.
// Gated on YAWN_HAS_MODEL3D so the file is a no-op when 3D support is
// compiled out.

#include <gtest/gtest.h>

#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D

#include "visual/gltf/M3DModel.h"
#include <string>

using namespace yawn::visual;

namespace {
std::string sampleBox01() {
    return std::string(YAWN_TINYGLTF_SAMPLE_DIR) + "/box01.glb";
}
std::string bundledFox() {
    return std::string(YAWN_BUNDLED_MODELS_DIR) + "/Fox.glb";
}
}

TEST(M3DModelTest, LoadsBox01) {
    M3DModel model;
    ASSERT_TRUE(model.load(sampleBox01())) << model.error();
    EXPECT_TRUE(model.isValid());
    EXPECT_GE(model.meshCount(), 1);
}

TEST(M3DModelTest, Box01HasVertices) {
    M3DModel model;
    ASSERT_TRUE(model.load(sampleBox01()));
    int total = 0;
    for (int i = 0; i < model.meshCount(); ++i) {
        total += static_cast<int>(model.mesh(i).vertices.size());
    }
    EXPECT_GT(total, 0);
}

TEST(M3DModelTest, Box01HasIndices) {
    M3DModel model;
    ASSERT_TRUE(model.load(sampleBox01()));
    int total = 0;
    for (int i = 0; i < model.meshCount(); ++i) {
        total += static_cast<int>(model.mesh(i).indices.size());
        // Indices come in triples for triangle lists.
        EXPECT_EQ(model.mesh(i).indices.size() % 3, 0u);
    }
    EXPECT_GT(total, 0);
}

TEST(M3DModelTest, Box01BoundsNonDegenerate) {
    M3DModel model;
    ASSERT_TRUE(model.load(sampleBox01()));
    const auto& b = model.bounds();
    ASSERT_TRUE(b.valid);
    EXPECT_LT(b.minCoord[0], b.maxCoord[0]);
    EXPECT_LT(b.minCoord[1], b.maxCoord[1]);
    EXPECT_LT(b.minCoord[2], b.maxCoord[2]);
}

TEST(M3DModelTest, Box01IndicesInRange) {
    M3DModel model;
    ASSERT_TRUE(model.load(sampleBox01()));
    for (int i = 0; i < model.meshCount(); ++i) {
        const auto& m = model.mesh(i);
        uint32_t vn = static_cast<uint32_t>(m.vertices.size());
        for (uint32_t idx : m.indices) {
            EXPECT_LT(idx, vn);
        }
    }
}

TEST(M3DModelTest, MissingFileFailsCleanly) {
    M3DModel model;
    EXPECT_FALSE(model.load("/does/not/exist/nonsense.glb"));
    EXPECT_FALSE(model.isValid());
    EXPECT_FALSE(model.error().empty());
}

TEST(M3DModelTest, ClearsStateOnReload) {
    M3DModel model;
    ASSERT_TRUE(model.load(sampleBox01()));
    int firstMeshes = model.meshCount();
    // Second load over a bad path should leave the model empty, not
    // stacked with the previous state.
    EXPECT_FALSE(model.load("/nope.glb"));
    EXPECT_EQ(model.meshCount(), 0);
    EXPECT_FALSE(model.isValid());
    // And loading again over a good path restores content.
    ASSERT_TRUE(model.load(sampleBox01()));
    EXPECT_EQ(model.meshCount(), firstMeshes);
}

// ── Rigged model (Fox.glb — shipped in assets/examples/3d) ────────────────

TEST(M3DModelTest, Box01HasNoSkinsOrAnims) {
    M3DModel model;
    ASSERT_TRUE(model.load(sampleBox01()));
    EXPECT_EQ(model.skinCount(), 0);
    EXPECT_EQ(model.animationCount(), 0);
    // And its single mesh should be static, not skinned.
    for (int i = 0; i < model.meshCount(); ++i) {
        EXPECT_EQ(model.mesh(i).skinIndex, -1);
    }
}

TEST(M3DModelTest, FoxHasSkinAndAnimations) {
    M3DModel model;
    ASSERT_TRUE(model.load(bundledFox())) << model.error();
    EXPECT_GT(model.skinCount(), 0);
    EXPECT_GT(model.animationCount(), 0);
    // Fox ships with Survey / Walk / Run — three clips.
    EXPECT_EQ(model.animationCount(), 3);
    for (int i = 0; i < model.animationCount(); ++i) {
        const auto& a = model.animation(i);
        EXPECT_GT(a.duration, 0.0f);
        EXPECT_FALSE(a.samplers.empty());
        EXPECT_FALSE(a.channels.empty());
    }
}

TEST(M3DModelTest, FoxMeshesReferenceSkin) {
    M3DModel model;
    ASSERT_TRUE(model.load(bundledFox()));
    bool anySkinned = false;
    for (int i = 0; i < model.meshCount(); ++i) {
        const auto& m = model.mesh(i);
        if (m.skinIndex >= 0) {
            anySkinned = true;
            // Skin index must be in range.
            EXPECT_LT(m.skinIndex, model.skinCount());
            // Skinned vertices should have non-zero weights on at least
            // one joint (the file was baked from real rigging).
            bool anyWeight = false;
            for (const auto& v : m.vertices) {
                float sum = v.weights[0] + v.weights[1] +
                             v.weights[2] + v.weights[3];
                if (sum > 0.0f) { anyWeight = true; break; }
            }
            EXPECT_TRUE(anyWeight);
        }
    }
    EXPECT_TRUE(anySkinned);
}

TEST(M3DModelTest, FoxSkinJointsAreValidNodes) {
    M3DModel model;
    ASSERT_TRUE(model.load(bundledFox()));
    ASSERT_GT(model.skinCount(), 0);
    const auto& sk = model.skin(0);
    EXPECT_FALSE(sk.joints.empty());
    EXPECT_EQ(sk.inverseBindMatrices.size(), sk.joints.size());
    for (int j : sk.joints) {
        EXPECT_GE(j, 0);
        EXPECT_LT(j, model.nodeCount());
    }
}

// ── PBR material parsing (Phase 3) ───────────────────────────────────────
// Synthesised glTF (data-URI buffer) with a known material so the new
// emissive / metallic / roughness / alpha fields are pinned, independent
// of any shipped asset (the bundled samples carry no PBR material).

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
std::string base64(const std::vector<uint8_t>& d) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; int val = 0, bits = -6;
    for (uint8_t c : d) {
        val = (val << 8) + c; bits += 8;
        while (bits >= 0) { o += T[(val >> bits) & 0x3F]; bits -= 6; }
    }
    if (bits > -6) o += T[((val << 8) >> (bits + 8)) & 0x3F];
    while (o.size() % 4) o += '=';
    return o;
}
} // namespace

TEST(M3DModelTest, ParsesPbrMaterialFields) {
    std::vector<uint8_t> buf;
    auto pushF   = [&](float v)    { uint8_t b[4]; std::memcpy(b, &v, 4); buf.insert(buf.end(), b, b + 4); };
    auto pushU16 = [&](uint16_t v) { uint8_t b[2]; std::memcpy(b, &v, 2); buf.insert(buf.end(), b, b + 2); };
    const float pos[9] = { 0,0,0, 1,0,0, 0,1,0 };
    for (float v : pos) pushF(v);
    pushU16(0); pushU16(1); pushU16(2);

    const std::string gltf =
        std::string("{\"asset\":{\"version\":\"2.0\"},")
        + "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},"
          "\"indices\":1,\"material\":0}]}],"
        + "\"materials\":[{\"pbrMetallicRoughness\":{"
          "\"baseColorFactor\":[0.2,0.4,0.6,1.0],"
          "\"metallicFactor\":0.25,\"roughnessFactor\":0.75},"
          "\"emissiveFactor\":[1.0,0.5,0.0],"
          "\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3,\"doubleSided\":true}],"
        + "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,"
          + base64(buf) + "\",\"byteLength\":" + std::to_string(buf.size()) + "}],"
        + "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36,\"target\":34962},"
          "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6,\"target\":34963}],"
        + "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,"
          "\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
          "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";

    auto p = std::filesystem::temp_directory_path() / "yawn_pbr_mat_test.gltf";
    { std::ofstream f(p); f << gltf; }
    M3DModel model;
    ASSERT_TRUE(model.load(p.string())) << model.error();
    std::error_code ec; std::filesystem::remove(p, ec);

    ASSERT_GE(model.materialCount(), 1);
    const auto& m = model.material(0);
    EXPECT_FLOAT_EQ(m.metallicFactor,  0.25f);
    EXPECT_FLOAT_EQ(m.roughnessFactor, 0.75f);
    EXPECT_FLOAT_EQ(m.emissiveFactor[0], 1.0f);
    EXPECT_FLOAT_EQ(m.emissiveFactor[1], 0.5f);
    EXPECT_FLOAT_EQ(m.emissiveFactor[2], 0.0f);
    EXPECT_EQ(m.alphaMode, M3DMaterial::AlphaMode::Mask);
    EXPECT_FLOAT_EQ(m.alphaCutoff, 0.3f);
    EXPECT_TRUE(m.doubleSided);
}

#endif // YAWN_HAS_MODEL3D
