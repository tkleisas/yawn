#pragma once

// TextRasterizer — stb_truetype-based line rasterizer producing an R8 alpha
// buffer. Keeps the TTF file mapped so subsequent rasterizations are free
// of file I/O.
//
// The raster is always a single horizontal line, baseline-aligned. Glyphs
// that overflow the requested width are clipped (no wrapping). Intended
// for VisualEngine's per-layer "text strip" texture bound to iChannel1.

#include "stb_truetype.h"

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

namespace yawn {
namespace visual {

class TextRasterizer {
public:
    bool load(const std::string& ttfPath) {
        FILE* f = std::fopen(ttfPath.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        m_fontData.resize(static_cast<size_t>(sz));
        if (std::fread(m_fontData.data(), 1, sz, f) != static_cast<size_t>(sz)) {
            std::fclose(f);
            m_fontData.clear();
            return false;
        }
        std::fclose(f);
        if (!stbtt_InitFont(&m_info, m_fontData.data(), 0)) {
            m_fontData.clear();
            return false;
        }
        m_ready = true;
        return true;
    }

    bool ready() const { return m_ready; }

    // Rasterize `text` into outBuf (R8, size width*height). Buffer is
    // fully cleared before drawing. Glyph baseline is placed at 80% of
    // the height so descenders fit. Returns pixel width of rendered text.
    int rasterize(const std::string& text, float pixelHeight,
                  unsigned char* outBuf, int width, int height) const {
        std::memset(outBuf, 0, static_cast<size_t>(width) * height);
        if (!m_ready || text.empty() || width <= 0 || height <= 0) return 0;

        const float scale = stbtt_ScaleForPixelHeight(&m_info, pixelHeight);
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&m_info, &ascent, &descent, &lineGap);
        const int baseline = static_cast<int>(ascent * scale);

        int penX = 0;
        const char* p = text.c_str();
        uint32_t prev = 0;
        while (*p) {
            // Minimal UTF-8 decode (ASCII fast path + multi-byte sequences).
            uint32_t cp = 0;
            unsigned char b = static_cast<unsigned char>(*p);
            if (b < 0x80) { cp = b; ++p; }
            else if ((b & 0xE0) == 0xC0) {
                cp = (b & 0x1F) << 6;
                ++p;
                if ((*p & 0xC0) == 0x80) { cp |= (*p & 0x3F); ++p; }
            } else if ((b & 0xF0) == 0xE0) {
                cp = (b & 0x0F) << 12;
                ++p;
                if ((*p & 0xC0) == 0x80) { cp |= (*p & 0x3F) << 6; ++p; }
                if ((*p & 0xC0) == 0x80) { cp |= (*p & 0x3F); ++p; }
            } else {
                cp = 0xFFFD;
                ++p;
            }

            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&m_info, cp, &advance, &lsb);

            // Kerning with previous glyph.
            if (prev) {
                int kern = stbtt_GetCodepointKernAdvance(&m_info, prev, cp);
                penX += static_cast<int>(kern * scale);
            }

            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&m_info, cp, scale, scale,
                                          &x0, &y0, &x1, &y1);

            int gx = penX + x0;
            int gy = baseline + y0;
            int gw = x1 - x0;
            int gh = y1 - y0;

            if (gw > 0 && gh > 0) {
                // Clip to buffer.
                int sx = std::max(0, -gx);
                int sy = std::max(0, -gy);
                int dx = gx + sx;
                int dy = gy + sy;
                int cw = std::min(gw - sx, width  - dx);
                int ch = std::min(gh - sy, height - dy);
                if (cw > 0 && ch > 0 && dx < width && dy < height) {
                    // Rasterize into a temp buffer sized to the glyph, then
                    // max-blend into outBuf so overlapping glyph boxes
                    // (rare but possible with italics / kerning) don't flatten.
                    std::vector<unsigned char> tmp(static_cast<size_t>(gw) * gh, 0);
                    stbtt_MakeCodepointBitmap(&m_info, tmp.data(), gw, gh, gw,
                                                scale, scale, cp);
                    for (int row = 0; row < ch; ++row) {
                        unsigned char* dst = outBuf + (dy + row) * width + dx;
                        const unsigned char* src = tmp.data() + (sy + row) * gw + sx;
                        for (int col = 0; col < cw; ++col) {
                            if (src[col] > dst[col]) dst[col] = src[col];
                        }
                    }
                }
            }

            penX += static_cast<int>(advance * scale);
            if (penX >= width) break;  // further chars clipped anyway
            prev = cp;
        }
        return penX;
    }

private:
    std::vector<unsigned char> m_fontData;
    stbtt_fontinfo             m_info{};
    bool                        m_ready = false;
};

} // namespace visual
} // namespace yawn
