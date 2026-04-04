// Primitives.cpp — paint() implementations for primitive UI widgets.
// Split from Primitives.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "Primitives.h"
#include "../Renderer.h"
#include "../Font.h"

namespace yawn {
namespace ui {
namespace fw {

// ═══════════════════════════════════════════════════════════════════════════
// Label
// ═══════════════════════════════════════════════════════════════════════════

void Label::paint(UIContext& ctx) {
    if (!ctx.renderer || !ctx.font) return;
    float scale = m_fontScale > 0 ? m_fontScale
                 : Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    float tw = ctx.font->textWidth(m_text, scale);
    float tx = m_bounds.x;
    if (m_align == TextAlign::Center)
        tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
    else if (m_align == TextAlign::Right)
        tx = m_bounds.x + m_bounds.w - tw;
    float lh = ctx.font->lineHeight(scale);
    float ty = m_bounds.y + (m_bounds.h - lh) * 0.5f - lh * 0.15f;
    ctx.font->drawText(*ctx.renderer, m_text.c_str(), tx, ty, scale, m_color);
}

// ═══════════════════════════════════════════════════════════════════════════
// FwButton
// ═══════════════════════════════════════════════════════════════════════════

void FwButton::paint(UIContext& ctx) {
    // Frame-based hover interpolation (works at any frame rate)
    constexpr float kHoverSpeed = 0.15f;
    if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
    else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

    if (!ctx.renderer || !ctx.font) return;
    Color bg = m_customColor ? m_bgColor : Theme::controlBg;
    if (!m_enabled) bg = Color{50, 50, 53, 255};
    else if (m_pressed) bg = Theme::controlActive;
    else if (m_hoverAlpha > 0.001f)
        bg = Color::lerp(bg, Theme::controlHover, m_hoverAlpha);
    if (m_isToggle && m_toggleOn)
        bg = m_customColor ? m_bgColor : Color{200, 100, 40, 255};

    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
    if (m_drawOutline)
        ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                      Theme::controlBorder);
    if (!m_label.empty()) {
        float scale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
        float tw = ctx.font->textWidth(m_label, scale);
        float lh = ctx.font->lineHeight(scale);
        float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
        if (tx < m_bounds.x + 2.0f) tx = m_bounds.x + 2.0f;
        float ty = m_bounds.y + (m_bounds.h - lh) * 0.5f - lh * 0.15f;
        if (m_pressed) ty += 1.0f;
        Color tc = m_customTextColor ? m_textColor
                 : (m_enabled ? Theme::textPrimary : Theme::textDim);
        ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, scale, tc);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FwToggle
// ═══════════════════════════════════════════════════════════════════════════

void FwToggle::paint(UIContext& ctx) {
    constexpr float kHoverSpeed = 0.15f;
    if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
    else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

    if (!ctx.renderer || !ctx.font) return;
    Color bg = m_on ? m_onColor : Theme::toggleOffBg;
    if (!m_on && m_hoverAlpha > 0.001f)
        bg = Color::lerp(bg, Theme::toggleOffHover, m_hoverAlpha);
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
    ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                  Theme::controlBorder);
    // LED dot
    float dotR = 4.0f;
    float dotX = m_bounds.x + 8 - dotR;
    float dotY = m_bounds.y + m_bounds.h * 0.5f - dotR;
    Color dotCol = m_on ? Color{80, 255, 80, 255} : Color{80, 80, 80, 255};
    ctx.renderer->drawRect(dotX, dotY, dotR * 2, dotR * 2, dotCol);

