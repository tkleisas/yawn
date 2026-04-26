#include "visual/VisualEngine.h"
#include "visual/VisualEngineAPI.h"
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
#include "visual/gltf/M3DModel.h"
#include "visual/gltf/M3DRenderer.h"
#endif
#include "util/Logger.h"
#include "audio/AudioEngine.h"
#include "audio/Mixer.h"
#include "core/Constants.h"

#include <vector>
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <regex>

namespace yawn {
namespace visual {

// ── RAII context scope ─────────────────────────────────────────────────────

VisualEngine::ContextScope::ContextScope(SDL_Window* newWin, SDL_GLContext newCtx) {
    prevWindow  = SDL_GL_GetCurrentWindow();
    prevContext = SDL_GL_GetCurrentContext();
    if (newWin && newCtx) {
        SDL_GL_MakeCurrent(newWin, newCtx);
    }
}

VisualEngine::ContextScope::~ContextScope() {
    if (restored) return;
    if (prevWindow && prevContext) {
        SDL_GL_MakeCurrent(prevWindow, prevContext);
    }
    restored = true;
}

// ── Shader sources ─────────────────────────────────────────────────────────

// Fullscreen triangle from gl_VertexID — no VBO.
static const char* kFullscreenVS = R"GLSL(
#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = (p + 1.0) * 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

static const char* kShaderToyPreamble = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 fragColor_out;

uniform vec3      iResolution;
uniform float     iTime;
uniform float     iTimeDelta;
uniform int       iFrame;
uniform vec4      iMouse;
uniform vec4      iDate;
uniform float     iSampleRate;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
uniform vec3      iChannelResolution[4];
uniform float     iChannelTime[4];

// Previous-pass output. On pass 0 this binds to a dummy black texture
// (no upstream pass exists); on each chain pass it binds to the prior
// pass's framebuffer colour attachment. Effect shaders sample it via
// `texture(iPrev, uv)` to compose on top of the running image.
uniform sampler2D iPrev;

// Previous *frame*'s final chain output. Lets feedback / echo / trail
// shaders sample what they emitted last frame and superimpose it on
// the current iPrev. The engine only allocates + populates this
// texture when at least one pass actually declares it (lazy), so
// layers that don't use iFeedback pay nothing. Black on the very
// first frame after the layer is created.
uniform sampler2D iFeedback;

// YAWN-specific extensions — not in Shadertoy.
// iTime advances with wall-clock; iTransportTime follows the transport
// position (stops when playback is paused). iBeat is transport beats.
// iAudioLevel is a 0..1 envelope-smoothed peak of the clip's audio source.
uniform float iBeat;
uniform float iTransportPlaying;
uniform float iTransportTime;
uniform float iAudioLevel;
uniform float iAudioLow;
uniform float iAudioMid;
uniform float iAudioHigh;
uniform float iKick;

// Rendered pixel width of the clip's text strip on iChannel1. Use this
// for wrap-correct scrolling: e.g. `mod(pxX, iTextWidth) / iTextTexWidth`.
uniform float iTextWidth;
uniform float iTextTexWidth;   // always 2048 — the texture's own width

// 8 generic knobs always bound. Pattern: lay a hardware encoder bank on
// these and every shader gets the same eight controls. Shaders that
// prefer named/annotated custom uniforms can still declare those — both
// live happily side-by-side.
uniform float knobA;
uniform float knobB;
uniform float knobC;
uniform float knobD;
uniform float knobE;
uniform float knobF;
uniform float knobG;
uniform float knobH;

void mainImage(out vec4 fragColor, in vec2 fragCoord);

void main() {
    vec4 c;
    mainImage(c, vUV * iResolution.xy);
    fragColor_out = c;
}

#line 1
)GLSL";

// Preamble for post-FX effects. Samples `iPrev` (previous stage's output)
// and shares most Shadertoy-style uniforms with layer shaders, plus the
// YAWN audio/beat extensions. A..H knobs are NOT available here — post-FX
// parameters use the same @range annotation convention as layer shaders.
static const char* kPostFXPreamble = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 fragColor_out;

uniform vec3      iResolution;
uniform float     iTime;
uniform float     iTimeDelta;
uniform int       iFrame;
uniform float     iBeat;
uniform float     iTransportTime;
uniform float     iTransportPlaying;
uniform float     iAudioLevel;
uniform float     iAudioLow;
uniform float     iAudioMid;
uniform float     iAudioHigh;
uniform float     iKick;
uniform sampler2D iPrev;

void mainImage(out vec4 fragColor, in vec2 fragCoord);

void main() {
    vec4 c;
    mainImage(c, vUV * iResolution.xy);
    fragColor_out = c;
}

#line 1
)GLSL";

static const char* kBlitFS = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vUV);
}
)GLSL";

// Composite pass — blends the source layer onto the accumulator using
// (opacity, blend-mode) into a second accumulator (ping-pong). Single
// shader handles all four modes so the draw-call path is uniform-only.
//
//   Normal   : result = mix(dst, src, opacity)
//   Add      : result = dst + src * opacity
//   Multiply : result = lerp(dst, dst * src, opacity)
//   Screen   : result = lerp(dst, 1 - (1-dst)*(1-src), opacity)
static const char* kCompositeFS = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform sampler2D uDst;
uniform float     uAlpha;
uniform int       uMode;
void main() {
    // Source alpha multiplied into the blend factor — lets a layer have
    // transparent regions that let underlying layers show through. Most
    // shaders output alpha=1 for every pixel, so this is a no-op for them.
    vec4 src = texture(uSrc, vUV);
    vec3 d   = texture(uDst, vUV).rgb;
    float a  = uAlpha * src.a;

    vec3 blended;
    if (uMode == 1) {
        blended = d + src.rgb;                              // Add
    } else if (uMode == 2) {
        blended = d * src.rgb;                              // Multiply
    } else if (uMode == 3) {
        blended = vec3(1.0) - (vec3(1.0) - d) *
                                (vec3(1.0) - src.rgb);       // Screen
    } else {
        blended = src.rgb;                                   // Normal
    }
    fragColor = vec4(mix(d, blended, a), 1.0);
}
)GLSL";

// ── Shader compilation ─────────────────────────────────────────────────────

GLuint VisualEngine::compileShaderProgram(const char* vertSrc, const char* fragSrc,
                                            const char* name) {
    auto compileOne = [](GLenum type, const char* src, const char* stageName,
                         const char* programName) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len > 0 ? len : 1);
            glGetShaderInfoLog(shader, len, nullptr, log.data());
            LOG_ERROR("Visual", "%s %s compile failed:\n%s",
                      programName, stageName, log.data());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compileOne(GL_VERTEX_SHADER, vertSrc, "vertex shader", name);
    if (!vs) return 0;
    GLuint fs = compileOne(GL_FRAGMENT_SHADER, fragSrc, "fragment shader", name);
    if (!fs) { glDeleteShader(vs); return 0; }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 0 ? len : 1);
        glGetProgramInfoLog(program, len, nullptr, log.data());
        LOG_ERROR("Visual", "%s link failed:\n%s", name, log.data());
        glDeleteProgram(program);
        program = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// ── Init / shutdown ────────────────────────────────────────────────────────

bool VisualEngine::init() {
    if (m_initialized) return true;
    if (!createOutputWindowAndContext()) return false;

    ContextScope scope(m_outputWindow, m_outputContext);

    if (!createBlitProgram())          { shutdown(); return false; }
    if (!createCompositeProgram())     { shutdown(); return false; }
    if (!createAccumFBOs())            { shutdown(); return false; }
    if (!createDummyChannels())        { shutdown(); return false; }

    glGenVertexArrays(1, &m_vao);

    // Load the same TTF as the UI font, so text on-screen looks consistent
    // with the rest of YAWN. Failure is non-fatal — text just won't render.
    if (m_textRasterizer.load("assets/fonts/JetBrainsMono-Regular.ttf")) {
        LOG_INFO("Visual", "TextRasterizer: font loaded (%d bytes).",
                  0);  // size not exposed; presence is the signal
    } else {
        LOG_WARN("Visual", "TextRasterizer: failed to load bundled font; "
                           "iChannel1 text will be blank.");
    }
    m_textScratch.assign(kTextTexW * kTextTexH, 0);

    m_initialized = true;
    LOG_INFO("Visual", "VisualEngine initialized (internal %dx%d)",
             kInternalWidth, kInternalHeight);
    return true;
}

