// ExportDialog.cpp — rendering and event implementations.
// Split from ExportDialog.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "ExportDialog.h"
#include "../Renderer.h"
#include "../Font.h"
#include <cstdio>
#include <algorithm>

namespace yawn {
namespace ui {
namespace fw {

void ExportDialog::paint(UIContext& ctx) {
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;

    // Dimmed backdrop
    r.drawRect(0, 0, m_screenW, m_screenH, Color{0, 0, 0, 140});

    float dw = m_preferredW;
    float dh = m_preferredH;

    // Dialog background
    r.drawRect(m_dx, m_dy, dw, dh, Color{45, 45, 52, 255});
    r.drawRectOutline(m_dx, m_dy, dw, dh, Color{75, 75, 85, 255});

    float titleScale = 14.0f / Theme::kFontSize;
    float textScale = 11.0f / Theme::kFontSize;
    float textH = f.lineHeight(textScale);
    float tabScale = 12.0f / Theme::kFontSize;

    // Title bar
    r.drawRect(m_dx, m_dy, dw, kTitleBarHeight, Color{55, 55, 62, 255});
    f.drawText(r, m_title.c_str(), m_dx + 12,
               m_dy + (kTitleBarHeight - f.lineHeight(titleScale)) * 0.5f,
               titleScale, Theme::textPrimary);

    // Close button
    auto cb = closeButtonRect();
    r.drawRect(cb.x, cb.y, cb.w, cb.h, Color{160, 50, 50, 255});
    float xScale = 10.0f / Theme::kFontSize;
    f.drawText(r, "X", cb.x + (cb.w - f.textWidth("X", xScale)) * 0.5f,
               cb.y + (cb.h - f.lineHeight(xScale)) * 0.5f,
               xScale, Theme::textPrimary);

    // Content area
    float contentX = m_dx + 16;
    float contentW = dw - 32;
    float rowH = 30.0f;
    float dropW = (std::min)(contentW * 0.5f, 180.0f);
    float dropX = contentX + contentW - dropW;
    float y = m_dy + kTitleBarHeight + 12;

    if (!m_rendering) {
        // ─── Format ─────────────────────────────────────────────────────
        drawLabel(r, f, "Format", contentX, y, textScale);
        static const char* formatItems[] = {"WAV", "FLAC", "OGG"};
        drawDropdown(r, f, dropX, y, dropW, rowH, textH, textScale,
                     formatItems, 3, static_cast<int>(m_config.format),
                     &m_formatOpen);
        y += rowH;

        // ─── Bit Depth ─────────────────────────────────────────────────
        drawLabel(r, f, "Bit Depth", contentX, y, textScale);
        static const char* bitDepthItems[] = {"16-bit", "24-bit", "32-bit float"};
        int bitIdx = static_cast<int>(m_config.bitDepth);
        // OGG ignores bit depth (always lossy)
        bool bitDisabled = (m_config.format == util::ExportFormat::OGG);
        if (bitDisabled) {
            Color dimBg{35, 35, 38, 255};
            r.drawRect(dropX, y, dropW, 24.0f, dimBg);
            r.drawRectOutline(dropX, y, dropW, 24.0f, Color{55, 55, 60, 255});
            f.drawText(r, "N/A (lossy)", dropX + 8,
                       y + (24.0f - textH) * 0.5f, textScale, Theme::textSecondary);
        } else {
            drawDropdown(r, f, dropX, y, dropW, rowH, textH, textScale,
                         bitDepthItems, 3, bitIdx, &m_bitDepthOpen);
        }
        y += rowH;

        // ─── Sample Rate ────────────────────────────────────────────────
        drawLabel(r, f, "Sample Rate", contentX, y, textScale);
        static const char* srItems[] = {"44100 Hz", "48000 Hz", "96000 Hz"};
        int srIdx = 0;
        if (m_config.sampleRate == 48000) srIdx = 1;
        else if (m_config.sampleRate == 96000) srIdx = 2;
        drawDropdown(r, f, dropX, y, dropW, rowH, textH, textScale,
                     srItems, 3, srIdx, &m_sampleRateOpen);
        y += rowH;

        // ─── Scope ──────────────────────────────────────────────────────
        drawLabel(r, f, "Scope", contentX, y, textScale);
        static const char* scopeItems[] = {"Full Arrangement", "Loop Region"};
        int scopeCount = (m_config.loopEnabled &&
                          m_config.loopEndBeats > m_config.loopStartBeats) ? 2 : 1;
        drawDropdown(r, f, dropX, y, dropW, rowH, textH, textScale,
                     scopeItems, scopeCount, m_config.scope, &m_scopeOpen);
        y += rowH;

        // ─── Summary ────────────────────────────────────────────────────
        y += 8;
        double startBeat = 0, endBeat = m_config.arrangementLengthBeats;
        if (m_config.scope == 1) {
            startBeat = m_config.loopStartBeats;
            endBeat = m_config.loopEndBeats;
        }
        double beats = endBeat - startBeat;
        char summary[128];
        std::snprintf(summary, sizeof(summary),
                      "%.0f beats (%.1f bars)", beats, beats / 4.0);
        drawLabel(r, f, summary, contentX, y, textScale);

        // Overlay pass for open dropdowns (drawn on top)
        float oy = m_dy + kTitleBarHeight + 12;
        drawDropdown(r, f, dropX, oy, dropW, rowH, textH, textScale,
                     formatItems, 3, static_cast<int>(m_config.format),
                     &m_formatOpen, true);
        oy += rowH;
        if (!bitDisabled)
            drawDropdown(r, f, dropX, oy, dropW, rowH, textH, textScale,
                         bitDepthItems, 3, bitIdx, &m_bitDepthOpen, true);
        oy += rowH;
        drawDropdown(r, f, dropX, oy, dropW, rowH, textH, textScale,
                     srItems, 3, srIdx, &m_sampleRateOpen, true);
        oy += rowH;
        drawDropdown(r, f, dropX, oy, dropW, rowH, textH, textScale,
                     scopeItems, scopeCount, m_config.scope, &m_scopeOpen, true);

    } else {
        // ─── Rendering progress ─────────────────────────────────────────
        float pct = m_progress.fraction.load() * 100.0f;
        char status[64];
        std::snprintf(status, sizeof(status), "Rendering... %.0f%%", pct);
        float sw = f.textWidth(status, textScale);
        f.drawText(r, status,
                   m_dx + (dw - sw) * 0.5f, y + 20,
                   textScale, Theme::textPrimary);

        // Progress bar
        float barX = contentX;
        float barY = y + 50;
        float barW = contentW;
        float barH = 16.0f;
        r.drawRect(barX, barY, barW, barH, Color{30, 30, 34, 255});
        r.drawRectOutline(barX, barY, barW, barH, Color{70, 70, 80, 255});
        float fillW = barW * m_progress.fraction.load();
        if (fillW > 0)
            r.drawRect(barX + 1, barY + 1, fillW - 2, barH - 2,
                       Color{80, 160, 80, 255});

        // Cancel hint
        const char* hint = "Press Escape or Cancel to abort";
        float hw = f.textWidth(hint, textScale);
        f.drawText(r, hint,
                   m_dx + (dw - hw) * 0.5f, barY + barH + 12,
                   textScale, Theme::textSecondary);
    }

    // Footer
    float footerY = m_dy + dh - kFooterHeight;
    r.drawRect(m_dx, footerY, dw, kFooterHeight, Color{50, 50, 55, 255});

    auto okR = okButtonRect();
    Color okBg = m_rendering ? Color{50, 50, 55, 255} : Color{50, 130, 50, 255};
    r.drawRect(okR.x, okR.y, okR.w, okR.h, okBg);
    f.drawText(r, m_okLabel.c_str(),
               okR.x + (okR.w - f.textWidth(m_okLabel.c_str(), tabScale)) * 0.5f,
               okR.y + (okR.h - f.lineHeight(tabScale)) * 0.5f,
               tabScale, Theme::textPrimary);

    auto cancelR = cancelButtonRect();
    r.drawRect(cancelR.x, cancelR.y, cancelR.w, cancelR.h, Color{130, 50, 50, 255});
    const char* cancelTxt = m_rendering ? "Cancel Render" : "Cancel";
    f.drawText(r, cancelTxt,
               cancelR.x + (cancelR.w - f.textWidth(cancelTxt, tabScale)) * 0.5f,
               cancelR.y + (cancelR.h - f.lineHeight(tabScale)) * 0.5f,
               tabScale, Theme::textPrimary);
}

bool ExportDialog::onMouseDown(MouseEvent& e) {
    float mx = e.x, my = e.y;

    // Click outside → cancel (if not rendering)
    if (mx < m_dx || mx > m_dx + m_preferredW ||
        my < m_dy || my > m_dy + m_preferredH) {
        if (!m_rendering) {
            close(DialogResult::Cancel);
        }
        e.consume();
        return true;
    }

    // Close button
    auto cb = closeButtonRect();
    if (mx >= cb.x && mx <= cb.x + cb.w && my >= cb.y && my <= cb.y + cb.h) {
        close(DialogResult::Cancel);
        e.consume();
        return true;
    }

    // OK button (disabled during render)
    if (!m_rendering) {
        auto okR = okButtonRect();
        if (mx >= okR.x && mx <= okR.x + okR.w && my >= okR.y && my <= okR.y + okR.h) {
            close(DialogResult::OK);
            e.consume();
            return true;
        }
    }

    // Cancel button
    auto cancelR = cancelButtonRect();
    if (mx >= cancelR.x && mx <= cancelR.x + cancelR.w &&
        my >= cancelR.y && my <= cancelR.y + cancelR.h) {
        close(DialogResult::Cancel);
        e.consume();
        return true;
    }

    // Dropdown clicks (only when not rendering)
    if (!m_rendering) {
        float rowH = 30.0f;
        float contentW = m_preferredW - 32;
        float dropW = (std::min)(contentW * 0.5f, 180.0f);
        float dropX = m_dx + 16 + contentW - dropW;
        float y = m_dy + kTitleBarHeight + 12;

        // Format dropdown
        if (handleDropdownClick(mx, my, dropX, y, dropW, 24.0f, 3,
                                &m_formatOpen, [this](int i) {
            m_config.format = static_cast<util::ExportFormat>(i);
            // Reset bit depth for OGG
            if (m_config.format == util::ExportFormat::OGG)
                m_config.bitDepth = util::BitDepth::Float32;
        })) { e.consume(); return true; }
        y += rowH;

        // Bit Depth dropdown
        bool bitDisabled = (m_config.format == util::ExportFormat::OGG);
        if (!bitDisabled) {
            if (handleDropdownClick(mx, my, dropX, y, dropW, 24.0f, 3,
                                    &m_bitDepthOpen, [this](int i) {
                m_config.bitDepth = static_cast<util::BitDepth>(i);
            })) { e.consume(); return true; }
        }
        y += rowH;

        // Sample Rate dropdown
        if (handleDropdownClick(mx, my, dropX, y, dropW, 24.0f, 3,
                                &m_sampleRateOpen, [this](int i) {
            static const int rates[] = {44100, 48000, 96000};
            m_config.sampleRate = rates[i];
        })) { e.consume(); return true; }
        y += rowH;

        // Scope dropdown
        int scopeCount = (m_config.loopEnabled &&
                          m_config.loopEndBeats > m_config.loopStartBeats) ? 2 : 1;
        if (handleDropdownClick(mx, my, dropX, y, dropW, 24.0f, scopeCount,
                                &m_scopeOpen, [this](int i) {
            m_config.scope = i;
        })) { e.consume(); return true; }

        // Close any open dropdown when clicking elsewhere
        m_formatOpen = false;
        m_bitDepthOpen = false;
        m_sampleRateOpen = false;
        m_scopeOpen = false;
    }

    e.consume();
    return true;
}

// ─── Drawing Helpers ────────────────────────────────────────────────────

void ExportDialog::drawLabel(Renderer2D& r, Font& f, const char* text,
                             float x, float y, float scale) {
    float textH = f.lineHeight(scale);
    f.drawText(r, text, x, y + (24.0f - textH) * 0.5f, scale, Theme::textSecondary);
}

void ExportDialog::drawDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                                float rh, float th, float ts,
                                const char* items[], int count, int selected,
                                bool* isOpen, bool overlayPass) {
    if (!overlayPass) {
        Color bg = *isOpen ? Color{55, 55, 60, 255} : Color{42, 42, 46, 255};
        Color border = *isOpen ? Color{100, 160, 220, 255} : Color{70, 70, 75, 255};
        r.drawRect(x, y, w, 24.0f, bg);
        r.drawRectOutline(x, y, w, 24.0f, border);

        float arrowZone = 14.0f;
        if (selected >= 0 && selected < count) {
            r.pushClip(x + 1, y, w - 6 - arrowZone + 5, 24.0f);
            f.drawText(r, items[selected], x + 8,
                       y + (24.0f - th) * 0.5f, ts, Theme::textPrimary);
            r.popClip();
        }

        // Triangle arrow
        float triSize = 4.0f;
        float triCx = x + w - arrowZone * 0.5f;
        float triCy = y + 12.0f;
        if (*isOpen) {
            r.drawTriangle(triCx, triCy - triSize * 0.5f,
                           triCx - triSize, triCy + triSize * 0.5f,
                           triCx + triSize, triCy + triSize * 0.5f,
                           Theme::textSecondary);
        } else {
            r.drawTriangle(triCx - triSize, triCy - triSize * 0.5f,
                           triCx + triSize, triCy - triSize * 0.5f,
                           triCx, triCy + triSize * 0.5f,
                           Theme::textSecondary);
        }
        return;
    }

    // Overlay pass: draw popup
    if (*isOpen) {
        float popupY = y + 24.0f;
        float popupH = count * 22.0f;
        r.drawRect(x, popupY, w, popupH, Color{30, 30, 34, 255});
        r.drawRectOutline(x, popupY, w, popupH, Color{90, 140, 200, 255});
        for (int i = 0; i < count; ++i) {
            float iy = popupY + i * 22.0f;
            Color itemBg, textCol;
            if (m_popupHover == i) {
                itemBg = Color{200, 200, 210, 255};
                textCol = Color{15, 15, 20, 255};
            } else if (i == selected) {
                itemBg = Color{70, 130, 200, 255};
                textCol = Color{255, 255, 255, 255};
            } else {
                itemBg = Color{30, 30, 34, 255};
                textCol = Theme::textPrimary;
            }
            r.drawRect(x + 1, iy, w - 2, 22.0f, itemBg);
            f.drawText(r, items[i], x + 8, iy + (22.0f - th) * 0.5f, ts, textCol);
        }
    }
}

bool ExportDialog::handleDropdownClick(float mx, float my, float x, float y,
                                       float w, float h, int count,
                                       bool* isOpen, std::function<void(int)> onSelect) {
    if (mx >= x && mx <= x + w && my >= y && my < y + h) {
        *isOpen = !*isOpen;
        m_popupHover = -1;
        return true;
    }
    if (*isOpen) {
        float popupY = y + h;
        for (int i = 0; i < count; ++i) {
            float iy = popupY + i * 22.0f;
            if (mx >= x && mx <= x + w && my >= iy && my < iy + 22.0f) {
                onSelect(i);
                *isOpen = false;
                return true;
            }
        }
        *isOpen = false;
        return true;
    }
    return false;
}

} // namespace fw
} // namespace ui
} // namespace yawn
