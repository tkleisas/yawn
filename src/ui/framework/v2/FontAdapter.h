#pragma once

// UI v2 — FontAdapter.
//
// Thin wrapper implementing fw2::TextMetrics on top of v1's `Font` class.
// Sits in the main executable (not yawn_core) because v1 Font pulls
// glad/gl.h; declaring the class here lets call sites include this
// header without dragging GL through their TUs — FontAdapter.cpp is
// the only TU that includes ui/Font.h.
//
// v1's Font uses `scale` (a multiplier over the bake pixelHeight) while
// fw2::TextMetrics speaks in absolute pixel font sizes — this adapter
// does the arithmetic: `scale = fontSize / font.pixelHeight()`.

#include "ui/framework/v2/UIContext.h"   // TextMetrics

namespace yawn {
namespace ui {

class Font;

namespace fw2 {

class FontAdapter : public TextMetrics {
public:
    explicit FontAdapter(::yawn::ui::Font* font) : m_font(font) {}

    float textWidth(const std::string& s, float fontSize) const override;
    float lineHeight(float fontSize) const override;
    void  drawText(::yawn::ui::Renderer2D& r, const std::string& s,
                   float x, float y, float fontSize,
                   ::yawn::ui::Color color) const override;

private:
    ::yawn::ui::Font* m_font;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
