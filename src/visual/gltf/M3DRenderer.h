#pragma once

// M3DRenderer — GL-side renderer that draws an M3DModel into an offscreen
// 640×360 RGBA8 + depth FBO. Its color attachment is the texture that
// VisualEngine::Layer binds as iChannel2, so every existing Shadertoy
// effect keeps working on 3D output just like it does on file/live video.
//
// Separate TU from M3DModel so yawn_core (no GL) stays clean; this file
// only compiles into the main exe.
//
// Lifetime: one M3DRenderer per layer. init() must be called with the
// output window's GL context current. setModel() uploads mesh data and
// texture images to the GPU. render() draws the model into the FBO at
// the given Transform. clear() tears down all GL state.

#include "visual/gltf/M3DModel.h"
#include "visual/gltf/M3DMath.h"
#include "visual/gltf/M3DTransform.h"

#include <glad/gl.h>
#include <cstdint>
#include <string>
#include <vector>

namespace yawn {
namespace visual {

class M3DRenderer {
public:
    static constexpr int kWidth  = 640;
    static constexpr int kHeight = 360;

    // Alias for legacy spellings inside the renderer body. New code
    // should use yawn::visual::M3DTransform directly.
    using Transform = M3DTransform;

    M3DRenderer() = default;
    ~M3DRenderer();

    M3DRenderer(const M3DRenderer&) = delete;
    M3DRenderer& operator=(const M3DRenderer&) = delete;

    // Build the FBO + shader program. Returns false if either step fails
    // (compile log goes to the YAWN logger). GL context must be current.
    bool init();

    // Upload mesh data + textures from `model` to the GPU. Any previously
    // loaded model is released first. Safe to call every time the clip's
    // modelPath changes. Also derives a default camera pose that frames
    // the model's bounding box with ~15% margin.
    void setModel(const M3DModel& model);

    // Tear down every GL object this renderer owns.
    void clear();

    // Convenience: single-transform render. Equivalent to
    //   beginFrame(); drawInstance(xf); endFrame();
    void render(const Transform& xf);

    // Multi-instance API for scene scripts that want to draw the same
    // model multiple times per frame. Call beginFrame() once, then
    // drawInstance() for each transform, then endFrame(). The FBO is
    // cleared in beginFrame() only, so successive drawInstance() calls
    // composite with the accumulated depth buffer — occluders Just Work.
    //
    // `animTime` advances the skeletal animation clock; pass 0 for
    // static models or scenes that don't want animation. It wraps
    // within the active clip's duration so callers can feed wall-time
    // directly.
    void beginFrame(float animTime = 0.0f);
    void drawInstance(const Transform& xf);
    void endFrame();

    // Pick which animation clip to play (0..model.animationCount()-1).
    // Out-of-range indices disable animation (fall back to bind pose).
    void setAnimationClip(int index);
    int  animationClip() const { return m_activeClip; }
    int  animationCount() const { return static_cast<int>(m_animations.size()); }
    const std::string& animationName(int i) const { return m_animations[i].name; }
    float animationDuration(int i) const { return m_animations[i].duration; }

    // The color attachment — consumers bind this to iChannel2. Zero
    // until init() succeeds.
    GLuint colorTexture() const { return m_colorTex; }

    bool hasModel() const { return !m_meshes.empty(); }

private:
    void destroyProgram();
    void destroyMeshes();
    void destroyTextures();
    void destroyFBO();

    struct GLMesh {
        GLuint vao        = 0;
        GLuint vbo        = 0;
        GLuint ebo        = 0;
        GLsizei indexCount = 0;
        int materialIndex = -1;
        m3d::Mat4 worldMatrix = m3d::identity();
        int skinIndex     = -1;   // -1 = static, else index into m_skins
    };

    struct GLMaterial {
        float baseColor[4] = { 1, 1, 1, 1 };
        int   texture      = -1;   // into m_textures, -1 = none
    };

    // Upper bound on joints per skin that our skinning shader can
    // handle. 128 covers essentially every VJ-relevant character rig;
    // the uniform array cost is 128 × mat4 = 2048 floats, well under
    // the standard 4096-float max uniform size.
    static constexpr int kMaxJoints = 128;

    // FBO + attachments.
    GLuint m_fbo      = 0;
    GLuint m_colorTex = 0;
    GLuint m_depthRb  = 0;

    // Shader program + uniform locations.
    GLuint m_program  = 0;
    GLint  m_locMVP   = -1;
    GLint  m_locModel = -1;
    GLint  m_locBaseColor = -1;
    GLint  m_locBaseTex   = -1;
    GLint  m_locHasTex    = -1;
    GLint  m_locLightDir  = -1;
    GLint  m_locAmbient   = -1;
    GLint  m_locIsSkinned = -1;
    GLint  m_locJointMats = -1;

    std::vector<GLMesh>          m_meshes;
    std::vector<GLMaterial>      m_materials;
    std::vector<GLuint>          m_textures;
    // Skeletal data — kept CPU-side so beginFrame() can evaluate the
    // animation and recompute joint matrices each frame.
    std::vector<M3DNode>         m_nodes;
    std::vector<M3DSkin>         m_skins;
    std::vector<M3DAnimation>    m_animations;
    // Live pose (per node): TRS currently in effect this frame.
    struct NodePose {
        float translation[3] = { 0, 0, 0 };
        float rotation[4]    = { 0, 0, 0, 1 };
        float scale[3]       = { 1, 1, 1 };
        m3d::Mat4 world      = m3d::identity();
    };
    std::vector<NodePose>        m_pose;
    // Scratch joint matrices computed in beginFrame — indexed per skin.
    std::vector<std::vector<m3d::Mat4>> m_jointMatrices;

    int                          m_activeClip = 0;

    // Normalization baked at setModel() time: recenters the model to
    // the origin and scales it to fit a unit sphere. Keeps the render
    // well-behaved regardless of how the glTF was authored (models in
    // the sample-asset set span 10⁴× in native size — Duck is ~0.03,
    // DamagedHelmet ~4.0 — so we can't just rely on one camera pose).
    float m_autoOffset[3] = { 0.0f, 0.0f, 0.0f };
    float m_autoScale     = 1.0f;
};

} // namespace visual
} // namespace yawn
