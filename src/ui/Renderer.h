#pragma once

#include "ui/Theme.h"
#include <glad/gl.h>
#include <string>
#include <vector>

namespace yawn {
namespace ui {

// Simple 2D batch renderer for OpenGL 3.3.
// Supports solid-color quads and alpha-textured quads (for text).
class Renderer2D {
public:
    Renderer2D() = default;
    ~Renderer2D();

    bool init();
    void shutdown();

    void beginFrame(int windowWidth, int windowHeight);
    void endFrame();

    // Solid-color primitives
    void drawRect(float x, float y, float w, float h, Color color);
    void drawRectOutline(float x, float y, float w, float h, Color color, float thickness = 1.0f);
    void drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color);
    void drawFilledCircle(float cx, float cy, float radius, Color color, int segments = 24);
    void drawRoundedRect(float x, float y, float w, float h, float radius, Color color, int cornerSegs = 6);

    // Textured quad (for font glyphs) — binds the given texture
    void drawTexturedQuad(float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          Color color, GLuint textureId);

    // Waveform: draws vertical bars from audio samples (min/max envelope)
    void drawWaveform(const float* samples, int sampleCount,
                      float x, float y, float w, float h, Color color);

    // Stereo waveform: two channels split top/bottom
    void drawWaveformStereo(const float* ch0, const float* ch1, int sampleCount,
                            float x, float y, float w, float h, Color color);

    // Scissor clipping
    void pushClip(float x, float y, float w, float h);
    void popClip();

private:
    struct Vertex {
        float x, y;
        float u, v;
        uint8_t r, g, b, a;
    };

    void flush();
    void addQuad(float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1,
                 Color color);
    void addTriVert(float x, float y, Color color);

    static constexpr int kMaxVertices = 65536;
    static constexpr int kMaxIndices = kMaxVertices * 6 / 4;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_shaderProgram = 0;
    GLuint m_whiteTexture = 0;

    GLint m_locProjection = -1;
    GLint m_locTexture = -1;

    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    GLuint m_currentTexture = 0;

    int m_windowWidth = 0;
    int m_windowHeight = 0;

    struct ClipRect { float x, y, w, h; };
    std::vector<ClipRect> m_clipStack;
};

} // namespace ui
} // namespace yawn
