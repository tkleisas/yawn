#pragma once
// ArrangementPanel — Horizontal timeline view for arrangement mode.
//
// Layout:
//   Top: Time ruler showing bar:beat markers (zoomable)
//   Left: Track headers (name, color bar, session/arr toggle)
//   Center: Clip timeline (horizontal blocks per track)
//   Bottom: Horizontal scrollbar
//
// Tracks are stacked vertically, clips are drawn as colored rectangles
// positioned by their startBeat/lengthBeats on the horizontal axis.

#include "ui/framework/Widget.h"
#include "ui/Theme.h"
#include "app/Project.h"
#include "audio/AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

class ArrangementPanel : public Widget {
public:
    // Layout constants
    static constexpr float kRulerH         = 24.0f;
    static constexpr float kTrackRowH      = 40.0f;
    static constexpr float kTrackHeaderW   = 100.0f;
    static constexpr float kScrollbarH     = 12.0f;
    static constexpr float kMinZoom        = 4.0f;   // pixels per beat minimum
    static constexpr float kMaxZoom        = 120.0f;  // pixels per beat maximum
    static constexpr float kDefaultZoom    = 20.0f;  // pixels per beat default

    ArrangementPanel() {
        setName("ArrangementPanel");
    }

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
    }

    // ─── Accessors ─────────────────────────────────────────────────────

    float scrollX() const { return m_scrollX; }
    float scrollY() const { return m_scrollY; }
    void setScrollX(float x) { m_scrollX = std::max(0.0f, x); }
    void setScrollY(float y) { m_scrollY = std::max(0.0f, y); }

    float zoom() const { return m_pixelsPerBeat; }
    void setZoom(float ppb) { m_pixelsPerBeat = std::clamp(ppb, kMinZoom, kMaxZoom); }

    int selectedTrack() const { return m_selectedTrack; }
    void setSelectedTrack(int t) { m_selectedTrack = t; }

    using TrackClickCallback = std::function<void(int)>;
    void setOnTrackClick(TrackClickCallback cb) { m_onTrackClick = std::move(cb); }

    using PlayheadClickCallback = std::function<void(double)>;
    void setOnPlayheadClick(PlayheadClickCallback cb) { m_onPlayheadClick = std::move(cb); }

    // ─── Widget overrides ──────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({400.0f, 200.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override {
        if (!m_project || !m_engine) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w,  h = m_bounds.h;

        // Background
        r.drawRect(x, y, w, h, Theme::background);

        float gridX = x + kTrackHeaderW;
        float gridW = w - kTrackHeaderW;
        float gridY = y + kRulerH;
        float gridH = h - kRulerH - kScrollbarH;

        paintRuler(r, f, gridX, y, gridW);
        paintTrackHeaders(r, f, x, gridY, gridH);
        paintClipTimeline(r, f, gridX, gridY, gridW, gridH);
        paintPlayhead(r, gridX, gridY, gridW, gridH + kRulerH);
        paintScrollbar(r, gridX, gridY + gridH, gridW);
    }
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (!m_project) return false;
        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;
        float gridX = x + kTrackHeaderW;
        float gridW = w - kTrackHeaderW;
        float gridY = y + kRulerH;
        float gridH = h - kRulerH - kScrollbarH;

        // Ruler click → set playhead
        if (e.x >= gridX && e.x < gridX + gridW &&
            e.y >= y && e.y < gridY) {
            double beat = (e.x - gridX + m_scrollX) / m_pixelsPerBeat;
            if (m_onPlayheadClick) m_onPlayheadClick(std::max(0.0, beat));
            return true;
        }

        // Track header click → select track
        if (e.x >= x && e.x < gridX &&
            e.y >= gridY && e.y < gridY + gridH) {
            int track = static_cast<int>((e.y - gridY + m_scrollY) / kTrackRowH);
            if (track >= 0 && track < m_project->numTracks()) {
                m_selectedTrack = track;
                if (m_onTrackClick) m_onTrackClick(track);
            }
            return true;
        }

        // Scrollbar drag
        float sbY = gridY + gridH;
        if (e.y >= sbY && e.y < sbY + kScrollbarH &&
            e.x >= gridX && e.x < gridX + gridW) {
            m_scrollDragging = true;
            m_scrollDragStart = e.x;
            m_scrollDragBase = m_scrollX;
            captureMouse();
            return true;
        }

        // Grid click → select track + potential clip interaction
        if (e.x >= gridX && e.x < gridX + gridW &&
            e.y >= gridY && e.y < gridY + gridH) {
            int track = static_cast<int>((e.y - gridY + m_scrollY) / kTrackRowH);
            if (track >= 0 && track < m_project->numTracks()) {
                m_selectedTrack = track;
                if (m_onTrackClick) m_onTrackClick(track);
            }
            return true;
        }

        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_scrollDragging) {
            m_scrollDragging = false;
            releaseMouse();
            return true;
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_scrollDragging) {
            float delta = e.x - m_scrollDragStart;
            float contentW = static_cast<float>(m_project->arrangementLength()) * m_pixelsPerBeat;
            float gridW = m_bounds.w - kTrackHeaderW;
            float maxScroll = std::max(0.0f, contentW - gridW);
            float ratio = contentW > gridW ? contentW / gridW : 1.0f;
            m_scrollX = std::clamp(m_scrollDragBase + delta * ratio, 0.0f, maxScroll);
            return true;
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_project) return false;

        // Ctrl+scroll = zoom
        if (e.mods.ctrl) {
            float zoomFactor = e.dy > 0 ? 1.15f : (1.0f / 1.15f);
            float oldZoom = m_pixelsPerBeat;
            m_pixelsPerBeat = std::clamp(m_pixelsPerBeat * zoomFactor, kMinZoom, kMaxZoom);

            // Zoom toward mouse position
            float gridX = m_bounds.x + kTrackHeaderW;
            float mouseGridX = e.x - gridX;
            float beatAtMouse = (mouseGridX + m_scrollX) / oldZoom;
            m_scrollX = beatAtMouse * m_pixelsPerBeat - mouseGridX;
            m_scrollX = std::max(0.0f, m_scrollX);
            return true;
        }

        // Shift+scroll = horizontal
        if (e.mods.shift || std::abs(e.dx) > std::abs(e.dy)) {
            float delta = e.mods.shift ? e.dy : e.dx;
            m_scrollX = std::max(0.0f, m_scrollX - delta * 30.0f);
            return true;
        }

        // Vertical scroll
        m_scrollY = std::max(0.0f, m_scrollY - e.dy * 30.0f);
        return true;
    }

