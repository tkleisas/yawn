#pragma once

#include <glad/gl.h>
#include <string>
#include <unordered_map>
#include "stb_truetype.h"

namespace yawn {
namespace ui {

struct Color;
class Renderer2D;

// Decode one UTF-8 codepoint from *p, advancing p past the consumed bytes.
// Returns 0xFFFD (replacement char) on invalid sequences.
uint32_t decodeUtf8(const char*& p);

// Return the byte length of the UTF-8 character starting at *p (1-4), or 1 on error.
int utf8CharLen(const char* p);

// Return byte offset of the previous UTF-8 character start before pos in str.
int utf8PrevCharOffset(const std::string& str, int pos);

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

    // Get metrics for rendering a Unicode codepoint
    struct GlyphQuad {
        float x0, y0, x1, y1;   // screen-space quad
        float u0, v0, u1, v1;   // texture coords
        float xAdvance;          // how far to move x for next char
    };

    GlyphQuad getGlyph(uint32_t codepoint, float x, float y, float scale) const;
    float textWidth(const std::string& text, float scale) const;
    float lineHeight(float scale) const;

    // Render text using the given renderer (UTF-8 encoded)
    void drawText(Renderer2D& renderer, const char* text,
                  float x, float y, float scale, Color color);

private:
    GLuint m_textureId = 0;
    float m_pixelHeight = 0.0f;
    int m_atlasWidth = 0;
    int m_atlasHeight = 0;
    std::unordered_map<uint32_t, stbtt_packedchar> m_glyphs;
};

} // namespace ui
} // namespace yawn
