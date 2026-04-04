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

    using ClipChangeCallback = std::function<void(int track)>;
    void setOnClipChange(ClipChangeCallback cb) { m_onClipChange = std::move(cb); }

    using TrackArrToggleCallback = std::function<void(int track, bool active)>;
    void setOnTrackArrToggle(TrackArrToggleCallback cb) { m_onTrackArrToggle = std::move(cb); }

    // ─── Snap grid ─────────────────────────────────────────────────────

    enum class Snap { Off, Bar, Beat, Half, Quarter, Eighth };

    Snap snap() const { return m_snap; }
    void setSnap(Snap s) { m_snap = s; }

    double snapVal() const {
        switch (m_snap) {
            case Snap::Off:     return 0.0;
            case Snap::Bar:     return 4.0;
            case Snap::Beat:    return 1.0;
            case Snap::Half:    return 0.5;
            case Snap::Quarter: return 0.25;
            case Snap::Eighth:  return 0.125;
        }
        return 1.0;
    }

    double snapBeat(double b) const {
        double s = snapVal();
        if (s <= 0.0) return b;
        return std::round(b / s) * s;
    }

    // ─── Clip selection ────────────────────────────────────────────────

    int selectedClipTrack() const { return m_selClipTrack; }
    int selectedClipIndex() const { return m_selClipIdx; }
    void clearClipSelection() { m_selClipTrack = -1; m_selClipIdx = -1; }

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

        // Track header click
        if (e.x >= x && e.x < gridX &&
            e.y >= gridY && e.y < gridY + gridH) {
            int track = static_cast<int>((e.y - gridY + m_scrollY) / kTrackRowH);
            if (track >= 0 && track < m_project->numTracks()) {
                float ty = gridY + track * kTrackRowH - m_scrollY;
                float btnX = x + kTrackHeaderW - 22;
                float btnY = ty + kTrackRowH - 17;
                // S/A toggle button (bottom-right of header)
                if (e.x >= btnX && e.x < btnX + 20 &&
                    e.y >= btnY && e.y < btnY + 14) {
                    auto& tr = m_project->track(track);
                    tr.arrangementActive = !tr.arrangementActive;
                    if (m_onTrackArrToggle)
                        m_onTrackArrToggle(track, tr.arrangementActive);
                    return true;
                }
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

        // Grid area — clip interaction
        if (e.x >= gridX && e.x < gridX + gridW &&
            e.y >= gridY && e.y < gridY + gridH) {
            int trackIdx = static_cast<int>((e.y - gridY + m_scrollY) / kTrackRowH);
            double beat = static_cast<double>((e.x - gridX + m_scrollX) / m_pixelsPerBeat);

            if (trackIdx < 0 || trackIdx >= m_project->numTracks()) return true;
            m_selectedTrack = trackIdx;
            if (m_onTrackClick) m_onTrackClick(trackIdx);

            // Hit-test clips on this track
            auto& clips = m_project->track(trackIdx).arrangementClips;
            int hitIdx = -1;
            for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
                if (beat >= clips[i].startBeat && beat < clips[i].endBeat()) {
                    hitIdx = i;
                    break;
                }
            }

            if (hitIdx >= 0) {
                m_selClipTrack = trackIdx;
                m_selClipIdx = hitIdx;
                auto& clip = clips[hitIdx];

                float clipLpx = gridX + static_cast<float>(clip.startBeat) * m_pixelsPerBeat - m_scrollX;
                float clipRpx = gridX + static_cast<float>(clip.endBeat()) * m_pixelsPerBeat - m_scrollX;
                constexpr float kEdge = 6.0f;
                bool wideEnough = (clip.lengthBeats * m_pixelsPerBeat) > kEdge * 3;

                if (e.x < clipLpx + kEdge && wideEnough)
                    m_dragMode = DragMode::ResizeLeft;
                else if (e.x > clipRpx - kEdge && wideEnough)
                    m_dragMode = DragMode::ResizeRight;
                else
                    m_dragMode = DragMode::MoveClip;

                m_dragStartBeat = beat;
                m_dragOrigStart = clip.startBeat;
                m_dragOrigLength = clip.lengthBeats;
                m_dragOrigOffset = clip.offsetBeats;
                m_dragOrigTrack = trackIdx;
                captureMouse();
            } else {
                clearClipSelection();

                // Double-click on empty space in MIDI track → add empty clip
                if (e.isDoubleClick() &&
                    m_project->track(trackIdx).type == Track::Type::Midi) {
                    double snapB = std::max(0.0, snapBeat(beat));
                    double len = snapVal() > 0 ? std::max(snapVal() * 4.0, 4.0) : 4.0;

                    ArrangementClip nc;
                    nc.type = ArrangementClip::Type::Midi;
                    nc.startBeat = snapB;
                    nc.lengthBeats = len;
                    nc.name = "Clip";
                    nc.midiClip = std::make_shared<midi::MidiClip>();
                    nc.midiClip->setLengthBeats(len);

                    m_project->track(trackIdx).arrangementClips.push_back(std::move(nc));
                    m_project->track(trackIdx).sortArrangementClips();
                    m_project->updateArrangementLength();

                    auto& cs = m_project->track(trackIdx).arrangementClips;
                    for (int i = 0; i < static_cast<int>(cs.size()); ++i) {
                        if (std::abs(cs[i].startBeat - snapB) < 0.001) {
                            m_selClipTrack = trackIdx;
                            m_selClipIdx = i;
                            break;
                        }
                    }
                    if (m_onClipChange) m_onClipChange(trackIdx);
                }
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
        if (m_dragMode != DragMode::None) {
            int affected = m_selClipTrack;
            bool crossTrack = (m_dragOrigTrack >= 0 && m_dragOrigTrack != m_selClipTrack);
            m_dragMode = DragMode::None;
            releaseMouse();
            if (m_onClipChange) {
                m_onClipChange(affected);
                if (crossTrack) m_onClipChange(m_dragOrigTrack);
            }
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

        if (m_dragMode == DragMode::None || m_selClipTrack < 0 || m_selClipIdx < 0)
            return false;

        float gridX = m_bounds.x + kTrackHeaderW;
        float gridY = m_bounds.y + kRulerH;
        double curBeat = static_cast<double>((e.x - gridX + m_scrollX) / m_pixelsPerBeat);
        double delta = curBeat - m_dragStartBeat;

        if (m_dragMode == DragMode::MoveClip) {
            double newStart = snapBeat(m_dragOrigStart + delta);
            newStart = std::max(0.0, newStart);

            int newTrack = static_cast<int>((e.y - gridY + m_scrollY) / kTrackRowH);
            newTrack = std::clamp(newTrack, 0, m_project->numTracks() - 1);

            if (newTrack != m_selClipTrack) {
                // Cross-track move
                auto& src = m_project->track(m_selClipTrack).arrangementClips;
                ArrangementClip moved = src[m_selClipIdx];
                moved.startBeat = newStart;
                src.erase(src.begin() + m_selClipIdx);
                m_project->track(m_selClipTrack).sortArrangementClips();

                auto& dst = m_project->track(newTrack).arrangementClips;
                dst.push_back(std::move(moved));
                m_project->track(newTrack).sortArrangementClips();

                m_selClipIdx = 0;
                for (int i = 0; i < static_cast<int>(dst.size()); ++i) {
                    if (std::abs(dst[i].startBeat - newStart) < 0.001) {
                        m_selClipIdx = i; break;
                    }
                }
                m_selClipTrack = newTrack;
                m_selectedTrack = newTrack;
            } else {
                // Same-track horizontal move
                auto& clips = m_project->track(m_selClipTrack).arrangementClips;
                clips[m_selClipIdx].startBeat = newStart;
                m_project->track(m_selClipTrack).sortArrangementClips();
                for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
                    if (std::abs(clips[i].startBeat - newStart) < 0.001) {
                        m_selClipIdx = i; break;
                    }
                }
            }
            m_project->updateArrangementLength();
        }
        else if (m_dragMode == DragMode::ResizeRight) {
            double newLen = snapBeat(m_dragOrigLength + delta);
            double minLen = snapVal() > 0 ? snapVal() : 0.25;
            newLen = std::max(minLen, newLen);
            m_project->track(m_selClipTrack).arrangementClips[m_selClipIdx].lengthBeats = newLen;
            m_project->updateArrangementLength();
        }
        else if (m_dragMode == DragMode::ResizeLeft) {
            double newStart = snapBeat(m_dragOrigStart + delta);
            newStart = std::max(0.0, newStart);
            double endBt = m_dragOrigStart + m_dragOrigLength;
            double minLen = snapVal() > 0 ? snapVal() : 0.25;
            if (newStart > endBt - minLen) newStart = endBt - minLen;

            auto& c = m_project->track(m_selClipTrack).arrangementClips[m_selClipIdx];
            c.lengthBeats = endBt - newStart;
            c.offsetBeats = m_dragOrigOffset + (newStart - m_dragOrigStart);
            c.startBeat = newStart;
            m_project->updateArrangementLength();
        }

        return true;
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

    bool onKeyDown(KeyEvent& e) override {
        if (!m_project) return false;

        // Delete selected clip
        if ((e.isDelete() || e.isBackspace()) && m_selClipTrack >= 0 && m_selClipIdx >= 0) {
            auto& clips = m_project->track(m_selClipTrack).arrangementClips;
            if (m_selClipIdx < static_cast<int>(clips.size())) {
                int t = m_selClipTrack;
                clips.erase(clips.begin() + m_selClipIdx);
                clearClipSelection();
                m_project->updateArrangementLength();
                if (m_onClipChange) m_onClipChange(t);
                return true;
            }
        }

        // Ctrl+D = duplicate selected clip (placed immediately after)
        if (e.keyCode == 'd' && e.mods.ctrl && m_selClipTrack >= 0 && m_selClipIdx >= 0) {
            auto& clips = m_project->track(m_selClipTrack).arrangementClips;
            if (m_selClipIdx < static_cast<int>(clips.size())) {
                ArrangementClip dup = clips[m_selClipIdx];
                double dupStart = dup.endBeat();
                dup.startBeat = dupStart;
                clips.push_back(std::move(dup));
                m_project->track(m_selClipTrack).sortArrangementClips();
                m_project->updateArrangementLength();
                for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
                    if (std::abs(clips[i].startBeat - dupStart) < 0.001) {
                        m_selClipIdx = i; break;
                    }
                }
                if (m_onClipChange) m_onClipChange(m_selClipTrack);
                return true;
            }
        }

        return false;
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
    ClipChangeCallback     m_onClipChange;
    TrackArrToggleCallback m_onTrackArrToggle;

    // Snap grid
    Snap m_snap = Snap::Beat;

    // Clip selection
    int m_selClipTrack = -1;
    int m_selClipIdx   = -1;

    // Clip drag state
    enum class DragMode { None, MoveClip, ResizeLeft, ResizeRight };
    DragMode m_dragMode      = DragMode::None;
    double   m_dragStartBeat = 0.0;
    double   m_dragOrigStart = 0.0;
    double   m_dragOrigLength = 0.0;
    double   m_dragOrigOffset = 0.0;
    int      m_dragOrigTrack  = -1;

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

            // Session/Arrangement toggle indicator
            bool arrActive = tr.arrangementActive;
            float btnX = x + kTrackHeaderW - 22;
            float btnY = ty + kTrackRowH - 17;
            Color btnBg = arrActive ? Color{80, 140, 80, 255} : Color{60, 60, 65, 255};
            r.drawRoundedRect(btnX, btnY, 20, 14, 2.0f, btnBg, 4);
            const char btnCh = arrActive ? 'A' : 'S';
            float labelScale = (Theme::kSmallFontSize * 0.8f) / f.pixelHeight();
            auto bg2 = f.getGlyph(btnCh, btnX + 6, btnY + 1, labelScale);
            r.drawTexturedQuad(bg2.x0, bg2.y0, bg2.x1 - bg2.x0, bg2.y1 - bg2.y0,
                               bg2.u0, bg2.v0, bg2.u1, bg2.v1,
                               Color{255, 255, 255, 220}, f.textureId());

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

            for (int ci = 0; ci < static_cast<int>(track.arrangementClips.size()); ++ci) {
                auto& clip = track.arrangementClips[ci];
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
                bool selected = (t == m_selClipTrack && ci == m_selClipIdx);
                Color bodyCol{clipCol.r, clipCol.g, clipCol.b,
                              static_cast<uint8_t>(selected ? 200 : 160)};
                r.drawRoundedRect(cx, cy, cw, ch, 2.0f, bodyCol, 4);

                // Clip border (brighter if selected)
                r.drawRectOutline(cx, cy, cw, ch,
                    selected ? Color{255, 255, 255, 200} : clipCol.withAlpha(100));

                // Resize handles on selected clip
                if (selected && cw > 18.0f) {
                    float hw = 3.0f, hh = ch * 0.4f;
                    float hy = cy + (ch - hh) * 0.5f;
                    r.drawRect(cx + 1, hy, hw, hh, Color{255, 255, 255, 140});
                    r.drawRect(cx + cw - hw - 1, hy, hw, hh, Color{255, 255, 255, 140});
                }

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