void VisualEngine::shutdown() {
    if (m_outputWindow && m_outputContext) {
        ContextScope scope(m_outputWindow, m_outputContext);
        for (auto& [trackIdx, layer] : m_layers) destroyLayer(layer);
        m_layers.clear();
        for (auto& pe : m_postFX) destroyPostFX(pe);
        m_postFX.clear();
        if (m_vao)               { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
        if (m_blitProgram)       { glDeleteProgram(m_blitProgram); m_blitProgram = 0; }
        if (m_compositeProgram)  { glDeleteProgram(m_compositeProgram); m_compositeProgram = 0; }
        for (int i = 0; i < 2; ++i) {
            if (m_accumFBO[i]) { glDeleteFramebuffers(1, &m_accumFBO[i]); m_accumFBO[i] = 0; }
            if (m_accumTex[i]) { glDeleteTextures(1, &m_accumTex[i]);     m_accumTex[i] = 0; }
        }
        for (int i = 0; i < 4; ++i) {
            if (m_dummyChannelTex[i]) {
                glDeleteTextures(1, &m_dummyChannelTex[i]);
                m_dummyChannelTex[i] = 0;
            }
        }
        if (m_audioTex) { glDeleteTextures(1, &m_audioTex); m_audioTex = 0; }
    }
    if (m_outputContext) {
        SDL_GL_DestroyContext(m_outputContext);
        m_outputContext = nullptr;
    }
    if (m_outputWindow) {
        SDL_DestroyWindow(m_outputWindow);
        m_outputWindow = nullptr;
    }
    m_initialized = false;
}

VisualEngine::~VisualEngine() { shutdown(); }

// ── Window + context creation ──────────────────────────────────────────────

bool VisualEngine::createOutputWindowAndContext() {
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    m_outputWindow = SDL_CreateWindow("Y.A.W.N - Visual Output",
                                        1280, 720, flags);
    if (!m_outputWindow) {
        LOG_ERROR("Visual", "Failed to create output window: %s", SDL_GetError());
        return false;
    }

    m_outputContext = SDL_GL_CreateContext(m_outputWindow);
    if (!m_outputContext) {
        LOG_ERROR("Visual", "Failed to create output GL context: %s", SDL_GetError());
        SDL_DestroyWindow(m_outputWindow);
        m_outputWindow = nullptr;
        return false;
    }
    SDL_GL_SetSwapInterval(0);
    return true;
}

bool VisualEngine::ensurePingFBOs(Layer& L) {
    if (L.pingFBO[0] && L.pingFBO[1]) return true;
    for (int i = 0; i < 2; ++i) {
        if (L.pingFBO[i]) continue;
        glGenTextures(1, &L.pingTex[i]);
        glBindTexture(GL_TEXTURE_2D, L.pingTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kInternalWidth, kInternalHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &L.pingFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, L.pingFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, L.pingTex[i], 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERROR("Visual",
                       "Chain ping-pong FBO[%d] incomplete (0x%04X)", i, status);
            return false;
        }
    }
    return true;
}

bool VisualEngine::ensureFeedbackTex(Layer& L) {
    if (L.feedbackTex) return true;
    glGenTextures(1, &L.feedbackTex);
    if (!L.feedbackTex) return false;
    glBindTexture(GL_TEXTURE_2D, L.feedbackTex);
    // Initialise to opaque black so the first frame after this layer
    // is created doesn't sample garbage uninitialised VRAM.
    std::vector<uint8_t> zero(size_t(kInternalWidth) * kInternalHeight * 4, 0);
    for (size_t i = 3; i < zero.size(); i += 4) zero[i] = 255;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kInternalWidth, kInternalHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, zero.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

bool VisualEngine::buildFBO(Layer& L) {
    glGenTextures(1, &L.fboTex);
    glBindTexture(GL_TEXTURE_2D, L.fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kInternalWidth, kInternalHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &L.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, L.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, L.fboTex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Visual", "Layer FBO incomplete (0x%04X)", status);
        return false;
    }
    return true;
}

void VisualEngine::destroyLayer(Layer& L) {
    if (L.fbo)      { glDeleteFramebuffers(1, &L.fbo); L.fbo = 0; }
    if (L.fboTex)   { glDeleteTextures(1, &L.fboTex);  L.fboTex = 0; }
    if (L.program)  { glDeleteProgram(L.program); L.program = 0; }
    // Chain passes + ping-pong FBOs.
    for (auto& cp : L.additionalPasses)
        if (cp.program) { glDeleteProgram(cp.program); cp.program = 0; }
    L.additionalPasses.clear();
    for (int i = 0; i < 2; ++i) {
        if (L.pingFBO[i]) { glDeleteFramebuffers(1, &L.pingFBO[i]); L.pingFBO[i] = 0; }
        if (L.pingTex[i]) { glDeleteTextures(1, &L.pingTex[i]);    L.pingTex[i] = 0; }
    }
    if (L.feedbackTex) { glDeleteTextures(1, &L.feedbackTex); L.feedbackTex = 0; }
    if (L.textTex)  { glDeleteTextures(1, &L.textTex); L.textTex = 0; }
    if (L.videoTex) { glDeleteTextures(1, &L.videoTex); L.videoTex = 0; }
    L.video.reset();
    if (L.liveVideo) { L.liveVideo->stop(); L.liveVideo.reset(); }
    L.liveUrl.clear();
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
    if (L.modelRenderer) { L.modelRenderer->clear(); L.modelRenderer.reset(); }
    L.modelPath.clear();
    L.sceneScript.reset();
    L.scenePath.clear();
#endif
}

void VisualEngine::uploadLayerText(Layer& L) {
    // Create the texture lazily so layers without text use no VRAM.
    if (L.textTex == 0) {
        glGenTextures(1, &L.textTex);
        glBindTexture(GL_TEXTURE_2D, L.textTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kTextTexW, kTextTexH, 0,
                     GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Wrap horizontally so shaders can scroll by shifting UV.x freely.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_RED };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    }
    // Rasterize at ~70% of tex height so glyphs fit with descender margin.
    const float pxH = kTextTexH * 0.70f;
    L.textPixelW = m_textRasterizer.rasterize(
        L.currentText, pxH,
        m_textScratch.data(), kTextTexW, kTextTexH);

    glBindTexture(GL_TEXTURE_2D, L.textTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTextTexW, kTextTexH,
                    GL_RED, GL_UNSIGNED_BYTE, m_textScratch.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    LOG_INFO("Visual", "Rasterised text (\"%s\") → %d px wide",
             L.currentText.c_str(), L.textPixelW);
}

// Built-in uniform names that our parser should skip so they don't show up
// as user parameters. Kept sorted alphabetically for readability.
static bool isBuiltinUniform(const std::string& name) {
    static const std::vector<std::string> builtins = {
        "iAudioHigh", "iAudioLevel", "iAudioLow", "iAudioMid",
        "iBeat", "iChannelResolution", "iChannelTime",
        "iChannel0", "iChannel1", "iChannel2", "iChannel3",
        "iDate", "iFrame", "iKick", "iMouse", "iResolution",
        "iSampleRate", "iTime", "iTimeDelta",
        "iTransportPlaying", "iTransportTime",
    };
    return std::find(builtins.begin(), builtins.end(), name) != builtins.end();
}

// Parse user-declared `uniform float NAME;` lines plus optional annotation
// `// @range min..max default=N`. Anything not matching keeps 0..1 default=0.
// Returns (name, min, max, defaultValue) tuples for the caller to build
// into VisualEngine::Param (with resolved GL location etc).
struct ParsedParam {
    std::string name;
    float min, max, defaultValue;
};
static std::vector<ParsedParam>
parseShaderParams(const std::string& source) {
    std::vector<ParsedParam> out;
    // Pull the "lazy skip" inside the optional annotation group so the
    // engine is actually motivated to advance past whitespace between ";"
    // and "//". With the skip outside the group, a lazy quantifier prefers
    // zero chars and the annotation never matches.
    std::regex re(
        R"(uniform\s+float\s+([A-Za-z_][A-Za-z0-9_]*)\s*;(?:[^\n]*?//[^\n]*@range\s+([-0-9.eE+]+)\s*\.\.\s*([-0-9.eE+]+)[^\n]*default\s*=\s*([-0-9.eE+]+))?)",
        std::regex::ECMAScript);

    auto begin = std::sregex_iterator(source.begin(), source.end(), re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string name = (*it)[1].str();
        if (isBuiltinUniform(name)) continue;

        ParsedParam p;
        p.name = name;
        if ((*it)[2].matched && (*it)[3].matched && (*it)[4].matched) {
            try {
                p.min          = std::stof((*it)[2].str());
                p.max          = std::stof((*it)[3].str());
                p.defaultValue = std::stof((*it)[4].str());
            } catch (...) {
                p.min = 0.0f; p.max = 1.0f; p.defaultValue = 0.0f;
            }
        } else {
            p.min = 0.0f; p.max = 1.0f; p.defaultValue = 0.0f;
        }
        out.push_back(std::move(p));
    }
    return out;
}

// Public helper — parse @range uniforms from a shader file on disk,
// without ever loading a GL program. Exposed so UI code can populate
// the custom-param knob row before the clip is launched.
std::vector<VisualEngine::LayerParamInfo>
VisualEngine::parseShaderFileParams(const std::string& shaderPath) {
    std::vector<LayerParamInfo> out;
    if (shaderPath.empty()) return out;
    std::ifstream f(shaderPath);
    if (!f) return out;
    std::stringstream buf;
    buf << f.rdbuf();
    auto parsed = parseShaderParams(buf.str());
    out.reserve(parsed.size());
    for (const auto& p : parsed) {
        LayerParamInfo info;
        info.name         = p.name;
        info.min          = p.min;
        info.max          = p.max;
        info.defaultValue = p.defaultValue;
        info.value        = p.defaultValue;
        out.push_back(std::move(info));
    }
    return out;
}

bool VisualEngine::compileShaderForLayer(Layer& L, const std::string& userSrc,
                                           const std::string& sourceLabel) {
    std::string full = kShaderToyPreamble;
    full += userSrc;
    GLuint program = compileShaderProgram(kFullscreenVS, full.c_str(),
                                           sourceLabel.c_str());
    if (!program) return false;
    if (L.program) glDeleteProgram(L.program);
    L.program = program;
    cacheUniformLocations(L);

    // Rebuild param list from source, preserving values across reloads.
    auto parsed = parseShaderParams(userSrc);
    std::vector<VisualEngine::Param> newParams;
    newParams.reserve(parsed.size());
    for (auto& pp : parsed) {
        VisualEngine::Param p;
        p.name          = pp.name;
        p.min           = pp.min;
        p.max           = pp.max;
        p.defaultValue  = pp.defaultValue;
        p.value         = pp.defaultValue;
        for (const auto& old : L.params) {
            if (old.name == p.name) {
                p.value = std::clamp(old.value, p.min, p.max);
                break;
            }
        }
        p.location = glGetUniformLocation(L.program, p.name.c_str());
        LOG_INFO("Visual",
                 "  param %-16s min=%.3f max=%.3f default=%.3f value=%.3f loc=%d",
                 p.name.c_str(), p.min, p.max, p.defaultValue,
                 p.value, static_cast<int>(p.location));
        newParams.push_back(std::move(p));
    }
    L.params = std::move(newParams);
    return true;
}

void VisualEngine::cacheUniformLocations(Layer& L) {
    auto loc = [&](const char* n) { return glGetUniformLocation(L.program, n); };
    L.loc_iResolution        = loc("iResolution");
    L.loc_iTime              = loc("iTime");
    L.loc_iTimeDelta         = loc("iTimeDelta");
    L.loc_iFrame             = loc("iFrame");
    L.loc_iMouse             = loc("iMouse");
    L.loc_iDate              = loc("iDate");
    L.loc_iSampleRate        = loc("iSampleRate");
    L.loc_iBeat              = loc("iBeat");
    L.loc_iTransportPlaying  = loc("iTransportPlaying");
    L.loc_iTransportTime     = loc("iTransportTime");
    L.loc_iAudioLevel        = loc("iAudioLevel");
    L.loc_iAudioLow          = loc("iAudioLow");
    L.loc_iAudioMid          = loc("iAudioMid");
    L.loc_iAudioHigh         = loc("iAudioHigh");
    L.loc_iKick              = loc("iKick");
    L.loc_iChannel[0]        = loc("iChannel0");
    L.loc_iChannel[1]        = loc("iChannel1");
    L.loc_iChannel[2]        = loc("iChannel2");
    L.loc_iChannel[3]        = loc("iChannel3");
    L.loc_iChannelResolution = loc("iChannelResolution[0]");
    L.loc_iChannelTime       = loc("iChannelTime[0]");
    L.loc_iTextWidth         = loc("iTextWidth");
    L.loc_iTextTexWidth      = loc("iTextTexWidth");

    static const char* kKnobNames[8] = {
        "knobA", "knobB", "knobC", "knobD",
        "knobE", "knobF", "knobG", "knobH"
    };
    for (int i = 0; i < 8; ++i)
        L.loc_knobs[i] = loc(kKnobNames[i]);
    // iPrev — dummy on pass 0 (no previous output exists), but cache the
    // location so a shader that declares `uniform sampler2D iPrev` and
    // is later moved to a chain position behaves consistently.
    L.loc_iPrev     = loc("iPrev");
    // iFeedback — previous frame's chain output (persistent across
    // frames). Same caching pattern as iPrev: we look it up
    // unconditionally; the renderer skips work when loc < 0.
    L.loc_iFeedback = loc("iFeedback");
}

// ── Shared shader programs (blit + composite) ─────────────────────────────

bool VisualEngine::createBlitProgram() {
    m_blitProgram = compileShaderProgram(kFullscreenVS, kBlitFS, "Blit");
    if (!m_blitProgram) return false;
    m_blit_tex_loc = glGetUniformLocation(m_blitProgram, "uTex");
    return true;
}

bool VisualEngine::createCompositeProgram() {
    m_compositeProgram = compileShaderProgram(kFullscreenVS, kCompositeFS, "Composite");
    if (!m_compositeProgram) return false;
    m_comp_src_loc   = glGetUniformLocation(m_compositeProgram, "uSrc");
    m_comp_dst_loc   = glGetUniformLocation(m_compositeProgram, "uDst");
    m_comp_alpha_loc = glGetUniformLocation(m_compositeProgram, "uAlpha");
    m_comp_mode_loc  = glGetUniformLocation(m_compositeProgram, "uMode");
    return true;
}

bool VisualEngine::createAccumFBOs() {
    for (int i = 0; i < 2; ++i) {
        glGenTextures(1, &m_accumTex[i]);
        glBindTexture(GL_TEXTURE_2D, m_accumTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kInternalWidth, kInternalHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &m_accumFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_accumFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_accumTex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERROR("Visual", "Accumulator FBO %d incomplete", i);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool VisualEngine::createDummyChannels() {
    const unsigned char blackPx[4] = {0, 0, 0, 255};
    for (int i = 0; i < 4; ++i) {
        glGenTextures(1, &m_dummyChannelTex[i]);
        glBindTexture(GL_TEXTURE_2D, m_dummyChannelTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, blackPx);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // iChannel0 audio texture: 512×2 R8. Row 0 = FFT, row 1 = waveform.
    glGenTextures(1, &m_audioTex);
    glBindTexture(GL_TEXTURE_2D, m_audioTex);
    m_audioTexPixels.assign(kAudioTexW * kAudioTexH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAudioTexW, kAudioTexH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, m_audioTexPixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_ONE };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// ── Visibility ─────────────────────────────────────────────────────────────

void VisualEngine::setOutputVisible(bool visible) {
    if (!m_outputWindow || visible == m_outputVisible) return;
    if (visible) SDL_ShowWindow(m_outputWindow);
    else         SDL_HideWindow(m_outputWindow);
    m_outputVisible = visible;
}

void VisualEngine::setFullscreen(bool fullscreen) {
    if (!m_outputWindow || fullscreen == m_fullscreen) return;
    SDL_SetWindowFullscreen(m_outputWindow, fullscreen);
    m_fullscreen = fullscreen;
}

// ── Layer management ───────────────────────────────────────────────────────

VisualEngine::Layer& VisualEngine::ensureLayer(int track) {
    auto it = m_layers.find(track);
    if (it != m_layers.end()) return it->second;
    auto [ni, _] = m_layers.emplace(track, Layer{});
    Layer& L = ni->second;
    ContextScope scope(m_outputWindow, m_outputContext);
    buildFBO(L);
    return L;
}

bool VisualEngine::hasLayer(int track) const {
    auto it = m_layers.find(track);
    return it != m_layers.end() && it->second.program != 0;
}

bool VisualEngine::loadLayer(int track, const std::string& path, int audioSource) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Visual", "Cannot open shader file: %s", path.c_str());
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();

    Layer& L = ensureLayer(track);
    ContextScope scope(m_outputWindow, m_outputContext);
    if (!compileShaderForLayer(L, buf.str(), path)) return false;

    L.shaderPath = path;
    std::error_code ec;
    auto mt = std::filesystem::last_write_time(path, ec);
    if (!ec) { L.mtime = mt; L.mtimeValid = true; }

    L.audioSource = audioSource;
    updateAudioWiring();
    LOG_INFO("Visual", "Layer %d loaded: %s (source %d)",
             track, path.c_str(), audioSource);
    return true;
}

void VisualEngine::setLayerAudioSource(int track, int audioSource) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    Layer& L = it->second;
    if (L.audioSource == audioSource) return;
    L.audioSource      = audioSource;
    // Reset envelope state so the new source doesn't inherit the old one's
    // residual smoothed values or spurious kick detections.
    L.smoothedLevel = L.smoothedLow = L.smoothedMid = L.smoothedHigh = 0.0f;
    L.kickBaseline = L.kickLevel = L.kickRefractory = L.kickLastLow = 0.0f;
    updateAudioWiring();
}

void VisualEngine::setLayerBlendMode(int track, BlendMode mode) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    it->second.blendMode = mode;
}

std::vector<VisualEngine::LayerParamInfo>
VisualEngine::getLayerParams(int track) const {
    std::vector<LayerParamInfo> out;
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return out;
    const auto& L = it->second;
    out.reserve(L.params.size());
    for (const auto& p : L.params) {
        out.push_back({p.name, p.value, p.min, p.max, p.defaultValue});
    }
    return out;
}

void VisualEngine::setLayerParam(int track, const std::string& name, float value) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    for (auto& p : it->second.params) {
        if (p.name == name) {
            p.value = std::clamp(value, p.min, p.max);
            return;
        }
    }
}

void VisualEngine::applyLayerParamValues(int track,
        const std::vector<std::pair<std::string, float>>& values) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    Layer& L = it->second;
    for (auto& p : L.params) {
        for (const auto& kv : values) {
            if (kv.first == p.name) {
                p.value = std::clamp(kv.second, p.min, p.max);
                break;
            }
        }
    }
    // The A..H knobs live in the same values list under the reserved
    // names "knobA".."knobH" — this keeps persistence a single JSON map.
    static const char* kKnobNames[8] = {
        "knobA","knobB","knobC","knobD","knobE","knobF","knobG","knobH"
    };
    for (int i = 0; i < 8; ++i) {
        for (const auto& kv : values) {
            if (kv.first == kKnobNames[i]) {
                L.knobValues[i] = std::clamp(kv.second, 0.0f, 1.0f);
                break;
            }
        }
    }
}

float VisualEngine::getLayerKnob(int track, int idx) const {
    if (idx < 0 || idx >= 8) return 0.0f;
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return 0.5f;
    return it->second.knobValues[idx];
}

void VisualEngine::setLayerKnob(int track, int idx, float value) {
    if (idx < 0 || idx >= 8) return;
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    it->second.knobValues[idx] = std::clamp(value, 0.0f, 1.0f);
}

const VisualLFO* VisualEngine::getLayerKnobLFO(int track, int idx) const {
    if (idx < 0 || idx >= 8) return nullptr;
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return nullptr;
    return &it->second.knobLFOs[idx];
}

void VisualEngine::setLayerKnobLFO(int track, int idx, const VisualLFO& lfo) {
    if (idx < 0 || idx >= 8) return;
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    it->second.knobLFOs[idx] = lfo;
}

float VisualEngine::getLayerKnobDisplayValue(int track, int idx) const {
    if (idx < 0 || idx >= 8) return 0.0f;
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return 0.5f;
    return it->second.knobDisplayValues[idx];
}

void VisualEngine::setLayerText(int track, const std::string& text) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    Layer& L = it->second;
    if (L.currentText == text) return;
    L.currentText = text;
    ContextScope scope(m_outputWindow, m_outputContext);
    uploadLayerText(L);
}

void VisualEngine::setLayerVideoTrim(int track, float inFrac, float outFrac) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    inFrac  = std::clamp(inFrac,  0.0f, 1.0f);
    outFrac = std::clamp(outFrac, 0.0f, 1.0f);
    if (outFrac <= inFrac) outFrac = std::min(1.0f, inFrac + 0.01f);
    it->second.videoIn  = inFrac;
    it->second.videoOut = outFrac;
}

void VisualEngine::setLayerVideoTiming(int track, int loopBars, float rate) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    it->second.videoLoopBars = std::max(0, loopBars);
    it->second.videoRate     = (rate > 0.0f) ? rate : 1.0f;
    // Re-home the launch origin so the change doesn't cause a jump.
    it->second.videoLaunchTime  = std::chrono::steady_clock::now();
    it->second.videoLaunchBeats = 0.0;
}

