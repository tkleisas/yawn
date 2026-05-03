#pragma once
// fw2::SamplerDisplayPanel — composite visualization for the Sampler
// instrument. Migrated from v1 fw::SamplerDisplayPanel. Two regions:
//   Top:    sample waveform (envelope min/max bars) with loop region +
//           live playhead, or a "Drop Sample" placeholder.
//   Bottom: small AMP ADSR envelope.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/ADSRDisplayWidget.h"
#include "ui/framework/v2/DragManager.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cstdint>

namespace yawn {
namespace ui {
namespace fw2 {

class SamplerDisplayPanel : public Widget {
public:
    SamplerDisplayPanel() {
        setName("SamplerDisplay");
        m_adsr.setLabel("AMP");
        m_adsr.setColor(Color{80, 220, 100, 255});
    }

    void setADSR(float a, float d, float s, float rel) {
        m_adsr.setADSR(a, d, s, rel);
    }

    void setSampleData(const float* data, int frames, int channels) {
        m_sampleData = data;
        m_sampleFrames = frames;
        m_sampleChannels = channels;
    }

    void setLoopPoints(float start, float end) {
        m_loopStart = start;
        m_loopEnd = end;
    }

    void setPlayhead(float pos, bool playing) {
        m_playhead = pos;
        m_playing = playing;
    }

    void setReverse(bool rev) { m_reverse = rev; }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 96.0f});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float gap = 3.0f;
        const float adsrH = std::min(40.0f, bounds.h * 0.35f);
        const float waveH = bounds.h - adsrH - gap;
        m_waveRect = {bounds.x, bounds.y, bounds.w, waveH};
        const Rect adsrRect{bounds.x, bounds.y + waveH + gap, bounds.w, adsrH};
        m_adsr.measure(Constraints::tight(adsrRect.w, adsrRect.h), ctx);
        m_adsr.layout(adsrRect, ctx);
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        r.drawRect(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                          Color{50, 50, 60, 255});

        const float lblFs = 7.0f * (48.0f / 26.0f);  // ≈ 12.92 px
        const char* label = m_reverse ? "SAMPLE \xe2\x97\x80" : "SAMPLE";
        if (tm)
            tm->drawText(r, label, m_waveRect.x + 3, m_waveRect.y + 1,
                          lblFs, ::yawn::ui::Theme::textDim);

        if (m_sampleData && m_sampleFrames > 0) {
            const float px = m_waveRect.x + 2;
            const float py = m_waveRect.y + 10;
            const float pw = m_waveRect.w - 4;
            const float ph = m_waveRect.h - 12;
            if (pw > 10 && ph > 6) {
                const float lsX = px + m_loopStart * pw;
                const float leX = px + m_loopEnd * pw;
                const bool hasLoop =
                    (m_loopStart > 0.001f || m_loopEnd < 0.999f);
                if (hasLoop) {
                    if (lsX > px)
                        r.drawRect(px, py, lsX - px, ph, Color{10, 10, 14, 150});
                    if (leX < px + pw)
                        r.drawRect(leX, py, px + pw - leX, ph, Color{10, 10, 14, 150});
                    r.drawRect(lsX, py, 1, ph, Color{255, 200, 50, 160});
                    r.drawRect(leX, py, 1, ph, Color{255, 200, 50, 160});
                }

                const float midY = py + ph * 0.5f;
                const float halfH = ph * 0.5f;
                int numBars = static_cast<int>(pw);
                if (numBars < 1) numBars = 1;
                const Color waveCol{0, 180, 230, 200};
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
                r.drawRect(px, midY, pw, 1, Color{50, 50, 60, 100});

                if (m_playing && m_playhead > 0.0f) {
                    const float phX = px + m_playhead * pw;
                    if (phX >= px && phX <= px + pw)
                        r.drawRect(phX, py, 1.5f, ph,
                                   Color{255, 255, 255, 200});
                }
            }
        } else if (tm) {
            const char* msg = "Drop Sample";
            const float tw = tm->textWidth(msg, lblFs);
            const float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            const float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            tm->drawText(r, msg, tx, ty, lblFs, Color{80, 80, 100, 180});
        }

        m_adsr.render(ctx);

        // "You can drop a sample here" highlight when an audio-clip
        // drag is active and the cursor is over this panel.
        DragManager::renderDropHighlight(m_bounds, ctx);
    }
#endif

private:
    ADSRDisplayWidget m_adsr;
    Rect m_waveRect{};
    const float* m_sampleData = nullptr;
    int m_sampleFrames = 0;
    int m_sampleChannels = 1;
    float m_loopStart = 0.0f, m_loopEnd = 1.0f;
    float m_playhead = 0.0f;
    bool m_playing = false;
    bool m_reverse = false;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
