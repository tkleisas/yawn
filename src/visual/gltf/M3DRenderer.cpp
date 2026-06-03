#include "visual/gltf/M3DRenderer.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D

namespace yawn {
namespace visual {

using m3d::Mat4;
using m3d::identity;
using m3d::multiply;
using m3d::translation;
using m3d::scale;
using m3d::eulerXYZDegrees;
using m3d::perspective;
using m3d::lookAt;

namespace {

// ── Shader source ─────────────────────────────────────────────────────────
//
// Lambert + ambient, plus per-instance tint / emissive / opacity so scene
// scripts can colour and glow individual instances. Full PBR / normal-map
// material support lands in a later phase.

// Two model-matrix paths, chosen by uInstanced:
//   0 → uModel/uMVP uniforms + per-draw instance uniforms (skinned/animated
//       and single-draw path — unchanged behaviour).
//   1 → per-instance vertex attributes (locations 5..10) + uViewProj, for
//       GPU-instanced static models (one draw for N copies).
constexpr const char* kVertexSrc =
    "#version 330 core\n"
    "layout(location=0)  in vec3  aPos;\n"
    "layout(location=1)  in vec3  aNormal;\n"
    "layout(location=2)  in vec2  aUV;\n"
    "layout(location=3)  in uvec4 aJoints;\n"
    "layout(location=4)  in vec4  aWeights;\n"
    "layout(location=5)  in mat4  aInstanceModel;\n"   // occupies 5,6,7,8
    "layout(location=9)  in vec4  aColorEmis;\n"        // rgb + emissive
    "layout(location=10) in float aOpacity;\n"
    "uniform mat4  uMVP;\n"
    "uniform mat4  uModel;\n"
    "uniform mat4  uViewProj;\n"
    "uniform int   uIsSkinned;\n"
    "uniform int   uInstanced;\n"
    "uniform mat4  uJointMatrices[128];\n"
    "uniform vec3  uInstanceColor;\n"
    "uniform float uInstanceEmissive;\n"
    "uniform float uInstanceOpacity;\n"
    "out vec3 vWorldNormal;\n"
    "out vec3 vWorldPos;\n"
    "out vec2 vUV;\n"
    "out vec4 vColorEmis;\n"
    "out float vOpacity;\n"
    "void main() {\n"
    "    vec4 local;\n"
    "    vec3 nrm;\n"
    "    if (uIsSkinned != 0) {\n"
    "        mat4 skin =\n"
    "            aWeights.x * uJointMatrices[int(aJoints.x)] +\n"
    "            aWeights.y * uJointMatrices[int(aJoints.y)] +\n"
    "            aWeights.z * uJointMatrices[int(aJoints.z)] +\n"
    "            aWeights.w * uJointMatrices[int(aJoints.w)];\n"
    "        local = skin * vec4(aPos, 1.0);\n"
    "        nrm   = mat3(skin) * aNormal;\n"
    "    } else {\n"
    "        local = vec4(aPos, 1.0);\n"
    "        nrm   = aNormal;\n"
    "    }\n"
    "    mat4 M = (uInstanced != 0) ? aInstanceModel : uModel;\n"
    "    vec4 world   = M * local;\n"
    "    vWorldPos    = world.xyz;\n"
    "    vWorldNormal = mat3(M) * nrm;\n"
    "    vUV          = aUV;\n"
    "    vColorEmis = (uInstanced != 0) ? aColorEmis\n"
    "                                   : vec4(uInstanceColor, uInstanceEmissive);\n"
    "    vOpacity   = (uInstanced != 0) ? aOpacity : uInstanceOpacity;\n"
    "    gl_Position = (uInstanced != 0) ? (uViewProj * world) : (uMVP * local);\n"
    "}\n";

// PBR-lite: base color + metallic/roughness (Blinn-Phong specular, view-
// dependent), emissive (factor × texture × strength), ambient occlusion,
// alpha MASK, plus per-instance tint/emissive/opacity. Normal mapping and
// a full Cook-Torrance BRDF are a later phase.
constexpr const char* kFragmentSrc =
    "#version 330 core\n"
    "in vec3 vWorldNormal;\n"
    "in vec3 vWorldPos;\n"
    "in vec2 vUV;\n"
    "in vec4 vColorEmis;\n"   // per-instance rgb + emissive
    "in float vOpacity;\n"
    "uniform vec4 uBaseColor;\n"
    "uniform sampler2D uBaseTex;       uniform int uHasTex;\n"
    "uniform float uMetallic;          uniform float uRoughness;\n"
    "uniform sampler2D uMRTex;         uniform int uHasMR;\n"
    "uniform vec3 uEmissiveFac;        uniform float uEmissiveStr;\n"
    "uniform sampler2D uEmissiveTex;   uniform int uHasEmissive;\n"
    "uniform sampler2D uOcclTex;       uniform int uHasOccl;  uniform float uOcclStr;\n"
    "uniform int uAlphaMask;           uniform float uAlphaCutoff;\n"
    "uniform vec3 uLightDir;           uniform vec3 uAmbient;  uniform float uLightInt;\n"
    "uniform vec3 uCameraPos;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 base = uBaseColor;\n"
    "    if (uHasTex != 0) base *= texture(uBaseTex, vUV);\n"
    "    base.rgb *= vColorEmis.rgb;\n"
    "    float metallic = uMetallic, rough = uRoughness;\n"
    "    if (uHasMR != 0) { vec4 mr = texture(uMRTex, vUV); rough *= mr.g; metallic *= mr.b; }\n"
    "    rough = clamp(rough, 0.04, 1.0);\n"
    "    float occ = 1.0;\n"
    "    if (uHasOccl != 0) occ = mix(1.0, texture(uOcclTex, vUV).r, uOcclStr);\n"
    "    vec3 emis = uEmissiveFac * uEmissiveStr;\n"
    "    if (uHasEmissive != 0) emis *= texture(uEmissiveTex, vUV).rgb;\n"
    "    vec3 N = normalize(vWorldNormal);\n"
    "    vec3 L = -normalize(uLightDir);\n"
    "    vec3 V = normalize(uCameraPos - vWorldPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float NdotL = max(dot(N, L), 0.0);\n"
    "    float NdotH = max(dot(N, H), 0.0);\n"
    "    // No IBL: keep full base diffuse (a (1-metallic) factor would render metals black).\n"
    "    vec3 diffuse = base.rgb;\n"
    "    vec3 specCol = mix(vec3(0.04), base.rgb, metallic);\n"
    "    float shin   = mix(8.0, 256.0, 1.0 - rough);\n"
    "    float spec   = (NdotL > 0.0) ? pow(NdotH, shin) * (1.0 - rough) : 0.0;\n"
    "    vec3 lit = (diffuse * NdotL + specCol * spec * (0.5 + metallic)) * uLightInt;\n"
    "    vec3 ambient = base.rgb * uAmbient * occ;\n"
    "    vec3 color = ambient + lit + emis + base.rgb * vColorEmis.a;\n"
    "    float alpha = base.a * vOpacity;\n"
    "    if (uAlphaMask != 0 && alpha < uAlphaCutoff) discard;\n"
    "    fragColor = vec4(color, alpha);\n"
    "}\n";

// ── Animation helpers ─────────────────────────────────────────────────────

void slerpQuat(const float a[4], const float b[4], float t, float out[4]) {
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    float dot = ax * bx + ay * by + az * bz + aw * bw;
    if (dot < 0.0f) { bx = -bx; by = -by; bz = -bz; bw = -bw; dot = -dot; }
    if (dot > 0.9995f) {
        out[0] = ax + t * (bx - ax);
        out[1] = ay + t * (by - ay);
        out[2] = az + t * (bz - az);
        out[3] = aw + t * (bw - aw);
    } else {
        float theta = std::acos(dot);
        float sinTheta = std::sin(theta);
        float s0 = std::sin((1.0f - t) * theta) / sinTheta;
        float s1 = std::sin(t * theta) / sinTheta;
        out[0] = s0 * ax + s1 * bx;
        out[1] = s0 * ay + s1 * by;
        out[2] = s0 * az + s1 * bz;
        out[3] = s0 * aw + s1 * bw;
    }
    float len = std::sqrt(out[0]*out[0] + out[1]*out[1] +
                           out[2]*out[2] + out[3]*out[3]);
    if (len > 1e-6f) { out[0]/=len; out[1]/=len; out[2]/=len; out[3]/=len; }
}

struct BracketResult { int lo, hi; float alpha; };
BracketResult bracketKeyframes(const std::vector<float>& times, float t) {
    BracketResult r{0, 0, 0.0f};
    if (times.empty()) return r;
    if (t <= times.front()) { r.lo = r.hi = 0; return r; }
    if (t >= times.back())  {
        r.lo = r.hi = static_cast<int>(times.size()) - 1;
        return r;
    }
    for (size_t i = 1; i < times.size(); ++i) {
        if (t < times[i]) {
            r.lo = static_cast<int>(i - 1);
            r.hi = static_cast<int>(i);
            float span = times[i] - times[i - 1];
            r.alpha = span > 1e-6f ? (t - times[i - 1]) / span : 0.0f;
            return r;
        }
    }
    return r;
}

GLuint compileShader(GLenum type, const char* src, const char* label) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        LOG_ERROR("M3D", "%s compile failed:\n%s", label, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof(log), &n, log);
        LOG_ERROR("M3D", "program link failed:\n%s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────

M3DRenderer::~M3DRenderer() { clear(); }

void M3DRenderer::clear() {
    for (auto& m : m_models) destroyModel(m);
    m_models.clear();
    if (m_instanceVBO) { glDeleteBuffers(1, &m_instanceVBO); m_instanceVBO = 0; }
    destroyProgram();
    destroyFBO();
}

void M3DRenderer::destroyProgram() {
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_locMVP = m_locModel = m_locBaseColor = m_locBaseTex =
        m_locHasTex = m_locLightDir = m_locAmbient = m_locIsSkinned =
        m_locJointMats = m_locInstColor = m_locInstEmissive =
        m_locInstOpacity = -1;
}

void M3DRenderer::destroyModel(Model& m) {
    for (auto& gm : m.meshes) {
        if (gm.vao) glDeleteVertexArrays(1, &gm.vao);
        if (gm.vbo) glDeleteBuffers(1, &gm.vbo);
        if (gm.ebo) glDeleteBuffers(1, &gm.ebo);
    }
    if (!m.textures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m.textures.size()),
                          m.textures.data());
    }
    m = Model{};
}

void M3DRenderer::destroyFBO() {
    if (m_fbo)      { glDeleteFramebuffers(1, &m_fbo);  m_fbo = 0; }
    if (m_colorTex) { glDeleteTextures(1, &m_colorTex); m_colorTex = 0; }
    if (m_depthRb)  { glDeleteRenderbuffers(1, &m_depthRb); m_depthRb = 0; }
}

bool M3DRenderer::init() {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   kVertexSrc,   "M3D vs");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentSrc, "M3D fs");
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    m_program = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!m_program) return false;

    m_locMVP          = glGetUniformLocation(m_program, "uMVP");
    m_locModel        = glGetUniformLocation(m_program, "uModel");
    m_locBaseColor    = glGetUniformLocation(m_program, "uBaseColor");
    m_locBaseTex      = glGetUniformLocation(m_program, "uBaseTex");
    m_locHasTex       = glGetUniformLocation(m_program, "uHasTex");
    m_locLightDir     = glGetUniformLocation(m_program, "uLightDir");
    m_locAmbient      = glGetUniformLocation(m_program, "uAmbient");
    m_locIsSkinned    = glGetUniformLocation(m_program, "uIsSkinned");
    m_locInstColor    = glGetUniformLocation(m_program, "uInstanceColor");
    m_locInstEmissive = glGetUniformLocation(m_program, "uInstanceEmissive");
    m_locInstOpacity  = glGetUniformLocation(m_program, "uInstanceOpacity");
    m_locMetallic     = glGetUniformLocation(m_program, "uMetallic");
    m_locRoughness    = glGetUniformLocation(m_program, "uRoughness");
    m_locMRTex        = glGetUniformLocation(m_program, "uMRTex");
    m_locHasMR        = glGetUniformLocation(m_program, "uHasMR");
    m_locEmissiveFac  = glGetUniformLocation(m_program, "uEmissiveFac");
    m_locEmissiveStr  = glGetUniformLocation(m_program, "uEmissiveStr");
    m_locEmissiveTex  = glGetUniformLocation(m_program, "uEmissiveTex");
    m_locHasEmissive  = glGetUniformLocation(m_program, "uHasEmissive");
    m_locOcclTex      = glGetUniformLocation(m_program, "uOcclTex");
    m_locHasOccl      = glGetUniformLocation(m_program, "uHasOccl");
    m_locOcclStr      = glGetUniformLocation(m_program, "uOcclStr");
    m_locAlphaMask    = glGetUniformLocation(m_program, "uAlphaMask");
    m_locAlphaCutoff  = glGetUniformLocation(m_program, "uAlphaCutoff");
    m_locCameraPos    = glGetUniformLocation(m_program, "uCameraPos");
    m_locLightInt     = glGetUniformLocation(m_program, "uLightInt");
    m_locViewProj     = glGetUniformLocation(m_program, "uViewProj");
    m_locInstanced    = glGetUniformLocation(m_program, "uInstanced");
    m_locJointMats    = glGetUniformLocation(m_program, "uJointMatrices[0]");
    if (m_locJointMats < 0) m_locJointMats =
        glGetUniformLocation(m_program, "uJointMatrices");

    // Shared per-instance attribute buffer for the instanced path. Seed
    // one identity element so the non-instanced path (which still has the
    // divisor-1 attributes bound in every VAO) reads valid data at index 0.
    glGenBuffers(1, &m_instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    {
        float seed[kInstanceFloats] = {0};
        seed[0] = seed[5] = seed[10] = seed[15] = 1.0f;   // identity model
        seed[16] = seed[17] = seed[18] = 1.0f;            // white color
        seed[19] = 0.0f;                                  // emissive
        seed[20] = 1.0f;                                  // opacity
        glBufferData(GL_ARRAY_BUFFER, sizeof(seed), seed, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &m_depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                           kWidth, kHeight);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, m_colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, m_depthRb);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("M3D", "FBO not complete: 0x%x", status);
        destroyFBO();
        destroyProgram();
        return false;
    }
    return true;
}