bool VisualEngine::setLayerVideo(int track, const std::string& path) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return false;
    Layer& L = it->second;

    ContextScope scope(m_outputWindow, m_outputContext);

    // Tear down existing video resources if the path changed / cleared.
    if (L.video) L.video->close();
    L.video.reset();
    L.videoPath.clear();
    L.lastVideoFrame = -1;
    // File and live are mutually exclusive — stop any live source too.
    if (L.liveVideo) { L.liveVideo->stop(); L.liveVideo.reset(); }
    L.liveUrl.clear();
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
    // And drop any loaded 3D model — all three paths share iChannel2.
    if (L.modelRenderer) { L.modelRenderer->clear(); L.modelRenderer.reset(); }
    L.modelPath.clear();
#endif

    if (path.empty()) {
        // Keep the GL texture around at zeros — we rebind to the dummy
        // below in the render path when video == nullptr.
        return true;
    }

    L.video = std::make_unique<VideoDecoder>();
    if (!L.video->open(path)) {
        LOG_WARN("Visual", "Layer %d: failed to open video %s",
                  track, path.c_str());
        L.video.reset();
        return false;
    }
    L.videoPath = path;
    L.videoLaunchTime  = std::chrono::steady_clock::now();
    L.videoLaunchBeats = 0.0;  // App refreshes this at next tick if bar-synced

    // Lazy-create the GL texture the first time we have a video.
    if (L.videoTex == 0) {
        glGenTextures(1, &L.videoTex);
        glBindTexture(GL_TEXTURE_2D, L.videoTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     VideoDecoder::kWidth, VideoDecoder::kHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return true;
}

LiveVideoSource::State VisualEngine::getLayerLiveState(int track) const {
    auto it = m_layers.find(track);
    if (it == m_layers.end() || !it->second.liveVideo)
        return LiveVideoSource::State::Stopped;
    return it->second.liveVideo->state();
}

std::string VisualEngine::getLayerLiveError(int track) const {
    auto it = m_layers.find(track);
    if (it == m_layers.end() || !it->second.liveVideo) return {};
    return it->second.liveVideo->error();
}

bool VisualEngine::setLayerLiveInput(int track, const std::string& url) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return false;
    Layer& L = it->second;

    ContextScope scope(m_outputWindow, m_outputContext);

    // Live and file video are mutually exclusive — tear down file side.
    if (L.video) L.video->close();
    L.video.reset();
    L.videoPath.clear();
    L.lastVideoFrame = -1;
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
    if (L.modelRenderer) { L.modelRenderer->clear(); L.modelRenderer.reset(); }
    L.modelPath.clear();
#endif

    // Same URL as already running? Leave the running source alone so
    // re-launching the clip doesn't stall it on a reconnect.
    if (L.liveVideo && L.liveUrl == url &&
        L.liveVideo->state() != LiveVideoSource::State::Stopped &&
        L.liveVideo->state() != LiveVideoSource::State::Failed) {
        return true;
    }

    // URL changed / empty / previously failed → stop any old session.
    if (L.liveVideo) { L.liveVideo->stop(); L.liveVideo.reset(); }
    L.liveUrl.clear();

    if (url.empty()) return true;

    L.liveVideo = std::make_unique<LiveVideoSource>();
    if (!L.liveVideo->start(url)) {
        LOG_WARN("Visual", "Layer %d: failed to start live input %s",
                  track, url.c_str());
        L.liveVideo.reset();
        return false;
    }
    L.liveUrl = url;

    // Share the same GL texture the file-video path uses — upload target
    // is identical (kWidth×kHeight RGBA8).
    if (L.videoTex == 0) {
        glGenTextures(1, &L.videoTex);
        glBindTexture(GL_TEXTURE_2D, L.videoTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     LiveVideoSource::kWidth, LiveVideoSource::kHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return true;
}

bool VisualEngine::setLayerSceneScript(int track, const std::string& path) {
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return false;
    Layer& L = it->second;

    if (path.empty()) {
        L.sceneScript.reset();
        L.scenePath.clear();
        return true;
    }

    // Same script already loaded → leave it alone (hot-reload handles
    // mtime-driven refresh).
    if (L.scenePath == path && L.sceneScript) return true;

    auto s = std::make_unique<M3DSceneScript>();
    s->load(path);   // load() logs failures but keeps state for retry
    L.sceneScript = std::move(s);
    L.scenePath   = path;
    return true;
#else
    (void)track; (void)path;
    return false;
#endif
}

void VisualEngine::setLayerWallClock(int track) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    Layer& L = it->second;
    L.transportDriven  = false;
    L.clipStartBeat    = 0.0;
    // Reset wall-clock origin so iTime restarts at 0 on next render.
    L.wallStart        = std::chrono::steady_clock::now();
    L.lastWallSeconds  = 0.0;
}

void VisualEngine::setLayerTransportClock(int track, double clipStartBeat) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    Layer& L = it->second;
    L.transportDriven  = true;
    L.clipStartBeat    = clipStartBeat;
    // Reset lastWallSeconds so tDelta on the first transport-driven
    // frame is a small positive number rather than a huge wall-clock
    // spike carried over from the previous session-clock phase.
    L.lastWallSeconds  = 0.0;
}

