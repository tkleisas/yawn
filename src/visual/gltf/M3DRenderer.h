#pragma once

// M3DRenderer — GL-side renderer that draws one or more M3DModels into an
// offscreen 640×360 RGBA8 + depth FBO. Its color attachment is the texture
// that VisualEngine::Layer binds as iChannel2, so every existing Shadertoy
// effect keeps working on 3D output just like it does on file/live video.
//
// Separate TU from M3DModel so yawn_core (no GL) stays clean; this file
// only compiles into the main exe.
//
// Lifetime: one M3DRenderer per layer. init() must be called with the
// output window's GL context current. setModels() uploads a list of models
// to the GPU (instances pick among them by index). render()/drawInstance()
// draw into the FBO. clear() tears down all GL state.

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

    // Legacy alias — older call sites spelled the pose type "Transform".
    using Transform = M3DTransform;

    M3DRenderer() = default;
    ~M3DRenderer();

    M3DRenderer(const M3DRenderer&) = delete;
    M3DRenderer& operator=(const M3DRenderer&) = delete;

    // Build the FBO + shader program. Returns false if either step fails
    // (compile log goes to the YAWN logger). GL context must be current.
    bool init();

    // Upload a list of models to the GPU (any previously loaded models are
    // released first). Instances select among them via M3DInstance::model.
    // Each model is normalized independently to ~unit size centred at the
    // origin, so a scene script composes them by placing/scaling instances.
    void setModels(const std::vector<M3DModel>& models);
    // Convenience: a single model (the common, non-scene case).
    void setModel(const M3DModel& model);

    // Tear down every GL object this renderer owns.
    void clear();

    // Convenience: single-instance render (model 0). Equivalent to
    //   beginFrame(); drawInstance(inst); endFrame();
    void render(const M3DInstance& inst);

    // Multi-instance API. Call beginFrame() once, then drawInstance() per
    // instance, then endFrame(). The FBO is cleared in beginFrame() only,
    // so successive draws composite against the shared depth buffer.
    //
    // `animTime` is the default skeletal-animation clock (seconds); an
    // instance can override it per-draw via M3DInstance::animTime. `cam`
    // selects the camera — auto-frame when cam.explicitCam is false.
    void beginFrame(float animTime = 0.0f, const M3DCamera& cam = M3DCamera{});
    void drawInstance(const M3DInstance& inst);
    void endFrame();

    // Animation-clip selection for model 0 (the simple single-model case).
    // Per-instance clips for multi-model scenes go through M3DInstance.
    void setAnimationClip(int index);
    int  animationClip()  const;
    int  animationCount() const;     // model 0
    const std::string& animationName(int i) const;
    float animationDuration(int i)   const;

    // The color attachment — consumers bind this to iChannel2. Zero until
    // init() succeeds.
    GLuint colorTexture() const { return m_colorTex; }

    bool hasModel()  const;
    int  modelCount() const { return static_cast<int>(m_models.size()); }

private:
    void destroyProgram();
    void destroyFBO();

    struct GLMesh {
        GLuint vao        = 0;
        GLuint vbo        = 0;
        GLuint ebo        = 0;
        GLsizei indexCount = 0;
        int materialIndex = -1;
        m3d::Mat4 worldMatrix = m3d::identity();
        int skinIndex     = -1;   // -1 = static, else index into the model's skins
    };

    struct GLMaterial {
        float baseColor[4] = { 1, 1, 1, 1 };
        int   texture      = -1;   // base color, into the model's textures
        float metallic     = 1.0f;
        float roughness    = 1.0f;
        int   mrTexture    = -1;   // metallic(B)/roughness(G)
        float emissive[3]  = { 0, 0, 0 };
        float emissiveStrength = 1.0f;
        int   emissiveTexture  = -1;
        int   occlTexture  = -1;   // ambient occlusion (R)
        float occlStrength = 1.0f;
        int   alphaMask    = 0;    // 1 = MASK (discard below cutoff)
        float alphaCutoff  = 0.5f;
    };

    // Live pose (per node): TRS currently in effect this frame.
    struct NodePose {
        float translation[3] = { 0, 0, 0 };
        float rotation[4]    = { 0, 0, 0, 1 };
        float scale[3]       = { 1, 1, 1 };
        m3d::Mat4 world      = m3d::identity();
    };

    // One uploaded model: GL buffers + CPU-side skeletal data (kept so the
    // animation can be re-evaluated per frame / per instance) + the
    // per-model normalization baked at upload time.
    struct Model {
        std::vector<GLMesh>       meshes;
        std::vector<GLMaterial>   materials;
        std::vector<GLuint>       textures;
        std::vector<M3DNode>      nodes;
        std::vector<M3DSkin>      skins;
        std::vector<M3DAnimation> animations;
        std::vector<NodePose>     pose;
        std::vector<std::vector<m3d::Mat4>> jointMatrices;
        int   activeClip = -1;
        float autoOffset[3] = { 0, 0, 0 };
        float autoScale     = 1.0f;
        // Cache of the last (clip, time) the pose was evaluated for, so a
        // run of instances sharing the default clip/time only pays once.
        int   evalClip = -2;
        float evalTime = -1e30f;
    };

    static constexpr int kMaxJoints = 128;

    void uploadModel(const M3DModel& src, Model& dst);
    void destroyModel(Model& m);
    // Evaluate `m`'s pose + joint matrices for (clip, animTime), skipping
    // the work if it matches the cached evaluation.
    void evaluatePose(Model& m, int clip, float animTime);

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
    GLint  m_locInstColor    = -1;
    GLint  m_locInstEmissive = -1;
    GLint  m_locInstOpacity  = -1;
    // PBR-lite material + lighting uniforms.
    GLint  m_locMetallic     = -1;
    GLint  m_locRoughness    = -1;
    GLint  m_locMRTex        = -1;
    GLint  m_locHasMR        = -1;
    GLint  m_locEmissiveFac  = -1;
    GLint  m_locEmissiveStr  = -1;
    GLint  m_locEmissiveTex  = -1;
    GLint  m_locHasEmissive  = -1;
    GLint  m_locOcclTex      = -1;
    GLint  m_locHasOccl      = -1;
    GLint  m_locOcclStr      = -1;
    GLint  m_locAlphaMask    = -1;
    GLint  m_locAlphaCutoff  = -1;
    GLint  m_locCameraPos    = -1;
    GLint  m_locLightInt     = -1;

    std::vector<Model> m_models;

    // Per-frame state set in beginFrame().
    m3d::Mat4 m_viewProj    = m3d::identity();
    float     m_frameAnimTime = 0.0f;
    float     m_cameraEye[3] = { 0.0f, 0.0f, 2.5f };

    // Lighting rig (settable via setLighting; defaults match the
    // historical fixed look). Direction points *toward* the scene.
    float m_lightDir[3]   = { -0.5f, -0.7f, -0.5f };
    float m_ambient[3]    = {  0.2f,  0.2f,  0.22f };
    float m_lightIntensity = 1.0f;

public:
    // Configure the directional light + ambient + intensity for the next
    // frame(s). Called by the engine from @range light uniforms.
    void setLighting(const float dir[3], const float ambient[3],
                     float intensity);
};

} // namespace visual
} // namespace yawn
