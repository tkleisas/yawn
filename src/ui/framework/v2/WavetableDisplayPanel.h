#pragma once
// fw2::WavetableDisplayPanel — single-cycle waveform preview for the
// Wavetable Synth instrument. Migrated from v1
// fw::WavetableDisplayPanel. Shows the currently-selected table frame
// plus a position indicator marking the Position-knob value.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cstdint>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class WavetableDisplayPanel : public Widget {
public:
    WavetableDisplayPanel() { setName("WavetableDisplay"); }

    void setWaveformData(const float* data, int size) {
        m_waveData = data;
        m_waveSize = size;
    }

    void setPosition(float pos) { m_position = std::clamp(pos, 0.0f, 1.0f); }
    void setTableName(const char* name) { m_tableName = name ? name : ""; }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 80.0f});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        m_waveRect = {bounds.x + 2, bounds.y + 12, bounds.w - 4, bounds.h - 14};
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        const float lblFs = 7.0f * (48.0f / 26.0f);  // ≈ 12.92 px

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        if (tm)
            tm->drawText(r,
                m_tableName.empty() ? "WAVETABLE" : m_tableName.c_str(),
                m_bounds.x + 3, m_bounds.y + 1, lblFs,
                ::yawn::ui::Theme::textDim);

        const float px = m_waveRect.x, py = m_waveRect.y;
        const float pw = m_waveRect.w, ph = m_waveRect.h;

        if (m_waveData && m_waveSize > 0 && pw > 4 && ph > 4) {
            const float midY = py + ph * 0.5f;
            const float halfH = ph * 0.45f;

            r.drawRect(px, midY, pw, 1, Color{40, 40, 50, 100});

            const Color waveCol{100, 200, 255, 220};
            int numPts = static_cast<int>(pw);
            if (numPts < 2) numPts = 2;
            for (int i = 0; i < numPts; ++i) {
                const float t = static_cast<float>(i) / (numPts - 1);
                const float sampleIdx = t * (m_waveSize - 1);
                const int idx0 = static_cast<int>(sampleIdx);
                const int idx1 = std::min(idx0 + 1, m_waveSize - 1);
                const float frac = sampleIdx - idx0;
                float val = m_waveData[idx0] * (1.0f - frac)
                          + m_waveData[idx1] * frac;
                val = std::clamp(val, -1.0f, 1.0f);

                const float x = px + i;
                const float y = midY - val * halfH;
                r.drawRect(x, y - 0.5f, 1.0f, 1.5f, waveCol);
            }

            const float posX = px + m_position * pw;
            r.drawRect(posX - 0.5f, py + ph - 3, 2, 3,
                       Color{255, 200, 50, 200});
        } else if (tm) {
            const char* msg = "No Table";
            const float tw = tm->textWidth(msg, lblFs);
            const float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            const float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            tm->drawText(r, msg, tx, ty, lblFs, Color{80, 80, 100, 180});
        }
    }
#endif

private:
    Rect m_waveRect{};
    const float* m_waveData = nullptr;
    int m_waveSize = 0;
    float m_position = 0.0f;
    std::string m_tableName;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