bool VisualEngine::setLayerModel(int track, const std::string& path) {
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return false;
    Layer& L = it->second;

    ContextScope scope(m_outputWindow, m_outputContext);

    // Mutual exclusion with file / live video. All three paths feed
    // iChannel2; a layer can only have one source at a time.
    if (L.video) L.video->close();
    L.video.reset();
    L.videoPath.clear();
    L.lastVideoFrame = -1;
    if (L.liveVideo) { L.liveVideo->stop(); L.liveVideo.reset(); }
    L.liveUrl.clear();

    // Same path already loaded → keep as-is (avoid churning the GL
    // upload on every re-launch of the clip).
    if (L.modelRenderer && L.modelPath == path && !path.empty())
        return true;

    if (L.modelRenderer) { L.modelRenderer->clear(); L.modelRenderer.reset(); }
    L.modelPath.clear();

    if (path.empty()) return true;

    M3DModel cpu;
    if (!cpu.load(path)) {
        LOG_WARN("Visual", "Layer %d: failed to load model %s (%s)",
                  track, path.c_str(), cpu.error().c_str());
        return false;
    }

    L.modelRenderer = std::make_unique<M3DRenderer>();
    if (!L.modelRenderer->init()) {
        LOG_ERROR("Visual", "Layer %d: M3DRenderer init failed", track);
        L.modelRenderer.reset();
        return false;
    }
    L.modelRenderer->setModel(cpu);
    L.modelPath = path;
    return true;
#else
    (void)track; (void)path;
    LOG_WARN("Visual", "3D model support disabled at build time");
    return false;
#endif
}

void VisualEngine::clearLayer(int track) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    ContextScope scope(m_outputWindow, m_outputContext);
    destroyLayer(it->second);
    m_layers.erase(it);
    updateAudioWiring();
}

// ── Shader chain helpers ──────────────────────────────────────────────

void VisualEngine::cacheChainPassUniformLocations(Layer::ChainPass& cp) {
    auto loc = [&](const char* n) { return glGetUniformLocation(cp.program, n); };
    cp.loc_iResolution        = loc("iResolution");
    cp.loc_iTime              = loc("iTime");
    cp.loc_iTimeDelta         = loc("iTimeDelta");
    cp.loc_iFrame             = loc("iFrame");
    cp.loc_iMouse             = loc("iMouse");
    cp.loc_iDate              = loc("iDate");
    cp.loc_iSampleRate        = loc("iSampleRate");
    cp.loc_iBeat              = loc("iBeat");
    cp.loc_iTransportPlaying  = loc("iTransportPlaying");
    cp.loc_iTransportTime     = loc("iTransportTime");
    cp.loc_iAudioLevel        = loc("iAudioLevel");
    cp.loc_iAudioLow          = loc("iAudioLow");
    cp.loc_iAudioMid          = loc("iAudioMid");
    cp.loc_iAudioHigh         = loc("iAudioHigh");
    cp.loc_iKick              = loc("iKick");
    cp.loc_iChannel[0]        = loc("iChannel0");
    cp.loc_iChannel[1]        = loc("iChannel1");
    cp.loc_iChannel[2]        = loc("iChannel2");
    cp.loc_iChannel[3]        = loc("iChannel3");
    cp.loc_iChannelResolution = loc("iChannelResolution[0]");
    cp.loc_iChannelTime       = loc("iChannelTime[0]");
    cp.loc_iTextWidth         = loc("iTextWidth");
    cp.loc_iTextTexWidth      = loc("iTextTexWidth");
    cp.loc_iPrev              = loc("iPrev");
    cp.loc_iFeedback          = loc("iFeedback");

    static const char* kKnobNames[8] = {
        "knobA", "knobB", "knobC", "knobD",
        "knobE", "knobF", "knobG", "knobH"
    };
    for (int i = 0; i < 8; ++i)
        cp.loc_knobs[i] = loc(kKnobNames[i]);
}

bool VisualEngine::compileChainPass(Layer::ChainPass& cp,
                                      const std::string& userSrc,
                                      const std::string& sourceLabel,
                                      const std::vector<std::pair<std::string,
                                                                  float>>& savedValues) {
    std::string full = kShaderToyPreamble;
    full += userSrc;
    GLuint program = compileShaderProgram(kFullscreenVS, full.c_str(),
                                            sourceLabel.c_str());
    if (!program) return false;
    if (cp.program) glDeleteProgram(cp.program);
    cp.program = program;
    cacheChainPassUniformLocations(cp);

    auto parsed = parseShaderParams(userSrc);
    std::vector<Param> newParams;
    newParams.reserve(parsed.size());
    for (auto& pp : parsed) {
        Param p;
        p.name         = pp.name;
        p.min          = pp.min;
        p.max          = pp.max;
        p.defaultValue = pp.defaultValue;
        p.value        = pp.defaultValue;
        // First preserve any value still on the previous compile of
        // this pass (live reload), then overlay the saved value from
        // the project file (initial load).
        for (const auto& old : cp.params)
            if (old.name == p.name) { p.value = std::clamp(old.value, p.min, p.max); break; }
        for (const auto& sv : savedValues)
            if (sv.first == p.name) { p.value = std::clamp(sv.second, p.min, p.max); break; }
        p.location = glGetUniformLocation(cp.program, p.name.c_str());
        newParams.push_back(std::move(p));
    }
    cp.params = std::move(newParams);
    return true;
}

void VisualEngine::setLayerChainPassParam(int track, int passIdx,
                                            const std::string& name,
                                            float value) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    Layer& L = it->second;
    if (passIdx < 0 || passIdx >= static_cast<int>(L.additionalPasses.size()))
        return;
    auto& cp = L.additionalPasses[passIdx];
    for (auto& p : cp.params) {
        if (p.name == name) {
            p.value = std::clamp(value, p.min, p.max);
            return;
        }
    }
}

bool VisualEngine::getLayerParamRange(int track, const std::string& name,
                                        float* outMin, float* outMax) const {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return false;
    for (const auto& p : it->second.params) {
        if (p.name == name) {
            if (outMin) *outMin = p.min;
            if (outMax) *outMax = p.max;
            return true;
        }
    }
    return false;
}

bool VisualEngine::getLayerChainPassParamRange(int track, int passIdx,
                                                 const std::string& name,
                                                 float* outMin,
                                                 float* outMax) const {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return false;
    const auto& L = it->second;
    if (passIdx < 0 || passIdx >= static_cast<int>(L.additionalPasses.size()))
        return false;
    for (const auto& p : L.additionalPasses[passIdx].params) {
        if (p.name == name) {
            if (outMin) *outMin = p.min;
            if (outMax) *outMax = p.max;
            return true;
        }
    }
    return false;
}

void VisualEngine::setLayerChainPassBypass(int track, int passIdx,
                                             bool bypassed) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return;
    Layer& L = it->second;
    if (passIdx < 0 || passIdx >= static_cast<int>(L.additionalPasses.size()))
        return;
    L.additionalPasses[passIdx].bypassed = bypassed;
}

bool VisualEngine::setLayerAdditionalPasses(int track,
        const std::vector<ChainPassSpec>& passes) {
    auto it = m_layers.find(track);
    if (it == m_layers.end()) return false;
    Layer& L = it->second;
    ContextScope scope(m_outputWindow, m_outputContext);

    // Compile every spec into a staging vector first — if any one
    // fails we keep the previous chain so a typo in pass N doesn't
    // wipe a working pass 0..N-1 from a live show.
    std::vector<Layer::ChainPass> staged;
    staged.reserve(passes.size());
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& spec = passes[i];
        std::ifstream f(spec.shaderPath);
        if (!f) {
            LOG_ERROR("Visual",
                       "Chain pass %zu: cannot open shader file: %s",
                       i + 1, spec.shaderPath.c_str());
            return false;
        }
        std::stringstream buf;
        buf << f.rdbuf();
        Layer::ChainPass cp;
        cp.shaderPath = spec.shaderPath;
        cp.bypassed   = spec.bypassed;
        std::error_code ec;
        auto mt = std::filesystem::last_write_time(spec.shaderPath, ec);
        if (!ec) { cp.mtime = mt; cp.mtimeValid = true; }
        if (!compileChainPass(cp, buf.str(), spec.shaderPath,
                                spec.paramValues)) {
            LOG_ERROR("Visual",
                       "Chain pass %zu (%s) failed to compile — chain unchanged",
                       i + 1, spec.shaderPath.c_str());
            // Free the program we just compiled (compileChainPass
            // allocated it on success — if it returned false the
            // program was rolled back already).
            return false;
        }
        staged.push_back(std::move(cp));
    }

    // Swap in — free old programs that the new chain doesn't reuse.
    for (auto& old : L.additionalPasses)
        if (old.program) { glDeleteProgram(old.program); old.program = 0; }
    L.additionalPasses = std::move(staged);
    LOG_INFO("Visual", "Layer %d: %zu additional shader pass(es)",
             track, L.additionalPasses.size());
    return true;
}

