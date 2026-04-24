#pragma once
// fw2::VocoderDisplayPanel — modulator sample + band-count visualizer
// for the Vocoder instrument. Migrated from v1
// fw::VocoderDisplayPanel.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cstdint>

namespace yawn {
namespace ui {
namespace fw2 {

class VocoderDisplayPanel : public Widget {
public:
    VocoderDisplayPanel() { setName("VocoderDisplay"); }

    void setModulatorData(const float* data, int frames) {
        m_modData = data;
        m_modFrames = frames;
    }

    void setPlayhead(float pos)    { m_playhead = std::clamp(pos, 0.0f, 1.0f); }
    void setPlaying(bool playing)  { m_playing = playing; }
    void setBandCount(int bands)   { m_bandCount = std::clamp(bands, 4, 32); }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 80.0f});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float bandH = 10.0f;
        m_waveRect = {bounds.x + 2, bounds.y + 12,
                      bounds.w - 4, bounds.h - 14 - bandH - 2};
        m_bandRect = {bounds.x + 2, bounds.y + bounds.h - bandH - 2,
                      bounds.w - 4, bandH};
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
            tm->drawText(r, "MODULATOR", m_bounds.x + 3, m_bounds.y + 1,
                          lblFs, ::yawn::ui::Theme::textDim);

        const float px = m_waveRect.x, py = m_waveRect.y;
        const float pw = m_waveRect.w, ph = m_waveRect.h;

        if (m_modData && m_modFrames > 0 && pw > 4 && ph > 4) {
            const float midY = py + ph * 0.5f;
            const float halfH = ph * 0.5f;

            int numBars = static_cast<int>(pw);
            if (numBars < 1) numBars = 1;
            const Color waveCol{200, 120, 255, 200};
            for (int i = 0; i < numBars; ++i) {
                const int startFrame = (i * m_modFrames) / numBars;
                const int endFrame = ((i + 1) * m_modFrames) / numBars;
                float minVal = 0.0f, maxVal = 0.0f;
                for (int s = startFrame; s < endFrame; ++s) {
                    const float v = m_modData[s];
                    if (v < minVal) minVal = v;
                    if (v > maxVal) maxVal = v;
                }
                float top = midY - maxVal * halfH;
                float bot = midY - minVal * halfH;
                float barH = bot - top;
                if (barH < 0.5f) { top = midY - 0.25f; barH = 0.5f; }
                r.drawRect(px + i, top, 1, barH, waveCol);
            }

            r.drawRect(px, midY, pw, 1, Color{50, 50, 60, 80});

            if (m_playing && m_playhead > 0.0f) {
                const float phX = px + m_playhead * pw;
                if (phX >= px && phX <= px + pw)
                    r.drawRect(phX, py, 1.5f, ph,
                               Color{255, 255, 255, 200});
            }
        } else if (tm) {
            const char* msg = "Drop Modulator";
            const float tw = tm->textWidth(msg, lblFs);
            const float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            const float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            tm->drawText(r, msg, tx, ty, lblFs, Color{80, 80, 100, 180});
        }

        if (m_bandRect.w > 4 && m_bandRect.h > 2) {
            const float barW = m_bandRect.w / m_bandCount;
            for (int b = 0; b < m_bandCount; ++b) {
                const float bx = m_bandRect.x + b * barW;
                const float t = static_cast<float>(b) / std::max(1, m_bandCount - 1);
                const uint8_t cr = static_cast<uint8_t>(80 + t * 150);
                const uint8_t cg = static_cast<uint8_t>(180 - t * 120);
                r.drawRect(bx + 0.5f, m_bandRect.y, barW - 1, m_bandRect.h,
                           Color{cr, cg, 220, 160});
            }
        }
    }
#endif

private:
    Rect m_waveRect{}, m_bandRect{};
    const float* m_modData = nullptr;
    int m_modFrames = 0;
    float m_playhead = 0.0f;
    bool m_playing = false;
    int m_bandCount = 16;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
