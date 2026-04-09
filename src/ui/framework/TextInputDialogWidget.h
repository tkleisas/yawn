#pragma once
// TextInputDialogWidget — Modal text-input dialog (e.g. "Save Preset" name entry).

#include "Dialog.h"
#include "../Theme.h"
#include "../Font.h"
#include <string>
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

class TextInputDialogWidget : public Dialog {
public:
    TextInputDialogWidget()
        : Dialog("", 420, 130)
    {
        m_showClose = false;
        m_showOKCancel = false;
        m_visible = false;
    }

    // Show the dialog with a prompt and default text.
    void prompt(const std::string& title,
                const std::string& defaultText,
                std::function<void(const std::string&)> onConfirm) {
        m_title     = title;
        m_text      = defaultText;
        m_cursor    = static_cast<int>(m_text.size());
        m_onConfirm = std::move(onConfirm);
        m_visible   = true;
    }

    bool isOpen() const { return m_visible; }
    void dismiss() { m_visible = false; m_onConfirm = nullptr; }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints&, const UIContext&) override {
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

        renderer->drawRect(0, 0, m_screenW, m_screenH, Color{0, 0, 0, 140});
        renderer->drawRect(m_dx, m_dy, dw, dh, Color{45, 45, 52, 255});
        renderer->drawRectOutline(m_dx, m_dy, dw, dh, Color{75, 75, 85, 255});

        float titleScale = 12.0f / Theme::kFontSize;
        font->drawText(*renderer, m_title.c_str(), m_dx + 16, m_dy + 14,
                       titleScale, Theme::textPrimary);

        // Text input field
        float fx = m_dx + 16;
        float fy = m_dy + 40;
        float fw = dw - 32;
        float fh = 28;
        renderer->drawRect(fx, fy, fw, fh, Color{25, 25, 30, 255});
        renderer->drawRectOutline(fx, fy, fw, fh, Color{80, 120, 200, 255});

        float textScale = 12.0f / Theme::kFontSize;
        float lineH = 48.0f * textScale;   // font pixel height * scale
        float textY = fy + (fh - lineH) * 0.5f;
        font->drawText(*renderer, m_text.c_str(), fx + 6, textY, textScale,
                       Theme::textPrimary);

        // Cursor
        std::string beforeCursor = m_text.substr(0, m_cursor);
        float cursorX = fx + 6 + font->textWidth(beforeCursor.c_str(), textScale);
        renderer->drawRect(cursorX, fy + 4, 1, fh - 8, Theme::textPrimary);

        // OK / Cancel buttons
        float btnScale = 13.0f / Theme::kFontSize;
        float btnY = m_dy + dh - kBtnH - 12;
        float okX  = m_dx + dw * 0.5f - kBtnW - 8;
        float cnX  = m_dx + dw * 0.5f + 8;

        renderer->drawRect(okX, btnY, kBtnW, kBtnH, Color{50, 130, 50, 255});
        renderer->drawRect(cnX, btnY, kBtnW, kBtnH, Color{100, 50, 50, 255});

        float textH = font->lineHeight(btnScale);
        float textOff = (kBtnH - textH) * 0.5f;
        float okTW = font->textWidth("OK", btnScale);
        float cnTW = font->textWidth("Cancel", btnScale);
        font->drawText(*renderer, "OK", okX + (kBtnW - okTW) * 0.5f, btnY + textOff,
                       btnScale, Theme::textPrimary);
        font->drawText(*renderer, "Cancel", cnX + (kBtnW - cnTW) * 0.5f, btnY + textOff,
                       btnScale, Theme::textPrimary);
#else
        (void)ctx;
#endif
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float dh = m_preferredH;
        float dw = m_preferredW;
        float btnY = m_dy + dh - kBtnH - 12;
        float okX  = m_dx + dw * 0.5f - kBtnW - 8;
        float cnX  = m_dx + dw * 0.5f + 8;

        if (e.x >= okX && e.x < okX + kBtnW && e.y >= btnY && e.y < btnY + kBtnH) {
            if (m_onConfirm && !m_text.empty()) m_onConfirm(m_text);
            dismiss();
            e.consume();
            return true;
        }
        if (e.x >= cnX && e.x < cnX + kBtnW && e.y >= btnY && e.y < btnY + kBtnH) {
            dismiss();
            e.consume();
            return true;
        }
        e.consume();
        return true;
    }

    bool onKeyDown(KeyEvent& e) override {
        if (e.keyCode == 27) { // Escape
            dismiss();
            return true;
        }
        if (e.keyCode == 13) { // Enter
            if (m_onConfirm && !m_text.empty()) m_onConfirm(m_text);
            dismiss();
            return true;
        }
        if (e.keyCode == 8) { // Backspace
            if (m_cursor > 0) {
                int len = ui::utf8PrevCharOffset(m_text, m_cursor);
                m_text.erase(m_cursor - len, len);
                m_cursor -= len;
            }
            return true;
        }
        if (e.keyCode == 127) { // Delete
            if (m_cursor < (int)m_text.size()) {
                int len = ui::utf8CharLen(&m_text[m_cursor]);
                m_text.erase(m_cursor, len);
            }
            return true;
        }
        if (e.keyCode == 1073741903) { // Right arrow (SDL)
            if (m_cursor < (int)m_text.size()) {
                m_cursor += ui::utf8CharLen(&m_text[m_cursor]);
            }
            return true;
        }
        if (e.keyCode == 1073741904) { // Left arrow (SDL)
            if (m_cursor > 0) {
                m_cursor -= ui::utf8PrevCharOffset(m_text, m_cursor);
            }
            return true;
        }
        return true; // modal — consume all keys
    }

    bool onTextInput(TextInputEvent& e) override {
        if (e.text[0]) {
            m_text.insert(m_cursor, e.text);
            m_cursor += static_cast<int>(std::strlen(e.text));
        }
        return true;
    }

private:
    static constexpr float kBtnH = 32.0f;
    static constexpr float kBtnW = 90.0f;

    std::string m_title;
    std::string m_text;
    int m_cursor = 0;
    std::function<void(const std::string&)> m_onConfirm;

    float m_screenW = 0;
    float m_screenH = 0;
    float m_dx = 0;
    float m_dy = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