// ── Post-FX chain implementation ──────────────────────────────────────

void VisualEngine::cachePostFXUniformLocations(PostEffect& pe) {
    auto loc = [&](const char* n) { return glGetUniformLocation(pe.program, n); };
    pe.loc_iResolution       = loc("iResolution");
    pe.loc_iTime             = loc("iTime");
    pe.loc_iTimeDelta        = loc("iTimeDelta");
    pe.loc_iFrame            = loc("iFrame");
    pe.loc_iBeat             = loc("iBeat");
    pe.loc_iTransportTime    = loc("iTransportTime");
    pe.loc_iTransportPlaying = loc("iTransportPlaying");
    pe.loc_iAudioLevel       = loc("iAudioLevel");
    pe.loc_iAudioLow         = loc("iAudioLow");
    pe.loc_iAudioMid         = loc("iAudioMid");
    pe.loc_iAudioHigh        = loc("iAudioHigh");
    pe.loc_iKick             = loc("iKick");
    pe.loc_iPrev             = loc("iPrev");
}

bool VisualEngine::compileShaderForPostFX(PostEffect& pe,
                                            const std::string& userSrc,
                                            const std::string& label) {
    std::string full = kPostFXPreamble;
    full += userSrc;
    GLuint program = compileShaderProgram(kFullscreenVS, full.c_str(),
                                            label.c_str());
    if (!program) return false;
    if (pe.program) glDeleteProgram(pe.program);
    pe.program = program;
    cachePostFXUniformLocations(pe);

    // Rebuild params, preserving saved values on hot reload.
    auto parsed = parseShaderParams(userSrc);
    std::vector<Param> newParams;
    newParams.reserve(parsed.size());
    for (auto& pp : parsed) {
        Param p;
        p.name          = pp.name;
        p.min           = pp.min;
        p.max           = pp.max;
        p.defaultValue  = pp.defaultValue;
        p.value         = pp.defaultValue;
        for (const auto& old : pe.params) {
            if (old.name == p.name) {
                p.value = std::clamp(old.value, p.min, p.max);
                break;
            }
        }
        p.location = glGetUniformLocation(pe.program, p.name.c_str());
        newParams.push_back(std::move(p));
    }
    pe.params = std::move(newParams);
    return true;
}

void VisualEngine::destroyPostFX(PostEffect& pe) {
    if (pe.program) { glDeleteProgram(pe.program); pe.program = 0; }
}

void VisualEngine::checkPostFXHotReload(PostEffect& pe) {
    if (pe.shaderPath.empty()) return;
    std::error_code ec;
    auto mt = std::filesystem::last_write_time(pe.shaderPath, ec);
    if (ec) return;
    if (!pe.mtimeValid) { pe.mtime = mt; pe.mtimeValid = true; return; }
    if (mt == pe.mtime) return;
    pe.mtime = mt;
    std::ifstream in(pe.shaderPath);
    if (!in) return;
    std::stringstream buf; buf << in.rdbuf();
    LOG_INFO("Visual", "Post-FX changed, reloading: %s", pe.shaderPath.c_str());
    if (!compileShaderForPostFX(pe, buf.str(), pe.shaderPath)) {
        LOG_WARN("Visual", "Post-FX hot-reload failed; keeping old program.");
    }
}

bool VisualEngine::addPostFX(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Visual", "Post-FX: cannot open %s", path.c_str());
        return false;
    }
    std::stringstream buf; buf << in.rdbuf();

    ContextScope scope(m_outputWindow, m_outputContext);
    PostEffect pe;
    pe.shaderPath = path;
    if (!compileShaderForPostFX(pe, buf.str(), path)) return false;

    std::error_code ec;
    auto mt = std::filesystem::last_write_time(path, ec);
    if (!ec) { pe.mtime = mt; pe.mtimeValid = true; }

    m_postFX.push_back(std::move(pe));
    updateAudioWiring();
    LOG_INFO("Visual", "Post-FX added: %s (now %zu in chain)",
             path.c_str(), m_postFX.size());
    return true;
}

void VisualEngine::removePostFX(int index) {
    if (index < 0 || index >= static_cast<int>(m_postFX.size())) return;
    ContextScope scope(m_outputWindow, m_outputContext);
    destroyPostFX(m_postFX[index]);
    m_postFX.erase(m_postFX.begin() + index);
    updateAudioWiring();
}

void VisualEngine::movePostFX(int index, int delta) {
    int n = static_cast<int>(m_postFX.size());
    if (index < 0 || index >= n) return;
    int dst = std::clamp(index + delta, 0, n - 1);
    if (dst == index) return;
    std::swap(m_postFX[index], m_postFX[dst]);
}

std::string VisualEngine::postFXPath(int index) const {
    if (index < 0 || index >= static_cast<int>(m_postFX.size())) return {};
    return m_postFX[index].shaderPath;
}

std::vector<VisualEngine::LayerParamInfo>
VisualEngine::getPostFXParams(int index) const {
    std::vector<LayerParamInfo> out;
    if (index < 0 || index >= static_cast<int>(m_postFX.size())) return out;
    const auto& pe = m_postFX[index];
    out.reserve(pe.params.size());
    for (const auto& p : pe.params) {
        out.push_back({p.name, p.value, p.min, p.max, p.defaultValue});
    }
    return out;
}

void VisualEngine::setPostFXParam(int index, const std::string& name, float value) {
    if (index < 0 || index >= static_cast<int>(m_postFX.size())) return;
    for (auto& p : m_postFX[index].params) {
        if (p.name == name) {
            p.value = std::clamp(value, p.min, p.max);
            return;
        }
    }
}

void VisualEngine::applyPostFXParamValues(int index,
        const std::vector<std::pair<std::string, float>>& values) {
    if (index < 0 || index >= static_cast<int>(m_postFX.size())) return;
    for (auto& p : m_postFX[index].params) {
        for (const auto& kv : values) {
            if (kv.first == p.name) {
                p.value = std::clamp(kv.second, p.min, p.max);
                break;
            }
        }
    }
}

void VisualEngine::checkLayerHotReload(Layer& L) {
    if (L.shaderPath.empty()) return;
    std::error_code ec;
    auto mt = std::filesystem::last_write_time(L.shaderPath, ec);
    if (ec) return;
    if (!L.mtimeValid) { L.mtime = mt; L.mtimeValid = true; return; }
    if (mt == L.mtime) return;
    L.mtime = mt;  // advance before reload to avoid retry storm

    std::ifstream in(L.shaderPath);
    if (!in) { LOG_WARN("Visual", "Hot reload: file gone (%s)", L.shaderPath.c_str()); return; }
    std::stringstream buf; buf << in.rdbuf();
    LOG_INFO("Visual", "Shader changed, reloading: %s", L.shaderPath.c_str());
    if (!compileShaderForLayer(L, buf.str(), L.shaderPath)) {
        LOG_WARN("Visual", "Hot-reload failed; keeping previous shader active.");
    }
}

// ── Audio reactivity (per-layer) ───────────────────────────────────────────

float VisualEngine::rawPeakFor(int source) const {
    if (!m_audioEngine) return 0.0f;
    const auto& mx = m_audioEngine->mixer();
    if (source < 0)
        return std::max(mx.master().peakL, mx.master().peakR);
    if (source < kMaxTracks) {
        const auto& tr = mx.trackChannel(source);
        return std::max(tr.peakL, tr.peakR);
    }
    return 0.0f;
}

VisualEngine::RawBands VisualEngine::rawBandsFor(int source) const {
    if (!m_audioEngine) return {0,0,0};
    const auto& mx = m_audioEngine->mixer();
    const audio::BandAnalyzer* b = nullptr;
    if (source < 0)                 b = &mx.master().bands;
    else if (source < kMaxTracks)   b = &mx.trackChannel(source).bands;
    if (!b) return {0,0,0};
    return { b->peakLow, b->peakMid, b->peakHigh };
}

float VisualEngine::readLayerLevel(Layer& L) {
    float raw = std::clamp(rawPeakFor(L.audioSource), 0.0f, 1.0f);
    constexpr float kAttack = 0.6f, kRelease = 0.08f;
    float k = (raw > L.smoothedLevel) ? kAttack : kRelease;
    L.smoothedLevel += (raw - L.smoothedLevel) * k;
    return L.smoothedLevel;
}

VisualEngine::AudioBands VisualEngine::readLayerBands(Layer& L) {
    RawBands rb = rawBandsFor(L.audioSource);
    float rLow  = std::clamp(rb.low,  0.0f, 1.0f);
    float rMid  = std::clamp(rb.mid,  0.0f, 1.0f);
    float rHigh = std::clamp(rb.high, 0.0f, 1.0f);

    constexpr float kAttack = 0.6f, kRelease = 0.08f;
    auto step = [&](float raw, float& smoothed) {
        float k = (raw > smoothed) ? kAttack : kRelease;
        smoothed += (raw - smoothed) * k;
        return smoothed;
    };
    return { step(rLow, L.smoothedLow),
             step(rMid, L.smoothedMid),
             step(rHigh, L.smoothedHigh) };
}

float VisualEngine::readLayerKick(Layer& L, float tDelta) {
    const float k = std::clamp(tDelta * 4.0f, 0.0f, 1.0f);
    L.kickBaseline += (L.smoothedLow - L.kickBaseline) * k;

    if (L.kickRefractory > 0.0f) L.kickRefractory -= tDelta;
    const float floor = 0.06f, ratio = 1.5f;
    bool rising = L.smoothedLow > L.kickLastLow;
    if (rising && L.kickRefractory <= 0.0f &&
        L.smoothedLow > floor &&
        L.smoothedLow > L.kickBaseline * ratio) {
        L.kickLevel = 1.0f;
        L.kickRefractory = 0.08f;
    }
    L.kickLastLow = L.smoothedLow;

    const float decay = std::exp(-8.0f * std::max(tDelta, 0.0f));
    L.kickLevel *= decay;
    return L.kickLevel;
}

