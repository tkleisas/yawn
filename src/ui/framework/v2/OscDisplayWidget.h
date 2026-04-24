#pragma once
// fw2::OscDisplayWidget — oscillator waveform preview.
// Migrated from v1 fw::OscDisplayWidget. Draws a single cycle of the
// selected waveform at an amplitude that tracks the oscillator level.

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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace ui {
namespace fw2 {

class OscDisplayWidget : public Widget {
public:
    OscDisplayWidget() { setName("OscDisplay"); }

    // 0=Sine, 1=Saw, 2=Square, 3=Triangle, 4=Noise
    void setWaveform(int type) { m_waveform = std::clamp(type, 0, 4); }
    void setLevel(float l)     { m_level = std::clamp(l, 0.0f, 1.0f); }
    void setColor(Color c)     { m_color = c; }
    void setLabel(const std::string& l) { m_label = l; }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({60.0f, 48.0f});
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

        const float wx = m_bounds.x + 4;
        const float wy = m_bounds.y + 11;
        const float ww = m_bounds.w - 8;
        const float wh = m_bounds.h - 14;
        if (ww < 10 || wh < 6) return;

        const float midY = wy + wh / 2;
        r.drawRect(wx, midY, ww, 1, Color{35, 35, 45, 255});

        const Color col = m_level > 0.01f
            ? m_color
            : Color{m_color.r, m_color.g, m_color.b, 50};
        const float amp = wh / 2 * std::min(1.0f, m_level + 0.25f);

        const int steps = static_cast<int>(ww);
        float prevX = wx, prevY = midY;
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            float val = 0;
            switch (m_waveform) {
                case 0: val = std::sin(t * 2 * static_cast<float>(M_PI)); break;
                case 1: val = 2.0f * t - 1.0f; break;
                case 2: val = t < 0.5f ? 1.0f : -1.0f; break;
                case 3: val = t < 0.5f ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t); break;
                case 4: val = static_cast<float>((i * 1103515245 + 12345) & 0x7fffffff)
                              / static_cast<float>(0x7fffffff) * 2.0f - 1.0f; break;
            }
            const float curX = wx + static_cast<float>(i);
            const float curY = midY - val * amp;
            if (i > 0)
                r.drawLine(prevX, prevY, curX, curY, col, 1.5f);
            prevX = curX;
            prevY = curY;
        }

        static const char* waveNames[] = {"Sin", "Saw", "Sqr", "Tri", "Nse"};
        const char* wn = (m_waveform >= 0 && m_waveform <= 4) ? waveNames[m_waveform] : "?";
        if (tm) {
            const float tw = tm->textWidth(wn, lblFs);
            tm->drawText(r, wn, m_bounds.x + m_bounds.w - tw - 3, m_bounds.y + 1,
                          lblFs, col);
        }
    }
#endif

private:
    int m_waveform = 0;
    float m_level = 1.0f;
    Color m_color{0, 200, 255, 255};
    std::string m_label;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
