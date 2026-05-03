#pragma once
// fw2::GranularDisplayPanel — sample waveform + grain position overlay
// for the Granular Synth. Migrated from v1 fw::GranularDisplayPanel.
// Highlights the grain spawn region (position + scan) and draws a
// playhead when the instrument is playing.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/DragManager.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yawn {
namespace ui {
namespace fw2 {

class GranularDisplayPanel : public Widget {
public:
    GranularDisplayPanel() { setName("GranularDisplay"); }

    void setSampleData(const float* data, int frames, int channels) {
        m_sampleData = data;
        m_sampleFrames = frames;
        m_sampleChannels = channels;
    }

    void setPosition(float pos)     { m_position = std::clamp(pos, 0.0f, 1.0f); }
    void setScanPosition(float scan) { m_scanPos = scan - std::floor(scan); }
    void setPlaying(bool playing)   { m_playing = playing; }
    void setGrainSize(float sizeNorm) {
        m_grainSizeNorm = std::clamp(sizeNorm, 0.0f, 1.0f);
    }

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
            tm->drawText(r, "GRAINS", m_bounds.x + 3, m_bounds.y + 1,
                          lblFs, ::yawn::ui::Theme::textDim);

        const float px = m_waveRect.x, py = m_waveRect.y;
        const float pw = m_waveRect.w, ph = m_waveRect.h;

        if (m_sampleData && m_sampleFrames > 0 && pw > 4 && ph > 4) {
            const float midY = py + ph * 0.5f;
            const float halfH = ph * 0.5f;

            int numBars = static_cast<int>(pw);
            if (numBars < 1) numBars = 1;
            const Color waveCol{0, 180, 230, 180};
            for (int i = 0; i < numBars; ++i) {
                const int startFrame = (i * m_sampleFrames) / numBars;
                const int endFrame = ((i + 1) * m_sampleFrames) / numBars;
                float minVal = 0.0f, maxVal = 0.0f;
                for (int s = startFrame; s < endFrame; ++s) {
                    const float v = m_sampleData[s * m_sampleChannels];
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

            if (m_playing) {
                float combinedPos = m_position + m_scanPos;
                combinedPos -= std::floor(combinedPos);
                const float cpX = px + combinedPos * pw;
                const float grainW = m_grainSizeNorm * pw;
                float regionStart = cpX - grainW * 0.5f;
                float regionEnd = cpX + grainW * 0.5f;
                if (regionStart < px) regionStart = px;
                if (regionEnd > px + pw) regionEnd = px + pw;
                r.drawRect(regionStart, py, regionEnd - regionStart, ph,
                           Color{255, 150, 50, 40});
                r.drawRect(cpX - 0.5f, py, 2, ph, Color{255, 200, 50, 200});
            }
        } else if (tm) {
            const char* msg = "Drop Sample";
            const float tw = tm->textWidth(msg, lblFs);
            const float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            const float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            tm->drawText(r, msg, tx, ty, lblFs, Color{80, 80, 100, 180});
        }
        // Drop-target highlight (audio-clip drag).
        DragManager::renderDropHighlight(m_bounds, ctx);
    }
#endif

private:
    Rect m_waveRect{};
    const float* m_sampleData = nullptr;
    int m_sampleFrames = 0;
    int m_sampleChannels = 1;
    float m_position = 0.5f;
    float m_scanPos = 0.0f;
    bool m_playing = false;
    float m_grainSizeNorm = 0.1f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