    if (!m_label.empty()) {
        float scale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
        float tx = m_bounds.x + 18;
        float lh = ctx.font->lineHeight(scale);
        float ty = m_bounds.y + (m_bounds.h - lh) * 0.5f - lh * 0.15f;
        ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, scale, Theme::textPrimary);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FwFader
// ═══════════════════════════════════════════════════════════════════════════

void FwFader::paint(UIContext& ctx) {
    if (!ctx.renderer) return;
    float cx = m_bounds.x + m_bounds.w * 0.5f;
    float trackW = 6.0f;
    float trackX = cx - trackW * 0.5f;

    ctx.renderer->drawRect(trackX, m_bounds.y, trackW, m_bounds.h,
                           Color{25, 25, 28, 255});

    float norm = (m_value - m_min) / (m_max - m_min);
    float fillH = m_bounds.h * norm;
    ctx.renderer->drawRect(trackX, m_bounds.y + m_bounds.h - fillH,
                           trackW, fillH, m_trackColor);

    float knobH = 6.0f;
    float knobW = m_bounds.w;
    float knobY = m_bounds.y + m_bounds.h - fillH - knobH * 0.5f;
    knobY = detail::cclamp(knobY, m_bounds.y, m_bounds.y + m_bounds.h - knobH);
    Color knobCol = m_dragging ? Color{220, 220, 220, 255}
                               : Color{180, 180, 180, 255};
    ctx.renderer->drawRect(m_bounds.x, knobY, knobW, knobH, knobCol);
}

// ═══════════════════════════════════════════════════════════════════════════
// FwKnob
// ═══════════════════════════════════════════════════════════════════════════

void FwKnob::renderArc(Renderer2D& renderer, float cx, float cy, float r,
                       float startNorm, float endNorm, Color color) {
    const int segs = 24;
    float startAngle = static_cast<float>(-M_PI * 0.75 + startNorm * M_PI * 1.5);
    float endAngle   = static_cast<float>(-M_PI * 0.75 + endNorm   * M_PI * 1.5);
    float step = (endAngle - startAngle) / segs;
    for (int i = 0; i < segs; ++i) {
        float a = startAngle + step * (i + 0.5f);
        float px = cx + std::cos(a) * r - 2;
        float py = cy + std::sin(a) * r - 2;
        renderer.drawRect(px, py, 4, 4, color);
    }
}

void FwKnob::paint(UIContext& ctx) {
    constexpr float kHoverSpeed = 0.15f;
    if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
    else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

    if (!ctx.renderer || !ctx.font) return;

    // Label on top (small, centered)
    float labelScale = 9.0f / Theme::kFontSize;
    float labelH = 0;
    if (!m_label.empty()) {
        float tw = ctx.font->textWidth(m_label, labelScale);
        float maxW = m_bounds.w - 2.0f;
        float tx = m_bounds.x + (m_bounds.w - detail::cmin(tw, maxW)) * 0.5f;
        float ty = m_bounds.y + 1;
        ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, labelScale,
                           Color{170, 175, 185, 255});
        labelH = ctx.font->lineHeight(labelScale) + 2;
    }

    // Arc in middle (below label, with clear gap)
    float arcArea = m_bounds.h - labelH;
    float cx = m_bounds.x + m_bounds.w * 0.5f;
    float cy = m_bounds.y + labelH + arcArea * 0.38f;
    float r = detail::cmin(m_bounds.w * 0.42f, arcArea * 0.32f);

    // Background arc
    renderArc(*ctx.renderer, cx, cy, r, 0.0f, 1.0f, Color{50, 50, 55, 255});

    // Use raw (unsnapped) value for smooth arc during discrete knob drag
    float displayVal = (m_dragging && m_step > 0) ? m_rawValue : m_value;
    float norm = (displayVal - m_min) / (m_max - m_min);
    norm = detail::cclamp(norm, 0.0f, 1.0f);

