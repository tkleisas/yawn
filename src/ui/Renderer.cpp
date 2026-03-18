#include "ui/Renderer.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace yawn {
namespace ui {

static const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

uniform mat4 uProjection;

out vec2 vTexCoord;
out vec4 vColor;

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";

static const char* kFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    FragColor = vColor * texColor;
}
)";

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

Renderer2D::~Renderer2D() {
    shutdown();
}

bool Renderer2D::init() {
    // Compile shaders
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;

    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vs);
    glAttachShader(m_shaderProgram, fs);
    glLinkProgram(m_shaderProgram);

    GLint success;
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(m_shaderProgram, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader link error: %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    m_locProjection = glGetUniformLocation(m_shaderProgram, "uProjection");
    m_locTexture = glGetUniformLocation(m_shaderProgram, "uTexture");

    // Create white 1x1 texture for solid-color rendering
    uint32_t white = 0xFFFFFFFF;
    glGenTextures(1, &m_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Create VAO, VBO, EBO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, kMaxVertices * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, kMaxIndices * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

    // Position (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(0);

    // TexCoord (vec2)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(1);

    // Color (vec4 normalized)
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    m_vertices.reserve(kMaxVertices);
    m_indices.reserve(kMaxIndices);

    return true;
}

void Renderer2D::shutdown() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_shaderProgram) { glDeleteProgram(m_shaderProgram); m_shaderProgram = 0; }
    if (m_whiteTexture) { glDeleteTextures(1, &m_whiteTexture); m_whiteTexture = 0; }
}

void Renderer2D::beginFrame(int windowWidth, int windowHeight) {
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

    m_vertices.clear();
    m_indices.clear();
    m_currentTexture = m_whiteTexture;
    m_clipStack.clear();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(m_shaderProgram);

    // Orthographic projection (top-left origin)
    float proj[16] = {0};
    proj[0] = 2.0f / windowWidth;
    proj[5] = -2.0f / windowHeight;
    proj[10] = -1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(m_locProjection, 1, GL_FALSE, proj);
    glUniform1i(m_locTexture, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_currentTexture);
    glBindVertexArray(m_vao);
}

void Renderer2D::endFrame() {
    flush();
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
}

void Renderer2D::flush() {
    if (m_vertices.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        m_vertices.size() * sizeof(Vertex), m_vertices.data());

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
        m_indices.size() * sizeof(uint32_t), m_indices.data());

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()),
                   GL_UNSIGNED_INT, nullptr);

    m_vertices.clear();
    m_indices.clear();
}

void Renderer2D::addQuad(float x0, float y0, float x1, float y1,
                          float u0, float v0, float u1, float v1,
                          Color color) {
    if (m_vertices.size() + 4 > kMaxVertices) {
        flush();
    }

    uint32_t base = static_cast<uint32_t>(m_vertices.size());

    m_vertices.push_back({x0, y0, u0, v0, color.r, color.g, color.b, color.a});
    m_vertices.push_back({x1, y0, u1, v0, color.r, color.g, color.b, color.a});
    m_vertices.push_back({x1, y1, u1, v1, color.r, color.g, color.b, color.a});
    m_vertices.push_back({x0, y1, u0, v1, color.r, color.g, color.b, color.a});

    m_indices.push_back(base + 0);
    m_indices.push_back(base + 1);
    m_indices.push_back(base + 2);
    m_indices.push_back(base + 0);
    m_indices.push_back(base + 2);
    m_indices.push_back(base + 3);
}

void Renderer2D::drawRect(float x, float y, float w, float h, Color color) {
    if (m_currentTexture != m_whiteTexture) {
        flush();
        m_currentTexture = m_whiteTexture;
        glBindTexture(GL_TEXTURE_2D, m_currentTexture);
    }
    addQuad(x, y, x + w, y + h, 0, 0, 1, 1, color);
}

void Renderer2D::drawRectOutline(float x, float y, float w, float h,
                                  Color color, float thickness) {
    float t = thickness;
    drawRect(x, y, w, t, color);               // top
    drawRect(x, y + h - t, w, t, color);       // bottom
    drawRect(x, y + t, t, h - 2 * t, color);   // left
    drawRect(x + w - t, y + t, t, h - 2 * t, color); // right
}

void Renderer2D::drawTexturedQuad(float x, float y, float w, float h,
                                   float u0, float v0, float u1, float v1,
                                   Color color, GLuint textureId) {
    if (m_currentTexture != textureId) {
        flush();
        m_currentTexture = textureId;
        glBindTexture(GL_TEXTURE_2D, m_currentTexture);
    }
    addQuad(x, y, x + w, y + h, u0, v0, u1, v1, color);
}

void Renderer2D::drawWaveform(const float* samples, int sampleCount,
                               float x, float y, float w, float h,
                               Color color) {
    if (sampleCount <= 0 || w <= 0) return;

    float midY = y + h * 0.5f;
    float halfH = h * 0.5f;
    int numBars = static_cast<int>(w);
    if (numBars <= 0) numBars = 1;
    float barWidth = w / numBars;

    for (int i = 0; i < numBars; ++i) {
        // Find peak in this segment
        int startSample = (i * sampleCount) / numBars;
        int endSample = ((i + 1) * sampleCount) / numBars;
        if (endSample > sampleCount) endSample = sampleCount;

        float peak = 0.0f;
        for (int s = startSample; s < endSample; ++s) {
            float abs = samples[s] < 0 ? -samples[s] : samples[s];
            if (abs > peak) peak = abs;
        }

        float barH = peak * halfH;
        if (barH < 0.5f) barH = 0.5f;

        drawRect(x + i * barWidth, midY - barH, barWidth, barH * 2.0f, color);
    }
}

void Renderer2D::pushClip(float x, float y, float w, float h) {
    flush();
    m_clipStack.push_back({x, y, w, h});
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<int>(x),
              m_windowHeight - static_cast<int>(y + h),
              static_cast<int>(w),
              static_cast<int>(h));
}

void Renderer2D::popClip() {
    flush();
    if (!m_clipStack.empty()) {
        m_clipStack.pop_back();
    }
    if (m_clipStack.empty()) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        auto& c = m_clipStack.back();
        glScissor(static_cast<int>(c.x),
                  m_windowHeight - static_cast<int>(c.y + c.h),
                  static_cast<int>(c.w),
                  static_cast<int>(c.h));
    }
}

} // namespace ui
} // namespace yawn
