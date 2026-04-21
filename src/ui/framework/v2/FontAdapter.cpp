#include "FontAdapter.h"

#include "ui/Font.h"

namespace yawn {
namespace ui {
namespace fw2 {

float FontAdapter::textWidth(const std::string& s, float fontSize) const {
    if (!m_font || !m_font->isLoaded()) return 0.0f;
    const float bake = m_font->pixelHeight();
    if (bake <= 0.0f) return 0.0f;
    return m_font->textWidth(s, fontSize / bake);
}

float FontAdapter::lineHeight(float fontSize) const {
    // Fall back to metrics-derived estimate when no font is loaded —
    // this path is extremely rare (we only hit it if the system-font
    // search at startup fails on every candidate) but keeps layout
    // from collapsing to zero height.
    if (!m_font || !m_font->isLoaded()) return fontSize * 1.2f;
    const float bake = m_font->pixelHeight();
    if (bake <= 0.0f) return fontSize * 1.2f;
    return m_font->lineHeight(fontSize / bake);
}

void FontAdapter::drawText(Renderer2D& r, const std::string& s,
                            float x, float y, float fontSize,
                            Color color) const {
    if (!m_font || !m_font->isLoaded() || s.empty()) return;
    const float bake = m_font->pixelHeight();
    if (bake <= 0.0f) return;
    m_font->drawText(r, s.c_str(), x, y, fontSize / bake, color);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