    // Value arc — neon palette
    Color arcCol;
    if (m_boolean) {
        bool on = m_value > (m_min + m_max) * 0.5f;
        arcCol = on ? Color{57, 255, 20, 255} : Color{80, 80, 85, 255};
        norm = on ? 1.0f : 0.0f;
    } else if (m_customColor) {
        arcCol = m_dragging ? m_arcColorActive : m_arcColor;
    } else {
        arcCol = m_dragging ? Color{0, 255, 255, 255} : Color{0, 200, 255, 255};
    }
    // Brighten arc on hover
    if (m_hoverAlpha > 0.001f && !m_boolean) {
        Color bright{
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.r + 40.0f)),
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.g + 40.0f)),
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.b + 40.0f)),
            arcCol.a};
        arcCol = Color::lerp(arcCol, bright, m_hoverAlpha);
    }
    renderArc(*ctx.renderer, cx, cy, r, 0.0f, norm, arcCol);

    // Dot indicator
    float angle = static_cast<float>(-M_PI * 0.75 + norm * M_PI * 1.5);
    float dotX = cx + std::cos(angle) * r * 0.65f - 2;
    float dotY = cy + std::sin(angle) * r * 0.65f - 2;
    ctx.renderer->drawRect(dotX, dotY, 4, 4, Theme::textPrimary);

    // Value text on bottom
    float valScale = 9.0f / Theme::kFontSize;
    float valY = m_bounds.y + m_bounds.h - ctx.font->lineHeight(valScale) - 1;
    if (m_editing) {
        std::string editStr = std::string(m_editText) + "|";
        float vw = ctx.font->textWidth(editStr, valScale);
        ctx.renderer->drawRect(m_bounds.x + 2, valY - 1, m_bounds.w - 4, 14.0f,
                               Color{40, 40, 50, 200});
        ctx.font->drawText(*ctx.renderer, editStr.c_str(),
                           m_bounds.x + (m_bounds.w - vw) * 0.5f,
                           valY, valScale, Color{255, 255, 255, 255});
    } else if (m_formatCb) {
        std::string valStr = m_formatCb(m_value);
        float vw = ctx.font->textWidth(valStr, valScale);
        ctx.font->drawText(*ctx.renderer, valStr.c_str(),
                           m_bounds.x + (m_bounds.w - vw) * 0.5f,
                           valY, valScale, Color{200, 205, 215, 255});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FwTextInput
// ═══════════════════════════════════════════════════════════════════════════

void FwTextInput::paint(UIContext& ctx) {
    if (!ctx.renderer || !ctx.font) return;
    Color bg = m_editing ? Color{55, 55, 60, 255} : Color{38, 38, 42, 255};
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
    ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                  m_editing ? Color{100, 160, 220, 255} : Color{60, 60, 65, 255});

    float scale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    float tx = m_bounds.x + 4;
    float lh = ctx.font->lineHeight(scale);
    float ty = m_bounds.y + (m_bounds.h - lh) * 0.5f - lh * 0.15f;

    if (m_text.empty() && !m_editing) {
        ctx.font->drawText(*ctx.renderer, m_placeholder.c_str(), tx, ty, scale, Theme::textDim);
    } else {
        const std::string& display = m_editing ? m_text + "|" : m_text;
        ctx.font->drawText(*ctx.renderer, display.c_str(), tx, ty, scale, Theme::textPrimary);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FwNumberInput
// ═══════════════════════════════════════════════════════════════════════════

void FwNumberInput::paint(UIContext& ctx) {
    if (!ctx.renderer || !ctx.font) return;
    Color bg = m_editing ? Color{55, 55, 60, 255}
              : m_hovered ? Color{48, 48, 52, 255}
                          : Color{38, 38, 42, 255};
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
    ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                  m_editing ? Color{100, 160, 220, 255} : Color{60, 60, 65, 255});

    char buf[64];
    if (m_editing) {
        std::snprintf(buf, sizeof(buf), "%s|", m_editText);
    } else {
        std::snprintf(buf, sizeof(buf), m_format.c_str(), m_value);
        if (!m_suffix.empty()) {
            size_t len = std::strlen(buf);
            std::snprintf(buf + len, sizeof(buf) - len, " %s", m_suffix.c_str());
        }
    }

    float scale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    float tw = ctx.font->textWidth(buf, scale);
    float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
    float lh = ctx.font->lineHeight(scale);
    float ty = m_bounds.y + (m_bounds.h - lh) * 0.5f - lh * 0.15f;
    ctx.font->drawText(*ctx.renderer, buf, tx, ty, scale, Theme::textPrimary);
}

// ═══════════════════════════════════════════════════════════════════════════
// FwDropDown
// ═══════════════════════════════════════════════════════════════════════════

void FwDropDown::paint(UIContext& ctx) {
    if (!ctx.renderer || !ctx.font) return;
    Color bg = m_open ? Color{55, 55, 60, 255} : Color{42, 42, 46, 255};
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
    ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                  m_open ? Color{100, 160, 220, 255} : Color{70, 70, 75, 255});

    float scale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    float lh = ctx.font->lineHeight(scale);
    float ty = m_bounds.y + (m_bounds.h - lh) * 0.5f - lh * 0.15f;

    // Clip text to dropdown bounds (leave room for arrow)
    float arrowZone = 14.0f;
    float textW = m_bounds.w - 6 - arrowZone;
    ctx.renderer->pushClip(m_bounds.x + 1, m_bounds.y, textW + 5, m_bounds.h);
    std::string txt = selectedText();
    ctx.font->drawText(*ctx.renderer, txt.c_str(), m_bounds.x + 6, ty, scale, Theme::textPrimary);
    ctx.renderer->popClip();

    // Draw triangle arrow (▼ or ▲)
    float triSize = 4.0f;
    float triCx = m_bounds.x + m_bounds.w - arrowZone * 0.5f;
    float triCy = m_bounds.y + m_bounds.h * 0.5f;
    if (m_open) {
        // Up triangle ▲
        ctx.renderer->drawTriangle(
            triCx, triCy - triSize * 0.5f,
            triCx - triSize, triCy + triSize * 0.5f,
            triCx + triSize, triCy + triSize * 0.5f,
            Theme::textSecondary);
    } else {
        // Down triangle ▼
        ctx.renderer->drawTriangle(
            triCx - triSize, triCy - triSize * 0.5f,
            triCx + triSize, triCy - triSize * 0.5f,
            triCx, triCy + triSize * 0.5f,
            Theme::textSecondary);
    }
}

