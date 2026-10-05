#pragma once

#include <SDL3/SDL.h>

// ShaderTestKernel — the GL compile path used by BOTH the visual engine
// and the shader test harness (tests/test_ShaderCorpus.cpp).
//
// `buildUserSource(userSrc)` reproduces EXACTLY what
// VisualEngine::compileShaderForLayer sends to glShaderSource:
//   { glslVersionLine(), TOY_PREAMBLE + user source }
// `compileFragment(name, userSrc, errOut)` runs the compile and, on
// failure, parses the driver message (e.g. "0:79(7)") and appends the
// *mapped* source line so a test failure shows the real GLSL text.
//
// Test target: SDL offscreen driver gives a genuine 3.3/4.6 core
// context with glad, no window manager needed.

#include <string>
#include <vector>

namespace yawn {
namespace visual {
namespace shadertest {

// Initialization: create an offscreen GL context (SDL) and load gl.
// Thin: returns false with reason on failure. Call once per process.
// Held so the test environment can destroy the window + context before
// SDL_Quit (LSan flags driver-side allocations otherwise).
inline SDL_Window*    s_win = nullptr;
inline SDL_GLContext  s_ctx = nullptr;

bool initContext(std::string& err);

// Engine-identical fragment stream (for golden/line-mapping tests).
std::string buildUserSource(const std::string& userSrc);

// Compile user source (with preamble + version line) as a fragment
// shader attached to a vertex stage mirroring the engine, then link.
// ReturnsGLE true on full success; errOut gains the driver message plus
// a "|line N| <text>" mapping for the first error line.
bool compileFragment(const std::string& name, const std::string& userSrc,
                     std::string& errOut);

// Shutdown (context + SDL teardown).
void shutdownContext();

} // namespace shadertest
} // namespace visual
} // namespace yawn
