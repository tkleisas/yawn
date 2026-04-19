#pragma once

// M3DModel — CPU-side glTF 2.0 loader.
//
// Parses a .gltf / .glb into a flat list of meshes (positions + normals +
// texcoords + indices), materials (base color factor + optional base
// color texture), and decoded RGBA8 textures. Node hierarchy is baked
// into each mesh's world matrix so the renderer can blit the list without
// walking the tree every frame.
//
// Skeletal / animation data is intentionally NOT consumed here — that
// lands with G.7. The loader reads what's needed for a static draw pass
// and ignores skin/animation blocks.
//
// No GL dependencies — safe for yawn_core. The GL-side consumer is
// M3DRenderer (G.3).
//
// Gated on YAWN_HAS_MODEL3D. When the macro is 0 the class still exists
// but load() always returns false with a "3D disabled" error, so callers
// don't need to #ifdef at every call site.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace yawn {
namespace visual {

// Interleaved vertex layout. Renderer uploads this as a single VBO with
// five attribute bindings (stride = sizeof(M3DVertex)). Skinning data
// (joints + weights) is all zeros on meshes without JOINTS_0/WEIGHTS_0
// — the renderer picks the shader variant based on the mesh's `skin`
// index, so the cost is just four dead attributes per static vertex.
struct M3DVertex {
    float    px, py, pz;               // position
    float    nx, ny, nz;               // normal
    float    u,  v;                    // texcoord (TEXCOORD_0)
    uint16_t joints[4]  = { 0, 0, 0, 0 };
    float    weights[4] = { 0, 0, 0, 0 };
};

struct M3DMesh {
    std::vector<M3DVertex> vertices;
    std::vector<uint32_t>  indices;
    int                    materialIndex = -1;  // into M3DModel::materials
    // For unrigged meshes this is the baked hierarchy transform applied
    // at vertex-bake time (renderer treats the VBO as world-space and
    // only layers the user transform on top — see the G.4/G.5 path).
    // For rigged meshes (skinIndex >= 0) vertices stay in mesh-local
    // space; the hierarchy gets reapplied via the joint matrices.
    std::array<float, 16>  worldMatrix{};
    int                    skinIndex = -1;      // into M3DModel::skins
};

// Scene-graph node. Preserved from glTF so per-frame animation channels
// can mutate translation/rotation/scale without losing the baseline.
struct M3DNode {
    std::string name;
    int         mesh = -1;                              // mesh index, or -1
    int         skin = -1;                              // skin index, or -1
    std::vector<int> children;                           // node indices
    // Base TRS from the glTF. Animation channels overwrite these each
    // frame; the renderer walks the tree to derive world transforms.
    float translation[3] = { 0.0f, 0.0f, 0.0f };
    float rotation[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };   // quaternion (xyzw)
    float scale[3]       = { 1.0f, 1.0f, 1.0f };
};

// Skin: ordered list of joint nodes + one inverse-bind matrix per
// joint. Per-vertex joint indices in M3DVertex refer into this list.
struct M3DSkin {
    std::vector<int>                   joints;              // node indices
    std::vector<std::array<float, 16>> inverseBindMatrices; // column-major
    int                                skeletonRoot = -1;   // optional root
};

// Animation sampler: keyframe times + output values, with an
// interpolation rule. Output layout depends on the target path
// (translation/scale = vec3 per frame; rotation = vec4 quaternion).
struct M3DAnimationSampler {
    enum class Interpolation { Step, Linear, CubicSpline };
    Interpolation       interp = Interpolation::Linear;
    std::vector<float>  inputTimes;   // seconds
    std::vector<float>  outputValues;
};

// Animation channel: which node/path this sampler drives.
struct M3DAnimationChannel {
    enum class Path { Translation, Rotation, Scale, Weights };
    int  targetNode = -1;
    Path path       = Path::Translation;
    int  samplerIndex = -1;
};

struct M3DAnimation {
    std::string                       name;
    std::vector<M3DAnimationSampler>  samplers;
    std::vector<M3DAnimationChannel>  channels;
    float                             duration = 0.0f;   // seconds
};

struct M3DTexture {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> rgba;   // width*height*4 bytes, RGBA8
};

struct M3DMaterial {
    std::array<float, 4> baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    int                  baseColorTexture = -1;  // into M3DModel::textures
};

// Axis-aligned bounds computed from the world-space vertex positions of
// every mesh combined. Used by the renderer's auto-framing camera.
struct M3DBounds {
    float minCoord[3]{  1e30f,  1e30f,  1e30f };
    float maxCoord[3]{ -1e30f, -1e30f, -1e30f };
    bool  valid = false;   // false if the model had no vertices
};

class M3DModel {
public:
    M3DModel() { for (int i = 0; i < 16; ++i) { /* no-op */ } }

    // Parse a .gltf or .glb. Format is chosen from the extension (case
    // insensitive). Returns true on success; on failure, error() is set
    // and the object is empty. Safe to call multiple times — each call
    // clears previous state.
    bool load(const std::string& path);

    bool isValid()       const { return m_valid; }
    const std::string& error() const { return m_error; }

    int meshCount()      const { return static_cast<int>(m_meshes.size()); }
    int materialCount()  const { return static_cast<int>(m_materials.size()); }
    int textureCount()   const { return static_cast<int>(m_textures.size()); }
    int nodeCount()      const { return static_cast<int>(m_nodes.size()); }
    int skinCount()      const { return static_cast<int>(m_skins.size()); }
    int animationCount() const { return static_cast<int>(m_animations.size()); }

    const M3DMesh&      mesh(int i)      const { return m_meshes[i]; }
    const M3DMaterial&  material(int i)  const { return m_materials[i]; }
    const M3DTexture&   texture(int i)   const { return m_textures[i]; }
    const M3DNode&      node(int i)      const { return m_nodes[i]; }
    const M3DSkin&      skin(int i)      const { return m_skins[i]; }
    const M3DAnimation& animation(int i) const { return m_animations[i]; }
    const M3DBounds&    bounds()         const { return m_bounds; }

private:
    void clear();

    std::vector<M3DMesh>      m_meshes;
    std::vector<M3DMaterial>  m_materials;
    std::vector<M3DTexture>   m_textures;
    std::vector<M3DNode>      m_nodes;
    std::vector<M3DSkin>      m_skins;
    std::vector<M3DAnimation> m_animations;
    M3DBounds                 m_bounds;
    std::string               m_error;
    bool                      m_valid = false;
};

} // namespace visual
} // namespace yawn
