#pragma once

#include "ui/Widget.h"
#include "ui/Theme.h"
#include <string>
#include <functional>

namespace yawn {
namespace ui {

// Simple modal confirmation dialog with Yes/No buttons.
// Renders as a centered overlay that blocks all input.

class ConfirmDialog {
public:
    bool isOpen() const { return m_open; }

    void show(const std::string& message, std::function<void()> onConfirm) {
        m_open = true;
        m_message = message;
        m_onConfirm = std::move(onConfirm);
    }

    void close() {
        m_open = false;
        m_onConfirm = nullptr;
    }

    // Returns true if click was consumed (always true when dialog is open)
    bool handleClick(float mx, float my, float screenW, float screenH) {
        if (!m_open) return false;

        float dw = kDialogW, dh = kDialogH;
        float dx = (screenW - dw) * 0.5f;
        float dy = (screenH - dh) * 0.5f;

        float btnY = dy + dh - kBtnH - 14;
        // Use generous hit zones that cover dynamic button widths
        float yesX = dx + dw * 0.5f - kBtnHitW - 8;
        float noX  = dx + dw * 0.5f + 8;

        if (mx >= yesX && mx < yesX + kBtnHitW && my >= btnY && my < btnY + kBtnH) {
            if (m_onConfirm) m_onConfirm();
            close();
            return true;
        }
        if (mx >= noX && mx < noX + kBtnHitW && my >= btnY && my < btnY + kBtnH) {
            close();
            return true;
        }

        return true;  // modal — consume all clicks
    }

    void render([[maybe_unused]] Renderer2D& renderer,
                [[maybe_unused]] Font& font,
                [[maybe_unused]] float screenW,
                [[maybe_unused]] float screenH) {
#ifndef YAWN_TEST_BUILD
        if (!m_open) return;

        // Dimmed overlay
        renderer.drawRect(0, 0, screenW, screenH, Color{0, 0, 0, 140});

        float dw = kDialogW, dh = kDialogH;
        float dx = (screenW - dw) * 0.5f;
        float dy = (screenH - dh) * 0.5f;

        // Dialog background
        renderer.drawRect(dx, dy, dw, dh, Color{45, 45, 52, 255});
        renderer.drawRectOutline(dx, dy, dw, dh, Color{75, 75, 85, 255});

        // Message
        float textScale = 12.0f / Theme::kFontSize;
        font.drawText(renderer, m_message.c_str(), dx + 20, dy + 28, textScale,
                      Theme::textPrimary);

        // Buttons
        float btnScale = 13.0f / Theme::kFontSize;
        float textH = font.lineHeight(btnScale);
        float yW = font.textWidth("Yes", btnScale);
        float nW = font.textWidth("No", btnScale);
        float btnPadX = 24.0f;
        float bwYes = yW + btnPadX * 2;
        float bwNo  = nW + btnPadX * 2;

        float btnY = dy + dh - kBtnH - 14;
        float yesX = dx + dw * 0.5f - bwYes - 8;
        float noX  = dx + dw * 0.5f + 8;

        renderer.drawRect(yesX, btnY, bwYes, kBtnH, Color{50, 130, 50, 255});
        renderer.drawRect(noX,  btnY, bwNo,  kBtnH, Color{130, 50, 50, 255});

        float textOffY = (kBtnH - textH) * 0.5f;
        font.drawText(renderer, "Yes", yesX + (bwYes - yW) * 0.5f, btnY + textOffY,
                      btnScale, Theme::textPrimary);
        font.drawText(renderer, "No",  noX  + (bwNo  - nW) * 0.5f, btnY + textOffY,
                      btnScale, Theme::textPrimary);
#endif
    }

private:
    static constexpr float kDialogW  = 500.0f;
    static constexpr float kDialogH  = 120.0f;
    static constexpr float kBtnH     = 36.0f;
    static constexpr float kBtnHitW  = 100.0f;  // generous hit area for click detection

    bool m_open = false;
    std::string m_message;
    std::function<void()> m_onConfirm;
};

} // namespace ui
} // namespace yawn
