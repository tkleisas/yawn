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
// Minimal lit material: Lambert + ambient. Per-vertex world transform so
// we can apply the user's Transform on top of the mesh's baked-in
// hierarchy transform without re-uploading the VBO.

// Vertex shader supports both static (uIsSkinned=0) and skinned
// (uIsSkinned=1) meshes. For skinned meshes, aPos/aNormal are in
// mesh-LOCAL space and the joint-matrix blend places the vertex where
// the animation wants it; the outer uMVP then handles camera + user
// transform on top. Static meshes have their hierarchy already baked
// into aPos and skip the joint blend entirely.
constexpr const char* kVertexSrc =
    "#version 330 core\n"
    "layout(location=0) in vec3  aPos;\n"
    "layout(location=1) in vec3  aNormal;\n"
    "layout(location=2) in vec2  aUV;\n"
    "layout(location=3) in uvec4 aJoints;\n"
    "layout(location=4) in vec4  aWeights;\n"
    "uniform mat4  uMVP;\n"
    "uniform mat4  uModel;\n"
    "uniform int   uIsSkinned;\n"
    "uniform mat4  uJointMatrices[128];\n"
    "out vec3 vWorldNormal;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    if (uIsSkinned != 0) {\n"
    "        mat4 skin =\n"
    "            aWeights.x * uJointMatrices[int(aJoints.x)] +\n"
    "            aWeights.y * uJointMatrices[int(aJoints.y)] +\n"
    "            aWeights.z * uJointMatrices[int(aJoints.z)] +\n"
    "            aWeights.w * uJointMatrices[int(aJoints.w)];\n"
    "        vec4 posed = skin * vec4(aPos, 1.0);\n"
    "        vWorldNormal = mat3(uModel) * (mat3(skin) * aNormal);\n"
    "        gl_Position  = uMVP * posed;\n"
    "    } else {\n"
    "        vWorldNormal = mat3(uModel) * aNormal;\n"
    "        gl_Position  = uMVP * vec4(aPos, 1.0);\n"
    "    }\n"
    "    vUV = aUV;\n"
    "}\n";

constexpr const char* kFragmentSrc =
    "#version 330 core\n"
    "in vec3 vWorldNormal;\n"
    "in vec2 vUV;\n"
    "uniform vec4 uBaseColor;\n"
    "uniform sampler2D uBaseTex;\n"
    "uniform int uHasTex;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uAmbient;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(vWorldNormal);\n"
    "    float diff = max(dot(n, -normalize(uLightDir)), 0.0);\n"
    "    vec4 base = uBaseColor;\n"
    "    if (uHasTex != 0) base *= texture(uBaseTex, vUV);\n"
    "    vec3 col = base.rgb * (diff + uAmbient);\n"
    "    fragColor = vec4(col, base.a);\n"
    "}\n";

// ── Animation helpers ─────────────────────────────────────────────────────

// Spherical linear interpolation between unit quaternions. Falls back
// to nlerp on near-parallel pairs where sinθ is ill-conditioned.
void slerpQuat(const float a[4], const float b[4], float t, float out[4]) {
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    float dot = ax * bx + ay * by + az * bz + aw * bw;
    // Shortest-arc correction.
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

// Find the two keyframes bracketing `t` inside a sampler's input
// array. Returns { lo, hi, alpha } where alpha is the interpolation
// factor in [0,1]. Edge-clamps to the endpoints outside the range.
struct BracketResult { int lo, hi; float alpha; };
BracketResult bracketKeyframes(const std::vector<float>& times, float t) {
    BracketResult r{0, 0, 0.0f};
    if (times.empty()) return r;
    if (t <= times.front()) { r.lo = r.hi = 0; return r; }
    if (t >= times.back())  {
        r.lo = r.hi = static_cast<int>(times.size()) - 1;
        return r;
    }
    // Linear search — keyframe counts per channel are typically small
    // (tens), so binary-search overhead isn't worth it.
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
    destroyMeshes();
    destroyTextures();
    destroyProgram();
    destroyFBO();
}

void M3DRenderer::destroyProgram() {
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_locMVP = m_locModel = m_locBaseColor = m_locBaseTex =
        m_locHasTex = m_locLightDir = m_locAmbient = -1;
}

void M3DRenderer::destroyMeshes() {
    for (auto& m : m_meshes) {
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ebo) glDeleteBuffers(1, &m.ebo);
    }
    m_meshes.clear();
    m_materials.clear();
    m_nodes.clear();
    m_skins.clear();
    m_animations.clear();
    m_pose.clear();
    m_jointMatrices.clear();
    m_activeClip = -1;
}

