#pragma once
// AboutDialog — Modal "About Y.A.W.N" dialog built on fw::Dialog.

#include "Dialog.h"
#include "../Theme.h"
#include "Version.h"
#include <glad/gl.h>

namespace yawn {
namespace ui {
namespace fw {

class AboutDialog : public Dialog {
public:
    AboutDialog()
        : Dialog("About Y.A.W.N", 480, 280)
    {
        m_showClose = false;
        m_showOKCancel = false;
        m_visible = false;
    }

    void setIconTexture(GLuint tex) { m_iconTexture = tex; }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& /*constraints*/, const UIContext& /*ctx*/) override {
        // Return full available size so the overlay covers the entire screen.
        return {m_screenW, m_screenH};
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        m_screenW = bounds.w;
        m_screenH = bounds.h;
        m_dx = (m_screenW - m_preferredW) * 0.5f;
        m_dy = (m_screenH - m_preferredH) * 0.5f;
        Dialog::layout(Rect{m_dx, m_dy, m_preferredW, m_preferredH}, ctx);
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        auto* renderer = ctx.renderer;
        auto* font     = ctx.font;
        float dw = m_preferredW;
        float dh = m_preferredH;

        // Dimmed fullscreen overlay
        renderer->drawRect(0, 0, m_screenW, m_screenH, Color{0, 0, 0, 140});

        // Dialog background and outline
        renderer->drawRect(m_dx, m_dy, dw, dh, Color{45, 45, 52, 255});
        renderer->drawRectOutline(m_dx, m_dy, dw, dh, Color{75, 75, 85, 255});

        // Icon
        float iconSize = 80.0f;
        float iconX = m_dx + 20;
        float iconY = m_dy + 24;
        if (m_iconTexture) {
            renderer->drawTexturedQuad(iconX, iconY, iconSize, iconSize,
                                       0.0f, 0.0f, 1.0f, 1.0f,
                                       Color{255, 255, 255, 255}, m_iconTexture);
        } else {
            renderer->drawRect(iconX, iconY, iconSize, iconSize, Color{60, 60, 70, 255});
        }

        // Text (positioned to the right of the icon)
        float ts      = 14.0f / Theme::kFontSize;
        float tsSmall = 11.0f / Theme::kFontSize;
        float lineH   = 20.0f;
        float textX   = iconX + iconSize + 16;
        float textY   = iconY + 4;

        font->drawText(*renderer, "Y.A.W.N", textX, textY,
                        18.0f / Theme::kFontSize, Color{100, 180, 255, 255});
        textY += lineH + 4;
        font->drawText(*renderer, "Yetanother Audio Workstation New", textX, textY,
                        tsSmall, Theme::textSecondary);
        textY += lineH;
        font->drawText(*renderer, "Version " YAWN_VERSION_STRING, textX, textY,
                        ts, Theme::textPrimary);
        textY += lineH + 4;
        font->drawText(*renderer, "Made with AI-Sloptronic(TM) technology", textX, textY,
                        tsSmall, Color{180, 140, 255, 255});

        // Credits below icon area
        float creditsY = iconY + iconSize + 16;
        float creditsX = m_dx + 20;
        font->drawText(*renderer, "PM: Tasos Kleisas", creditsX, creditsY,
                        tsSmall, Theme::textSecondary);
        creditsY += lineH;
        font->drawText(*renderer, "Chief Engineer: Claude (Anthropic)", creditsX, creditsY,
                        tsSmall, Theme::textSecondary);
        creditsY += lineH;
        font->drawText(*renderer, "Where \"it compiles\" is the new \"it works\"", creditsX, creditsY,
                        10.0f / Theme::kFontSize, Color{120, 120, 130, 255});

        // OK button
        float btnScale = 13.0f / Theme::kFontSize;
        float textH    = font->lineHeight(btnScale);
        float okW      = font->textWidth("OK", btnScale);
        float btnPadX  = 24.0f;
        float btnW     = okW + btnPadX * 2;
        float btnH     = kBtnH;
        float btnX     = m_dx + (dw - btnW) * 0.5f;
        float btnY     = m_dy + dh - btnH - 14;

        renderer->drawRect(btnX, btnY, btnW, btnH, Color{60, 120, 200, 255});
        float textOffY = (btnH - textH) * 0.5f;
        font->drawText(*renderer, "OK", btnX + (btnW - okW) * 0.5f, btnY + textOffY,
                        btnScale, Theme::textPrimary);
#else
        (void)ctx;
#endif
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float dw = m_preferredW;
        float dh = m_preferredH;

        // Generous hit zone centered on the OK button
        float hitW = 100.0f;
        float hitX = m_dx + (dw - hitW) * 0.5f;
        float btnY = m_dy + dh - kBtnH - 14;

        if (e.x >= hitX && e.x < hitX + hitW &&
            e.y >= btnY && e.y < btnY + kBtnH) {
            close(DialogResult::OK);
            e.consume();
            return true;
        }

        // Modal — consume all clicks
        e.consume();
        return true;
    }

    bool onKeyDown(KeyEvent& e) override {
        if (e.keyCode == 27 || e.keyCode == 13) { // Escape or Enter
            close(DialogResult::OK);
            return true;
        }
        return true; // modal — consume all keys
    }

private:
    static constexpr float kBtnH = 36.0f;

    float m_screenW = 0;
    float m_screenH = 0;
    float m_dx = 0;
    float m_dy = 0;
    GLuint m_iconTexture = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