private:
    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;
    float m_pixelsPerBeat = kDefaultZoom;
    int   m_selectedTrack = 0;

    // Scrollbar drag state
    bool  m_scrollDragging = false;
    float m_scrollDragStart = 0;
    float m_scrollDragBase  = 0;

    TrackClickCallback     m_onTrackClick;
    PlayheadClickCallback  m_onPlayheadClick;

    // ─── Painting helpers ──────────────────────────────────────────────

#ifndef YAWN_TEST_BUILD

    void paintRuler(Renderer2D& r, Font& f, float x, float y, float w) {
        r.drawRect(x, y, w, kRulerH, Color{35, 35, 40, 255});
        r.pushClip(x, y, w, kRulerH);

        int beatsPerBar = m_engine ? m_engine->transport().numerator() : 4;
        float barWidthPx = beatsPerBar * m_pixelsPerBeat;

        // Determine bar label interval based on zoom
        int barLabelEvery = 1;
        if (barWidthPx < 30.0f) barLabelEvery = 8;
        else if (barWidthPx < 60.0f) barLabelEvery = 4;
        else if (barWidthPx < 120.0f) barLabelEvery = 2;

        float startBeat = m_scrollX / m_pixelsPerBeat;
        int startBar = static_cast<int>(startBeat / beatsPerBar);
        float endBeat = startBeat + w / m_pixelsPerBeat;
        int endBar = static_cast<int>(endBeat / beatsPerBar) + 1;

        float scale = Theme::kSmallFontSize / f.pixelHeight();
        for (int bar = startBar; bar <= endBar; ++bar) {
            float beatPos = bar * beatsPerBar;
            float px = x + beatPos * m_pixelsPerBeat - m_scrollX;

            // Bar line
            r.drawRect(px, y, 1, kRulerH, Color{80, 80, 90, 255});

            // Bar number label
            if (bar >= 0 && (bar % barLabelEvery) == 0) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", bar + 1);
                float tx = px + 3;
                for (const char* p = buf; *p; ++p) {
                    auto g = f.getGlyph(*p, tx, y + 4, scale);
                    r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::textSecondary, f.textureId());
                    tx += g.xAdvance;
                }
            }

            // Beat ticks within bar (if zoomed enough)
            if (m_pixelsPerBeat >= 8.0f) {
                for (int b = 1; b < beatsPerBar; ++b) {
                    float bpx = x + (beatPos + b) * m_pixelsPerBeat - m_scrollX;
                    r.drawRect(bpx, y + kRulerH * 0.6f, 1, kRulerH * 0.4f,
                               Color{60, 60, 70, 255});
                }
            }
        }

        // Bottom border
        r.drawRect(x, y + kRulerH - 1, w, 1, Color{70, 70, 80, 255});
        r.popClip();
    }

    void paintTrackHeaders(Renderer2D& r, Font& f, float x, float y, float h) {
        r.pushClip(x, y, kTrackHeaderW, h);
        int numTracks = m_project->numTracks();
        float scale = Theme::kSmallFontSize / f.pixelHeight();

        for (int t = 0; t < numTracks; ++t) {
            float ty = y + t * kTrackRowH - m_scrollY;
            if (ty + kTrackRowH < y || ty > y + h) continue;

            auto& tr = m_project->track(t);

            // Background
            Color bg = (t == m_selectedTrack)
                ? Color{50, 55, 65, 255} : Color{42, 42, 46, 255};
            r.drawRect(x, ty, kTrackHeaderW, kTrackRowH, bg);

            // Color bar
            Color col = Theme::trackColors[tr.colorIndex % Theme::kNumTrackColors];
            r.drawRect(x + 2, ty + 2, 3, kTrackRowH - 4, col);

            // Track name
            float tx = x + 8;
            float textY = ty + (kTrackRowH - Theme::kSmallFontSize) * 0.5f;
            float maxX = x + kTrackHeaderW - 4;
            for (const char* p = tr.name.c_str(); *p && tx < maxX; ++p) {
                auto g = f.getGlyph(*p, tx, textY, scale);
                r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                   g.u0, g.v0, g.u1, g.v1,
                                   Theme::textPrimary, f.textureId());
                tx += g.xAdvance;
            }

            // Bottom border
            r.drawRect(x, ty + kTrackRowH - 1, kTrackHeaderW, 1,
                       Color{60, 60, 65, 255});
        }
        r.popClip();
    }

    void paintClipTimeline(Renderer2D& r, Font& f, float x, float y, float w, float h) {
        r.pushClip(x, y, w, h);

        int numTracks = m_project->numTracks();
        float scale = Theme::kSmallFontSize / f.pixelHeight();
        float viewStartBeat = m_scrollX / m_pixelsPerBeat;
        float viewEndBeat = viewStartBeat + w / m_pixelsPerBeat;

        // Grid lines (bar lines)
        int beatsPerBar = m_engine ? m_engine->transport().numerator() : 4;
        {
            int startBar = static_cast<int>(viewStartBeat / beatsPerBar);
            int endBar = static_cast<int>(viewEndBeat / beatsPerBar) + 1;
            for (int bar = startBar; bar <= endBar; ++bar) {
                float px = x + bar * beatsPerBar * m_pixelsPerBeat - m_scrollX;
                r.drawRect(px, y, 1, h, Color{45, 45, 50, 255});
            }
        }

        // Track row backgrounds and clips
        for (int t = 0; t < numTracks; ++t) {
            float ty = y + t * kTrackRowH - m_scrollY;
            if (ty + kTrackRowH < y || ty > y + h) continue;

            // Track row background
            Color rowBg = (t % 2 == 0) ? Color{33, 33, 36, 255} : Color{30, 30, 33, 255};
            r.drawRect(x, ty, w, kTrackRowH, rowBg);

            // Row border
            r.drawRect(x, ty + kTrackRowH - 1, w, 1, Color{45, 45, 50, 100});

            // Arrangement clips for this track
            auto& track = m_project->track(t);
            Color trackCol = Theme::trackColors[track.colorIndex % Theme::kNumTrackColors];

            for (auto& clip : track.arrangementClips) {
                if (clip.endBeat() < viewStartBeat || clip.startBeat > viewEndBeat)
                    continue;

                float cx = x + static_cast<float>(clip.startBeat) * m_pixelsPerBeat - m_scrollX;
                float cw = static_cast<float>(clip.lengthBeats) * m_pixelsPerBeat;
                float cy = ty + 2;
                float ch = kTrackRowH - 4;

                // Clip body
                Color clipCol = clip.colorIndex >= 0
                    ? Theme::trackColors[clip.colorIndex % Theme::kNumTrackColors]
                    : trackCol;
                Color bodyCol{clipCol.r, clipCol.g, clipCol.b, 160};
                r.drawRoundedRect(cx, cy, cw, ch, 2.0f, bodyCol, 4);

                // Clip border
                r.drawRectOutline(cx, cy, cw, ch, clipCol.withAlpha(100));

                // Clip name
                if (cw > 20.0f && !clip.name.empty()) {
                    float ntx = cx + 4;
                    float nty = cy + (ch - Theme::kSmallFontSize) * 0.5f;
                    float nMaxX = cx + cw - 4;
                    for (const char* p = clip.name.c_str(); *p && ntx < nMaxX; ++p) {
                        auto g = f.getGlyph(*p, ntx, nty, scale);
                        r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                           g.u0, g.v0, g.u1, g.v1,
                                           Color{255, 255, 255, 220}, f.textureId());
                        ntx += g.xAdvance;
                    }
                }
            }
        }

        r.popClip();
    }

    void paintPlayhead(Renderer2D& r, float x, float y, float w, float h) {
        if (!m_engine) return;
        double beat = m_engine->transport().positionInBeats();
        float px = x + static_cast<float>(beat) * m_pixelsPerBeat - m_scrollX;

        if (px >= x && px <= x + w) {
            float rulerY = y - kRulerH;
            // Playhead triangle on ruler
            float triSize = 5.0f;
            r.drawTriangle(px - triSize, rulerY + kRulerH - 1,
                          px + triSize, rulerY + kRulerH - 1,
                          px, rulerY + kRulerH - triSize - 1,
                          Color{255, 255, 255, 200});
            // Playhead line through grid
            r.drawRect(px, y, 1, h - kRulerH, Color{255, 255, 255, 150});
        }
    }

    void paintScrollbar(Renderer2D& r, float x, float y, float w) {
        r.drawRect(x, y, w, kScrollbarH, Color{40, 40, 45, 255});

        float contentW = static_cast<float>(m_project->arrangementLength()) * m_pixelsPerBeat;
        if (contentW <= w) return;

        float thumbW = std::max(20.0f, w * (w / contentW));
        float scrollFrac = m_scrollX / (contentW - w);
        float thumbX = x + scrollFrac * (w - thumbW);

        Color thumbCol = m_scrollDragging ? Color{120, 120, 130, 255} : Color{80, 80, 90, 255};
        r.drawRoundedRect(thumbX, y + 1, thumbW, kScrollbarH - 2, 3.0f, thumbCol, 4);
    }

#endif // !YAWN_TEST_BUILD
};

} // namespace fw
} // namespace ui
} // namespace yawn