// ── Audio wiring (gate mixer analyzers on layers' sources) ────────────────

void VisualEngine::updateAudioWiring() {
    if (!m_audioEngine) return;
    std::vector<int> sources;
    sources.reserve(m_layers.size() + 1);
    for (auto& [track, layer] : m_layers) {
        if (layer.program == 0) continue;   // no shader = not listening
        sources.push_back(layer.audioSource);
    }
    // Post-FX share a single global master audio feed.
    if (!m_postFX.empty()) sources.push_back(-1);
    const_cast<audio::AudioEngine*>(m_audioEngine)->mixer()
        .setVisualAudioSources(sources);
}

void VisualEngine::setAudioEngine(const audio::AudioEngine* eng) {
    m_audioEngine = eng;
    updateAudioWiring();
}

// ── Audio texture (iChannel0) ─────────────────────────────────────────────

namespace {
void runFFTRadix2(std::vector<float>& re, std::vector<float>& im) {
    const int n = static_cast<int>(re.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / len;
        double wRe = std::cos(angle);
        double wIm = std::sin(angle);
        for (int i = 0; i < n; i += len) {
            double curRe = 1.0, curIm = 0.0;
            for (int j = 0; j < len / 2; ++j) {
                float tRe = static_cast<float>(re[i + j + len/2] * curRe -
                                                 im[i + j + len/2] * curIm);
                float tIm = static_cast<float>(re[i + j + len/2] * curIm +
                                                 im[i + j + len/2] * curRe);
                re[i + j + len/2] = re[i + j] - tRe;
                im[i + j + len/2] = im[i + j] - tIm;
                re[i + j] += tRe;
                im[i + j] += tIm;
                double newRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = newRe;
            }
        }
    }
}
} // anon