void M3DRenderer::destroyTextures() {
    if (!m_textures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_textures.size()),
                          m_textures.data());
        m_textures.clear();
    }
}

void M3DRenderer::destroyFBO() {
    if (m_fbo)      { glDeleteFramebuffers(1, &m_fbo);  m_fbo = 0; }
    if (m_colorTex) { glDeleteTextures(1, &m_colorTex); m_colorTex = 0; }
    if (m_depthRb)  { glDeleteRenderbuffers(1, &m_depthRb); m_depthRb = 0; }
}

bool M3DRenderer::init() {
    // Shader.
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

    m_locMVP       = glGetUniformLocation(m_program, "uMVP");
    m_locModel     = glGetUniformLocation(m_program, "uModel");
    m_locBaseColor = glGetUniformLocation(m_program, "uBaseColor");
    m_locBaseTex   = glGetUniformLocation(m_program, "uBaseTex");
    m_locHasTex    = glGetUniformLocation(m_program, "uHasTex");
    m_locLightDir  = glGetUniformLocation(m_program, "uLightDir");
    m_locAmbient   = glGetUniformLocation(m_program, "uAmbient");
    m_locIsSkinned = glGetUniformLocation(m_program, "uIsSkinned");
    m_locJointMats = glGetUniformLocation(m_program, "uJointMatrices[0]");
    if (m_locJointMats < 0) m_locJointMats =
        glGetUniformLocation(m_program, "uJointMatrices");

    // FBO with color + depth attachments. Depth as a renderbuffer (we
    // never sample it), color as a texture so the outer shader pipeline
    // can bind it as iChannel2.
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

void M3DRenderer::setModel(const M3DModel& model) {
    destroyMeshes();
    destroyTextures();
    if (!model.isValid()) return;

    // Textures — plain RGBA8, linear filtering, clamped. Mipmaps skipped
    // to keep upload cheap; re-enable later if we hit aliasing issues on
    // minified glyphs.
    m_textures.resize(model.textureCount(), 0);
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
        m_textures[i] = tex;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Materials.
    m_materials.resize(model.materialCount());
    for (int i = 0; i < model.materialCount(); ++i) {
        const auto& m = model.material(i);
        for (int c = 0; c < 4; ++c) m_materials[i].baseColor[c] = m.baseColorFactor[c];
        m_materials[i].texture = m.baseColorTexture;
    }

    // Meshes — upload each to its own VAO/VBO/EBO.
    m_meshes.resize(model.meshCount());
    for (int i = 0; i < model.meshCount(); ++i) {
        const auto& src = model.mesh(i);
        auto& gm = m_meshes[i];

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

        // pos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                sizeof(M3DVertex),
                                reinterpret_cast<void*>(offsetof(M3DVertex, px)));
        // normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                sizeof(M3DVertex),
                                reinterpret_cast<void*>(offsetof(M3DVertex, nx)));
        // uv
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                sizeof(M3DVertex),
                                reinterpret_cast<void*>(offsetof(M3DVertex, u)));
        // joints (uvec4) — integer attribute, bound with IPointer.
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT,
                                sizeof(M3DVertex),
                                reinterpret_cast<void*>(offsetof(M3DVertex, joints)));
        // weights (vec4)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE,
                                sizeof(M3DVertex),
                                reinterpret_cast<void*>(offsetof(M3DVertex, weights)));

        gm.indexCount    = static_cast<GLsizei>(src.indices.size());
        gm.materialIndex = src.materialIndex;
        gm.worldMatrix   = src.worldMatrix;
        gm.skinIndex     = src.skinIndex;
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Copy skeletal data CPU-side so beginFrame() can evaluate the
    // animation and refresh joint matrices each tick. The M3DModel
    // itself is discarded after setModel() — everything the renderer
    // needs per-frame lives here.
    m_nodes.assign(model.nodeCount(), M3DNode{});
    for (int i = 0; i < model.nodeCount(); ++i) m_nodes[i] = model.node(i);
    m_skins.assign(model.skinCount(), M3DSkin{});
    for (int i = 0; i < model.skinCount(); ++i) m_skins[i] = model.skin(i);
    m_animations.assign(model.animationCount(), M3DAnimation{});
    for (int i = 0; i < model.animationCount(); ++i)
        m_animations[i] = model.animation(i);

    // Seed the pose from each node's base TRS.
    m_pose.assign(m_nodes.size(), NodePose{});
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        for (int k = 0; k < 3; ++k) m_pose[i].translation[k] = m_nodes[i].translation[k];
        for (int k = 0; k < 4; ++k) m_pose[i].rotation[k]    = m_nodes[i].rotation[k];
        for (int k = 0; k < 3; ++k) m_pose[i].scale[k]       = m_nodes[i].scale[k];
    }

    // Joint-matrix scratch: one per skin, kMaxJoints entries each
    // (the shader's uniform array is fixed-size; unused trailing
    // entries stay at identity).
    m_jointMatrices.assign(m_skins.size(),
                            std::vector<m3d::Mat4>(kMaxJoints, m3d::identity()));
    m_activeClip = m_animations.empty() ? -1 : 0;

    // Normalize: center the model at origin, then scale so the largest
    // single-axis half-extent fits with a small margin inside the
    // camera's visible half-height at z=0. A bounding-sphere fit would
    // be conservative and leave a boxy model (like a duck) at ~60% of
    // the frame; fitting to the largest half-extent instead gets us to
    // ~90%, which reads as "full-frame" for most content.
    const auto& b = model.bounds();
    if (b.valid) {
        m_autoOffset[0] = -0.5f * (b.minCoord[0] + b.maxCoord[0]);
        m_autoOffset[1] = -0.5f * (b.minCoord[1] + b.maxCoord[1]);
        m_autoOffset[2] = -0.5f * (b.minCoord[2] + b.maxCoord[2]);
        float hx = 0.5f * (b.maxCoord[0] - b.minCoord[0]);
        float hy = 0.5f * (b.maxCoord[1] - b.minCoord[1]);
        float hz = 0.5f * (b.maxCoord[2] - b.minCoord[2]);
        float maxHalf = std::max(hx, std::max(hy, hz));
        if (maxHalf < 1e-6f) maxHalf = 1.0f;
        // Camera at z=2.5, FOV 45° → visible half-height ≈ 1.036. Scale
        // the model so its largest axis reaches ~90% of that (0.93).
        m_autoScale = 0.93f / maxHalf;
    } else {
        m_autoOffset[0] = m_autoOffset[1] = m_autoOffset[2] = 0.0f;
        m_autoScale = 1.0f;
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────

void M3DRenderer::render(const Transform& xf) {
    beginFrame(0.0f);
    drawInstance(xf);
    endFrame();
}

void M3DRenderer::setAnimationClip(int index) {
    if (index < 0 || index >= static_cast<int>(m_animations.size())) {
        m_activeClip = -1;
        return;
    }
    m_activeClip = index;
}

void M3DRenderer::beginFrame(float animTime) {
    if (!m_fbo || !m_program || m_meshes.empty()) return;

    // ── Evaluate the active animation (if any) ─────────────────────────
    // Reset pose to the base TRS so sampler evaluation is idempotent
    // across re-runs with the same time.
    for (size_t i = 0; i < m_pose.size(); ++i) {
        for (int k = 0; k < 3; ++k) m_pose[i].translation[k] = m_nodes[i].translation[k];
        for (int k = 0; k < 4; ++k) m_pose[i].rotation[k]    = m_nodes[i].rotation[k];
        for (int k = 0; k < 3; ++k) m_pose[i].scale[k]       = m_nodes[i].scale[k];
    }

    if (m_activeClip >= 0 &&
        m_activeClip < static_cast<int>(m_animations.size())) {
        const auto& anim = m_animations[m_activeClip];
        float clipTime = animTime;
        if (anim.duration > 0.0f) {
            clipTime = std::fmod(animTime, anim.duration);
            if (clipTime < 0.0f) clipTime += anim.duration;
        }
        for (const auto& ch : anim.channels) {
            if (ch.targetNode < 0 ||
                ch.targetNode >= static_cast<int>(m_pose.size())) continue;
            if (ch.samplerIndex < 0 ||
                ch.samplerIndex >= static_cast<int>(anim.samplers.size())) continue;
            const auto& s = anim.samplers[ch.samplerIndex];
            if (s.inputTimes.empty()) continue;

            auto br = bracketKeyframes(s.inputTimes, clipTime);

            // Step and CubicSpline interpolation: for the MVP we treat
            // Step as "use lo frame" and CubicSpline as Linear (full
            // cubicspline support deferred — it's rare in hand-exported
            // assets and the read would need to account for in/out
            // tangents stored alongside values).
            using Interp = M3DAnimationSampler::Interpolation;
            auto& pose = m_pose[ch.targetNode];
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
                    // Morph target weights not consumed yet (deferred).
                    break;
            }
        }
    }

    // ── Propagate the pose to world matrices via the node tree ─────────
    // Build a parent-index array from children relationships so we can
    // walk roots-first. We mark "unprocessed" as -2; nodes with no
    // parent keep -1.
    static thread_local std::vector<int> parent;
    parent.assign(m_nodes.size(), -1);
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        for (int c : m_nodes[i].children) {
            if (c >= 0 && c < static_cast<int>(m_nodes.size())) {
                parent[c] = static_cast<int>(i);
            }
        }
    }

    auto localFromPose = [](const NodePose& p) {
        Mat4 T = translation(p.translation[0], p.translation[1], p.translation[2]);
        Mat4 R = m3d::quatToMatrix(p.rotation[0], p.rotation[1],
                                     p.rotation[2], p.rotation[3]);
        Mat4 S = scale(p.scale[0], p.scale[1], p.scale[2]);
        return multiply(T, multiply(R, S));
    };

    // Simple topo walk: keep iterating until every node's world has
    // been set. Bounded by hierarchy depth × node count in the worst
    // case — fine for typical character rigs (<100 joints, depth ~10).
    static thread_local std::vector<char> done;
    done.assign(m_nodes.size(), 0);
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            if (done[i]) continue;
            int p = parent[i];
            if (p < 0) {
                m_pose[i].world = localFromPose(m_pose[i]);
                done[i] = 1;
                progress = true;
            } else if (done[p]) {
                m_pose[i].world = multiply(m_pose[p].world, localFromPose(m_pose[i]));
                done[i] = 1;
                progress = true;
            }
        }
    }

    // ── Build joint matrices per skin ──────────────────────────────────
    for (size_t si = 0; si < m_skins.size(); ++si) {
        const auto& skin = m_skins[si];
        auto& mats = m_jointMatrices[si];
        for (size_t j = 0; j < skin.joints.size() && j < kMaxJoints; ++j) {
            int node = skin.joints[j];
            if (node < 0 || node >= static_cast<int>(m_pose.size())) {
                mats[j] = m3d::identity();
                continue;
            }
            // jointMatrix = jointWorld · inverseBindMatrix
            // (the mesh vertices are in bind-space; the IBM takes them
            // back to the joint's bind-pose local, and jointWorld
            // places them in the joint's animated world position.)
            const auto& ibm = skin.inverseBindMatrices[j];
            Mat4 ibmMat;
            for (int k = 0; k < 16; ++k) ibmMat[k] = ibm[k];
            mats[j] = multiply(m_pose[node].world, ibmMat);
        }
    }

    // ── GL state for this frame ────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, kWidth, kHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_program);

    // Light: angled from the upper-front-right. World space. Set once
    // per frame — scene instances share the same lighting rig.
    if (m_locLightDir >= 0)
        glUniform3f(m_locLightDir, -0.5f, -0.7f, -0.5f);
    if (m_locAmbient >= 0)
        glUniform3f(m_locAmbient, 0.2f, 0.2f, 0.22f);
}

