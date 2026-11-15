// VisualProbe — look-iteration harness for the visual shader family.
//
// Renders one shader or a layered recipe at the engine's internal
// 640×360 through the PRODUCTION compile path (the same preamble the
// visual engine uses — src/visual/ShaderTestKernel.cpp), with:
//   • a seed picture bound to iChannel2 (any stb-supported image; a
//     deterministic procedural gradient is generated when none is given)
//   • deterministic fake audio (iAudioLevel/Low/Mid/High, iKick) and
//     transport (iBeat, iTransportTime) so any time t is reproducible
//   • per-layer iFeedback ping-pong (previous frame) like the engine
//   • CPU layer compositing with the engine's four blend modes
//   • an optional post-FX chain (passes sample iPrev; the chain keeps its
//     previous frame as iFeedback)
// and writes PNG frames to an output directory.
//
// Usage:
//   yawn_visual_probe --shader assets/shaders/examples/14_kaleidoscope.frag \
//       --image seed.jpg --out /tmp/kframes --frames 4
//
//   yawn_visual_probe \
//       --layer assets/shaders/examples/mandala_seed.frag \
//       --layer assets/shaders/examples/thangka_frame.frag \
//           --blend add --opacity 0.8 \
//       --post assets/shaders/post/bloom.frag \
//       --post assets/shaders/post/chroma.frag \
//       --image seed.jpg --frames 6 --set segments=0.6 --knobA 0.3
//
// Options:
//   --shader <frag>          single-layer sugar for --layer
//   --layer <frag>           add a layer pass
//   --blend <m>              blend mode of the last --layer
//                            (normal|add|multiply|screen, default normal)
//   --opacity <0..1>         layer opacity (track volume), default 1
//   --post <frag>            append a post-FX pass (samples iPrev)
//   --image <path>           seed picture for iChannel2
//   --out <dir>              output directory (default probe_frames)
//   --frames <n>             frames to render (default 1)
//   --fps <n>                frame rate for time uniforms (default 30)
//   --start <sec>            first frame time (default 0)
//   --bpm <n>                tempo for iBeat/bar frac (default 120)
//   --width/--height <n>     render size (default 640×360)
//   --set <name>=<value>     set a custom float uniform (repeatable)
//   --knobA..--knobH <0..1>  set the generic knob uniforms
//   --help                   this text

#include "visual/ShaderTestKernel.h"
#include "visual/ShaderParamParse.h"

#include <glad/gl.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace yawn::visual;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct LayerSpec {
    std::string shader;
    std::string blend = "normal";
    float opacity = 1.0f;
};

struct Options {
    std::vector<LayerSpec> layers;
    std::vector<std::string> posts;
    std::string image;
    std::string outDir = "probe_frames";
    int frames = 1;
    int width = 640;
    int height = 360;
    double fps = 30.0;
    double start = 0.0;
    double bpm = 120.0;
    std::vector<std::pair<std::string, float>> params;
    float knobs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
};

void usage() {
    std::printf(
        "yawn_visual_probe — render visual shaders to PNG frames\n\n"
        "  --shader <frag>       single-layer sugar for --layer\n"
        "  --layer <frag>        add a layer pass\n"
        "  --blend <mode>        normal|add|multiply|screen (last --layer)\n"
        "  --opacity <0..1>      layer opacity (last --layer)\n"
        "  --post <frag>         append a post-FX pass (samples iPrev)\n"
        "  --image <path>        seed picture for iChannel2\n"
        "  --out <dir>           output directory (default probe_frames)\n"
        "  --frames <n>          frames to render (default 1)\n"
        "  --fps <n>             time step (default 30)\n"
        "  --start <sec>         first frame time (default 0)\n"
        "  --bpm <n>             tempo for iBeat (default 120)\n"
        "  --width/--height <n>  render size (default 640x360)\n"
        "  --set <name>=<value>  set a custom float uniform (repeatable)\n"
        "  --knobA..--knobH <v>  generic knob uniforms\n"
        "  --help\n");
}

