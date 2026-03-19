#pragma once

#include <glad/gl.h>
#include <string>
#include "stb_truetype.h"

namespace yawn {
namespace ui {

struct Color;
class Renderer2D;

class Font {
public:
    Font() = default;
    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // Load a TrueType font from file. pixelHeight is the baking size.
    bool load(const std::string& path, float pixelHeight);
    void destroy();

    bool isLoaded() const { return m_textureId != 0; }
    GLuint textureId() const { return m_textureId; }
    float pixelHeight() const { return m_pixelHeight; }

    // Get metrics for rendering a character (ASCII 32-127)
    struct GlyphQuad {
        float x0, y0, x1, y1;   // screen-space quad
        float u0, v0, u1, v1;   // texture coords
        float xAdvance;          // how far to move x for next char
    };

    GlyphQuad getGlyph(char c, float x, float y, float scale) const;
    float textWidth(const std::string& text, float scale) const;
    float lineHeight(float scale) const;

    // Render text using the given renderer
    void drawText(Renderer2D& renderer, const char* text,
                  float x, float y, float scale, Color color);

private:
    GLuint m_textureId = 0;
    float m_pixelHeight = 0.0f;
    int m_atlasWidth = 0;
    int m_atlasHeight = 0;
    stbtt_bakedchar* m_charData = nullptr; // heap-allocated array of 96 entries
};

} // namespace ui
} // namespace yawn