void VisualEngine::updateAudioTexture() {
    if (!m_audioEngine || !m_audioTex) return;

    static thread_local std::vector<float> samples(audio::Mixer::kVisTapSize);
    m_audioEngine->mixer().readVisualSamples(samples.data(),
                                               audio::Mixer::kVisTapSize);

    const int n = audio::Mixer::kVisTapSize;
    static const std::vector<float> window = [&]{
        std::vector<float> w(n);
        for (int i = 0; i < n; ++i)
            w[i] = 0.5f * (1.0f - std::cos(2.0 * M_PI * i / (n - 1)));
        return w;
    }();
    static thread_local std::vector<float> fftRe, fftIm;
    if ((int)fftRe.size() != n) { fftRe.assign(n, 0.0f); fftIm.assign(n, 0.0f); }
    for (int i = 0; i < n; ++i) {
        fftRe[i] = samples[i] * window[i];
        fftIm[i] = 0.0f;
    }
    runFFTRadix2(fftRe, fftIm);

    const int spectrumBins = kAudioTexW;
    for (int i = 0; i < spectrumBins; ++i) {
        float re = fftRe[i], im = fftIm[i];
        float mag = std::sqrt(re * re + im * im) / static_cast<float>(n);
        float dB = 20.0f * std::log10(std::max(mag, 1e-6f));
        float norm = std::clamp((dB + 96.0f) / 96.0f, 0.0f, 1.0f);
        m_audioTexPixels[i] = static_cast<unsigned char>(norm * 255.0f);
    }
    const int waveStart = n - kAudioTexW;
    for (int i = 0; i < kAudioTexW; ++i) {
        float s = samples[waveStart + i];
        float centered = std::clamp(s * 0.5f + 0.5f, 0.0f, 1.0f);
        m_audioTexPixels[kAudioTexW + i] =
            static_cast<unsigned char>(centered * 255.0f);
    }

    glBindTexture(GL_TEXTURE_2D, m_audioTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAudioTexW, kAudioTexH,
                    GL_RED, GL_UNSIGNED_BYTE, m_audioTexPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── Per-layer render ───────────────────────────────────────────────────────

void VisualEngine::renderLayerToFBO(Layer& L, double transportSeconds,
                                      double transportBeats, bool playing) {
    if (!L.program || !L.fbo) return;

    // Clock selection: wall-clock for session launches, transport-beat
    // for arrangement launches (so scrubbing the playhead seeks the
    // visuals). Beats → seconds via the audio engine's current BPM —
    // BPM changes mid-clip gently rescale iTime, which is acceptable
    // loose-sync behavior for visuals.
    double preWall = 0.0;
    if (L.transportDriven && m_audioEngine) {
        const double bpm = std::max(1.0, m_audioEngine->transport().bpm());
        const double clipBeats = transportBeats - L.clipStartBeat;
        // Clamp to zero if the user scrubs before the clip starts.
        // clipBeats can also go negative momentarily between an arm
        // and the first pollArrangementVisualPlayback tick; guard too.
        preWall = std::max(0.0, clipBeats) * 60.0 / bpm;
    } else {
        preWall = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - L.wallStart).count();
    }
    // tDelta can be negative when the user scrubs backwards — clamp so
    // the spin integrator doesn't accumulate massive negative values
    // that then take seconds to burn off. Scrubbing visually jumps;
    // it doesn't need to feed continuous-velocity effects.
    const float  tDelta  = std::max(0.0f,
                                      static_cast<float>(preWall - L.lastWallSeconds));

    // Audio reactivity: sampled once per frame because readLayerKick /
    // readLayerLevel mutate the layer's envelope/decay state. The 3D
    // pass (scene script) and the later shader-uniform block both
    // consume these same values.
    const float audioLevel = readLayerLevel(L);
    const auto  audioBands = readLayerBands(L);
    const float audioKick  = readLayerKick(L, tDelta);

#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
    // 3D pass: render the layer's model into the M3DRenderer's own FBO
    // so its color attachment is ready to bind as iChannel2.
    //
    // Two modes:
    //   • No scene script → single instance, pose driven by @range
    //     uniforms (modelPos*, modelRot*, modelSpin*, modelScale).
    //   • Scene script    → the script's tick() callback returns a
    //     list of transforms, one draw call per entry, accumulating
    //     into the FBO with the shared depth buffer.
    if (L.modelRenderer && L.modelRenderer->hasModel()) {
        auto readParam = [&](const char* name, float fallback) -> float {
            for (const auto& p : L.params) {
                if (p.name == name) return p.value;
            }
            return fallback;
        };

        if (L.sceneScript) {
            L.sceneScript->pollHotReload();
            M3DSceneScript::Inputs in;
            in.time       = static_cast<float>(preWall);
            in.beat       = static_cast<float>(transportBeats);
            in.playing    = playing;
            in.audioLevel = audioLevel;
            in.audioLow   = audioBands.low;
            in.audioMid   = audioBands.mid;
            in.audioHigh  = audioBands.high;
            in.kick       = audioKick;
            for (int i = 0; i < 8; ++i) in.knobs[i] = L.knobDisplayValues[i];

            static thread_local std::vector<M3DTransform> instances;
            instances.clear();
            L.sceneScript->tick(in, instances);

            L.modelRenderer->beginFrame(static_cast<float>(preWall));
            for (const auto& inst : instances) {
                L.modelRenderer->drawInstance(inst);
            }
            L.modelRenderer->endFrame();
        } else {
            // Static path — integrate spin and read @range uniforms.
            float spinX = readParam("modelSpinX", 0.0f);
            float spinY = readParam("modelSpinY", 0.0f);
            float spinZ = readParam("modelSpinZ", 0.0f);
            L.modelSpinAccum[0] = std::fmod(L.modelSpinAccum[0] + spinX * tDelta, 360.0f);
            L.modelSpinAccum[1] = std::fmod(L.modelSpinAccum[1] + spinY * tDelta, 360.0f);
            L.modelSpinAccum[2] = std::fmod(L.modelSpinAccum[2] + spinZ * tDelta, 360.0f);

            M3DRenderer::Transform xf;
            xf.position[0]    = readParam("modelPosX", 0.0f);
            xf.position[1]    = readParam("modelPosY", 0.0f);
            xf.position[2]    = readParam("modelPosZ", 0.0f);
            xf.rotationDeg[0] = readParam("modelRotX", 0.0f) + L.modelSpinAccum[0];
            xf.rotationDeg[1] = readParam("modelRotY", 0.0f) + L.modelSpinAccum[1];
            xf.rotationDeg[2] = readParam("modelRotZ", 0.0f) + L.modelSpinAccum[2];
            xf.scale          = readParam("modelScale", 1.0f);
            // Use the begin/draw/end form (rather than the convenience
            // render(xf)) so wall-clock time drives skeletal animation
            // on rigged models even without a scene script.
            L.modelRenderer->beginFrame(static_cast<float>(preWall));
            L.modelRenderer->drawInstance(xf);
            L.modelRenderer->endFrame();
        }
    }
#endif

    // tDelta was computed up top so the 3D pass could use it; just
    // finalize the per-layer wall clock here for shader uniforms.
    const float tSecs  = static_cast<float>(preWall);
    L.lastWallSeconds  = preWall;

    // One-shot diagnostic: log per-layer text state the first time it
    // renders with a non-empty text string. Helps diagnose "black screen"
    // reports by showing whether the texture actually made it to the GPU.
    if (!L.textDiagLogged && !L.currentText.empty()) {
        L.textDiagLogged = true;
        LOG_INFO("Visual",
                 "Layer text-channel diag: text=\"%s\" textTex=%u "
                 "loc_iChannel1=%d textPixelW=%d",
                 L.currentText.c_str(),
                 static_cast<unsigned>(L.textTex),
                 static_cast<int>(L.loc_iChannel[1]),
                 L.textPixelW);
    }

    // Compute knob (base + LFO) values once per frame — chain passes
    // share the same A..H macros, so it'd be wasteful to evaluate the
    // LFOs N times. The per-pass loop just uploads the cached value.
    for (int i = 0; i < 8; ++i) {
        float v = L.knobValues[i];
        if (L.knobLFOs[i].enabled) {
            float mod = L.knobLFOs[i].evaluate(transportBeats, preWall);
            v = std::clamp(v + mod * L.knobLFOs[i].depth, 0.0f, 1.0f);
        }
        L.knobDisplayValues[i] = v;  // cached for the UI
    }

    // iChannel2 = video frame if attached, else dummy black.
    // Live source: just pull the newest frame. latestFrame() returns false
    // when nothing is ready yet (pre-connect) or when no new frame has
    // arrived since the last tick — no GL upload either way.
    if (L.liveVideo && L.videoTex) {
        static thread_local std::vector<uint8_t> liveRGBA;
        const size_t need = size_t(LiveVideoSource::kWidth) *
                             LiveVideoSource::kHeight * 4;
        if (liveRGBA.size() != need) liveRGBA.assign(need, 0);
        if (L.liveVideo->latestFrame(liveRGBA.data())) {
            glBindTexture(GL_TEXTURE_2D, L.videoTex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                             LiveVideoSource::kWidth,
                             LiveVideoSource::kHeight,
                             GL_RGBA, GL_UNSIGNED_BYTE, liveRGBA.data());
        }
    }
    if (L.video && L.video->isOpen() && L.videoTex) {
        const double fps   = L.video->fps() > 1.0 ? L.video->fps() : 30.0;
        const int    count = L.video->frameCount();
        const float  rate  = (L.videoRate > 0.0f) ? L.videoRate : 1.0f;

        // Trim in/out in frames. At most the full source; caller clamps
        // fractions but be defensive.
        int inFrame  = static_cast<int>(std::clamp(L.videoIn,  0.0f, 1.0f) * count);
        int outFrame = static_cast<int>(std::clamp(L.videoOut, 0.0f, 1.0f) * count);
        if (outFrame <= inFrame) outFrame = std::min(count, inFrame + 1);
        int rangeLen = std::max(1, outFrame - inFrame);

        int targetFrame = inFrame;
        if (L.transportDriven) {
            // Arrangement mode: frame index is clip-relative and
            // directly follows the transport playhead, so scrubbing
            // seeks to the exact frame the user hovers.
            const double elapsed = std::max(0.0, preWall);
            int advanced = static_cast<int>(elapsed * fps * rate);
            targetFrame = inFrame + (advanced % std::max(1, rangeLen));
        } else if (L.videoLoopBars > 0 && count > 0) {
            // Transport-locked: N bars worth of beats maps to the whole
            // playable range, regardless of the video's native duration.
            if (L.videoLaunchBeats == 0.0) L.videoLaunchBeats = transportBeats;
            double elapsedBeats = transportBeats - L.videoLaunchBeats;
            double loopBeats = static_cast<double>(L.videoLoopBars) * 4.0;
            if (loopBeats < 0.0001) loopBeats = 1.0;
            double phase = std::fmod(elapsedBeats, loopBeats);
            if (phase < 0.0) phase += loopBeats;
            double norm = phase / loopBeats;   // 0..1 across the loop
            targetFrame = inFrame + static_cast<int>(norm * rangeLen * rate) % rangeLen;
        } else {
            // Free-running at source fps × rate inside the trimmed range.
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - L.videoLaunchTime).count();
            int advanced = static_cast<int>(elapsed * fps * rate);
            targetFrame = inFrame + (advanced % rangeLen);
        }

        if (targetFrame != L.lastVideoFrame) {
            static thread_local std::vector<uint8_t> rgba;
            const size_t need = VideoDecoder::kWidth * VideoDecoder::kHeight * 4;
            if (rgba.size() != need) rgba.assign(need, 0);
            if (L.video->decodeFrame(targetFrame, rgba.data())) {
                glBindTexture(GL_TEXTURE_2D, L.videoTex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                 VideoDecoder::kWidth, VideoDecoder::kHeight,
                                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                L.lastVideoFrame = targetFrame;
            }
        }
    }
    // Resolve which texture iChannel2 sees (model > video > dummy).
    GLuint iCh2Tex = m_dummyChannelTex[2];
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
    if (L.modelRenderer && L.modelRenderer->hasModel() &&
        L.modelRenderer->colorTexture()) {
        iCh2Tex = L.modelRenderer->colorTexture();
    } else
#endif
    if ((L.video || L.liveVideo) && L.videoTex) {
        iCh2Tex = L.videoTex;
    }

    // Render one pass into `targetFBO`. Templated over the uniform-loc
    // source so it works for pass 0 (Layer fields) and chain passes
    // (ChainPass fields) — both have identical loc field names + the
    // same param schema. iPrevTex is the previous pass's output (for
    // chain passes); pass 0's iPrev binds to a dummy black texture.
    auto renderPass = [&](GLuint program, auto& src,
                          GLuint targetFBO, GLuint iPrevTex,
                          const std::vector<Param>& params) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glViewport(0, 0, kInternalWidth, kInternalHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);

        if (src.loc_iResolution >= 0)
            glUniform3f(src.loc_iResolution,
                        static_cast<float>(kInternalWidth),
                        static_cast<float>(kInternalHeight), 1.0f);
        if (src.loc_iTime       >= 0) glUniform1f(src.loc_iTime, tSecs);
        if (src.loc_iTimeDelta  >= 0) glUniform1f(src.loc_iTimeDelta, tDelta);
        if (src.loc_iFrame      >= 0) glUniform1i(src.loc_iFrame, L.frameCounter);
        if (src.loc_iMouse      >= 0) glUniform4f(src.loc_iMouse, 0.0f, 0.0f, 0.0f, 0.0f);

        {
            std::time_t now = std::time(nullptr);
            std::tm lt = *std::localtime(&now);
            const float secondsOfDay =
                static_cast<float>(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
            if (src.loc_iDate >= 0)
                glUniform4f(src.loc_iDate,
                            static_cast<float>(lt.tm_year + 1900),
                            static_cast<float>(lt.tm_mon),
                            static_cast<float>(lt.tm_mday),
                            secondsOfDay);
        }

        if (src.loc_iSampleRate       >= 0) glUniform1f(src.loc_iSampleRate, 44100.0f);
        if (src.loc_iBeat             >= 0) glUniform1f(src.loc_iBeat, static_cast<float>(transportBeats));
        if (src.loc_iTransportPlaying >= 0) glUniform1f(src.loc_iTransportPlaying, playing ? 1.0f : 0.0f);
        if (src.loc_iTransportTime    >= 0) glUniform1f(src.loc_iTransportTime, static_cast<float>(transportSeconds));

        if (src.loc_iAudioLevel >= 0) glUniform1f(src.loc_iAudioLevel, audioLevel);
        if (src.loc_iAudioLow   >= 0) glUniform1f(src.loc_iAudioLow,   audioBands.low);
        if (src.loc_iAudioMid   >= 0) glUniform1f(src.loc_iAudioMid,   audioBands.mid);
        if (src.loc_iAudioHigh  >= 0) glUniform1f(src.loc_iAudioHigh,  audioBands.high);
        if (src.loc_iKick       >= 0) glUniform1f(src.loc_iKick,       audioKick);

        // iChannel0..3 — the underlying GL texture bindings on TU0..3
        // are stable across passes (we set them at the same time we
        // upload uniforms here so program state stays self-contained).
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_audioTex ? m_audioTex : m_dummyChannelTex[0]);
        if (src.loc_iChannel[0] >= 0) glUniform1i(src.loc_iChannel[0], 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, L.textTex ? L.textTex : m_dummyChannelTex[1]);
        if (src.loc_iChannel[1] >= 0) glUniform1i(src.loc_iChannel[1], 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, iCh2Tex);
        if (src.loc_iChannel[2] >= 0) glUniform1i(src.loc_iChannel[2], 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_dummyChannelTex[3]);
        if (src.loc_iChannel[3] >= 0) glUniform1i(src.loc_iChannel[3], 3);

        // iPrev — sampler unit 4 so it doesn't clobber iChannel3.
        // For pass 0 we point it at the dummy black texture; chain
        // passes get the previous output.
        if (src.loc_iPrev >= 0) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D,
                          iPrevTex ? iPrevTex : m_dummyChannelTex[3]);
            glUniform1i(src.loc_iPrev, 4);
        }

        // iFeedback — sampler unit 5. Persistent texture holding the
        // previous *frame*'s final chain output (lazily allocated by
        // the layer-level needsFeedback check just above this lambda).
        // Falls back to dummy black when the layer never opted in.
        if (src.loc_iFeedback >= 0) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D,
                          L.feedbackTex ? L.feedbackTex : m_dummyChannelTex[3]);
            glUniform1i(src.loc_iFeedback, 5);
        }

        if (src.loc_iChannelResolution >= 0) {
            const float res[12] = {
                static_cast<float>(kAudioTexW), static_cast<float>(kAudioTexH), 1.0f,
                static_cast<float>(kTextTexW),  static_cast<float>(kTextTexH),  1.0f,
                1.0f, 1.0f, 1.0f,
                1.0f, 1.0f, 1.0f,
            };
            glUniform3fv(src.loc_iChannelResolution, 4, res);
        }
        if (src.loc_iTextWidth >= 0) {
            float w = (L.textPixelW > 0) ? static_cast<float>(L.textPixelW)
                                          : static_cast<float>(kTextTexW);
            glUniform1f(src.loc_iTextWidth, w);
        }
        if (src.loc_iTextTexWidth >= 0)
            glUniform1f(src.loc_iTextTexWidth, static_cast<float>(kTextTexW));
        if (src.loc_iChannelTime >= 0) {
            const float t[4] = {tSecs, tSecs, tSecs, tSecs};
            glUniform1fv(src.loc_iChannelTime, 4, t);
        }

        // A..H knob macros (shared across passes — values were
        // computed once above into knobDisplayValues).
        for (int i = 0; i < 8; ++i)
            if (src.loc_knobs[i] >= 0)
                glUniform1f(src.loc_knobs[i], L.knobDisplayValues[i]);

        // Per-pass user-declared @range params.
        for (const auto& p : params)
            if (p.location >= 0) glUniform1f(p.location, p.value);

        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    // Lazy feedback-texture allocation. Walk every pass once and
    // check whether anybody actually declared `iFeedback` — only then
    // do we allocate the persistent texture and pay the per-frame
    // copy cost at the bottom of this function.
    bool needsFeedback = (L.loc_iFeedback >= 0);
    if (!needsFeedback) {
        for (const auto& cp : L.additionalPasses) {
            if (cp.loc_iFeedback >= 0) { needsFeedback = true; break; }
        }
    }
    if (needsFeedback) ensureFeedbackTex(L);

    // Render the chain. Single-pass: write straight into L.fbo, no
    // ping-pong allocation. Multi-pass: pass 0 → pingFBO[0], chain
    // passes ping-pong, last pass writes into L.fbo so the composite
    // stage downstream is unchanged.
    if (L.additionalPasses.empty()) {
        renderPass(L.program, L, L.fbo, 0, L.params);
    } else if (!ensurePingFBOs(L)) {
        // Allocation failed — degrade to single-pass to keep showing
        // something rather than blanking the layer.
        renderPass(L.program, L, L.fbo, 0, L.params);
    } else {
        // Count active (non-bypassed) passes up front so we know
        // which one is "last" (writes into L.fbo) when some are
        // bypassed. activeI counts only non-bypassed passes encountered
        // so far, keeping ping-pong slot alternation stable across
        // bypass holes.
        size_t activeCount = 0;
        for (const auto& cp : L.additionalPasses)
            if (!cp.bypassed) ++activeCount;

        if (activeCount == 0) {
            // Every chain pass bypassed → behaves like a single-pass
            // render. Write directly into L.fbo.
            renderPass(L.program, L, L.fbo, 0, L.params);
        } else {
            renderPass(L.program, L, L.pingFBO[0], 0, L.params);
            size_t activeI = 0;
            for (size_t i = 0; i < L.additionalPasses.size(); ++i) {
                auto& cp = L.additionalPasses[i];
                if (cp.bypassed) continue;
                const int prevSlot = static_cast<int>(activeI) % 2;
                const int currSlot = (static_cast<int>(activeI) + 1) % 2;
                const bool isLast  = (activeI + 1 == activeCount);
                const GLuint target = isLast ? L.fbo : L.pingFBO[currSlot];
                renderPass(cp.program, cp, target, L.pingTex[prevSlot], cp.params);
                ++activeI;
            }
        }
    }

    // Capture this frame's final chain output into the persistent
    // feedback texture so the *next* frame's iFeedback samples it.
    // glCopyTexSubImage2D works in GL 3.3 (we'd otherwise need
    // glCopyImageSubData from 4.3 or a blit-FBO dance). Source = the
    // layer's main FBO (now holding the final pass output); dest =
    // the feedback texture. Skip entirely when no pass uses it.
    if (needsFeedback && L.feedbackTex) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, L.fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindTexture(GL_TEXTURE_2D, L.feedbackTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                             kInternalWidth, kInternalHeight);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    ++L.frameCounter;
}