bool parseArgs(int argc, char** argv, Options& o) {
    auto need = [&](int i) -> const char* {
        return (i + 1 < argc) ? argv[i + 1] : nullptr;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(); return false; }
        if (a == "--shader" || a == "--layer") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "%s needs a path\n", a.c_str()); return false; }
            o.layers.push_back(LayerSpec{v});
            ++i;
        } else if (a == "--blend" || a == "--opacity") {
            const char* v = need(i);
            if (!v || o.layers.empty()) {
                std::fprintf(stderr, "%s needs a preceding --layer and a value\n", a.c_str());
                return false;
            }
            if (a == "--blend") o.layers.back().blend = v;
            else o.layers.back().opacity = std::clamp(std::atof(v), 0.0, 1.0);
            ++i;
        } else if (a == "--post") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--post needs a path\n"); return false; }
            o.posts.emplace_back(v);
            ++i;
        } else if (a == "--image") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--image needs a path\n"); return false; }
            o.image = v;
            ++i;
        } else if (a == "--out") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--out needs a path\n"); return false; }
            o.outDir = v;
            ++i;
        } else if (a == "--frames") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--frames needs a number\n"); return false; }
            o.frames = std::max(1, std::atoi(v));
            ++i;
        } else if (a == "--fps") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--fps needs a number\n"); return false; }
            o.fps = std::max(1.0, std::atof(v));
            ++i;
        } else if (a == "--start") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--start needs a number\n"); return false; }
            o.start = std::atof(v);
            ++i;
        } else if (a == "--bpm") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--bpm needs a number\n"); return false; }
            o.bpm = std::max(1.0, std::atof(v));
            ++i;
        } else if (a == "--width" || a == "--height") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "%s needs a number\n", a.c_str()); return false; }
            (a == "--width" ? o.width : o.height) = std::max(16, std::atoi(v));
            ++i;
        } else if (a == "--set") {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "--set needs name=value\n"); return false; }
            const std::string kv = v;
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "--set expects name=value, got '%s'\n", v);
                return false;
            }
            o.params.emplace_back(kv.substr(0, eq),
                                  static_cast<float>(std::atof(kv.c_str() + eq + 1)));
            ++i;
        } else if (a.size() == 7 && a.compare(0, 6, "--knob") == 0 &&
                   a[6] >= 'A' && a[6] <= 'H') {
            const char* v = need(i);
            if (!v) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); return false; }
            o.knobs[a[6] - 'A'] = std::clamp(static_cast<float>(std::atof(v)), 0.0f, 1.0f);
            ++i;
        } else {
            std::fprintf(stderr, "unknown option '%s' (try --help)\n", a.c_str());
            return false;
        }
    }
    if (o.layers.empty()) {
        std::fprintf(stderr, "no shader given (use --shader or --layer)\n");
        return false;
    }
    return true;
}

// ── GL helpers ──────────────────────────────────────────────────────────

struct Target {
    GLuint tex = 0;
    GLuint fbo = 0;
};

bool makeTarget(int w, int h, Target& t, const char* what) {
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, t.tex, 0);
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "%s: framebuffer incomplete (0x%04x)\n", what, st);
        return false;
    }
    return true;
}