void M3DRenderer::drawInstance(const Transform& xf) {
    if (!m_fbo || !m_program || m_meshes.empty()) return;

    // Composite model matrix:
    //   1. Normalization:   translate by m_autoOffset, then scale by m_autoScale
    //                        (puts the model centred at origin, fit to ~unit).
    //   2. User transform:  T · R · uniformScale on top.
    Mat4 normMat = multiply(
        scale(m_autoScale, m_autoScale, m_autoScale),
        translation(m_autoOffset[0], m_autoOffset[1], m_autoOffset[2]));
    Mat4 userT = multiply(
        translation(xf.position[0], xf.position[1], xf.position[2]),
        multiply(
            eulerXYZDegrees(xf.rotationDeg[0], xf.rotationDeg[1], xf.rotationDeg[2]),
            scale(xf.scale, xf.scale, xf.scale)));
    Mat4 outerModel = multiply(userT, normMat);

    // Fixed camera at (0, 0, 2.5) looking at origin — works for any
    // normalized model under 45° FOV with a comfortable margin.
    Mat4 view = lookAt(0.0f, 0.0f, 2.5f,
                        0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f);
    Mat4 proj = perspective(45.0f * 3.14159265f / 180.0f,
                              static_cast<float>(kWidth) / kHeight,
                              0.1f, 100.0f);
    Mat4 viewProj = multiply(proj, view);

    for (const auto& gm : m_meshes) {
        // Static meshes had their hierarchy baked into vertices at
        // load time, so the outer transform is all we layer on. Skinned
        // meshes also use outerModel as the rigid outer positioning;
        // the skinning blend in the vertex shader deals with the
        // hierarchy internally via the uploaded joint matrices.
        Mat4 model = outerModel;
        Mat4 mvp   = multiply(viewProj, model);

        if (m_locModel >= 0)
            glUniformMatrix4fv(m_locModel, 1, GL_FALSE, model.data());
        if (m_locMVP >= 0)
            glUniformMatrix4fv(m_locMVP, 1, GL_FALSE, mvp.data());

        // Skinning pathway: upload the mesh's skin's joint matrices
        // and flag the shader. Skin index -1 = static mesh.
        const bool skinned = (gm.skinIndex >= 0 &&
                               gm.skinIndex < static_cast<int>(m_jointMatrices.size()));
        if (m_locIsSkinned >= 0)
            glUniform1i(m_locIsSkinned, skinned ? 1 : 0);
        if (skinned && m_locJointMats >= 0) {
            const auto& mats = m_jointMatrices[gm.skinIndex];
            // glUniformMatrix4fv can take an array stride directly —
            // the Mat4 contract (std::array<float,16>) is contiguous.
            glUniformMatrix4fv(m_locJointMats, kMaxJoints, GL_FALSE,
                                reinterpret_cast<const float*>(mats.data()));
        }

        float baseColor[4] = { 1, 1, 1, 1 };
        int   texIdx       = -1;
        if (gm.materialIndex >= 0 &&
            gm.materialIndex < static_cast<int>(m_materials.size())) {
            const auto& m = m_materials[gm.materialIndex];
            std::memcpy(baseColor, m.baseColor, sizeof(baseColor));
            texIdx = m.texture;
        }
        if (m_locBaseColor >= 0)
            glUniform4fv(m_locBaseColor, 1, baseColor);

        GLuint boundTex = 0;
        if (texIdx >= 0 && texIdx < static_cast<int>(m_textures.size())) {
            boundTex = m_textures[texIdx];
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, boundTex);
        if (m_locBaseTex >= 0) glUniform1i(m_locBaseTex, 0);
        if (m_locHasTex  >= 0) glUniform1i(m_locHasTex, boundTex ? 1 : 0);

        glBindVertexArray(gm.vao);
        glDrawElements(GL_TRIANGLES, gm.indexCount, GL_UNSIGNED_INT, nullptr);
    }
}

void M3DRenderer::endFrame() {
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
}

} // namespace visual
} // namespace yawn

#else  // !YAWN_HAS_MODEL3D — inert stubs so the build stays green

namespace yawn {
namespace visual {

M3DRenderer::~M3DRenderer() {}
void M3DRenderer::clear() {}
void M3DRenderer::destroyProgram()  {}
void M3DRenderer::destroyMeshes()   {}
void M3DRenderer::destroyTextures() {}
void M3DRenderer::destroyFBO()      {}
bool M3DRenderer::init() { return false; }
void M3DRenderer::setModel(const M3DModel&) {}
void M3DRenderer::render(const Transform&) {}
void M3DRenderer::beginFrame(float) {}
void M3DRenderer::drawInstance(const Transform&) {}
void M3DRenderer::endFrame() {}
void M3DRenderer::setAnimationClip(int) {}

} // namespace visual
} // namespace yawn

#endif
