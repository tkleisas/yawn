#pragma once
// ConfirmDialogWidget — Modal Yes/No confirmation dialog built on fw::Dialog.

#include "Dialog.h"
#include "../Theme.h"
#include <string>
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

class ConfirmDialogWidget : public Dialog {
public:
    ConfirmDialogWidget()
        : Dialog("", 500, 120)
    {
        m_showClose = false;
        m_showOKCancel = false;
        m_visible = false;
    }

    // ─── Public API ─────────────────────────────────────────────────────

    void prompt(const std::string& message, std::function<void()> onConfirm) {
        m_message   = message;
        m_onConfirm = std::move(onConfirm);
        m_visible   = true;
    }

    bool isOpen() const { return m_visible; }

    void dismiss() {
        m_visible   = false;
        m_onConfirm = nullptr;
    }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& /*constraints*/, const UIContext& /*ctx*/) override {
        return {m_screenW, m_screenH};
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds  = bounds;
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

        // Message text
        float textScale = 12.0f / Theme::kFontSize;
        font->drawText(*renderer, m_message.c_str(), m_dx + 20, m_dy + 28,
                        textScale, Theme::textPrimary);

        // Buttons
        float btnScale = 13.0f / Theme::kFontSize;
        float textH    = font->lineHeight(btnScale);
        float yW       = font->textWidth("Yes", btnScale);
        float nW       = font->textWidth("No", btnScale);
        float btnPadX  = 24.0f;
        float bwYes    = yW + btnPadX * 2;
        float bwNo     = nW + btnPadX * 2;

        float btnY = m_dy + dh - kBtnH - 14;
        float yesX = m_dx + dw * 0.5f - bwYes - 8;
        float noX  = m_dx + dw * 0.5f + 8;

        renderer->drawRect(yesX, btnY, bwYes, kBtnH, Color{50, 130, 50, 255});
        renderer->drawRect(noX,  btnY, bwNo,  kBtnH, Color{130, 50, 50, 255});

        float textOffY = (kBtnH - textH) * 0.5f;
        font->drawText(*renderer, "Yes", yesX + (bwYes - yW) * 0.5f, btnY + textOffY,
                        btnScale, Theme::textPrimary);
        font->drawText(*renderer, "No",  noX  + (bwNo  - nW) * 0.5f, btnY + textOffY,
                        btnScale, Theme::textPrimary);
#else
        (void)ctx;
#endif
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float dw = m_preferredW;
        float dh = m_preferredH;

        float btnY = m_dy + dh - kBtnH - 14;

        // Use generous hit zones matching the old ConfirmDialog
        float yesX = m_dx + dw * 0.5f - kBtnHitW - 8;
        float noX  = m_dx + dw * 0.5f + 8;

        // Yes button
        if (e.x >= yesX && e.x < yesX + kBtnHitW &&
            e.y >= btnY && e.y < btnY + kBtnH) {
            if (m_onConfirm) m_onConfirm();
            dismiss();
            e.consume();
            return true;
        }

        // No button
        if (e.x >= noX && e.x < noX + kBtnHitW &&
            e.y >= btnY && e.y < btnY + kBtnH) {
            dismiss();
            e.consume();
            return true;
        }

        // Modal — consume all clicks
        e.consume();
        return true;
    }

    bool onKeyDown(KeyEvent& e) override {
        if (e.keyCode == 27) { // Escape → dismiss
            dismiss();
            return true;
        }
        if (e.keyCode == 13) { // Enter → confirm
            if (m_onConfirm) m_onConfirm();
            dismiss();
            return true;
        }
        return true; // modal — consume all keys
    }

private:
    static constexpr float kBtnH    = 36.0f;
    static constexpr float kBtnHitW = 100.0f;

    std::string m_message;
    std::function<void()> m_onConfirm;

    float m_screenW = 0;
    float m_screenH = 0;
    float m_dx = 0;
    float m_dy = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
