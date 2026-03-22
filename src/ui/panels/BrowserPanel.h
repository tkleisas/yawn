#pragma once
// BrowserPanel — Placeholder panel for the file browser / presets / config.
//
// Currently renders a dark background with a "BROWSER" label.
// Will eventually host a tabbed panel with file browser, preset browser,
// and other configuration items.

#include "ui/framework/Widget.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"

namespace yawn {
namespace ui {
namespace fw {

class BrowserPanel : public Widget {
public:
    BrowserPanel() = default;

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    void paint(UIContext& ctx) override {
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;

        r.drawRect(x, y, w, h, Color{32, 32, 36});

        // Tab bar placeholder
        float tabH = 24.0f;
        r.drawRect(x, y, w, tabH, Color{38, 38, 42});
        r.drawRect(x, y + tabH - 1, w, 1, Theme::clipSlotBorder);

        // Active tab
        float tabW = 70.0f;
        r.drawRect(x + 2, y + 2, tabW, tabH - 3, Color{50, 50, 56});

        if (f.isLoaded()) {
            float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.85f;
            f.drawText(r, "Files", x + 10, y + 5, scale, Theme::textPrimary);
            f.drawText(r, "Presets", x + tabW + 10, y + 5, scale, Theme::textDim);
            f.drawText(r, "Config", x + tabW * 2 + 10, y + 5, scale, Theme::textDim);
        }

        // "Coming soon" label
        if (f.isLoaded() && h > 60 && w > 80) {
            float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.7f;
            f.drawText(r, "BROWSER", x + w * 0.5f - 28, y + h * 0.5f - 8, scale, Theme::textDim);
        }
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
