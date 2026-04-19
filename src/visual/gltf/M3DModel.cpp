#include "visual/gltf/M3DModel.h"
#include "visual/gltf/M3DMath.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D

// tinygltf headers must see the same skip macros we used in the single
// TU that stamps out its implementation; otherwise it would pull in
// stb_image.h / json.hpp again here as additional declarations.
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_JSON
#include "stb_image.h"
#include <nlohmann/json.hpp>
#include "tiny_gltf.h"

namespace yawn {
namespace visual {

using m3d::Mat4;
using m3d::identity;
using m3d::multiply;
using m3d::translation;
using m3d::scale;
using m3d::quatToMatrix;
using m3d::transformPoint;
using m3d::transformDir;

namespace {

// Turn a glTF node's TRS (or explicit matrix) into a local transform.
Mat4 localMatrixOf(const tinygltf::Node& n) {
    if (n.matrix.size() == 16) {
        Mat4 m{};
        for (int i = 0; i < 16; ++i) m[i] = static_cast<float>(n.matrix[i]);
        return m;
    }
    Mat4 T = identity(), R = identity(), S = identity();
    if (n.translation.size() == 3) {
        T = translation(
            static_cast<float>(n.translation[0]),
            static_cast<float>(n.translation[1]),
            static_cast<float>(n.translation[2]));
    }
    if (n.rotation.size() == 4) {
        R = quatToMatrix(
            static_cast<float>(n.rotation[0]),
            static_cast<float>(n.rotation[1]),
            static_cast<float>(n.rotation[2]),
            static_cast<float>(n.rotation[3]));
    }
    if (n.scale.size() == 3) {
        S = scale(
            static_cast<float>(n.scale[0]),
            static_cast<float>(n.scale[1]),
            static_cast<float>(n.scale[2]));
    }
    return multiply(T, multiply(R, S));
}

// ── Accessor helpers ──────────────────────────────────────────────────────
//
// tinygltf hands us accessors with (bufferView, byteOffset, componentType,
// type). For the geometry subset we need — POSITION, NORMAL, TEXCOORD_0,
// and indices — this pair of helpers does the read.

const uint8_t* accessorPtr(const tinygltf::Model& gm,
                             const tinygltf::Accessor& a,
                             size_t& stride) {
    const auto& bv = gm.bufferViews[a.bufferView];
    const auto& buf = gm.buffers[bv.buffer];
    size_t elemSize = tinygltf::GetComponentSizeInBytes(a.componentType) *
                       tinygltf::GetNumComponentsInType(a.type);
    stride = bv.byteStride > 0 ? bv.byteStride : elemSize;
    return buf.data.data() + bv.byteOffset + a.byteOffset;
}

// Read a float VEC3 at index i.
void readVec3(const tinygltf::Model& gm, const tinygltf::Accessor& a,
               size_t i, float out[3]) {
    size_t stride = 0;
    const uint8_t* base = accessorPtr(gm, a, stride);
    const float* src = reinterpret_cast<const float*>(base + i * stride);
    out[0] = src[0]; out[1] = src[1]; out[2] = src[2];
}

// Read a float VEC2 at index i.
void readVec2(const tinygltf::Model& gm, const tinygltf::Accessor& a,
               size_t i, float out[2]) {
    size_t stride = 0;
    const uint8_t* base = accessorPtr(gm, a, stride);
    const float* src = reinterpret_cast<const float*>(base + i * stride);
    out[0] = src[0]; out[1] = src[1];
}

// Read a scalar index; handles uint8 / uint16 / uint32 variants.
uint32_t readIndex(const tinygltf::Model& gm, const tinygltf::Accessor& a,
                    size_t i) {
    size_t stride = 0;
    const uint8_t* base = accessorPtr(gm, a, stride);
    const uint8_t* p = base + i * stride;
    switch (a.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return *reinterpret_cast<const uint8_t*>(p);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return *reinterpret_cast<const uint16_t*>(p);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return *reinterpret_cast<const uint32_t*>(p);
        default:
            return 0;
    }
}

// Read a 4-component joint index (uint8 or uint16) at vertex i. Returns
// false if the accessor isn't a JOINTS_0-shaped VEC4.
bool readJoints4(const tinygltf::Model& gm, const tinygltf::Accessor& a,
                  size_t i, uint16_t out[4]) {
    if (a.type != TINYGLTF_TYPE_VEC4) return false;
    size_t stride = 0;
    const uint8_t* base = accessorPtr(gm, a, stride);
    const uint8_t* p = base + i * stride;
    switch (a.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const uint8_t* s = p;
            out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out[3] = s[3];
            return true;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(p);
            out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out[3] = s[3];
            return true;
        }
        default:
            return false;
    }
}

// Read a 4-component float weight at vertex i. Returns false if the
// accessor isn't a VEC4 float (also the common normalized-ubyte
// variant, which we convert to float).
bool readWeights4(const tinygltf::Model& gm, const tinygltf::Accessor& a,
                   size_t i, float out[4]) {
    if (a.type != TINYGLTF_TYPE_VEC4) return false;
    size_t stride = 0;
    const uint8_t* base = accessorPtr(gm, a, stride);
    const uint8_t* p = base + i * stride;
    switch (a.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            const float* s = reinterpret_cast<const float*>(p);
            out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out[3] = s[3];
            return true;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const uint8_t* s = p;
            out[0] = s[0] / 255.0f; out[1] = s[1] / 255.0f;
            out[2] = s[2] / 255.0f; out[3] = s[3] / 255.0f;
            return true;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(p);
            out[0] = s[0] / 65535.0f; out[1] = s[1] / 65535.0f;
            out[2] = s[2] / 65535.0f; out[3] = s[3] / 65535.0f;
            return true;
        }
        default:
            return false;
    }
}

// Generate flat-shaded normals from a triangle list. Called only when a
// primitive lacks a NORMAL accessor — rare for well-tooled exports but
// cheap to implement so we don't drop those models.
void generateFlatNormals(std::vector<M3DVertex>& verts,
                          const std::vector<uint32_t>& indices) {
    for (auto& v : verts) { v.nx = v.ny = v.nz = 0.0f; }
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const auto& a = verts[i0];
        const auto& b = verts[i1];
        const auto& c = verts[i2];
        float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
        float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        verts[i0].nx += nx; verts[i0].ny += ny; verts[i0].nz += nz;
        verts[i1].nx += nx; verts[i1].ny += ny; verts[i1].nz += nz;
        verts[i2].nx += nx; verts[i2].ny += ny; verts[i2].nz += nz;
    }
    for (auto& v : verts) {
        float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (len > 1e-6f) { v.nx /= len; v.ny /= len; v.nz /= len; }
    }
}

