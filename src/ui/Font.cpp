#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#undef STB_TRUETYPE_IMPLEMENTATION
#include "ui/Font.h"
#include "ui/Theme.h"
#include "ui/Renderer.h"
#include "util/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace yawn {
namespace ui {

// ─── UTF-8 helpers ──────────────────────────────────────────────────────────

uint32_t decodeUtf8(const char*& p) {
    const auto* s = reinterpret_cast<const unsigned char*>(p);
    uint32_t cp;
    if (s[0] < 0x80) {
        cp = s[0];
        p += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = (s[0] & 0x1F) << 6 | (s[1] & 0x3F);
        p += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = (s[0] & 0x0F) << 12 | (s[1] & 0x3F) << 6 | (s[2] & 0x3F);
        p += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = (s[0] & 0x07) << 18 | (s[1] & 0x3F) << 12 | (s[2] & 0x3F) << 6 | (s[3] & 0x3F);
        p += 4;
    } else {
        cp = 0xFFFD;
        p += 1;
    }
    return cp;
}

int utf8CharLen(const char* p) {
    auto s = static_cast<unsigned char>(*p);
    if (s < 0x80)        return 1;
    if ((s & 0xE0) == 0xC0) return 2;
    if ((s & 0xF0) == 0xE0) return 3;
    if ((s & 0xF8) == 0xF0) return 4;
    return 1;
}

int utf8PrevCharOffset(const std::string& str, int pos) {
    if (pos <= 0) return 0;
    int i = pos - 1;
    while (i > 0 && (static_cast<unsigned char>(str[i]) & 0xC0) == 0x80)
        --i;
    return pos - i;
}

// ─── Font ───────────────────────────────────────────────────────────────────

Font::~Font() {
    destroy();
}

bool Font::load(const std::string& path, float pixelHeight) {
    destroy();

    // Read font file
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        LOG_ERROR("UI", "Failed to open font file: %s", path.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<unsigned char> fontData(fileSize);
    if (fread(fontData.data(), 1, fileSize, f) != static_cast<size_t>(fileSize)) {
        fclose(f);
        return false;
    }

    m_pixelHeight = pixelHeight;
    m_atlasWidth = 1024;
    m_atlasHeight = 1024;

    // Define Unicode ranges to bake
    struct Range {
        int firstCodepoint;
        int count;
    };
    Range ranges[] = {
        { 0x0020, 95 },    // Basic Latin (space through ~)
        { 0x00A0, 96 },    // Latin-1 Supplement
        { 0x0100, 128 },   // Latin Extended-A
        { 0x0250, 96 },    // IPA Extensions
        { 0x0370, 144 },   // Greek and Coptic
        { 0x0400, 256 },   // Cyrillic
    };
    constexpr int numRanges = sizeof(ranges) / sizeof(ranges[0]);

    // Allocate temporary packed char arrays per range
    std::vector<stbtt_packedchar> packedChars[numRanges];
    stbtt_pack_range packRanges[numRanges];
    for (int i = 0; i < numRanges; ++i) {
        packedChars[i].resize(ranges[i].count);
        packRanges[i].font_size = pixelHeight;
        packRanges[i].first_unicode_codepoint_in_range = ranges[i].firstCodepoint;
        packRanges[i].array_of_unicode_codepoints = nullptr;
        packRanges[i].num_chars = ranges[i].count;
        packRanges[i].chardata_for_range = packedChars[i].data();
    }

    std::vector<unsigned char> bitmap(m_atlasWidth * m_atlasHeight);

    stbtt_pack_context ctx;
    if (!stbtt_PackBegin(&ctx, bitmap.data(), m_atlasWidth, m_atlasHeight, 0, 1, nullptr)) {
        LOG_ERROR("UI", "stbtt_PackBegin failed");
        return false;
    }

    stbtt_PackSetOversampling(&ctx, 2, 2);

    int result = stbtt_PackFontRanges(&ctx, fontData.data(), 0, packRanges, numRanges);
    stbtt_PackEnd(&ctx);

    if (!result) {
        LOG_WARN("UI", "Font atlas may be too small for all requested ranges");
    }

    // Build glyph map from packed data
    for (int i = 0; i < numRanges; ++i) {
        for (int j = 0; j < ranges[i].count; ++j) {
            uint32_t cp = ranges[i].firstCodepoint + j;
            const auto& pc = packedChars[i][j];
            // Skip glyphs that weren't packed (x0==x1 means no glyph)
            if (pc.x0 == 0 && pc.x1 == 0 && pc.y0 == 0 && pc.y1 == 0)
                continue;
            m_glyphs[cp] = pc;
        }
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

    LOG_INFO("UI", "Font loaded: %s (%.0fpx, %zu glyphs, %dx%d atlas)",
             path.c_str(), pixelHeight, m_glyphs.size(), m_atlasWidth, m_atlasHeight);
    return true;
}

void Font::destroy() {
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_glyphs.clear();
}

Font::GlyphQuad Font::getGlyph(uint32_t codepoint, float x, float y, float scale) const {
    GlyphQuad result = {};
    auto it = m_glyphs.find(codepoint);
    if (it == m_glyphs.end()) return result;

    const stbtt_packedchar& pc = it->second;

    float s = scale;
    result.x0 = x + pc.xoff * s;
    result.y0 = y + pc.yoff * s + m_pixelHeight * s;
    result.x1 = x + pc.xoff2 * s;
    result.y1 = y + pc.yoff2 * s + m_pixelHeight * s;

    result.u0 = static_cast<float>(pc.x0) / m_atlasWidth;
    result.v0 = static_cast<float>(pc.y0) / m_atlasHeight;
    result.u1 = static_cast<float>(pc.x1) / m_atlasWidth;
    result.v1 = static_cast<float>(pc.y1) / m_atlasHeight;

    result.xAdvance = pc.xadvance * s;
    return result;
}

float Font::textWidth(const std::string& text, float scale) const {
    if (m_glyphs.empty()) return 0;

    float w = 0;
    const char* p = text.c_str();
    while (*p) {
        uint32_t cp = decodeUtf8(p);
        auto it = m_glyphs.find(cp);
        if (it != m_glyphs.end())
            w += it->second.xadvance * scale;
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
    const char* p = text;
    while (*p) {
        uint32_t cp = decodeUtf8(p);
        auto g = getGlyph(cp, tx, y, scale);
        if (g.xAdvance > 0) {
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       color, m_textureId);
        }
        tx += g.xAdvance;
    }
}

} // namespace ui
} // namespace yawn
