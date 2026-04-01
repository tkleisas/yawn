#include "ui/Renderer.h"
#include "util/Logger.h"
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
        LOG_ERROR("UI", "Shader compile error: %s", log);
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
        LOG_ERROR("UI", "Shader link error: %s", log);
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

void Renderer2D::addTriVert(float x, float y, Color color) {
    if (m_vertices.size() + 1 > kMaxVertices) flush();
    m_vertices.push_back({x, y, 0.0f, 0.0f, color.r, color.g, color.b, color.a});
}

void Renderer2D::drawTriangle(float x0, float y0, float x1, float y1,
                               float x2, float y2, Color color) {
    if (m_currentTexture != m_whiteTexture) {
        flush();
        m_currentTexture = m_whiteTexture;
        glBindTexture(GL_TEXTURE_2D, m_currentTexture);
    }
    if (m_vertices.size() + 3 > kMaxVertices) flush();
    uint32_t base = static_cast<uint32_t>(m_vertices.size());
    addTriVert(x0, y0, color);
    addTriVert(x1, y1, color);
    addTriVert(x2, y2, color);
    m_indices.push_back(base + 0);
    m_indices.push_back(base + 1);
    m_indices.push_back(base + 2);
}

void Renderer2D::drawFilledCircle(float cx, float cy, float radius,
                                   Color color, int segments) {
    if (m_currentTexture != m_whiteTexture) {
        flush();
        m_currentTexture = m_whiteTexture;
        glBindTexture(GL_TEXTURE_2D, m_currentTexture);
    }
    if (segments < 6) segments = 6;
    if (m_vertices.size() + static_cast<size_t>(segments) + 1 > kMaxVertices) flush();
    uint32_t base = static_cast<uint32_t>(m_vertices.size());
    // Center vertex
    addTriVert(cx, cy, color);
    // Perimeter vertices
    for (int i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) * 2.0f * 3.14159265f / static_cast<float>(segments);
        addTriVert(cx + radius * std::cos(angle), cy + radius * std::sin(angle), color);
    }
    // Fan triangles
    for (int i = 0; i < segments; ++i) {
        m_indices.push_back(base);
        m_indices.push_back(base + 1 + i);
        m_indices.push_back(base + 2 + i);
    }
}

void Renderer2D::drawRoundedRect(float x, float y, float w, float h,
                                  float radius, Color color, int cornerSegs) {
    if (m_currentTexture != m_whiteTexture) {
        flush();
        m_currentTexture = m_whiteTexture;
        glBindTexture(GL_TEXTURE_2D, m_currentTexture);
    }
    if (radius <= 0 || cornerSegs < 1) { drawRect(x, y, w, h, color); return; }
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    // Inner body (3 rects)
    drawRect(x + radius, y, w - 2 * radius, h, color);
    drawRect(x, y + radius, radius, h - 2 * radius, color);
    drawRect(x + w - radius, y + radius, radius, h - 2 * radius, color);
    // 4 corner fans
    float cx[4] = {x + radius, x + w - radius, x + w - radius, x + radius};
    float cy[4] = {y + radius, y + radius, y + h - radius, y + h - radius};
    float startAngle[4] = {3.14159265f, 1.5f * 3.14159265f, 0.0f, 0.5f * 3.14159265f};
    for (int c = 0; c < 4; ++c) {
        uint32_t base = static_cast<uint32_t>(m_vertices.size());
        addTriVert(cx[c], cy[c], color);
        for (int i = 0; i <= cornerSegs; ++i) {
            float angle = startAngle[c] + static_cast<float>(i) * 0.5f * 3.14159265f / static_cast<float>(cornerSegs);
            addTriVert(cx[c] + radius * std::cos(angle), cy[c] + radius * std::sin(angle), color);
        }
        for (int i = 0; i < cornerSegs; ++i) {
            m_indices.push_back(base);
            m_indices.push_back(base + 1 + i);
            m_indices.push_back(base + 2 + i);
        }
    }
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
        int startSample = (i * sampleCount) / numBars;
        int endSample = ((i + 1) * sampleCount) / numBars;
        if (endSample > sampleCount) endSample = sampleCount;

        float minVal = 0.0f, maxVal = 0.0f;
        for (int s = startSample; s < endSample; ++s) {
            float v = samples[s];
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
        }

        float top = midY - maxVal * halfH;
        float bot = midY - minVal * halfH;
        float barH = bot - top;
        if (barH < 0.5f) { top = midY - 0.25f; barH = 0.5f; }

        drawRect(x + i * barWidth, top, barWidth, barH, color);
    }
}

void Renderer2D::drawWaveformStereo(const float* ch0, const float* ch1,
                                     int sampleCount,
                                     float x, float y, float w, float h,
                                     Color color) {
    if (sampleCount <= 0 || w <= 0) return;
    float halfH = h * 0.5f;
    float sepY = y + halfH;
    // Top half = channel 0, bottom half = channel 1
    drawWaveform(ch0, sampleCount, x, y, w, halfH - 0.5f, color);
    drawRect(x, sepY - 0.5f, w, 1.0f, Color{50, 50, 55, 128});
    drawWaveform(ch1, sampleCount, x, sepY + 0.5f, w, halfH - 0.5f, color);
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