void growBounds(M3DBounds& b, float x, float y, float z) {
    if (!b.valid) {
        b.minCoord[0] = b.maxCoord[0] = x;
        b.minCoord[1] = b.maxCoord[1] = y;
        b.minCoord[2] = b.maxCoord[2] = z;
        b.valid = true;
        return;
    }
    b.minCoord[0] = std::min(b.minCoord[0], x);
    b.minCoord[1] = std::min(b.minCoord[1], y);
    b.minCoord[2] = std::min(b.minCoord[2], z);
    b.maxCoord[0] = std::max(b.maxCoord[0], x);
    b.maxCoord[1] = std::max(b.maxCoord[1], y);
    b.maxCoord[2] = std::max(b.maxCoord[2], z);
}

// ── Primitive extraction ──────────────────────────────────────────────────

void extractPrimitive(const tinygltf::Model& gm,
                       const tinygltf::Primitive& prim,
                       const Mat4& worldM,
                       int skinIndex,
                       M3DBounds& bounds,
                       std::vector<M3DMesh>& outMeshes) {
    // We only render triangle lists. Points / lines / triangle-strips /
    // triangle-fans are skipped — they're rare in demo assets and need
    // different index handling.
    if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) return;

    // POSITION is mandatory.
    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) return;
    const auto& posAcc = gm.accessors[posIt->second];
    if (posAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        posAcc.type != TINYGLTF_TYPE_VEC3) return;

    M3DMesh mesh;
    mesh.materialIndex = prim.material;
    mesh.worldMatrix   = worldM;
    mesh.skinIndex     = skinIndex;   // -1 if the owning node has no skin
    mesh.vertices.resize(posAcc.count);

    const tinygltf::Accessor* normAcc = nullptr;
    const tinygltf::Accessor* uvAcc   = nullptr;
    const tinygltf::Accessor* joinAcc = nullptr;
    const tinygltf::Accessor* wgtAcc  = nullptr;
    auto normIt = prim.attributes.find("NORMAL");
    if (normIt != prim.attributes.end()) {
        const auto& a = gm.accessors[normIt->second];
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
            a.type == TINYGLTF_TYPE_VEC3 && a.count == posAcc.count) {
            normAcc = &a;
        }
    }
    auto uvIt = prim.attributes.find("TEXCOORD_0");
    if (uvIt != prim.attributes.end()) {
        const auto& a = gm.accessors[uvIt->second];
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
            a.type == TINYGLTF_TYPE_VEC2 && a.count == posAcc.count) {
            uvAcc = &a;
        }
    }
    // Only pick up JOINTS_0/WEIGHTS_0 when the owning node has a skin.
    // A mesh with joints but no skin is malformed glTF and we treat
    // it as static — safer than upload-time surprise.
    if (skinIndex >= 0) {
        auto jIt = prim.attributes.find("JOINTS_0");
        auto wIt = prim.attributes.find("WEIGHTS_0");
        if (jIt != prim.attributes.end() && wIt != prim.attributes.end()) {
            const auto& ja = gm.accessors[jIt->second];
            const auto& wa = gm.accessors[wIt->second];
            if (ja.count == posAcc.count && wa.count == posAcc.count) {
                joinAcc = &ja;
                wgtAcc  = &wa;
            }
        }
        // No joints/weights → fall back to static; skin reference only
        // matters if the vertices actually have the attributes.
        if (!joinAcc || !wgtAcc) mesh.skinIndex = -1;
    }

    const bool isSkinned = (mesh.skinIndex >= 0);

    for (size_t i = 0; i < posAcc.count; ++i) {
        float p[3], n[3] = { 0, 1, 0 }, t[2] = { 0, 0 };
        readVec3(gm, posAcc, i, p);
        if (normAcc) readVec3(gm, *normAcc, i, n);
        if (uvAcc)   readVec2(gm, *uvAcc,   i, t);

        auto& vout = mesh.vertices[i];
        if (isSkinned) {
            // Skinned meshes keep their vertices in mesh-LOCAL space;
            // the renderer's skinning pass will reapply the hierarchy
            // each frame via the joint matrices.
            vout.px = p[0]; vout.py = p[1]; vout.pz = p[2];
            vout.nx = n[0]; vout.ny = n[1]; vout.nz = n[2];
        } else {
            // Static meshes: bake the hierarchy into the vertices so
            // the renderer can treat the VBO as world-space (matches
            // the G.4 static path — no change for unrigged models).
            transformPoint(worldM, p[0], p[1], p[2],
                            vout.px, vout.py, vout.pz);
            float tx, ty, tz;
            transformDir(worldM, n[0], n[1], n[2], tx, ty, tz);
            float ln = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (ln > 1e-6f) { vout.nx = tx/ln; vout.ny = ty/ln; vout.nz = tz/ln; }
            else            { vout.nx = 0; vout.ny = 1; vout.nz = 0; }
        }
        vout.u = t[0];
        vout.v = t[1];

        if (isSkinned && joinAcc && wgtAcc) {
            readJoints4(gm,  *joinAcc, i, vout.joints);
            readWeights4(gm, *wgtAcc,  i, vout.weights);
        }

        growBounds(bounds, vout.px, vout.py, vout.pz);
    }

    // Indices (or generate sequential ones if the primitive is non-indexed).
    if (prim.indices >= 0) {
        const auto& ia = gm.accessors[prim.indices];
        mesh.indices.resize(ia.count);
        for (size_t i = 0; i < ia.count; ++i) {
            mesh.indices[i] = readIndex(gm, ia, i);
        }
    } else {
        mesh.indices.resize(posAcc.count);
        for (size_t i = 0; i < posAcc.count; ++i) {
            mesh.indices[i] = static_cast<uint32_t>(i);
        }
    }

    if (!normAcc) {
        generateFlatNormals(mesh.vertices, mesh.indices);
        if (!isSkinned) {
            // Static path: we already baked world-space, so normals
            // need the matching direction transform for consistency.
            for (auto& v : mesh.vertices) {
                float tx, ty, tz;
                transformDir(worldM, v.nx, v.ny, v.nz, tx, ty, tz);
                float ln = std::sqrt(tx * tx + ty * ty + tz * tz);
                if (ln > 1e-6f) { v.nx = tx/ln; v.ny = ty/ln; v.nz = tz/ln; }
            }
        }
    }

    outMeshes.push_back(std::move(mesh));
}

