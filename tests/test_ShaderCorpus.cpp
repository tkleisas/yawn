// test_ShaderCorpus.cpp — the GL shader test harness (GLSL_TEST build):
// a real offscreen 3.3/4.6 core context, every bundled example shader
// compiled through the EXACT VisualEngine preamble, and the live-code
// ghost-uniforms contract pinned. Failures print the driver message
// WITH the mapped source line so shader bugs read like compiler
// diagnostics, not like a black window.

#include <gtest/gtest.h>
#include "visual/ShaderTestKernel.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#include <glad/gl.h>

#ifndef YAWN_BUNDLED_SHADER_EXAMPLES_DIR
#define YAWN_BUNDLED_SHADER_EXAMPLES_DIR "."
#endif

namespace yws = yawn::visual::shadertest;

namespace {

std::vector<std::string> exampleShaders() {
    const std::string dir = YAWN_BUNDLED_SHADER_EXAMPLES_DIR;
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const auto& p = e.path();
        if (!p.extension().string().empty() && p.extension() == ".frag")
            out.push_back(p.filename().string());   // engine-relative name
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // anonymous

// Environment fixture: the offscreen context exists once
class ShaderCorpusTest : public ::testing::Environment {
public:
    void SetUp() override {}
};

class ShaderCorpus : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        std::string err;
        s_ctxReady = yws::initContext(err);
        if (!s_ctxReady)
            GTEST_MESSAGE_(err.c_str(), ::testing::TestPartResult::kFatalFailure);
    }
    static void TearDownTestSuite() {
        if (s_ctxReady) yws::shutdownContext();
    }
    void TearDown() override {
        // Drain errors and give the driver a beat between compiles.
        while (glGetError() != GL_NO_ERROR) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    static inline bool s_ctxReady = false;
};

// Bisection helper: compile progressively longer prefixes of a shader's
// non-blank lines; the first prefix that fails names the culprit line,
// and its text is printed with the mapped driver error. h
// (TEMP diagnostic — remove once the 31_ghost fragment parses.)
// FUNCTION-level deletion bisect: blank one *helper* function at a time
// (rot / aa / diamond / cool / streak) and re-compile 31's stream. THE
// single LINE bisect couldn't isolate multi-line constructs.
TEST_F(ShaderCorpus, LineBisect31Ghost) {
    std::ifstream uf("assets/shaders/examples/31_ghost_accent.frag");
    std::ostringstream us;
    us << uf.rdbuf();
    std::string src = us.str();

    const std::vector<std::string> helpers = {
        "vec3 tone(",
        "float aa(",
        "vec3 cool(",
        "float diamond(",
        "mat2 rot(",
    };
    int culprits = 0;
    std::string report;
    for (const auto& h : helpers) {
        size_t b = src.find(h);
        if (b == std::string::npos) continue;
        size_t e = src.find("\n}", b);
        if (e == std::string::npos) continue;
        e += 3;
        // blank the whole function
        std::string stripped =
            src.substr(0, b) + "// [function bisected out]\n" +
            src.substr(e);
        // map near-tones: mainImage uses removed helpers?
        // try compile; link errors from missing helper = inconclusive.
        std::string err;
        const bool ok = yws::compileFragment("fn-bisect", stripped, err);
        if (!ok) continue;   // still failing (inconclusive)
        report += "removing " + h + " made it COMPILE\n";
    }
    std::string msg = "no removal switched the outcome";
    if (!report.empty()) msg = report;
    EXPECT_TRUE(report.find("made it COMPILE") == std::string::npos)
        << msg;
}

TEST_F(ShaderCorpus, EveryBundledShaderCompiles) {
    const auto shaders = exampleShaders();
    std::cout << "[corpus] bundled shaders: " << shaders.size() << "\n";
    ASSERT_FALSE(shaders.empty());
    int okCount = 0;
    for (const auto& name : shaders) {
        const std::string path =
            std::string(YAWN_BUNDLED_SHADER_EXAMPLES_DIR) + "/" + name;
        std::string err;
        // Full path for file read; the kernel only cares about text.
        const bool ok = yws::compileFragment(name, readFile(path), err);
        if (ok) ++okCount;
        else    GTEST_MESSAGE_(("shader: " + name + "\n" + err).c_str(),
                              ::testing::TestPartResult::kNonFatalFailure);
    }
    EXPECT_EQ(okCount, static_cast<int>(shaders.size()));
}

// The preamble + all example shaders must also produce a *usable
// program* — compile occur both as a source pair and as a program link.
TEST_F(ShaderCorpus, GhostUniformsProgramLinks) {
    // The preamble declares iGhostCount/iGhost0..7/iBeatBarFrac (phase B):
    // a user shader consumes them WITHOUT redeclaring — exactly what the
    // engine's on-disk ghost shader does.
    const std::string ghost =
        "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
        "    float lum = iGhostCount * 0.0;\n"
        "    lum += iGhost0.x + iBeatBarFrac;\n"
        "    fragColor = vec4(vec3(lum), 1.0);\n"
        "}\n";
    std::string err;
    EXPECT_TRUE(yws::compileFragment("ghost_minimal", ghost, err)) << err;
}

TEST_F(ShaderCorpus, KnobUniformsCompile) {
    // The preamble bakes knobA..H: consumes like the app does. Uses a
    // non-builtin name (NOT 'mix' — the builtin function name shadows).
    const std::string fx =
        "uniform float myMix; // @range 0..1 default=0.3\n"
        "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
        "    vec4 base = vec4(knobA, knobB, knobC, 1.0);\n"
        "    fragColor = mix(base, vec4(1.0), clamp(myMix, 0.0, 1.0));\n"
        "}\n";
    std::string err;
    const bool r = yws::compileFragment("knob_minimal", fx, err);
    std::cout << "[knob r: " << r << " err: " << err << "]\n";
    EXPECT_TRUE(r) << err;
}
