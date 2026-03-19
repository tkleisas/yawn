#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#undef STB_TRUETYPE_IMPLEMENTATION
#include "ui/Font.h"
#include "ui/Theme.h"
#include "ui/Renderer.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace yawn {
namespace ui {

Font::~Font() {
    destroy();
}

bool Font::load(const std::string& path, float pixelHeight) {
    destroy();

    // Read font file
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "Failed to open font file: %s\n", path.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<unsigned char> fontData(fileSize);
    fread(fontData.data(), 1, fileSize, f);
    fclose(f);

    m_pixelHeight = pixelHeight;
    m_atlasWidth = 512;
    m_atlasHeight = 512;

    // Bake ASCII 32-127 into a bitmap
    std::vector<unsigned char> bitmap(m_atlasWidth * m_atlasHeight);
    m_charData = new stbtt_bakedchar[96];

    int result = stbtt_BakeFontBitmap(fontData.data(), 0, pixelHeight,
        bitmap.data(), m_atlasWidth, m_atlasHeight, 32, 96, m_charData);

    if (result <= 0) {
        std::fprintf(stderr, "Warning: Font atlas may be too small (result=%d)\n", result);
    }

    // Upload to OpenGL texture
    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlasWidth, m_atlasHeight,
                 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Swizzle: use red channel as alpha, white as color
    GLint swizzle[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    std::printf("Font loaded: %s (%.0fpx)\n", path.c_str(), pixelHeight);
    return true;
}

void Font::destroy() {
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    if (m_charData) {
        delete[] m_charData;
        m_charData = nullptr;
    }
}

Font::GlyphQuad Font::getGlyph(char c, float x, float y, float scale) const {
    GlyphQuad result = {};
    if (!m_charData || c < 32 || c > 127) return result;

    const stbtt_bakedchar& bc = m_charData[c - 32];

    float s = scale;
    result.x0 = x + bc.xoff * s;
    result.y0 = y + bc.yoff * s + m_pixelHeight * s;
    result.x1 = result.x0 + (bc.x1 - bc.x0) * s;
    result.y1 = result.y0 + (bc.y1 - bc.y0) * s;

    result.u0 = static_cast<float>(bc.x0) / m_atlasWidth;
    result.v0 = static_cast<float>(bc.y0) / m_atlasHeight;
    result.u1 = static_cast<float>(bc.x1) / m_atlasWidth;
    result.v1 = static_cast<float>(bc.y1) / m_atlasHeight;

    result.xAdvance = bc.xadvance * s;
    return result;
}

float Font::textWidth(const std::string& text, float scale) const {
    if (!m_charData) return 0;

    float w = 0;
    for (char c : text) {
        if (c < 32 || c > 127) continue;
        w += m_charData[c - 32].xadvance * scale;
    }
    return w;
}

float Font::lineHeight(float scale) const {
    return m_pixelHeight * scale;
}

void Font::drawText(Renderer2D& renderer, const char* text,
                    float x, float y, float scale, Color color) {
    if (!isLoaded() || !text) return;
    float tx = x;
    for (const char* p = text; *p; ++p) {
        auto g = getGlyph(*p, tx, y, scale);
        renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                   g.u0, g.v0, g.u1, g.v1,
                                   color, m_textureId);
        tx += g.xAdvance;
    }
}

} // namespace ui
} // namespace yawn
