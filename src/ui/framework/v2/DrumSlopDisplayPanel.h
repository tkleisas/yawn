#pragma once
// fw2::DrumSlopDisplayPanel — slice loop waveform + 4×4 pad grid for
// DrumSlop. Migrated from v1 fw::DrumSlopDisplayPanel. Clicks on pads
// fire the setOnPadClick callback; waveform area just visualises slice
// boundaries + the selected-pad highlight.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/DragManager.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class DrumSlopDisplayPanel : public Widget {
public:
    DrumSlopDisplayPanel() { setName("DrumSlopDisplay"); }

    void setLoopData(const float* data, int frames, int channels) {
        m_loopData = data;
        m_loopFrames = frames;
        m_loopChannels = channels;
    }

    void setSliceCount(int count) { m_sliceCount = std::clamp(count, 1, 16); }
    void setSliceBoundaries(const std::vector<int64_t>& bounds) { m_sliceBounds = bounds; }
    void setBaseNote(int note)    { m_baseNote = std::clamp(note, 0, 127); }
    void setSelectedPad(int idx)  { m_selectedPad = std::clamp(idx, 0, 15); }
    void setPadPlaying(int idx, bool playing) {
        if (idx >= 0 && idx < 16) m_padPlaying[idx] = playing;
    }

    void setOnPadClick(std::function<void(int)> cb) {
        m_onPadClick = std::move(cb);
    }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 160.0f});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float gap = 2.0f;
        m_waveH = std::min(40.0f, bounds.h * 0.35f);
        m_waveRect = {bounds.x, bounds.y, bounds.w, m_waveH};
        m_gridRect = {bounds.x, bounds.y + m_waveH + gap,
                      bounds.w, bounds.h - m_waveH - gap};
    }

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        const float mx = e.x, my = e.y;
        if (mx >= m_gridRect.x && mx < m_gridRect.x + m_gridRect.w &&
            my >= m_gridRect.y && my < m_gridRect.y + m_gridRect.h) {
            constexpr int cols = 4, rows = 4;
            const float cellW = m_gridRect.w / cols;
            const float cellH = m_gridRect.h / rows;
            const int col = static_cast<int>((mx - m_gridRect.x) / cellW);
            const int row = static_cast<int>((my - m_gridRect.y) / cellH);
            const int padIdx = row * cols + col;
            if (padIdx >= 0 && padIdx < m_sliceCount && m_onPadClick)
                m_onPadClick(padIdx);
            return true;
        }
        return false;
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        // ─── Waveform area ───
        r.drawRect(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                          Color{50, 50, 60, 255});

        if (m_loopData && m_loopFrames > 0) {
            const float px = m_waveRect.x + 2;
            const float py = m_waveRect.y + 2;
            const float pw = m_waveRect.w - 4;
            const float ph = m_waveRect.h - 4;
            if (pw > 4 && ph > 4) {
                const float midY = py + ph * 0.5f;
                const float halfH = ph * 0.5f;
                int numBars = static_cast<int>(pw);
                if (numBars < 1) numBars = 1;
                const Color waveCol{0, 180, 230, 180};
                for (int i = 0; i < numBars; ++i) {
                    const int startFrame = (i * m_loopFrames) / numBars;
                    const int endFrame = ((i + 1) * m_loopFrames) / numBars;
                    float minVal = 0.0f, maxVal = 0.0f;
                    for (int s = startFrame; s < endFrame; ++s) {
                        const float v = m_loopData[s * m_loopChannels];
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

                if (!m_sliceBounds.empty()) {
                    const Color sliceCol{255, 200, 50, 140};
                    for (int i = 1; i < (int)m_sliceBounds.size() - 1; ++i) {
                        const float xPos = px + (float)m_sliceBounds[i] / m_loopFrames * pw;
                        r.drawRect(xPos, py, 1, ph, sliceCol);
                    }
                }

                if (m_selectedPad >= 0 && m_selectedPad < (int)m_sliceBounds.size() - 1) {
                    const float selStart = px + (float)m_sliceBounds[m_selectedPad] / m_loopFrames * pw;
                    const float selEnd = px + (float)m_sliceBounds[m_selectedPad + 1] / m_loopFrames * pw;
                    r.drawRect(selStart, py, selEnd - selStart, ph,
                               Color{255, 200, 50, 30});
                }
            }
        } else if (tm) {
            const float lblFs = 7.0f * (48.0f / 26.0f);
            const char* msg = "Drop Loop";
            const float tw = tm->textWidth(msg, lblFs);
            const float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            const float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            tm->drawText(r, msg, tx, ty, lblFs, Color{80, 80, 100, 180});
        }

        // ─── Pad grid ───
        constexpr int cols = 4, rows = 4;
        const float cellW = m_gridRect.w / cols;
        const float cellH = m_gridRect.h / rows;
        const float lblFs    = 6.5f * (48.0f / 26.0f);
        const float padNumFs = 5.5f * (48.0f / 26.0f);

        static const char* noteNames[] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        };

        for (int i = 0; i < 16; ++i) {
            const int col = i % cols;
            const int row = i / cols;
            const float cx = m_gridRect.x + col * cellW;
            const float cy = m_gridRect.y + row * cellH;

            const bool active = i < m_sliceCount;
            const bool selected = (i == m_selectedPad);
            const bool playing = m_padPlaying[i];

            Color bg{35, 35, 42, 255};
            if (!active)        bg = Color{25, 25, 30, 255};
            else if (playing)   bg = Color{50, 90, 50, 255};
            else if (selected)  bg = Color{55, 55, 70, 255};
            r.drawRect(cx + 1, cy + 1, cellW - 2, cellH - 2, bg);

            const Color border = selected ? Color{255, 200, 50, 200} :
                                 playing  ? Color{80, 200, 80, 200}  :
                                            Color{60, 60, 75, 255};
            r.drawRectOutline(cx + 1, cy + 1, cellW - 2, cellH - 2, border);

            if (active && tm) {
                const int midiNote = m_baseNote + i;
                const int octave = (midiNote / 12) - 1;
                const int noteIdx = midiNote % 12;
                char label[16];
                std::snprintf(label, sizeof(label), "%s%d",
                              noteNames[noteIdx], octave);
                const float tw = tm->textWidth(label, lblFs);
                const float tx = cx + (cellW - tw) * 0.5f;
                const float ty = cy + cellH * 0.5f - 3.5f;
                const Color textCol = selected
                    ? Color{255, 220, 100, 255}
                    : Color{160, 160, 180, 255};
                tm->drawText(r, label, tx, ty, lblFs, textCol);

                char numBuf[4];
                std::snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
                tm->drawText(r, numBuf, cx + 3, cy + 2, padNumFs,
                              Color{80, 80, 100, 150});
            }
        }
        // Drop-target highlight (audio-clip drag).
        DragManager::renderDropHighlight(m_bounds, ctx);
    }
#endif

private:
    Rect m_waveRect{}, m_gridRect{};
    float m_waveH = 40.0f;

    const float* m_loopData = nullptr;
    int m_loopFrames = 0;
    int m_loopChannels = 1;

    int m_sliceCount = 8;
    std::vector<int64_t> m_sliceBounds;
    int m_baseNote = 36;
    int m_selectedPad = 0;
    bool m_padPlaying[16] = {};

    std::function<void(int)> m_onPadClick;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