void clearTarget(const Target& t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

GLuint makeDummyRGBA() {
    Target t;
    makeTarget(1, 1, t, "dummy");
    return t.tex;   // fbo intentionally leaked — process-lifetime probe
}

// iChannel0 format parity: 512×2 R8 (row 0 spectrum, row 1 waveform).
GLuint makeR8(int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::vector<unsigned char> zero(static_cast<size_t>(w) * h, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, zero.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return tex;
}

// Deterministic seed picture for image-less probes: gradient bands +
// radial rings + a few colour blocks, so palette/geometry shaders have
// something real to chew on.
std::vector<unsigned char> defaultSeed(int s) {
    std::vector<unsigned char> px(static_cast<size_t>(s) * s * 4);
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            const float u = static_cast<float>(x) / s;
            const float v = static_cast<float>(y) / s;
            const float ring = 0.5f + 0.5f *
                std::sin(40.0f * std::sqrt((u - 0.5f) * (u - 0.5f) +
                                           (v - 0.5f) * (v - 0.5f)));
            unsigned char r = static_cast<unsigned char>(255 * (0.30f * u + 0.20f * ring + 0.15f * v));
            unsigned char g = static_cast<unsigned char>(255 * (0.25f * v + 0.35f * ring));
            unsigned char b = static_cast<unsigned char>(255 * (0.55f * (1.0f - u) + 0.25f * ring));
            if (((x / 64) + (y / 64)) % 3 == 0) {
                r = static_cast<unsigned char>(r * 0.5f + 160);
                b = static_cast<unsigned char>(b * 0.5f + 40);
            }
            unsigned char* o = &px[(static_cast<size_t>(y) * s + x) * 4];
            o[0] = r; o[1] = g; o[2] = b; o[3] = 255;
        }
    }
    return px;
}