void FwDropDown::paintOverlay(UIContext& ctx) {
    if (!m_open || !ctx.renderer || !ctx.font) return;

    float scale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    float lh = ctx.font->lineHeight(scale);
    float itemH = m_bounds.h;
    int mv = maxVisible();
    float listH = mv * itemH;
    float listX = m_bounds.x;
    float listY = m_bounds.y + m_bounds.h;

    ctx.renderer->drawRect(listX, listY, m_bounds.w, listH, Color{30, 30, 34, 255});
    ctx.renderer->drawRectOutline(listX, listY, m_bounds.w, listH, Color{90, 140, 200, 255});

    // Clip item text to popup bounds
    ctx.renderer->pushClip(listX, listY, m_bounds.w, listH);
    for (int i = 0; i < static_cast<int>(m_items.size()) && i < mv; ++i) {
        float iy = listY + i * itemH;

        Color itemBg;
        Color textCol;
        if (i == m_hoverItem) {
            itemBg = Color{200, 200, 210, 255};
            textCol = Color{15, 15, 20, 255};
        } else if (i == m_selected) {
            itemBg = Color{70, 130, 200, 255};
            textCol = Color{255, 255, 255, 255};
        } else {
            itemBg = Color{30, 30, 34, 255};
            textCol = Theme::textPrimary;
        }

        ctx.renderer->drawRect(listX + 1, iy, m_bounds.w - 2, itemH, itemBg);

        float itemTy = iy + (itemH - lh) * 0.5f - lh * 0.15f;
        ctx.font->drawText(*ctx.renderer, m_items[i].c_str(),
                           listX + 8, itemTy, scale, textCol);
    }
    ctx.renderer->popClip();
}

// ═══════════════════════════════════════════════════════════════════════════
// MeterWidget
// ═══════════════════════════════════════════════════════════════════════════

