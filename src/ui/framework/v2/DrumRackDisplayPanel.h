#pragma once
// fw2::DrumRackDisplayPanel — 128-pad drum machine with paged 4×4 grid.
// Migrated from v1 fw::DrumRackDisplayPanel. Shows the selected pad's
// sample waveform on top, an 8-page navigation bar below, and a 4×4
// pad grid showing 16 notes per page. Clicks fire onPadClick / onPageChange.

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

namespace yawn {
namespace ui {
namespace fw2 {

class DrumRackDisplayPanel : public Widget {
public:
    DrumRackDisplayPanel() { setName("DrumRackDisplay"); }

    void setSelectedPad(int note) { m_selectedPad = std::clamp(note, 0, 127); }
    void setPage(int page)        { m_page = std::clamp(page, 0, 7); }
    int  page() const             { return m_page; }

    void setPadHasSample(int note, bool has) {
        if (note >= 0 && note < 128) m_padHasSample[note] = has;
    }
    void setPadPlaying(int note, bool playing) {
        if (note >= 0 && note < 128) m_padPlaying[note] = playing;
    }
    void setSelectedPadWaveform(const float* data, int frames, int channels) {
        m_waveData = data;
        m_waveFrames = frames;
        m_waveChannels = channels;
    }

    void setOnPadClick(std::function<void(int)> cb) { m_onPadClick = std::move(cb); }
    void setOnPageChange(std::function<void(int)> cb) { m_onPageChange = std::move(cb); }

    // Hit-test helper for cross-panel drag-drop. Returns the MIDI
    // note of the pad under (sx, sy), or -1 if the point is not
    // over any pad in the current page's grid. Used by the App-side
    // global drag drop dispatcher to route audio-buffer drops to a
    // specific pad rather than the whole rack.
    int padNoteAt(float sx, float sy) const {
        if (sx < m_gridRect.x || sx >= m_gridRect.x + m_gridRect.w ||
            sy < m_gridRect.y || sy >= m_gridRect.y + m_gridRect.h)
            return -1;
        constexpr int cols = 4, rows = 4;
        const float cellW = m_gridRect.w / cols;
        const float cellH = m_gridRect.h / rows;
        const int col = static_cast<int>((sx - m_gridRect.x) / cellW);
        const int row = static_cast<int>((sy - m_gridRect.y) / cellH);
        const int padIdx = row * cols + col;
        const int note = m_page * 16 + padIdx;
        return (note >= 0 && note < 128) ? note : -1;
    }
    const Rect& gridRect() const { return m_gridRect; }

    Size onMeasure(Constraints c, UIContext&) override {
        // Bumped from 160 → 220 so the pad-grid cells are large
        // enough to show readable note labels at the user's font
        // scale. Wave + page-bar grow proportionally.
        return c.constrain({c.maxW, 220.0f});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float gap = 4.0f;
        m_waveH    = std::min(48.0f, bounds.h * 0.22f);
        m_pageBarH = 26.0f;   // was 16 — fits theme.fontSizeSmall comfortably
        m_waveRect    = {bounds.x, bounds.y, bounds.w, m_waveH};
        m_pageBarRect = {bounds.x, bounds.y + m_waveH + gap, bounds.w, m_pageBarH};
        m_gridRect    = {bounds.x, bounds.y + m_waveH + m_pageBarH + gap * 2,
                         bounds.w, bounds.h - m_waveH - m_pageBarH - gap * 2};
    }

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        const float mx = e.x, my = e.y;

        if (mx >= m_pageBarRect.x && mx < m_pageBarRect.x + m_pageBarRect.w &&
            my >= m_pageBarRect.y && my < m_pageBarRect.y + m_pageBarRect.h) {
            const float btnW = 22.0f;   // matches paint-side width
            if (mx < m_pageBarRect.x + btnW) {
                const int newPage = std::max(0, m_page - 1);
                if (newPage != m_page) {
                    m_page = newPage;
                    if (m_onPageChange) m_onPageChange(m_page);
                }
                return true;
            }
            if (mx > m_pageBarRect.x + m_pageBarRect.w - btnW) {
                const int newPage = std::min(7, m_page + 1);
                if (newPage != m_page) {
                    m_page = newPage;
                    if (m_onPageChange) m_onPageChange(m_page);
                }
                return true;
            }
            const float innerW = m_pageBarRect.w - btnW * 2;
            const float pageW = innerW / 8.0f;
            int clickedPage = static_cast<int>((mx - m_pageBarRect.x - btnW) / pageW);
            clickedPage = std::clamp(clickedPage, 0, 7);
            if (clickedPage != m_page) {
                m_page = clickedPage;
                if (m_onPageChange) m_onPageChange(m_page);
            }
            return true;
        }