void walkNode(const tinygltf::Model& gm, int nodeIdx, const Mat4& parentM,
              M3DBounds& bounds, std::vector<M3DMesh>& outMeshes) {
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(gm.nodes.size())) return;
    const auto& n = gm.nodes[nodeIdx];
    Mat4 worldM = multiply(parentM, localMatrixOf(n));

    if (n.mesh >= 0 && n.mesh < static_cast<int>(gm.meshes.size())) {
        const auto& m = gm.meshes[n.mesh];
        int skinIdx = n.skin; // -1 if the node has no skin
        for (const auto& prim : m.primitives) {
            extractPrimitive(gm, prim, worldM, skinIdx, bounds, outMeshes);
        }
    }
    for (int child : n.children) walkNode(gm, child, worldM, bounds, outMeshes);
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────

void M3DModel::clear() {
    m_meshes.clear();
    m_materials.clear();
    m_textures.clear();
    m_nodes.clear();
    m_skins.clear();
    m_animations.clear();
    m_bounds = M3DBounds{};
    m_error.clear();
    m_valid = false;
}

bool M3DModel::load(const std::string& path) {
    clear();

    tinygltf::Model   gm;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // Extension check — .glb is binary, everything else treated as .gltf JSON.
    bool isGlb = false;
    if (path.size() >= 4) {
        std::string ext = path.substr(path.size() - 4);
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        isGlb = (ext == ".glb");
    }

    bool ok = isGlb
        ? loader.LoadBinaryFromFile(&gm, &err, &warn, path)
        : loader.LoadASCIIFromFile(&gm, &err, &warn, path);
    if (!ok) {
        m_error = err.empty() ? "glTF load failed" : err;
        LOG_ERROR("M3D", "Load failed (%s): %s", path.c_str(), m_error.c_str());
        return false;
    }
    if (!warn.empty()) {
        LOG_WARN("M3D", "glTF warning (%s): %s", path.c_str(), warn.c_str());
    }

    // Materials — translate tinygltf's PBR block into our simpler struct.
    m_materials.reserve(gm.materials.size());
    for (const auto& gMat : gm.materials) {
        M3DMaterial mat;
        const auto& pbr = gMat.pbrMetallicRoughness;
        for (size_t i = 0; i < 4 && i < pbr.baseColorFactor.size(); ++i) {
            mat.baseColorFactor[i] = static_cast<float>(pbr.baseColorFactor[i]);
        }
        mat.baseColorTexture = pbr.baseColorTexture.index;
        m_materials.push_back(mat);
    }

    // Textures — tinygltf decoded any embedded PNGs/JPEGs into RGBA8 for
    // us already (via stb_image). We just copy the pixel buffer.
    m_textures.reserve(gm.textures.size());
    for (const auto& gTex : gm.textures) {
        M3DTexture t;
        if (gTex.source >= 0 && gTex.source < static_cast<int>(gm.images.size())) {
            const auto& img = gm.images[gTex.source];
            t.width  = img.width;
            t.height = img.height;
            if (img.component == 4 && !img.image.empty()) {
                t.rgba = img.image;
            } else if (img.component == 3 && !img.image.empty()) {
                // Pad RGB → RGBA so the GL side always uploads as GL_RGBA8.
                t.rgba.resize(size_t(img.width) * img.height * 4);
                for (int p = 0; p < img.width * img.height; ++p) {
                    t.rgba[p * 4 + 0] = img.image[p * 3 + 0];
                    t.rgba[p * 4 + 1] = img.image[p * 3 + 1];
                    t.rgba[p * 4 + 2] = img.image[p * 3 + 2];
                    t.rgba[p * 4 + 3] = 255;
                }
            }
        }
        m_textures.push_back(std::move(t));
    }

    // Nodes — preserve the scene graph so animation channels have
    // something to mutate each frame. We keep the base TRS here; the
    // per-frame evaluator will overlay keyframe samples on top.
    m_nodes.reserve(gm.nodes.size());
    for (const auto& gNode : gm.nodes) {
        M3DNode n;
        n.name = gNode.name;
        n.mesh = gNode.mesh;
        n.skin = gNode.skin;
        n.children.assign(gNode.children.begin(), gNode.children.end());

        if (gNode.translation.size() == 3) {
            n.translation[0] = static_cast<float>(gNode.translation[0]);
            n.translation[1] = static_cast<float>(gNode.translation[1]);
            n.translation[2] = static_cast<float>(gNode.translation[2]);
        }
        if (gNode.rotation.size() == 4) {
            n.rotation[0] = static_cast<float>(gNode.rotation[0]);
            n.rotation[1] = static_cast<float>(gNode.rotation[1]);
            n.rotation[2] = static_cast<float>(gNode.rotation[2]);
            n.rotation[3] = static_cast<float>(gNode.rotation[3]);
        }
        if (gNode.scale.size() == 3) {
            n.scale[0] = static_cast<float>(gNode.scale[0]);
            n.scale[1] = static_cast<float>(gNode.scale[1]);
            n.scale[2] = static_cast<float>(gNode.scale[2]);
        }
        // If the node was given as an explicit matrix, decompose it
        // into TRS so the animation evaluator has a uniform starting
        // point. Non-TRS matrices are rare in practice but we cover
        // the plain translation case; full decomposition is deferred.
        if (gNode.matrix.size() == 16 &&
            gNode.translation.empty() && gNode.rotation.empty() &&
            gNode.scale.empty()) {
            n.translation[0] = static_cast<float>(gNode.matrix[12]);
            n.translation[1] = static_cast<float>(gNode.matrix[13]);
            n.translation[2] = static_cast<float>(gNode.matrix[14]);
            // rotation/scale left at identity — sufficient for meshes
            // with mostly axis-aligned root placement; complex
            // matrix-only rigs need a full polar-decomposition pass
            // which we'll add when we hit a model that needs it.
        }
        m_nodes.push_back(std::move(n));
    }

    // Skins — ordered joint list + inverse bind matrices. The matrices
    // live in a single VEC4×4 float accessor (16 floats per joint).
    m_skins.reserve(gm.skins.size());
    for (const auto& gSkin : gm.skins) {
        M3DSkin sk;
        sk.joints.assign(gSkin.joints.begin(), gSkin.joints.end());
        sk.skeletonRoot = gSkin.skeleton;
        sk.inverseBindMatrices.resize(sk.joints.size(), identity());
        if (gSkin.inverseBindMatrices >= 0 &&
            gSkin.inverseBindMatrices < static_cast<int>(gm.accessors.size())) {
            const auto& acc = gm.accessors[gSkin.inverseBindMatrices];
            if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
                acc.type == TINYGLTF_TYPE_MAT4 &&
                acc.count == sk.joints.size()) {
                size_t stride = 0;
                const uint8_t* base = accessorPtr(gm, acc, stride);
                for (size_t i = 0; i < sk.joints.size(); ++i) {
                    const float* src = reinterpret_cast<const float*>(
                        base + i * (stride ? stride : sizeof(float) * 16));
                    for (int k = 0; k < 16; ++k) sk.inverseBindMatrices[i][k] = src[k];
                }
            }
        }
        m_skins.push_back(std::move(sk));
    }

    // Animations — samplers hold keyframe arrays; channels point into
    // them and name which node/path to drive.
    m_animations.reserve(gm.animations.size());
    for (const auto& gAnim : gm.animations) {
        M3DAnimation anim;
        anim.name = gAnim.name;
        anim.samplers.reserve(gAnim.samplers.size());
        for (const auto& gs : gAnim.samplers) {
            M3DAnimationSampler s;
            if      (gs.interpolation == "STEP")        s.interp = M3DAnimationSampler::Interpolation::Step;
            else if (gs.interpolation == "CUBICSPLINE") s.interp = M3DAnimationSampler::Interpolation::CubicSpline;
            else                                         s.interp = M3DAnimationSampler::Interpolation::Linear;
            if (gs.input >= 0 && gs.input < static_cast<int>(gm.accessors.size())) {
                const auto& ia = gm.accessors[gs.input];
                s.inputTimes.resize(ia.count, 0.0f);
                size_t stride = 0;
                const uint8_t* base = accessorPtr(gm, ia, stride);
                for (size_t i = 0; i < ia.count; ++i) {
                    const float* src = reinterpret_cast<const float*>(
                        base + i * (stride ? stride : sizeof(float)));
                    s.inputTimes[i] = src[0];
                    if (src[0] > anim.duration) anim.duration = src[0];
                }
            }
            if (gs.output >= 0 && gs.output < static_cast<int>(gm.accessors.size())) {
                const auto& oa = gm.accessors[gs.output];
                int ncomp = tinygltf::GetNumComponentsInType(oa.type);
                s.outputValues.resize(oa.count * ncomp, 0.0f);
                size_t stride = 0;
                const uint8_t* base = accessorPtr(gm, oa, stride);
                size_t elemBytes = sizeof(float) * ncomp;
                for (size_t i = 0; i < oa.count; ++i) {
                    const float* src = reinterpret_cast<const float*>(
                        base + i * (stride ? stride : elemBytes));
                    for (int c = 0; c < ncomp; ++c)
                        s.outputValues[i * ncomp + c] = src[c];
                }
            }
            anim.samplers.push_back(std::move(s));
        }
        anim.channels.reserve(gAnim.channels.size());
        for (const auto& gc : gAnim.channels) {
            M3DAnimationChannel c;
            c.targetNode   = gc.target_node;
            c.samplerIndex = gc.sampler;
            if      (gc.target_path == "translation") c.path = M3DAnimationChannel::Path::Translation;
            else if (gc.target_path == "rotation")    c.path = M3DAnimationChannel::Path::Rotation;
            else if (gc.target_path == "scale")       c.path = M3DAnimationChannel::Path::Scale;
            else                                       c.path = M3DAnimationChannel::Path::Weights;
            anim.channels.push_back(c);
        }
        m_animations.push_back(std::move(anim));
    }

    // Meshes — pick the default scene (or first, or all if none flagged),
    // walk its nodes with accumulated transforms, extract primitives.
    int sceneIdx = gm.defaultScene >= 0 ? gm.defaultScene : 0;
    if (!gm.scenes.empty() &&
        sceneIdx < static_cast<int>(gm.scenes.size())) {
        for (int root : gm.scenes[sceneIdx].nodes) {
            walkNode(gm, root, identity(), m_bounds, m_meshes);
        }
    } else {
        // Degenerate file without a scene — walk every node that has a
        // mesh, with identity transform. Better than returning empty.
        for (size_t i = 0; i < gm.nodes.size(); ++i) {
            if (gm.nodes[i].mesh >= 0) {
                walkNode(gm, static_cast<int>(i), identity(),
                         m_bounds, m_meshes);
            }
        }
    }

    if (m_meshes.empty()) {
        m_error = "glTF had no triangle mesh primitives";
        LOG_WARN("M3D", "Load produced no meshes for %s", path.c_str());
        return false;
    }

    m_valid = true;
    LOG_INFO("M3D",
             "Loaded %s: %zu meshes (%zu skinned), %zu materials, %zu textures, "
             "%zu nodes, %zu skins, %zu animations, bbox=[%g,%g,%g]..[%g,%g,%g]",
             path.c_str(), m_meshes.size(),
             [this]() {
                 size_t c = 0;
                 for (const auto& m : m_meshes) if (m.skinIndex >= 0) ++c;
                 return c;
             }(),
             m_materials.size(), m_textures.size(),
             m_nodes.size(), m_skins.size(), m_animations.size(),
             m_bounds.minCoord[0], m_bounds.minCoord[1], m_bounds.minCoord[2],
             m_bounds.maxCoord[0], m_bounds.maxCoord[1], m_bounds.maxCoord[2]);
    return true;
}

} // namespace visual
} // namespace yawn

#else   // !YAWN_HAS_MODEL3D — inert stub so callers don't need #ifdefs

namespace yawn {
namespace visual {

void M3DModel::clear() {
    m_meshes.clear();
    m_materials.clear();
    m_textures.clear();
    m_nodes.clear();
    m_skins.clear();
    m_animations.clear();
    m_bounds = M3DBounds{};
    m_error.clear();
    m_valid = false;
}

bool M3DModel::load(const std::string& /*path*/) {
    clear();
    m_error = "3D model support disabled at build time (YAWN_HAS_MODEL3D=0)";
    return false;
}

} // namespace visual
} // namespace yawn

#endif // YAWN_HAS_MODEL3D