// ── Model upload ──────────────────────────────────────────────────────────

void M3DRenderer::uploadModel(const M3DModel& model, Model& dst) {
    if (!model.isValid()) return;

    dst.textures.resize(model.textureCount(), 0);
    for (int i = 0; i < model.textureCount(); ++i) {
        const auto& t = model.texture(i);
        if (t.width <= 0 || t.height <= 0 || t.rgba.empty()) continue;
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        dst.textures[i] = tex;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    dst.materials.resize(model.materialCount());
    for (int i = 0; i < model.materialCount(); ++i) {
        const auto& m  = model.material(i);
        auto&       gm = dst.materials[i];
        for (int c = 0; c < 4; ++c) gm.baseColor[c] = m.baseColorFactor[c];
        gm.texture          = m.baseColorTexture;
        gm.metallic         = m.metallicFactor;
        gm.roughness        = m.roughnessFactor;
        gm.mrTexture        = m.metallicRoughnessTexture;
        for (int c = 0; c < 3; ++c) gm.emissive[c] = m.emissiveFactor[c];
        gm.emissiveStrength = m.emissiveStrength;
        gm.emissiveTexture  = m.emissiveTexture;
        gm.occlTexture      = m.occlusionTexture;
        gm.occlStrength     = m.occlusionStrength;
        gm.alphaMask        = (m.alphaMode == M3DMaterial::AlphaMode::Mask) ? 1 : 0;
        gm.alphaCutoff      = m.alphaCutoff;
    }

    dst.meshes.resize(model.meshCount());
    for (int i = 0; i < model.meshCount(); ++i) {
        const auto& src = model.mesh(i);
        auto& gm = dst.meshes[i];

        glGenVertexArrays(1, &gm.vao);
        glBindVertexArray(gm.vao);
        glGenBuffers(1, &gm.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(src.vertices.size() * sizeof(M3DVertex)),
                      src.vertices.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &gm.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(src.indices.size() * sizeof(uint32_t)),
                      src.indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(M3DVertex),
                              reinterpret_cast<void*>(offsetof(M3DVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(M3DVertex),
                              reinterpret_cast<void*>(offsetof(M3DVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(M3DVertex),
                              reinterpret_cast<void*>(offsetof(M3DVertex, u)));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, sizeof(M3DVertex),
                               reinterpret_cast<void*>(offsetof(M3DVertex, joints)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(M3DVertex),
                              reinterpret_cast<void*>(offsetof(M3DVertex, weights)));

        // Per-instance attributes from the shared instance VBO (divisor 1):
        // locations 5..8 = mat4 model, 9 = vec4 (rgb + emissive), 10 = opacity.
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        const GLsizei iStride = kInstanceFloats * sizeof(float);
        for (int c = 0; c < 4; ++c) {
            glEnableVertexAttribArray(5 + c);
            glVertexAttribPointer(5 + c, 4, GL_FLOAT, GL_FALSE, iStride,
                reinterpret_cast<void*>(static_cast<uintptr_t>(c * 4 * sizeof(float))));
            glVertexAttribDivisor(5 + c, 1);
        }
        glEnableVertexAttribArray(9);
        glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, iStride,
            reinterpret_cast<void*>(static_cast<uintptr_t>(16 * sizeof(float))));
        glVertexAttribDivisor(9, 1);
        glEnableVertexAttribArray(10);
        glVertexAttribPointer(10, 1, GL_FLOAT, GL_FALSE, iStride,
            reinterpret_cast<void*>(static_cast<uintptr_t>(20 * sizeof(float))));
        glVertexAttribDivisor(10, 1);

        gm.indexCount    = static_cast<GLsizei>(src.indices.size());
        gm.materialIndex = src.materialIndex;
        gm.worldMatrix   = src.worldMatrix;
        gm.skinIndex     = src.skinIndex;
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    dst.nodes.assign(model.nodeCount(), M3DNode{});
    for (int i = 0; i < model.nodeCount(); ++i) dst.nodes[i] = model.node(i);
    dst.skins.assign(model.skinCount(), M3DSkin{});
    for (int i = 0; i < model.skinCount(); ++i) dst.skins[i] = model.skin(i);
    dst.animations.assign(model.animationCount(), M3DAnimation{});
    for (int i = 0; i < model.animationCount(); ++i)
        dst.animations[i] = model.animation(i);

    dst.pose.assign(dst.nodes.size(), NodePose{});
    for (size_t i = 0; i < dst.nodes.size(); ++i) {
        for (int k = 0; k < 3; ++k) dst.pose[i].translation[k] = dst.nodes[i].translation[k];
        for (int k = 0; k < 4; ++k) dst.pose[i].rotation[k]    = dst.nodes[i].rotation[k];
        for (int k = 0; k < 3; ++k) dst.pose[i].scale[k]       = dst.nodes[i].scale[k];
    }
    dst.jointMatrices.assign(dst.skins.size(),
                             std::vector<m3d::Mat4>(kMaxJoints, m3d::identity()));
    dst.activeClip = dst.animations.empty() ? -1 : 0;
    dst.evalClip = -2;
    dst.evalTime = -1e30f;

    // Normalize: centre at origin and scale the largest half-extent to
    // ~90% of the auto-camera's visible half-height. Each model is fit
    // independently so a scene script composes unit-sized pieces.
    const auto& b = model.bounds();
    if (b.valid) {
        dst.autoOffset[0] = -0.5f * (b.minCoord[0] + b.maxCoord[0]);
        dst.autoOffset[1] = -0.5f * (b.minCoord[1] + b.maxCoord[1]);
        dst.autoOffset[2] = -0.5f * (b.minCoord[2] + b.maxCoord[2]);
        float hx = 0.5f * (b.maxCoord[0] - b.minCoord[0]);
        float hy = 0.5f * (b.maxCoord[1] - b.minCoord[1]);
        float hz = 0.5f * (b.maxCoord[2] - b.minCoord[2]);
        float maxHalf = std::max(hx, std::max(hy, hz));
        if (maxHalf < 1e-6f) maxHalf = 1.0f;
        dst.autoScale = 0.93f / maxHalf;
    } else {
        dst.autoOffset[0] = dst.autoOffset[1] = dst.autoOffset[2] = 0.0f;
        dst.autoScale = 1.0f;
    }
}

void M3DRenderer::setModels(const std::vector<M3DModel>& models) {
    for (auto& m : m_models) destroyModel(m);
    m_models.clear();
    m_models.reserve(models.size());
    for (const auto& src : models) {
        Model m;
        uploadModel(src, m);
        m_models.push_back(std::move(m));
    }
}

void M3DRenderer::setModel(const M3DModel& model) {
    std::vector<M3DModel> one;
    one.push_back(model);   // copy — M3DModel is copyable
    setModels(one);
}

bool M3DRenderer::hasModel() const {
    return !m_models.empty() && !m_models[0].meshes.empty();
}

void M3DRenderer::setAnimationClip(int index) {
    if (m_models.empty()) return;
    auto& m = m_models[0];
    m.activeClip = (index < 0 || index >= static_cast<int>(m.animations.size()))
                   ? -1 : index;
    m.evalClip = -2;   // force re-eval
}
int M3DRenderer::animationClip() const {
    return m_models.empty() ? -1 : m_models[0].activeClip;
}
int M3DRenderer::animationCount() const {
    return m_models.empty() ? 0 : static_cast<int>(m_models[0].animations.size());
}
const std::string& M3DRenderer::animationName(int i) const {
    static const std::string kEmpty;
    if (m_models.empty() || i < 0 ||
        i >= static_cast<int>(m_models[0].animations.size())) return kEmpty;
    return m_models[0].animations[i].name;
}
float M3DRenderer::animationDuration(int i) const {
    if (m_models.empty() || i < 0 ||
        i >= static_cast<int>(m_models[0].animations.size())) return 0.0f;
    return m_models[0].animations[i].duration;
}

// ── Pose evaluation ─────────────────────────────────────────────────────────

void M3DRenderer::evaluatePose(Model& m, int clip, float animTime) {
    if (m.evalClip == clip && m.evalTime == animTime) return;   // cache hit
    m.evalClip = clip;
    m.evalTime = animTime;

    // Reset pose to the base TRS (idempotent sampler evaluation).
    for (size_t i = 0; i < m.pose.size(); ++i) {
        for (int k = 0; k < 3; ++k) m.pose[i].translation[k] = m.nodes[i].translation[k];
        for (int k = 0; k < 4; ++k) m.pose[i].rotation[k]    = m.nodes[i].rotation[k];
        for (int k = 0; k < 3; ++k) m.pose[i].scale[k]       = m.nodes[i].scale[k];
    }

    if (clip >= 0 && clip < static_cast<int>(m.animations.size())) {
        const auto& anim = m.animations[clip];
        float clipTime = animTime;
        if (anim.duration > 0.0f) {
            clipTime = std::fmod(animTime, anim.duration);
            if (clipTime < 0.0f) clipTime += anim.duration;
        }
        for (const auto& ch : anim.channels) {
            if (ch.targetNode < 0 ||
                ch.targetNode >= static_cast<int>(m.pose.size())) continue;
            if (ch.samplerIndex < 0 ||
                ch.samplerIndex >= static_cast<int>(anim.samplers.size())) continue;
            const auto& s = anim.samplers[ch.samplerIndex];
            if (s.inputTimes.empty()) continue;
            auto br = bracketKeyframes(s.inputTimes, clipTime);
            using Interp = M3DAnimationSampler::Interpolation;
            auto& pose = m.pose[ch.targetNode];
            switch (ch.path) {
                case M3DAnimationChannel::Path::Translation: {
                    const float* lo = s.outputValues.data() + br.lo * 3;
                    const float* hi = s.outputValues.data() + br.hi * 3;
                    float a = (s.interp == Interp::Step) ? 0.0f : br.alpha;
                    for (int k = 0; k < 3; ++k)
                        pose.translation[k] = lo[k] + a * (hi[k] - lo[k]);
                    break;
                }
                case M3DAnimationChannel::Path::Scale: {
                    const float* lo = s.outputValues.data() + br.lo * 3;
                    const float* hi = s.outputValues.data() + br.hi * 3;
                    float a = (s.interp == Interp::Step) ? 0.0f : br.alpha;
                    for (int k = 0; k < 3; ++k)
                        pose.scale[k] = lo[k] + a * (hi[k] - lo[k]);
                    break;
                }
                case M3DAnimationChannel::Path::Rotation: {
                    const float* lo = s.outputValues.data() + br.lo * 4;
                    const float* hi = s.outputValues.data() + br.hi * 4;
                    if (s.interp == Interp::Step) {
                        for (int k = 0; k < 4; ++k) pose.rotation[k] = lo[k];
                    } else {
                        slerpQuat(lo, hi, br.alpha, pose.rotation);
                    }
                    break;
                }
                case M3DAnimationChannel::Path::Weights:
                    break;
            }
        }
    }

    // Propagate pose → world matrices via the node tree.
    static thread_local std::vector<int> parent;
    parent.assign(m.nodes.size(), -1);
    for (size_t i = 0; i < m.nodes.size(); ++i)
        for (int c : m.nodes[i].children)
            if (c >= 0 && c < static_cast<int>(m.nodes.size()))
                parent[c] = static_cast<int>(i);

    auto localFromPose = [](const NodePose& p) {
        Mat4 T = translation(p.translation[0], p.translation[1], p.translation[2]);
        Mat4 R = m3d::quatToMatrix(p.rotation[0], p.rotation[1],
                                     p.rotation[2], p.rotation[3]);
        Mat4 S = scale(p.scale[0], p.scale[1], p.scale[2]);
        return multiply(T, multiply(R, S));
    };

    static thread_local std::vector<char> done;
    done.assign(m.nodes.size(), 0);
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            if (done[i]) continue;
            int p = parent[i];
            if (p < 0) {
                m.pose[i].world = localFromPose(m.pose[i]);
                done[i] = 1; progress = true;
            } else if (done[p]) {
                m.pose[i].world = multiply(m.pose[p].world, localFromPose(m.pose[i]));
                done[i] = 1; progress = true;
            }
        }
    }

    for (size_t si = 0; si < m.skins.size(); ++si) {
        const auto& skin = m.skins[si];
        auto& mats = m.jointMatrices[si];
        for (size_t j = 0; j < skin.joints.size() && j < kMaxJoints; ++j) {
            int node = skin.joints[j];
            if (node < 0 || node >= static_cast<int>(m.pose.size())) {
                mats[j] = m3d::identity();
                continue;
            }
            const auto& ibm = skin.inverseBindMatrices[j];
            Mat4 ibmMat;
            for (int k = 0; k < 16; ++k) ibmMat[k] = ibm[k];
            mats[j] = multiply(m.pose[node].world, ibmMat);
        }
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────

void M3DRenderer::render(const M3DInstance& inst) {
    beginFrame(0.0f);
    drawInstance(inst);
    endFrame();
}

void M3DRenderer::beginFrame(float animTime, const M3DCamera& cam) {
    if (!m_fbo || !m_program || m_models.empty()) return;
    m_frameAnimTime = animTime;

    // Camera → view/projection.
    Mat4 view, proj;
    const float aspect = static_cast<float>(kWidth) / kHeight;
    if (cam.explicitCam) {
        // Guard a degenerate look direction (camera on the up axis).
        float dir[3] = { cam.target[0] - cam.pos[0],
                         cam.target[1] - cam.pos[1],
                         cam.target[2] - cam.pos[2] };
        float dl = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        float upx = 0.0f, upy = 1.0f, upz = 0.0f;
        if (dl > 1e-6f) {
            float c = std::fabs(dir[1] / dl);
            if (c > 0.999f) { upx = 0.0f; upy = 0.0f; upz = 1.0f; }
        }
        float fov = (cam.fov > 1.0f && cam.fov < 179.0f) ? cam.fov : 50.0f;
        view = lookAt(cam.pos[0], cam.pos[1], cam.pos[2],
                      cam.target[0], cam.target[1], cam.target[2], upx, upy, upz);
        proj = perspective(fov * 3.14159265f / 180.0f, aspect, 0.05f, 200.0f);
        m_cameraEye[0] = cam.pos[0]; m_cameraEye[1] = cam.pos[1]; m_cameraEye[2] = cam.pos[2];
    } else {
        view = lookAt(0.0f, 0.0f, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        proj = perspective(45.0f * 3.14159265f / 180.0f, aspect, 0.1f, 100.0f);
        m_cameraEye[0] = 0.0f; m_cameraEye[1] = 0.0f; m_cameraEye[2] = 2.5f;
    }
    m_viewProj = multiply(proj, view);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, kWidth, kHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_program);
    if (m_locLightDir >= 0)
        glUniform3f(m_locLightDir, m_lightDir[0], m_lightDir[1], m_lightDir[2]);
    if (m_locAmbient  >= 0)
        glUniform3f(m_locAmbient, m_ambient[0], m_ambient[1], m_ambient[2]);
    if (m_locLightInt >= 0)  glUniform1f(m_locLightInt, m_lightIntensity);
    if (m_locCameraPos >= 0)
        glUniform3f(m_locCameraPos, m_cameraEye[0], m_cameraEye[1], m_cameraEye[2]);
    if (m_locViewProj >= 0)
        glUniformMatrix4fv(m_locViewProj, 1, GL_FALSE, m_viewProj.data());
    if (m_locInstanced >= 0) glUniform1i(m_locInstanced, 0);
}

void M3DRenderer::setLighting(const float dir[3], const float ambient[3],
                              float intensity) {
    for (int i = 0; i < 3; ++i) { m_lightDir[i] = dir[i]; m_ambient[i] = ambient[i]; }
    m_lightIntensity = intensity;
}

void M3DRenderer::drawInstance(const M3DInstance& inst) {
    if (!m_fbo || !m_program || m_models.empty()) return;

    int mi = inst.model;
    if (mi < 0 || mi >= static_cast<int>(m_models.size())) mi = 0;
    Model& mdl = m_models[mi];
    if (mdl.meshes.empty()) return;
    if (m_locInstanced >= 0) glUniform1i(m_locInstanced, 0);   // uniform path

    const int   clip = (inst.animClip >= 0) ? inst.animClip : mdl.activeClip;
    const float at   = (inst.animTime >= 0.0f) ? inst.animTime : m_frameAnimTime;
    evaluatePose(mdl, clip, at);

    Mat4 normMat = multiply(
        scale(mdl.autoScale, mdl.autoScale, mdl.autoScale),
        translation(mdl.autoOffset[0], mdl.autoOffset[1], mdl.autoOffset[2]));
    Mat4 userT = multiply(
        translation(inst.position[0], inst.position[1], inst.position[2]),
        multiply(
            eulerXYZDegrees(inst.rotationDeg[0], inst.rotationDeg[1], inst.rotationDeg[2]),
            scale(inst.scale * inst.scale3[0],
                  inst.scale * inst.scale3[1],
                  inst.scale * inst.scale3[2])));
    Mat4 outerModel = multiply(userT, normMat);

    if (m_locInstColor >= 0)
        glUniform3f(m_locInstColor, inst.color[0], inst.color[1], inst.color[2]);
    if (m_locInstEmissive >= 0) glUniform1f(m_locInstEmissive, inst.emissive);
    if (m_locInstOpacity  >= 0) glUniform1f(m_locInstOpacity,  inst.opacity);

    for (const auto& gm : mdl.meshes) {
        Mat4 model = outerModel;
        Mat4 mvp   = multiply(m_viewProj, model);
        if (m_locModel >= 0) glUniformMatrix4fv(m_locModel, 1, GL_FALSE, model.data());
        if (m_locMVP   >= 0) glUniformMatrix4fv(m_locMVP,   1, GL_FALSE, mvp.data());

        const bool skinned = (gm.skinIndex >= 0 &&
                              gm.skinIndex < static_cast<int>(mdl.jointMatrices.size()));
        if (m_locIsSkinned >= 0) glUniform1i(m_locIsSkinned, skinned ? 1 : 0);
        if (skinned && m_locJointMats >= 0) {
            const auto& mats = mdl.jointMatrices[gm.skinIndex];
            glUniformMatrix4fv(m_locJointMats, kMaxJoints, GL_FALSE,
                                reinterpret_cast<const float*>(mats.data()));
        }

        applyMaterial(mdl, gm);
        glBindVertexArray(gm.vao);
        glDrawElements(GL_TRIANGLES, gm.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glActiveTexture(GL_TEXTURE0);   // leave unit 0 active (renderer convention)
}

// Set the material uniforms + bind the 4 material textures for one mesh.
// Shared by the uniform (drawInstance) and instanced (drawInstances) paths.
void M3DRenderer::applyMaterial(const Model& mdl, const GLMesh& gm) {
    GLMaterial mat;   // defaults = plain white, fully rough dielectric
    if (gm.materialIndex >= 0 &&
        gm.materialIndex < static_cast<int>(mdl.materials.size()))
        mat = mdl.materials[gm.materialIndex];

    if (m_locBaseColor >= 0) glUniform4fv(m_locBaseColor, 1, mat.baseColor);
    if (m_locMetallic  >= 0) glUniform1f(m_locMetallic,  mat.metallic);
    if (m_locRoughness >= 0) glUniform1f(m_locRoughness, mat.roughness);
    if (m_locEmissiveFac >= 0)
        glUniform3f(m_locEmissiveFac, mat.emissive[0], mat.emissive[1], mat.emissive[2]);
    if (m_locEmissiveStr >= 0) glUniform1f(m_locEmissiveStr, mat.emissiveStrength);
    if (m_locOcclStr     >= 0) glUniform1f(m_locOcclStr, mat.occlStrength);
    if (m_locAlphaMask   >= 0) glUniform1i(m_locAlphaMask, mat.alphaMask);
    if (m_locAlphaCutoff >= 0) glUniform1f(m_locAlphaCutoff, mat.alphaCutoff);

    auto bindTex = [&](int idx, int unit, GLint sampLoc, GLint hasLoc) {
        GLuint tex = (idx >= 0 && idx < static_cast<int>(mdl.textures.size()))
                     ? mdl.textures[idx] : 0;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex);
        if (sampLoc >= 0) glUniform1i(sampLoc, unit);
        if (hasLoc  >= 0) glUniform1i(hasLoc,  tex ? 1 : 0);
    };
    bindTex(mat.texture,         0, m_locBaseTex,     m_locHasTex);
    bindTex(mat.mrTexture,       1, m_locMRTex,       m_locHasMR);
    bindTex(mat.emissiveTexture, 2, m_locEmissiveTex, m_locHasEmissive);
    bindTex(mat.occlTexture,     3, m_locOcclTex,     m_locHasOccl);
}

// Draw an instance list: static models batch into one glDrawElementsInstanced
// per mesh; skinned models fall back to the per-instance uniform path (their
// pose is per-instance, so they can't share an instance buffer).
void M3DRenderer::drawInstances(const std::vector<M3DInstance>& instances) {
    if (!m_fbo || !m_program || m_models.empty() || instances.empty()) return;

    std::vector<std::vector<const M3DInstance*>> byModel(m_models.size());
    for (const auto& in : instances) {
        int mi = in.model;
        if (mi < 0 || mi >= static_cast<int>(m_models.size())) mi = 0;
        byModel[mi].push_back(&in);
    }

    for (size_t mi = 0; mi < m_models.size(); ++mi) {
        auto& group = byModel[mi];
        if (group.empty()) continue;
        Model& mdl = m_models[mi];
        if (mdl.meshes.empty()) continue;

        bool hasSkin = false;
        for (const auto& gm : mdl.meshes)
            if (gm.skinIndex >= 0) { hasSkin = true; break; }

        if (hasSkin) {
            // Per-instance pose → no batching; use the uniform path.
            for (const auto* in : group) drawInstance(*in);
            continue;
        }

        // Static model → pack the group into the instance buffer and issue
        // one instanced draw per mesh.
        const Mat4 normMat = multiply(
            scale(mdl.autoScale, mdl.autoScale, mdl.autoScale),
            translation(mdl.autoOffset[0], mdl.autoOffset[1], mdl.autoOffset[2]));
        std::vector<float> buf;
        buf.reserve(group.size() * kInstanceFloats);
        for (const auto* in : group) {
            Mat4 userT = multiply(
                translation(in->position[0], in->position[1], in->position[2]),
                multiply(
                    eulerXYZDegrees(in->rotationDeg[0], in->rotationDeg[1], in->rotationDeg[2]),
                    scale(in->scale * in->scale3[0],
                          in->scale * in->scale3[1],
                          in->scale * in->scale3[2])));
            Mat4 m = multiply(userT, normMat);
            for (int k = 0; k < 16; ++k) buf.push_back(m[k]);
            buf.push_back(in->color[0]); buf.push_back(in->color[1]);
            buf.push_back(in->color[2]); buf.push_back(in->emissive);
            buf.push_back(in->opacity);
        }
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(buf.size() * sizeof(float)),
                     buf.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (m_locInstanced >= 0) glUniform1i(m_locInstanced, 1);
        if (m_locIsSkinned >= 0) glUniform1i(m_locIsSkinned, 0);
        const GLsizei n = static_cast<GLsizei>(group.size());
        for (const auto& gm : mdl.meshes) {
            applyMaterial(mdl, gm);
            glBindVertexArray(gm.vao);
            glDrawElementsInstanced(GL_TRIANGLES, gm.indexCount,
                                    GL_UNSIGNED_INT, nullptr, n);
        }
        if (m_locInstanced >= 0) glUniform1i(m_locInstanced, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

void M3DRenderer::endFrame() {
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

} // namespace visual
} // namespace yawn

#else  // !YAWN_HAS_MODEL3D — inert stubs so the build stays green

namespace yawn {
namespace visual {

M3DRenderer::~M3DRenderer() {}
void M3DRenderer::clear() {}
void M3DRenderer::destroyProgram() {}
void M3DRenderer::destroyFBO()     {}
void M3DRenderer::destroyModel(Model&) {}
void M3DRenderer::uploadModel(const M3DModel&, Model&) {}
void M3DRenderer::evaluatePose(Model&, int, float) {}
bool M3DRenderer::init() { return false; }
void M3DRenderer::setModels(const std::vector<M3DModel>&) {}
void M3DRenderer::setModel(const M3DModel&) {}
void M3DRenderer::render(const M3DInstance&) {}
void M3DRenderer::beginFrame(float, const M3DCamera&) {}
void M3DRenderer::drawInstance(const M3DInstance&) {}
void M3DRenderer::drawInstances(const std::vector<M3DInstance>&) {}
void M3DRenderer::applyMaterial(const Model&, const GLMesh&) {}
void M3DRenderer::endFrame() {}
void M3DRenderer::setLighting(const float[3], const float[3], float) {}
bool M3DRenderer::hasModel() const { return false; }
void M3DRenderer::setAnimationClip(int) {}
int  M3DRenderer::animationClip()  const { return -1; }
int  M3DRenderer::animationCount() const { return 0; }
const std::string& M3DRenderer::animationName(int) const {
    static const std::string kEmpty; return kEmpty;
}
float M3DRenderer::animationDuration(int) const { return 0.0f; }

} // namespace visual
} // namespace yawn

#endif