void MeterWidget::paint(UIContext& ctx) {
    if (!ctx.renderer) return;
    float halfW = m_bounds.w * 0.5f - 1;
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, halfW, m_bounds.h,
                           Color{20, 20, 22});
    ctx.renderer->drawRect(m_bounds.x + halfW + 2, m_bounds.y, halfW, m_bounds.h,
                           Color{20, 20, 22});

    float hL = dbToHeight(m_peakL) * m_bounds.h;
    float hR = dbToHeight(m_peakR) * m_bounds.h;
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y + m_bounds.h - hL,
                           halfW, hL, meterColor(m_peakL));
    ctx.renderer->drawRect(m_bounds.x + halfW + 2, m_bounds.y + m_bounds.h - hR,
                           halfW, hR, meterColor(m_peakR));
}

// ═══════════════════════════════════════════════════════════════════════════
// PanWidget
// ═══════════════════════════════════════════════════════════════════════════

void PanWidget::paint(UIContext& ctx) {
    if (!ctx.renderer) return;
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                           Theme::clipSlotEmpty);
    float center = m_bounds.x + m_bounds.w * 0.5f;
    float thumbX = center + m_value * (m_bounds.w * 0.5f - 4) - 4;
    ctx.renderer->drawRect(thumbX, m_bounds.y, 8, m_bounds.h, m_thumbColor);
}

// ═══════════════════════════════════════════════════════════════════════════
// ScrollBar
// ═══════════════════════════════════════════════════════════════════════════

void ScrollBar::paint(UIContext& ctx) {
    if (!ctx.renderer) return;
    ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                           Color{40, 40, 45});
    if (m_contentSize <= m_bounds.w) return;

    float thumbW = std::max(20.0f,
        m_bounds.w * (m_bounds.w / std::max(1.0f, m_contentSize)));
    float maxScroll = m_contentSize - m_bounds.w;
    float scrollFrac = m_scrollPos / std::max(1.0f, maxScroll);
    float thumbX = m_bounds.x + scrollFrac * (m_bounds.w - thumbW);

    Color thumbCol = (m_dragging || m_hovered)
        ? Color{120, 120, 130} : Color{90, 90, 100};
    ctx.renderer->drawRect(thumbX, m_bounds.y, thumbW, m_bounds.h, thumbCol);
}

// ═══════════════════════════════════════════════════════════════════════════
// FwDentedKnob
// ═══════════════════════════════════════════════════════════════════════════

void FwDentedKnob::renderArc(Renderer2D& renderer, float cx, float cy, float r,
                              float startNorm, float endNorm, Color color) {
    const int segs = 24;
    float startAngle = static_cast<float>(-M_PI * 0.75 + startNorm * M_PI * 1.5);
    float endAngle   = static_cast<float>(-M_PI * 0.75 + endNorm   * M_PI * 1.5);
    float step = (endAngle - startAngle) / segs;
    for (int i = 0; i < segs; ++i) {
        float a = startAngle + step * (i + 0.5f);
        float px = cx + std::cos(a) * r - 2;
        float py = cy + std::sin(a) * r - 2;
        renderer.drawRect(px, py, 4, 4, color);
    }
}

