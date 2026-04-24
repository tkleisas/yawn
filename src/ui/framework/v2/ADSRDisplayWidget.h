#pragma once
// fw2::ADSRDisplayWidget — ADSR envelope curve preview.
// Migrated from v1 fw::ADSRDisplayWidget. Attack is linear, decay and
// release use an exponential shape; sustain is held for a fixed
// fraction of the display width.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class ADSRDisplayWidget : public Widget {
public:
    ADSRDisplayWidget() { setName("ADSRDisplay"); }

    void setADSR(float a, float d, float s, float rel) {
        m_attack = a; m_decay = d; m_sustain = s; m_release = rel;
    }
    void setColor(Color c) { m_color = c; }
    void setLabel(const std::string& l) { m_label = l; }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({120.0f, 48.0f});
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        auto* tm = ctx.textMetrics;
        const float lblFs = 7.0f * (48.0f / 26.0f);  // ≈ 12.92 px
        if (tm && !m_label.empty())
            tm->drawText(r, m_label, m_bounds.x + 3, m_bounds.y + 1,
                          lblFs, ::yawn::ui::Theme::textDim);

        const float px = m_bounds.x + 4;
        const float py = m_bounds.y + 11;
        const float pw = m_bounds.w - 8;
        const float ph = m_bounds.h - 14;
        if (pw < 10 || ph < 6) return;

        float total = m_attack + m_decay + 0.25f + m_release;
        if (total < 0.001f) total = 1.0f;
        const float sc = pw / total;

        const float aEnd = px + m_attack * sc;
        const float dEnd = aEnd + m_decay * sc;
        const float sEnd = dEnd + 0.25f * sc;
        const float rEnd = std::min(sEnd + m_release * sc, px + pw);
        const float sY   = py + ph * (1.0f - m_sustain);

        drawSeg(r, px,   py + ph, aEnd, py,      false);
        drawSeg(r, aEnd, py,      dEnd, sY,      true);
        drawSeg(r, dEnd, sY,      sEnd, sY,      false);
        drawSeg(r, sEnd, sY,      rEnd, py + ph, true);

        const Color dimCol{m_color.r, m_color.g, m_color.b, 50};
        r.drawRect(aEnd, py, 1, ph, dimCol);
        r.drawRect(dEnd, py, 1, ph, dimCol);
        r.drawRect(sEnd, py, 1, ph, dimCol);
    }
#endif

private:
    float m_attack = 0.01f, m_decay = 0.1f, m_sustain = 0.7f, m_release = 0.3f;
    Color m_color{80, 220, 100, 255};
    std::string m_label;

#ifndef YAWN_TEST_BUILD
    void drawSeg(::yawn::ui::Renderer2D& r, float x1, float y1,
                 float x2, float y2, bool exponential) {
        const float dx = x2 - x1;
        if (std::abs(dx) < 1) return;
        const int steps = std::max(4, static_cast<int>(std::abs(dx) / 2.0f));
        float prevX = x1, prevY = y1;
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            const float cpx = x1 + dx * t;
            const float dy = y2 - y1;
            float cpy;
            if (exponential && std::abs(dy) > 1)
                cpy = y1 + dy * (1.0f - std::exp(-t * 3.5f)) / (1.0f - std::exp(-3.5f));
            else
                cpy = y1 + dy * t;
            r.drawLine(prevX, prevY, cpx, cpy, m_color, 1.5f);
            prevX = cpx;
            prevY = cpy;
        }
    }
#endif
};

} // namespace fw2
} // namespace ui
} // namespace yawn
