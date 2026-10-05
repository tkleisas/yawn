#include "visual/ShaderTestKernel.h"
#include "ui/GlCaps.h"

#include <glad/gl.h>
#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <fstream>
#include <sstream>

namespace yawn {
namespace visual {
namespace shadertest {

static constexpr const char* kFullscreenVS = R"GLSL(
out vec2 vUV;
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0,
                  gl_VertexID == 2 ? 3.0 : -1.0);
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

static const char* kShaderToyPreamble = R"GLSL(
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

// Live-coding improvisation ghosts (phase B): iGhostN = upcoming fires
// in schedule order — vec4(pitch/127, velocity 0..1, beats until the
// fire moment, track index). iGhostCount carries the valid entry count
// (0..8). iBeatBarFrac is the 0..1 position inside the current bar.
uniform float iGhostCount;
uniform vec4 iGhost0;
uniform vec4 iGhost1;
uniform vec4 iGhost2;
uniform vec4 iGhost3;
uniform vec4 iGhost4;
uniform vec4 iGhost5;
uniform vec4 iGhost6;
uniform vec4 iGhost7;
uniform float iBeatBarFrac;

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

std::string buildUserSource(const std::string& userSrc) {
    // YAWN_TEST_SKIP_PRE=1 removes the preamble — the bisect step that
    // decides whether the preamble + a specific user source interact.
    static const bool skipPre =
        std::getenv("YAWN_TEST_SKIP_PRE") != nullptr;
    if (skipPre) return userSrc;
    return std::string(kShaderToyPreamble) + "\n" + userSrc;
}

// ── Context lifecycle ──────────────────────────────────────────────────

bool initContext(std::string& err) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        err = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* w = SDL_CreateWindow("shader probe", 64, 64,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!w) { err = std::string("window: ") + SDL_GetError(); return false; }
    SDL_GLContext ctx = SDL_GL_CreateContext(w);
    if (!ctx) { err = std::string("context: ") + SDL_GetError(); return false; }
    const int version = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
    if (version == 0) {
        err = "glad load failed";
        return false;
    }
    // Publish the same caps the engine compiles against.
    ui::GlCaps::major = GLAD_VERSION_MAJOR(version);
    ui::GlCaps::minor = GLAD_VERSION_MINOR(version);
    ui::GlCaps::extTextureSwizzle  = GLAD_GL_ARB_texture_swizzle != 0;
    ui::GlCaps::extInstancedArrays = GLAD_GL_ARB_instanced_arrays != 0;
    return true;
}

void shutdownContext() {
    SDL_Quit();
}

// ── Engine-identical compile ───────────────────────────────────────────
static bool YAWN_kernel_trace() {
    return std::getenv("YAWN_KERNEL_TRACE") != nullptr;
}

bool compileFragment(const std::string& name, const std::string& userSrc,
                     std::string& errOut) {
    static bool firstDumpDone = false;
    const std::string full = buildUserSource(userSrc);
    if (name == "31_ghost_accent.frag" && !firstDumpDone) {
        firstDumpDone = true;
        std::ofstream df("kernel_full_dump.frag", std::ios::binary);
        df << full;
    }

    auto compileOne = [](GLenum type, const char* stageSrc,
                         const char* typeName, std::string& errOut) -> GLuint {
        GLuint out = glCreateShader(type);
        const char* sources[2] = { ui::GlCaps::glslVersionLine(), stageSrc };
        glShaderSource(out, 2, sources, nullptr);

        glCompileShader(out);
        GLint ok = 0;
        glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
        if (std::getenv("YAWN_KERNEL_TRACE"))
            errOut += std::string("[trace] ") + typeName + " ok=" +
                      std::to_string(ok) + " len=" +
                      std::to_string(stageSrc ? strlen(stageSrc) : 0) + "\n";
        if (!ok) {
            GLint len = 0;
            glGetShaderiv(out, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len > 0 ? len : 1);
            glGetShaderInfoLog(out, len, nullptr, log.data());
            errOut += std::string(typeName) + " compile failed:\n";
            errOut += log.data();
            // Map the first "0:N(M)" onto THE STAGE'S OWN source text so
            // the failing line quoted by the driver is human-readable.
            const std::string zone = log.data();
            const size_t colon = zone.find("0:");
            if (colon != std::string::npos) {
                const size_t close = zone.find('(', colon);
                if (close != std::string::npos && close > colon + 2) {
                    const int wantLine =
                        std::atoi(zone.substr(colon + 2,
                                              close - colon - 2).c_str());
                    const std::string stageTxt = stageSrc;
                    size_t pos = 0;
                    int ln = 1;
                    while (ln <= wantLine && pos <= stageTxt.size()) {
                        const size_t nl = stageTxt.find('\n', pos);
                        const size_t end =
                            nl != std::string::npos ? nl : stageTxt.size();
                        if (ln == wantLine) {
                            errOut += ">>> |line " +
                                      std::to_string(wantLine) + "| = |" +
                                      stageTxt.substr(pos, end - pos) + "|\n";
                            break;
                        }
                        pos = end + 1;
                        ++ln;
                    }
                }
            }
            glDeleteShader(out);
            return 0;
        }
        return out;
    };

    // Diagnostic toggle: skip the vertex stage to A/B the driver's
    // behavior with/without a preceding shader object in the context.
    static const bool skipVs = std::getenv("YAWN_TEST_SKIP_VS") != nullptr;
    GLuint vs = 0;
    GLuint fs = 0;
    if (!skipVs)
        vs = compileOne(GL_VERTEX_SHADER, kFullscreenVS, "vertex", errOut);
    fs = compileOne(GL_FRAGMENT_SHADER, full.c_str(), "fragment", errOut);
    (void)vs;
    const bool vsOk = (vs != 0);
    const bool fsOk = (fs != 0);
    bool ok = vsOk && fsOk;
    if (ok) {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        GLint lok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &lok);
        if (YAWN_kernel_trace())
            errOut += std::string("[trace] link lok=") +
                      std::to_string(lok) + "\n";
        if (!lok) {
            GLint len = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len > 0 ? len : 1);
            glGetProgramInfoLog(prog, len, nullptr, log.data());
            errOut += std::string("link failed:\n") + log.data();
            ok = false;
        }
        glDeleteProgram(prog);
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return ok;
}

} // namespace shadertest
} // namespace visual
} // namespace yawn
