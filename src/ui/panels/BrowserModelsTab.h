#pragma once
// BrowserModelsTab — 3D Models tab for the BrowserPanel (fw2::Widget).
// A scrollable list of .glb/.gltf models found across the bundled
// examples folder and a user library folder (scanned by ModelLibrary).
// Double-click assigns the model to the selected visual clip. A live
// rendered thumbnail per row is a planned follow-up (5c); for now each
// row shows a small generic 3D glyph.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Renderer.h"
#include "ui/Theme.h"
#include "visual/ModelLibrary.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class BrowserModelsTab : public Widget {
public:
    BrowserModelsTab() { setFocusable(false); }

    // Directories to scan; `bundledCount` of them are flagged bundled.
    void setLibraryDirs(std::vector<std::string> dirs, int bundledCount) {
        m_library.setDirectories(std::move(dirs), bundledCount);
        refresh();
    }
    void refresh() { m_library.refresh(); }

    // Called when the user double-clicks a model (absolute path).
    void setOnAssign(std::function<void(const std::string&)> cb) {
        m_onAssign = std::move(cb);
    }

protected:
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }
    void onLayout(Rect bounds, UIContext&) override { m_bounds = bounds; }

    bool onMouseDown(MouseEvent& e) override {
        const auto& entries = m_library.entries();
        const float listY = m_bounds.y + kHeaderH;
        const float listH = m_bounds.h - kHeaderH;
        if (e.y < listY || e.y >= listY + listH) return false;
        int row = static_cast<int>((e.y - listY) / kRowHeight) + m_scrollOffset;
        if (row < 0 || row >= static_cast<int>(entries.size())) return false;

        auto now = std::chrono::steady_clock::now();
        bool dbl = (row == m_lastClickRow &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastClickTime).count() < 400);
        m_lastClickRow = row; m_lastClickTime = now; m_selectedIndex = row;
        if (dbl && m_onAssign) m_onAssign(entries[row].path);
        return true;
    }

    bool onScroll(ScrollEvent& e) override {
        m_scrollOffset -= static_cast<int>(e.dy * 3);
        if (m_scrollOffset < 0) m_scrollOffset = 0;
        return true;
    }

public:
    void render(UIContext& ctx) override {
        if (!isVisible() || !ctx.renderer || !ctx.textMetrics) return;
        auto& r = *ctx.renderer; auto& tm = *ctx.textMetrics;
        const auto& entries = m_library.entries();
        const float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w, h = m_bounds.h;
        const float fs = theme().metrics.fontSizeSmall;

        // Header hint.
        tm.drawText(r, "Double-click to assign to the selected visual clip",
                    x + 6, y + 5, fs * 0.85f, ::yawn::ui::Theme::textDim);

        const float listY = y + kHeaderH, listH = h - kHeaderH;
        if (listH <= 0) return;

        const int total = static_cast<int>(entries.size());
        if (total == 0) {
            const char* msg = "No models. Drop .glb files in ~/.yawn/models3d";
            float tw = tm.textWidth(msg, fs);
            tm.drawText(r, msg, x + std::max(4.0f, (w - tw) * 0.5f),
                        listY + listH * 0.4f, fs, ::yawn::ui::Theme::textDim);
            return;
        }

        int visible = static_cast<int>(listH / kRowHeight);
        int maxScroll = std::max(0, total - visible);
        if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;

        r.pushClip(x, listY, w, listH);
        const float lh = tm.lineHeight(fs);
        for (int i = 0; i < visible + 1 && (i + m_scrollOffset) < total; ++i) {
            int idx = i + m_scrollOffset;
            const auto& m = entries[idx];
            float ry = listY + i * kRowHeight;
            if (idx == m_selectedIndex) r.drawRect(x, ry, w, kRowHeight, Color{50, 60, 80, 255});
            else if (i % 2 == 1)        r.drawRect(x, ry, w, kRowHeight, Color{36, 36, 40, 255});

            // Generic 3D glyph (placeholder for the future thumbnail).
            float ic = ry + 3, is = kRowHeight - 6;
            r.drawRectOutline(x + 5, ic, is, is, Color{120, 150, 200, 255}, 1.0f);
            r.drawRect(x + 5 + is * 0.25f, ic + is * 0.25f, is * 0.6f, is * 0.6f,
                       Color{70, 90, 130, 200});

            float textY = ry + (kRowHeight - lh) * 0.5f;
            tm.drawText(r, m.name, x + 5 + is + 6, textY, fs,
                        ::yawn::ui::Theme::textPrimary);
            if (m.bundled) {
                const char* tag = "bundled";
                float tw = tm.textWidth(tag, fs * 0.85f);
                tm.drawText(r, tag, x + w - tw - 8, textY, fs * 0.85f,
                            Color{120, 140, 170, 255});
            }
        }
        if (total > visible) {
            float frac = static_cast<float>(visible) / total;
            float thumbH = std::max(8.0f, listH * frac);
            float sf = (maxScroll > 0) ? float(m_scrollOffset) / maxScroll : 0.0f;
            r.drawRect(x + w - 4, listY + sf * (listH - thumbH), 3, thumbH,
                       Color{80, 80, 90, 255});
        }
        r.popClip();
    }

private:
    static constexpr float kRowHeight = 22.0f;
    static constexpr float kHeaderH   = 22.0f;

    visual::ModelLibrary m_library;
    int m_scrollOffset = 0;
    int m_selectedIndex = -1;
    int m_lastClickRow = -1;
    std::chrono::steady_clock::time_point m_lastClickTime;
    std::function<void(const std::string&)> m_onAssign;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