        if (mx >= m_gridRect.x && mx < m_gridRect.x + m_gridRect.w &&
            my >= m_gridRect.y && my < m_gridRect.y + m_gridRect.h) {
            constexpr int cols = 4, rows = 4;
            const float cellW = m_gridRect.w / cols;
            const float cellH = m_gridRect.h / rows;
            const int col = static_cast<int>((mx - m_gridRect.x) / cellW);
            const int row = static_cast<int>((my - m_gridRect.y) / cellH);
            const int padIdx = row * cols + col;
            const int note = m_page * 16 + padIdx;
            if (note >= 0 && note < 128 && m_onPadClick)
                m_onPadClick(note);
            return true;
        }
        return false;
    }

    // Mouse-wheel scroll over the panel pages through the 8 banks
    // of 16 pads. Wheel up → previous page; wheel down → next page.
    // Matches the natural "show me more pads" gesture that DAWs
    // like FL/Bitwig give you on a paged drum grid.
    bool onScroll(ScrollEvent& e) override {
        if (e.x < bounds().x || e.x >= bounds().x + bounds().w ||
            e.y < bounds().y || e.y >= bounds().y + bounds().h)
            return false;
        if (e.dy > 0.0f && m_page > 0) {
            --m_page;
            if (m_onPageChange) m_onPageChange(m_page);
            return true;
        }
        if (e.dy < 0.0f && m_page < 7) {
            ++m_page;
            if (m_onPageChange) m_onPageChange(m_page);
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

        // ─── Waveform ───
        r.drawRect(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                          Color{50, 50, 60, 255});

        if (m_waveData && m_waveFrames > 0) {
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
                    const int startFrame = (i * m_waveFrames) / numBars;
                    const int endFrame = ((i + 1) * m_waveFrames) / numBars;
                    float minVal = 0.0f, maxVal = 0.0f;
                    for (int s = startFrame; s < endFrame; ++s) {
                        const float v = m_waveData[s * m_waveChannels];
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
            }
        } else if (tm) {
            // Use the theme's "small" font so the placeholder
            // label stays in step with the user's font-scale
            // preference (Preferences → Theme).
            const float lblFs = theme().metrics.fontSizeSmall;
            const float lh = tm->lineHeight(lblFs);
            const char* msg = "Drop Sample";
            const float tw = tm->textWidth(msg, lblFs);
            const float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            const float ty = m_waveRect.y + (m_waveRect.h - lh) * 0.5f;
            tm->drawText(r, msg, tx, ty, lblFs, Color{120, 120, 140, 200});
        }

        // ─── Page bar ───
        r.drawRect(m_pageBarRect.x, m_pageBarRect.y, m_pageBarRect.w,
                   m_pageBarRect.h, Color{25, 25, 32, 255});

        const float btnW  = 22.0f;                              // arrow buttons
        const float barFs = theme().metrics.fontSizeSmall;
        const float barLh = tm ? tm->lineHeight(barFs) : barFs;
        const float barTextY = m_pageBarRect.y +
                                (m_pageBarRect.h - barLh) * 0.5f;

        if (tm) {
            // Centred "<" / ">" arrows. Centring keeps the visual
            // weight balanced when the user bumps the font scale.
            const float lw = tm->textWidth("<", barFs);
            const float rw = tm->textWidth(">", barFs);
            tm->drawText(r, "<",
                          m_pageBarRect.x + (btnW - lw) * 0.5f,
                          barTextY, barFs, Color{200, 200, 220, 230});
            tm->drawText(r, ">",
                          m_pageBarRect.x + m_pageBarRect.w - btnW + (btnW - rw) * 0.5f,
                          barTextY, barFs, Color{200, 200, 220, 230});
        }

        const float innerW = m_pageBarRect.w - btnW * 2;
        const float pageW = innerW / 8.0f;
        static const char* pageLabels[] = {
            "C-1", "C0", "C1", "C2", "C3", "C4", "C5", "C6"
        };
        for (int i = 0; i < 8; ++i) {
            const float pbx = m_pageBarRect.x + btnW + i * pageW;
            const bool active = (i == m_page);
            const Color bg = active ? Color{70, 70, 95, 255}
                                     : Color{30, 30, 38, 255};
            r.drawRect(pbx + 1, m_pageBarRect.y + 1, pageW - 2,
                       m_pageBarH - 2, bg);
            if (tm) {
                const float tw = tm->textWidth(pageLabels[i], barFs);
                const float tx = pbx + (pageW - tw) * 0.5f;
                const Color tc = active ? Color{255, 200, 50, 255}
                                         : Color{160, 160, 180, 200};
                tm->drawText(r, pageLabels[i], tx, barTextY, barFs, tc);
            }
        }

        // ─── Pad grid ───
        constexpr int cols = 4, rows = 4;
        const float cellW = m_gridRect.w / cols;
        const float cellH = m_gridRect.h / rows;
        // Use the regular theme font for pad labels — large enough
        // to read on a typical 1080p/1440p screen at default scale
        // and scales up cleanly when the user bumps font size.
        const float padFs = theme().metrics.fontSize;

        static const char* noteNames[] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        };

        const int baseNote = m_page * 16;
        for (int i = 0; i < 16; ++i) {
            const int note = baseNote + i;
            if (note >= 128) break;
            const int col = i % cols;
            const int row = i / cols;
            const float cx = m_gridRect.x + col * cellW;
            const float cy = m_gridRect.y + row * cellH;

            const bool hasSample = m_padHasSample[note];
            const bool selected  = (note == m_selectedPad);
            const bool playing   = m_padPlaying[note];

            Color bg{35, 35, 42, 255};
            if (playing)        bg = Color{50, 90, 50, 255};
            else if (selected)  bg = Color{55, 55, 70, 255};
            else if (hasSample) bg = Color{40, 40, 52, 255};
            r.drawRect(cx + 1, cy + 1, cellW - 2, cellH - 2, bg);

            const Color border = selected   ? Color{255, 200, 50, 200} :
                                 playing    ? Color{80, 200, 80, 200}  :
                                 hasSample  ? Color{70, 70, 90, 255}   :
                                              Color{50, 50, 60, 255};
            r.drawRectOutline(cx + 1, cy + 1, cellW - 2, cellH - 2, border);

            const int octave = (note / 12) - 1;
            const int noteIdx = note % 12;
            char label[16];
            std::snprintf(label, sizeof(label), "%s%d",
                          noteNames[noteIdx], octave);
            if (tm) {
                const float tw = tm->textWidth(label, padFs);
                const float lh = tm->lineHeight(padFs);
                const float tx = cx + (cellW - tw) * 0.5f;
                const float ty = cy + (cellH - lh) * 0.5f;
                const Color textCol = selected   ? Color{255, 220, 100, 255} :
                                      hasSample  ? Color{200, 200, 220, 255} :
                                                   Color{120, 120, 140, 200};
                tm->drawText(r, label, tx, ty, padFs, textCol);
            }

            if (hasSample) {
                // Bigger has-sample dot — was 3×3, now scales with
                // the font so it stays proportional to the label.
                const float dotSz = std::max(4.0f, padFs * 0.35f);
                r.drawRect(cx + cellW - dotSz - 3, cy + 3, dotSz, dotSz,
                           Color{0, 180, 230, 200});
            }
        }

        // Drop-target highlight: when an audio-clip drag is in
        // progress and the cursor is over a pad in this grid,
        // overlay a bright accent ring on that pad so the user
        // knows exactly where the drop will land. Skips entirely
        // when no drag is active — zero cost for the common case.
        {
            auto& dm = DragManager::instance();
            if (dm.isDraggingAudioClip()) {
                const int hoverPad = padNoteAt(dm.currentX(), dm.currentY());
                if (hoverPad >= 0 && hoverPad / 16 == m_page) {
                    const int idx = hoverPad - m_page * 16;
                    const int col = idx % cols;
                    const int row = idx / cols;
                    const float cx = m_gridRect.x + col * cellW;
                    const float cy = m_gridRect.y + row * cellH;
                    // Two-layer ring: outer faint glow, inner bright.
                    r.drawRectOutline(cx,     cy,     cellW,     cellH,
                                       Color{0, 200, 240, 70}, 4.0f);
                    r.drawRectOutline(cx + 1, cy + 1, cellW - 2, cellH - 2,
                                       Color{0, 220, 255, 230}, 2.0f);
                }
            }
        }
    }
#endif

private:
    Rect m_waveRect{}, m_pageBarRect{}, m_gridRect{};
    float m_waveH = 36.0f;
    float m_pageBarH = 16.0f;

    int m_selectedPad = 36;
    int m_page = 2;

    bool m_padHasSample[128] = {};
    bool m_padPlaying[128] = {};

    const float* m_waveData = nullptr;
    int m_waveFrames = 0;
    int m_waveChannels = 1;

    std::function<void(int)> m_onPadClick;
    std::function<void(int)> m_onPageChange;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