void bindTex(GLenum unit, GLuint tex) {
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

void set1f(GLuint p, const char* n, float v) {
    const GLint l = glGetUniformLocation(p, n);
    if (l >= 0) glUniform1f(l, v);
}
void set2f(GLuint p, const char* n, float a, float b) {
    const GLint l = glGetUniformLocation(p, n);
    if (l >= 0) glUniform2f(l, a, b);
}
void set3f(GLuint p, const char* n, float a, float b, float c) {
    const GLint l = glGetUniformLocation(p, n);
    if (l >= 0) glUniform3f(l, a, b, c);
}
void set4f(GLuint p, const char* n, float a, float b, float c, float d) {
    const GLint l = glGetUniformLocation(p, n);
    if (l >= 0) glUniform4f(l, a, b, c, d);
}

struct AudioSnapshot {
    float beat, barFrac, level, low, mid, high, kick;
};

AudioSnapshot fakeAudio(double t, double bpm) {
    AudioSnapshot a{};
    a.beat = static_cast<float>(t * bpm / 60.0);
    const float phase = a.beat - std::floor(a.beat);
    a.barFrac = (a.beat / 4.0f) - std::floor(a.beat / 4.0f);
    a.kick = std::exp(-phase * 7.0f);
    a.low = std::clamp(0.25f + 0.55f * a.kick, 0.0f, 1.0f);
    a.mid = std::clamp(0.30f + 0.25f *
        static_cast<float>(std::sin(2.0 * kPi * 0.37 * t)), 0.0f, 1.0f);
    a.high = std::clamp(0.25f + 0.20f *
        static_cast<float>(std::sin(2.0 * kPi * 0.90 * t)), 0.0f, 1.0f);
    a.level = std::clamp(0.35f + 0.30f * a.kick + 0.10f *
        static_cast<float>(std::sin(2.0 * kPi * 0.11 * t)), 0.0f, 1.0f);
    return a;
}

// Bind every uniform the production preamble declares (+ the caller's
// custom params), so shaders behave exactly as they would in the app
// with the same inputs.
void bindUniforms(GLuint prog, int frame, double t, double dt,
                  const Options& o, const AudioSnapshot& a,
                  const std::vector<yawn::visual::ParsedParam>& defaults,
                  GLuint seedTex, int seedW, int seedH,
                  GLuint audioTex, GLuint textTex, GLuint blackTex,
                  GLuint prevTex, GLuint feedbackTex) {
    glUseProgram(prog);
    set3f(prog, "iResolution", static_cast<float>(o.width),
          static_cast<float>(o.height), 1.0f);
    set1f(prog, "iTime", static_cast<float>(t));
    set1f(prog, "iTimeDelta", static_cast<float>(dt));
    { const GLint l = glGetUniformLocation(prog, "iFrame"); if (l >= 0) glUniform1i(l, frame); }
    set4f(prog, "iMouse", 0, 0, 0, 0);
    set4f(prog, "iDate", 2026.0f, 9.0f, 18.0f, static_cast<float>(std::fmod(t, 86400.0)));
    set1f(prog, "iSampleRate", 48000.0f);

    {
        const float res[12] = {
            512.0f, 2.0f, 1.0f,
            2048.0f, 64.0f, 1.0f,
            static_cast<float>(seedW), static_cast<float>(seedH), 1.0f,
            1.0f, 1.0f, 1.0f,
        };
        const GLint l = glGetUniformLocation(prog, "iChannelResolution[0]");
        if (l >= 0) glUniform3fv(l, 4, res);
        const float times[4] = {
            static_cast<float>(t), static_cast<float>(t),
            static_cast<float>(t), static_cast<float>(t)};
        const GLint lt = glGetUniformLocation(prog, "iChannelTime[0]");
        if (lt >= 0) glUniform1fv(lt, 4, times);
    }

    set1f(prog, "iBeat", a.beat);
    set1f(prog, "iBeatBarFrac", a.barFrac);
    set1f(prog, "iTransportPlaying", 1.0f);
    set1f(prog, "iTransportTime", static_cast<float>(t));
    set1f(prog, "iAudioLevel", a.level);
    set1f(prog, "iAudioLow", a.low);
    set1f(prog, "iAudioMid", a.mid);
    set1f(prog, "iAudioHigh", a.high);
    set1f(prog, "iKick", a.kick);
    set1f(prog, "iGhostCount", 0.0f);
    for (int g = 0; g < 8; ++g)
        set4f(prog, ("iGhost" + std::to_string(g)).c_str(), 0, 0, 0, 0);
    set1f(prog, "iTextWidth", 0.0f);
    set1f(prog, "iTextTexWidth", 2048.0f);
    for (int k = 0; k < 8; ++k) {
        const char name[2] = {static_cast<char>('A' + k), 0};
        set1f(prog, (std::string("knob") + name).c_str(), o.knobs[k]);
    }
    // The shader's own @range defaults first (the app starts clips here
    // too), then --set overrides on top.
    for (const auto& d : defaults)
        set1f(prog, d.name.c_str(), d.defaultValue);
    for (const auto& [name, value] : o.params)
        set1f(prog, name.c_str(), value);

    const GLint s0 = glGetUniformLocation(prog, "iChannel0");
    const GLint s1 = glGetUniformLocation(prog, "iChannel1");
    const GLint s2 = glGetUniformLocation(prog, "iChannel2");
    const GLint s3 = glGetUniformLocation(prog, "iChannel3");
    const GLint sPrev = glGetUniformLocation(prog, "iPrev");
    const GLint sFb = glGetUniformLocation(prog, "iFeedback");
    if (s0 >= 0) glUniform1i(s0, 0);
    if (s1 >= 0) glUniform1i(s1, 1);
    if (s2 >= 0) glUniform1i(s2, 2);
    if (s3 >= 0) glUniform1i(s3, 3);
    if (sPrev >= 0) glUniform1i(sPrev, 4);
    if (sFb >= 0) glUniform1i(sFb, 5);
    bindTex(GL_TEXTURE0, audioTex);
    bindTex(GL_TEXTURE1, textTex);
    bindTex(GL_TEXTURE2, seedTex);
    bindTex(GL_TEXTURE3, blackTex);
    bindTex(GL_TEXTURE4, prevTex);
    bindTex(GL_TEXTURE5, feedbackTex);
}

std::vector<unsigned char> readPixels(GLuint fbo, int w, int h) {
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return px;
}

// Engine compositor math (docs/visual.md → Compositing):
//   normal: mix(dst, src, α)   add: mix(dst, dst+src, α)
//   multiply: mix(dst, dst*src, α)   screen: mix(dst, 1-(1-dst)(1-src), α)
//   α = opacity × sourceAlpha
void compositeLayer(std::vector<float>& acc,
                    const std::vector<unsigned char>& src, float opacity,
                    const std::string& blend) {
    const int mode = blend == "add" ? 1 : blend == "multiply" ? 2
                   : blend == "screen" ? 3 : 0;
    for (size_t i = 0; i < acc.size(); i += 4) {
        const float sr = src[i + 0] / 255.0f;
        const float sg = src[i + 1] / 255.0f;
        const float sb = src[i + 2] / 255.0f;
        const float a = opacity * (src[i + 3] / 255.0f);
        float br = sr, bg = sg, bb = sb;
        if (mode == 1) { br = acc[i] + sr; bg = acc[i + 1] + sg; bb = acc[i + 2] + sb; }
        else if (mode == 2) { br = acc[i] * sr; bg = acc[i + 1] * sg; bb = acc[i + 2] * sb; }
        else if (mode == 3) {
            br = acc[i] + sr - acc[i] * sr;
            bg = acc[i + 1] + sg - acc[i + 1] * sg;
            bb = acc[i + 2] + sb - acc[i + 2] * sb;
        }
        acc[i]     = acc[i]     * (1.0f - a) + br * a;
        acc[i + 1] = acc[i + 1] * (1.0f - a) + bg * a;
        acc[i + 2] = acc[i + 2] * (1.0f - a) + bb * a;
        acc[i + 3] = acc[i + 3] * (1.0f - a) + a;
    }
}

bool savePng(const std::string& path, int w, int h,
             const std::vector<unsigned char>& bottomUp) {
    std::vector<unsigned char> topDown(bottomUp.size());
    const size_t row = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y)
        std::memcpy(&topDown[static_cast<size_t>(y) * row],
                    &bottomUp[static_cast<size_t>(h - 1 - y) * row], row);
    return stbi_write_png(path.c_str(), w, h, 4, topDown.data(),
                          static_cast<int>(row)) != 0;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) return 2;

    std::string kernelErr;
    if (!shadertest::initContext(kernelErr)) {
        std::fprintf(stderr, "GL context: %s\n", kernelErr.c_str());
        return 1;
    }

    // ── Seed picture (iChannel2) ───────────────────────────────────
    int seedW = 512, seedH = 512;
    std::vector<unsigned char> seedPixels;
    if (!opt.image.empty()) {
        int ch = 0;
        unsigned char* loaded = stbi_load(opt.image.c_str(), &seedW, &seedH, &ch, 4);
        if (!loaded) {
            std::fprintf(stderr, "cannot load seed image '%s'\n", opt.image.c_str());
            shadertest::shutdownContext();
            return 1;
        }
        seedPixels.assign(loaded, loaded + static_cast<size_t>(seedW) * seedH * 4);
        stbi_image_free(loaded);
    } else {
        seedPixels = defaultSeed(seedW);
        std::fprintf(stderr, "(no --image: using the built-in procedural seed)\n");
    }
    Target seedTarget;
    if (!makeTarget(seedW, seedH, seedTarget, "seed")) return 1;
    glBindTexture(GL_TEXTURE_2D, seedTarget.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, seedW, seedH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, seedPixels.data());

    const GLuint audioTex = makeR8(512, 2);
    const GLuint textTex = makeR8(2048, 64);
    const GLuint blackTex = makeDummyRGBA();

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // ── Compile programs ───────────────────────────────────────────
    std::vector<GLuint> layerProgs;
    std::vector<std::vector<yawn::visual::ParsedParam>> layerDefaults;
    for (const auto& l : opt.layers) {
        std::string err;
        const std::string src = readFile(l.shader);
        const GLuint p = shadertest::compileProgram(l.shader, src, err);
        if (!p) {
            std::fprintf(stderr, "%s: %s\n", l.shader.c_str(), err.c_str());
            return 1;
        }
        layerProgs.push_back(p);
        layerDefaults.push_back(yawn::visual::parseShaderParams(src));
    }
    std::vector<GLuint> postProgs;
    std::vector<std::vector<yawn::visual::ParsedParam>> postDefaults;
    for (const auto& path : opt.posts) {
        std::string err;
        const std::string src = readFile(path);
        const GLuint p = shadertest::compileProgram(path, src, err);
        if (!p) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
            return 1;
        }
        postProgs.push_back(p);
        postDefaults.push_back(yawn::visual::parseShaderParams(src));
    }

    // ── Render targets ─────────────────────────────────────────────
    const int w = opt.width, h = opt.height;
    struct LayerRT {
        Target work, prev;
    };
    std::vector<LayerRT> layerRT(opt.layers.size());
    for (auto& rt : layerRT) {
        if (!makeTarget(w, h, rt.work, "layer work") ||
            !makeTarget(w, h, rt.prev, "layer prev")) return 1;
        clearTarget(rt.prev);
    }

    std::vector<Target> chain;   // [0] = composite input, [1..n] = post outputs
    chain.resize(postProgs.size() + 1);
    for (auto& t : chain)
        if (!makeTarget(w, h, t, "post chain")) return 1;
    Target postPrev;             // previous frame's final post output
    if (!postProgs.empty()) {
        if (!makeTarget(w, h, postPrev, "post prev")) return 1;
        clearTarget(postPrev);
    }

    std::error_code ec;
    fs::create_directories(opt.outDir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create '%s': %s\n",
                     opt.outDir.c_str(), ec.message().c_str());
        return 1;
    }

    const double dt = 1.0 / opt.fps;
    for (int f = 0; f < opt.frames; ++f) {
        const double t = opt.start + f * dt;
        const AudioSnapshot audio = fakeAudio(t, opt.bpm);

        // Layers (each with its own previous-frame feedback).
        std::vector<float> acc(static_cast<size_t>(w) * h * 4, 0.0f);
        for (size_t li = 0; li < layerProgs.size(); ++li) {
            Target& rt = layerRT[li].work;
            glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
            glViewport(0, 0, w, h);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            bindUniforms(layerProgs[li], f, t, dt, opt, audio,
                         layerDefaults[li],
                         seedTarget.tex, seedW, seedH,
                         audioTex, textTex, blackTex,
                         blackTex, layerRT[li].prev.tex);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            compositeLayer(acc, readPixels(rt.fbo, w, h),
                           opt.layers[li].opacity, opt.layers[li].blend);
            // Keep this frame as next frame's iFeedback.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, rt.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, layerRT[li].prev.fbo);
            glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // Upload the composite, run the post chain (passes see iPrev and
        // the chain's previous-frame output as iFeedback).
        std::vector<unsigned char> composited(acc.size());
        for (size_t i = 0; i < acc.size(); ++i)
            composited[i] = static_cast<unsigned char>(
                std::clamp(acc[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        glBindTexture(GL_TEXTURE_2D, chain[0].tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, composited.data());

        GLuint finalFbo = chain[0].fbo;
        if (!postProgs.empty()) {
            for (size_t pi = 0; pi < postProgs.size(); ++pi) {
                Target& dstT = chain[pi + 1];
                glBindFramebuffer(GL_FRAMEBUFFER, dstT.fbo);
                glViewport(0, 0, w, h);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                bindUniforms(postProgs[pi], f, t, dt, opt, audio,
                             postDefaults[pi],
                             seedTarget.tex, seedW, seedH,
                             audioTex, textTex, blackTex,
                             chain[pi].tex, postPrev.tex);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            const Target& last = chain[postProgs.size()];
            glBindFramebuffer(GL_READ_FRAMEBUFFER, last.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postPrev.fbo);
            glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            finalFbo = last.fbo;
        }

        char name[64];
        std::snprintf(name, sizeof(name), "frame_%04d.png", f);
        const std::string path = (fs::path(opt.outDir) / name).string();
        if (!savePng(path, w, h, readPixels(finalFbo, w, h))) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return 1;
        }
        std::printf("wrote %s  (t=%.3fs  beat=%.2f)\n",
                    path.c_str(), t, audio.beat);
    }

    shadertest::shutdownContext();
    return 0;
}