void FwDentedKnob::paint(UIContext& ctx) {
    constexpr float kHoverSpeed = 0.15f;
    if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
    else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

    if (!ctx.renderer || !ctx.font) return;

    float labelScale = 9.0f / Theme::kFontSize;
    float labelH = 0;
    if (!m_label.empty()) {
        float tw = ctx.font->textWidth(m_label, labelScale);
        float maxW = m_bounds.w - 2.0f;
        float tx = m_bounds.x + (m_bounds.w - detail::cmin(tw, maxW)) * 0.5f;
        float ty = m_bounds.y + 1;
        ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, labelScale,
                           Color{170, 175, 185, 255});
        labelH = ctx.font->lineHeight(labelScale) + 2;
    }

    float arcArea = m_bounds.h - labelH;
    float cx = m_bounds.x + m_bounds.w * 0.5f;
    float cy = m_bounds.y + labelH + arcArea * 0.38f;
    float r = detail::cmin(m_bounds.w * 0.42f, arcArea * 0.32f);

    // Background arc
    renderArc(*ctx.renderer, cx, cy, r, 0.0f, 1.0f, Color{50, 50, 55, 255});

    float range = m_max - m_min;
    float norm = (range > 0) ? (m_value - m_min) / range : 0.0f;
    norm = detail::cclamp(norm, 0.0f, 1.0f);

    Color arcCol = m_customColor ? m_arcColor : Color{0, 200, 255, 255};
    if (m_dragging) {
        arcCol = m_customColor ? Color{static_cast<uint8_t>(detail::cmin(255.0f, m_arcColor.r + 55.0f)),
                                        static_cast<uint8_t>(detail::cmin(255.0f, m_arcColor.g + 55.0f)),
                                        static_cast<uint8_t>(detail::cmin(255.0f, m_arcColor.b + 55.0f)),
                                        m_arcColor.a}
                               : Color{0, 255, 255, 255};
    }
    if (m_hoverAlpha > 0.001f) {
        Color bright{
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.r + 40.0f)),
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.g + 40.0f)),
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.b + 40.0f)),
            arcCol.a};
        arcCol = Color::lerp(arcCol, bright, m_hoverAlpha);
    }
    renderArc(*ctx.renderer, cx, cy, r, 0.0f, norm, arcCol);

    // Dot indicator
    float angle = static_cast<float>(-M_PI * 0.75 + norm * M_PI * 1.5);
    float dotX = cx + std::cos(angle) * r * 0.65f - 2;
    float dotY = cy + std::sin(angle) * r * 0.65f - 2;
    ctx.renderer->drawRect(dotX, dotY, 4, 4, Theme::textPrimary);

    // Detent tick marks on outer ring
    for (auto& d : m_detents) {
        float dNorm = (range > 0) ? (d.value - m_min) / range : 0.0f;
        float dAngle = static_cast<float>(-M_PI * 0.75 + dNorm * M_PI * 1.5);
        float outerR = r + 4.0f;
        float dx = cx + std::cos(dAngle) * outerR - 1.5f;
        float dy = cy + std::sin(dAngle) * outerR - 1.5f;
        Color tickCol = (std::abs(m_value - d.value) < 0.001f)
            ? Color{255, 220, 80, 255} : Color{120, 120, 130, 255};
        ctx.renderer->drawRect(dx, dy, 3, 3, tickCol);
    }

    // Value text
    float valScale = 9.0f / Theme::kFontSize;
    float valY = m_bounds.y + m_bounds.h - ctx.font->lineHeight(valScale) - 1;
    if (m_formatCb) {
        std::string valStr = m_formatCb(m_value);
        float vw = ctx.font->textWidth(valStr, valScale);
        ctx.font->drawText(*ctx.renderer, valStr.c_str(),
                           m_bounds.x + (m_bounds.w - vw) * 0.5f,
                           valY, valScale, Color{200, 205, 215, 255});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FwKnob360
// ═══════════════════════════════════════════════════════════════════════════

void FwKnob360::paint(UIContext& ctx) {
    constexpr float kHoverSpeed = 0.15f;
    if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
    else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

    if (!ctx.renderer || !ctx.font) return;

    float labelScale = 9.0f / Theme::kFontSize;
    float labelH = 0;
    if (!m_label.empty()) {
        float tw = ctx.font->textWidth(m_label, labelScale);
        float maxW = m_bounds.w - 2.0f;
        float tx = m_bounds.x + (m_bounds.w - detail::cmin(tw, maxW)) * 0.5f;
        float ty = m_bounds.y + 1;
        ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, labelScale,
                           Color{170, 175, 185, 255});
        labelH = ctx.font->lineHeight(labelScale) + 2;
    }

    float arcArea = m_bounds.h - labelH;
    float cx = m_bounds.x + m_bounds.w * 0.5f;
    float cy = m_bounds.y + labelH + arcArea * 0.38f;
    float r = detail::cmin(m_bounds.w * 0.42f, arcArea * 0.32f);

    // Full circle background (360°)
    const int segs = 32;
    for (int i = 0; i < segs; ++i) {
        float a = static_cast<float>(2.0 * M_PI * i / segs);
        float px = cx + std::cos(a) * r - 2;
        float py = cy + std::sin(a) * r - 2;
        ctx.renderer->drawRect(px, py, 4, 4, Color{50, 50, 55, 255});
    }

    // Value arc — from top (12 o'clock) clockwise
    float range = m_max - m_min;
    float norm = (range > 0) ? (m_value - m_min) / range : 0.0f;
    norm = detail::cclamp(norm, 0.0f, 1.0f);

    Color arcCol = m_customColor ? m_arcColor : Color{0, 200, 255, 255};
    if (m_dragging) {
        arcCol = m_customColor ? Color{static_cast<uint8_t>(detail::cmin(255.0f, m_arcColor.r + 55.0f)),
                                        static_cast<uint8_t>(detail::cmin(255.0f, m_arcColor.g + 55.0f)),
                                        static_cast<uint8_t>(detail::cmin(255.0f, m_arcColor.b + 55.0f)),
                                        m_arcColor.a}
                               : Color{0, 255, 255, 255};
    }
    if (m_hoverAlpha > 0.001f) {
        Color bright{
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.r + 40.0f)),
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.g + 40.0f)),
            static_cast<uint8_t>(detail::cmin(255.0f, arcCol.b + 40.0f)),
            arcCol.a};
        arcCol = Color::lerp(arcCol, bright, m_hoverAlpha);
    }

    int valSegs = static_cast<int>(norm * segs);
    for (int i = 0; i < valSegs; ++i) {
        // Start from top (-π/2), go clockwise
        float a = static_cast<float>(-M_PI * 0.5 + 2.0 * M_PI * i / segs);
        float px = cx + std::cos(a) * r - 2;
        float py = cy + std::sin(a) * r - 2;
        ctx.renderer->drawRect(px, py, 4, 4, arcCol);
    }

    // Dot indicator at current position
    float dotAngle = static_cast<float>(-M_PI * 0.5 + norm * 2.0 * M_PI);
    float dotX = cx + std::cos(dotAngle) * r * 0.65f - 2;
    float dotY = cy + std::sin(dotAngle) * r * 0.65f - 2;
    ctx.renderer->drawRect(dotX, dotY, 4, 4, Theme::textPrimary);

    // Value text
    float valScale = 9.0f / Theme::kFontSize;
    float valY = m_bounds.y + m_bounds.h - ctx.font->lineHeight(valScale) - 1;
    if (m_formatCb) {
        std::string valStr = m_formatCb(m_value);
        float vw = ctx.font->textWidth(valStr, valScale);
        ctx.font->drawText(*ctx.renderer, valStr.c_str(),
                           m_bounds.x + (m_bounds.w - vw) * 0.5f,
                           valY, valScale, Color{200, 205, 215, 255});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FwToggleSwitch
// ═══════════════════════════════════════════════════════════════════════════

void FwToggleSwitch::paint(UIContext& ctx) {
    constexpr float kAnimSpeed = 0.15f;
    float target = m_state ? 1.0f : 0.0f;
    if (m_animPos < target) m_animPos = detail::cmin(target, m_animPos + kAnimSpeed);
    else if (m_animPos > target) m_animPos = detail::cmax(target, m_animPos - kAnimSpeed);

    if (!ctx.renderer || !ctx.font) return;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w, h = m_bounds.h;

    // Top label (parameter name)
    float labelScale = 9.0f / Theme::kFontSize;
    float labelH = 0;
    if (!m_topLabel.empty()) {
        float tw = ctx.font->textWidth(m_topLabel, labelScale);
        float tx = x + (w - tw) * 0.5f;
        ctx.font->drawText(*ctx.renderer, m_topLabel.c_str(), tx, y + 1,
                           labelScale, Color{170, 175, 185, 255});
        labelH = 12.0f;
    }

    float switchY = y + labelH;
    float switchH = h - labelH;
    float halfW = w * 0.5f;

    // Background track
    ctx.renderer->drawRoundedRect(x, switchY, w, switchH, switchH * 0.4f, Color{35, 35, 40, 255});
    ctx.renderer->drawRectOutline(x, switchY, w, switchH, Color{60, 60, 65, 255});

    // Sliding pill indicator
    float pillX = x + m_animPos * halfW;
    Color pillCol = m_state ? m_activeColor : Color{80, 80, 90, 255};
    ctx.renderer->drawRoundedRect(pillX + 1, switchY + 1, halfW - 2, switchH - 2, (switchH - 2) * 0.4f, pillCol);

    // Labels
    float scale = 9.0f / Theme::kFontSize;
    float lh = ctx.font->lineHeight(scale);
    float ty = switchY + (switchH - lh) * 0.5f - lh * 0.15f;

    Color leftCol  = m_state ? Theme::textDim : Theme::textPrimary;
    Color rightCol = m_state ? Theme::textPrimary : Theme::textDim;

    float ltw = ctx.font->textWidth(m_leftLabel, scale);
    ctx.font->drawText(*ctx.renderer, m_leftLabel.c_str(),
                       x + (halfW - ltw) * 0.5f, ty, scale, leftCol);

    float rtw = ctx.font->textWidth(m_rightLabel, scale);
    ctx.font->drawText(*ctx.renderer, m_rightLabel.c_str(),
                       x + halfW + (halfW - rtw) * 0.5f, ty, scale, rightCol);
}

// ═══════════════════════════════════════════════════════════════════════════
// FwStepSelector
// ═══════════════════════════════════════════════════════════════════════════

void FwStepSelector::paint(UIContext& ctx) {
    if (!ctx.renderer || !ctx.font) return;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w, h = m_bounds.h;
    float arrowW = 16.0f;

    // Label (if any)
    float labelScale = 9.0f / Theme::kFontSize;
    float labelH = 0;
    if (!m_label.empty()) {
        float tw = ctx.font->textWidth(m_label, labelScale);
        float tx = x + (w - tw) * 0.5f;
        ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, y + 1,
                           labelScale, Color{170, 175, 185, 255});
        labelH = 12.0f;
    }

    float dispY = y + labelH;
    float dispH = h - labelH;

    // Background
    ctx.renderer->drawRect(x, dispY, w, dispH, Color{38, 38, 42, 255});
    ctx.renderer->drawRectOutline(x, dispY, w, dispH, Color{60, 60, 65, 255});

    // Left arrow (◀)
    float triSize = 4.0f;
    float laCx = x + arrowW * 0.5f;
    float laCy = dispY + dispH * 0.5f;
    ctx.renderer->drawTriangle(
        laCx + triSize, laCy - triSize,
        laCx - triSize, laCy,
        laCx + triSize, laCy + triSize,
        Theme::textSecondary);

    // Right arrow (▶)
    float raCx = x + w - arrowW * 0.5f;
    float raCy = laCy;
    ctx.renderer->drawTriangle(
        raCx - triSize, raCy - triSize,
        raCx + triSize, raCy,
        raCx - triSize, raCy + triSize,
        Theme::textSecondary);

    // Value display (centered between arrows)
    float valScale = 9.0f / Theme::kFontSize;
    std::string valStr;
    if (m_formatCb) {
        valStr = m_formatCb(m_value);
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", m_value);
        valStr = buf;
    }
    float vw = ctx.font->textWidth(valStr, valScale);
    float lh = ctx.font->lineHeight(valScale);
    float vtx = x + (w - vw) * 0.5f;
    float vty = dispY + (dispH - lh) * 0.5f - lh * 0.15f;
    ctx.font->drawText(*ctx.renderer, valStr.c_str(), vtx, vty, valScale, Theme::textPrimary);
}

} // namespace fw
} // namespace ui
} // namespace yawn
