#pragma once
// GlCaps — capabilities of the OpenGL context we actually got.
//
// Populated once by ui::Window::create() after glad loads the context
// (main thread, before any renderer init); read by every shader-
// compiling site (Renderer2D, VisualEngine, M3DRenderer) and by the
// texture-swizzle / instancing fallbacks.
//
// YAWN's renderers are written against the GL 3.3 feature set, but the
// hard floor is 3.1: Windows Sandy Bridge-era Intel drivers and the
// Raspberry Pi 4/5 (Mesa v3d) top out at desktop GL 3.1. On a < 3.3
// context, engine-injected shader preambles switch from GLSL 3.30 to
// GLSL 1.40, attribute locations are bound via glBindAttribLocation
// instead of layout() qualifiers, and texture swizzle / instanced
// arrays go through their ARB extensions (or CPU fallbacks) when the
// core feature is missing.
//
// Defaults assume 3.3 so behavior is unchanged where no window exists
// (unit tests, offline tools).

namespace yawn {
namespace ui {

struct GlCaps {
    static inline int  major = 3;
    static inline int  minor = 3;
    static inline bool extTextureSwizzle  = false;  // GL_ARB_texture_swizzle
    static inline bool extInstancedArrays = false;  // GL_ARB_instanced_arrays

    static bool atLeast(int maj, int min) {
        return major > maj || (major == maj && minor >= min);
    }

    // Version line for engine-injected shader preambles. GLSL 1.40 is
    // the GL 3.1 language; the bodies we inject after it are written
    // in the common subset (no layout qualifiers, no 3.30 intrinsics).
    static const char* glslVersionLine() {
        return atLeast(3, 3) ? "#version 330 core\n" : "#version 140\n";
    }

    static bool textureSwizzle()  { return atLeast(3, 3) || extTextureSwizzle; }
    static bool instancedArrays() { return atLeast(3, 3) || extInstancedArrays; }
};

} // namespace ui
} // namespace yawn