// ── tick (orchestrates all layers + composite) ────────────────────────────

void VisualEngine::tick(double transportSeconds, double transportBeats, bool playing) {
    if (!m_initialized || !m_outputVisible || !m_outputWindow) return;

    ContextScope scope(m_outputWindow, m_outputContext);

    // Build ordered list of active layers (track idx ascending = bottom→top).
    std::vector<int> trackOrder;
    trackOrder.reserve(m_layers.size());
    for (auto& [track, layer] : m_layers)
        if (layer.program) trackOrder.push_back(track);
    std::sort(trackOrder.begin(), trackOrder.end());

    // Refresh master FFT texture once per frame (used by all layers).
    updateAudioTexture();

    // Stage 1: render each layer into its own FBO.
    for (int t : trackOrder) {
        Layer& L = m_layers[t];
        checkLayerHotReload(L);
        renderLayerToFBO(L, transportSeconds, transportBeats, playing);
    }

    // Stage 2: ping-pong composite. Accumulator A starts black; for each
    // layer, sample A + layer → write to B with blend math; swap. The
    // final A is blitted to the output window.
    glBindVertexArray(m_vao);
    glDisable(GL_BLEND);

    int cur = 0;  // reads from accumTex[cur], writes to accumFBO[1-cur]
    // Clear both accumulators to black at the start of each frame — cheaper
    // to clear one at a time as needed, but both lets us flip freely.
    for (int i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_accumFBO[i]);
        glViewport(0, 0, kInternalWidth, kInternalHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glUseProgram(m_compositeProgram);
    for (int t : trackOrder) {
        Layer& L = m_layers[t];

        float opacity = 1.0f;
        if (m_audioEngine && t < kMaxTracks) {
            opacity = std::clamp(m_audioEngine->mixer().trackChannel(t).volume,
                                  0.0f, 1.0f);
        }
        if (opacity <= 0.0f) continue;

        int next = 1 - cur;
        glBindFramebuffer(GL_FRAMEBUFFER, m_accumFBO[next]);
        glViewport(0, 0, kInternalWidth, kInternalHeight);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, L.fboTex);       // uSrc
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_accumTex[cur]); // uDst

        if (m_comp_src_loc   >= 0) glUniform1i(m_comp_src_loc, 0);
        if (m_comp_dst_loc   >= 0) glUniform1i(m_comp_dst_loc, 1);
        if (m_comp_alpha_loc >= 0) glUniform1f(m_comp_alpha_loc, opacity);
        if (m_comp_mode_loc  >= 0)
            glUniform1i(m_comp_mode_loc, static_cast<int>(L.blendMode));

        glDrawArrays(GL_TRIANGLES, 0, 3);
        cur = next;
    }

    // Stage 3: post-FX chain. Each effect samples the previous stage's
    // accumulator as iPrev and writes into the other — classic ping-pong
    // reusing the same two FBOs we already keep around for compositing.
    if (!m_postFX.empty()) {
        // Compute shared per-frame post-fx audio state (envelope-smoothed
        // master bands + kick). Matches the layer envelope treatment.
        const float wallSecsF = static_cast<float>(
            std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        const float tDelta = 1.0f / 60.0f;  // rough; only used for kick decay

        auto envStep = [](float raw, float& smoothed) {
            constexpr float kAttack = 0.6f, kRelease = 0.08f;
            float k = (raw > smoothed) ? kAttack : kRelease;
            smoothed += (raw - smoothed) * k;
            return smoothed;
        };

        float rawLevel = 0.0f, rawLow = 0.0f, rawMid = 0.0f, rawHigh = 0.0f;
        if (m_audioEngine) {
            const auto& mx = m_audioEngine->mixer();
            rawLevel = std::clamp(std::max(mx.master().peakL, mx.master().peakR), 0.0f, 1.0f);
            rawLow   = std::clamp(mx.master().bands.peakLow,  0.0f, 1.0f);
            rawMid   = std::clamp(mx.master().bands.peakMid,  0.0f, 1.0f);
            rawHigh  = std::clamp(mx.master().bands.peakHigh, 0.0f, 1.0f);
        }
        envStep(rawLevel, m_postSmoothedLevel);
        envStep(rawLow,   m_postSmoothedLow);
        envStep(rawMid,   m_postSmoothedMid);
        envStep(rawHigh,  m_postSmoothedHigh);

        // Kick detector on smoothed low band.
        {
            float k = std::clamp(tDelta * 4.0f, 0.0f, 1.0f);
            m_postKickBaseline += (m_postSmoothedLow - m_postKickBaseline) * k;
            if (m_postKickRefractory > 0.0f) m_postKickRefractory -= tDelta;
            bool rising = m_postSmoothedLow > m_postKickLastLow;
            if (rising && m_postKickRefractory <= 0.0f &&
                m_postSmoothedLow > 0.06f &&
                m_postSmoothedLow > m_postKickBaseline * 1.5f) {
                m_postKickLevel = 1.0f;
                m_postKickRefractory = 0.08f;
            }
            m_postKickLastLow = m_postSmoothedLow;
            m_postKickLevel *= std::exp(-8.0f * std::max(tDelta, 0.0f));
        }

        for (auto& pe : m_postFX) {
            if (!pe.program) continue;
            checkPostFXHotReload(pe);

            int next = 1 - cur;
            glBindFramebuffer(GL_FRAMEBUFFER, m_accumFBO[next]);
            glViewport(0, 0, kInternalWidth, kInternalHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(pe.program);

            if (pe.loc_iResolution >= 0)
                glUniform3f(pe.loc_iResolution,
                            static_cast<float>(kInternalWidth),
                            static_cast<float>(kInternalHeight), 1.0f);
            const float tSecs = static_cast<float>(
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            (void)wallSecsF;
            if (pe.loc_iTime             >= 0) glUniform1f(pe.loc_iTime, tSecs);
            if (pe.loc_iTimeDelta        >= 0) glUniform1f(pe.loc_iTimeDelta, tDelta);
            if (pe.loc_iFrame            >= 0) glUniform1i(pe.loc_iFrame, pe.frameCounter);
            if (pe.loc_iBeat             >= 0) glUniform1f(pe.loc_iBeat, static_cast<float>(transportBeats));
            if (pe.loc_iTransportTime    >= 0) glUniform1f(pe.loc_iTransportTime, static_cast<float>(transportSeconds));
            if (pe.loc_iTransportPlaying >= 0) glUniform1f(pe.loc_iTransportPlaying, playing ? 1.0f : 0.0f);
            if (pe.loc_iAudioLevel       >= 0) glUniform1f(pe.loc_iAudioLevel, m_postSmoothedLevel);
            if (pe.loc_iAudioLow         >= 0) glUniform1f(pe.loc_iAudioLow,   m_postSmoothedLow);
            if (pe.loc_iAudioMid         >= 0) glUniform1f(pe.loc_iAudioMid,   m_postSmoothedMid);
            if (pe.loc_iAudioHigh        >= 0) glUniform1f(pe.loc_iAudioHigh,  m_postSmoothedHigh);
            if (pe.loc_iKick             >= 0) glUniform1f(pe.loc_iKick,       m_postKickLevel);

            for (const auto& p : pe.params) {
                if (p.location >= 0) glUniform1f(p.location, p.value);
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_accumTex[cur]);
            if (pe.loc_iPrev >= 0) glUniform1i(pe.loc_iPrev, 0);

            glDrawArrays(GL_TRIANGLES, 0, 3);
            ++pe.frameCounter;
            cur = next;
        }
    }

    // Stage 4: blit the final accumulator to the output window.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int winW = 0, winH = 0;
    SDL_GetWindowSizeInPixels(m_outputWindow, &winW, &winH);
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_blitProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_accumTex[cur]);
    if (m_blit_tex_loc >= 0) glUniform1i(m_blit_tex_loc, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    SDL_GL_SwapWindow(m_outputWindow);
}

// ── Free-accessor registrar (header in VisualEngineAPI.h) ─────────────────
//
// Fills in the PostFXAPI function-pointer table at static-init time so
// yawn_core's stub accessors start calling the real implementations.
// yawn_tests doesn't link this translation unit, so the table stays
// null there and post-fx serialisation becomes a no-op — which is what
// the tests want.

namespace {
int impl_count(const VisualEngine& e)                    { return e.numPostFX(); }
std::string impl_path(const VisualEngine& e, int i)      { return e.postFXPath(i); }
void impl_clear(VisualEngine& e) {
    while (e.numPostFX() > 0) e.removePostFX(0);
}
bool impl_add(VisualEngine& e, const std::string& path)  { return e.addPostFX(path); }
std::vector<std::pair<std::string, float>>
impl_getParamValues(const VisualEngine& e, int index) {
    std::vector<std::pair<std::string, float>> out;
    auto params = e.getPostFXParams(index);
    out.reserve(params.size());
    for (const auto& p : params) out.emplace_back(p.name, p.value);
    return out;
}
void impl_applyParamValues(VisualEngine& e, int index,
        const std::vector<std::pair<std::string, float>>& values) {
    e.applyPostFXParamValues(index, values);
}

struct Registrar {
    Registrar() {
        auto& t = postFXAPITable();
        t.count             = &impl_count;
        t.path              = &impl_path;
        t.clear             = &impl_clear;
        t.add               = &impl_add;
        t.getParamValues    = &impl_getParamValues;
        t.applyParamValues  = &impl_applyParamValues;
    }
};
// Static-init fills the dispatch table when the main exe loads.
static Registrar g_registrar;
} // anonymous

} // namespace visual
} // namespace yawn
