#pragma once
// ArrangementPanel — Horizontal timeline view for arrangement mode.
//
// Layout:
//   Top: Time ruler showing bar:beat markers (zoomable)
//   Left: Track headers (name, color bar, session/arr toggle, auto expand)
//   Center: Clip timeline (horizontal blocks per track) + automation lanes
//   Bottom: Horizontal scrollbar
//
// Tracks are stacked vertically with variable height (expanded tracks
// show automation lanes below the main clip row).

#include "ui/framework/Widget.h"
#include "ui/Theme.h"
#include "app/Project.h"
#include "audio/AudioEngine.h"
#include "automation/AutomationTypes.h"
#include "util/UndoManager.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace yawn {
namespace ui {
namespace fw {

class ArrangementPanel : public Widget {
public:
    // Layout constants
    static constexpr float kRulerH       = 24.0f;
    static constexpr float kTrackRowH    = 56.0f;
    static constexpr float kTrackHeaderW = 150.0f;
    static constexpr float kScrollbarH   = 12.0f;
    static constexpr float kAutoLaneH    = 28.0f;
    static constexpr float kMinZoom      = 4.0f;
    static constexpr float kMaxZoom      = 120.0f;
    static constexpr float kDefaultZoom  = 20.0f;

    ArrangementPanel() { setName("ArrangementPanel"); }

    void init(Project* project, audio::AudioEngine* engine,
              undo::UndoManager* undoMgr = nullptr) {
        m_project = project;
        m_engine  = engine;
        m_undoManager = undoMgr;
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

    // ─── Callbacks ─────────────────────────────────────────────────────

    using TrackClickCallback     = std::function<void(int)>;
    using PlayheadClickCallback  = std::function<void(double)>;
    using ClipChangeCallback     = std::function<void(int track)>;
    using TrackArrToggleCallback = std::function<void(int track, bool active)>;

    void setOnTrackClick(TrackClickCallback cb)         { m_onTrackClick = std::move(cb); }
    void setOnPlayheadClick(PlayheadClickCallback cb)   { m_onPlayheadClick = std::move(cb); }
    void setOnClipChange(ClipChangeCallback cb)         { m_onClipChange = std::move(cb); }
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

    // ─── Loop range ───────────────────────────────────────────────────

    double loopStart() const { return m_loopStart; }
    double loopEnd() const { return m_loopEnd; }
    bool loopEnabled() const { return m_loopEnabled; }
    void setLoopEnabled(bool e) { m_loopEnabled = e; }
    void setLoopRange(double s, double e) { m_loopStart = s; m_loopEnd = e; }

    using LoopChangeCallback = std::function<void(bool enabled, double start, double end)>;
    void setOnLoopChange(LoopChangeCallback cb) { m_onLoopChange = std::move(cb); }

    // ─── Auto-scroll ──────────────────────────────────────────────────

    bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool a) { m_autoScroll = a; }

    // ─── Clip selection ────────────────────────────────────────────────

    int selectedClipTrack() const { return m_selClipTrack; }
    int selectedClipIndex() const { return m_selClipIdx; }
    void clearClipSelection() { m_selClipTrack = -1; m_selClipIdx = -1; }

    // ─── Track layout (variable height for automation lanes) ───────────

    bool isTrackExpanded(int t) const { return m_expandedTracks.count(t) > 0; }
    void toggleTrackExpanded(int t) {
        if (m_expandedTracks.count(t)) m_expandedTracks.erase(t);
        else m_expandedTracks.insert(t);
    }

    // Per-track custom base height (user can resize)
    float trackBaseHeight(int t) const {
        auto it = m_trackHeights.find(t);
        return (it != m_trackHeights.end()) ? it->second : kTrackRowH;
    }
    void setTrackBaseHeight(int t, float h) {
        m_trackHeights[t] = std::max(kMinTrackH, std::min(kMaxTrackH, h));
    }

    static constexpr float kMinTrackH = 32.0f;
    static constexpr float kMaxTrackH = 200.0f;

    // Cursor hint — true when hovering a track resize handle
    bool wantsVerticalResize() const { return m_hoverResize || m_dragMode == DragMode::ResizeTrackH; }

    // Effective row height (main row + expanded auto lanes)
    float trackRowHeight(int t) const;
    // Cumulative Y offset for track t
    float trackYOffset(int t) const;
    // Track index at given Y offset from grid top
    int trackAtY(float relY) const;
    // Auto lane index within a track (-1 = main clip row)
    int autoLaneAtY(int track, float relYInTrack) const;
    // Label for an automation target
    std::string autoLaneName(const automation::AutomationTarget& tgt) const;

    // ─── Widget overrides ──────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({400.0f, 200.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
    bool onMouseDown(MouseEvent&) override { return false; }
    bool onMouseUp(MouseEvent&) override { return false; }
    bool onMouseMove(MouseMoveEvent&) override { return false; }
    bool onScroll(ScrollEvent&) override { return false; }
    bool onKeyDown(KeyEvent&) override { return false; }
#else
    void paint(UIContext& ctx) override;
    bool onMouseDown(MouseEvent& e) override;
    bool onMouseUp(MouseEvent& e) override;
    bool onMouseMove(MouseMoveEvent& e) override;
    bool onScroll(ScrollEvent& e) override;
    bool onKeyDown(KeyEvent& e) override;
#endif

private:
    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;

    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;
    float m_pixelsPerBeat = kDefaultZoom;
    int   m_selectedTrack = 0;

    // Scrollbar drag state
    bool  m_scrollDragging = false;
    float m_scrollDragStart = 0;
    float m_scrollDragBase  = 0;

    // Callbacks
    TrackClickCallback     m_onTrackClick;
    PlayheadClickCallback  m_onPlayheadClick;
    ClipChangeCallback     m_onClipChange;
    TrackArrToggleCallback m_onTrackArrToggle;
    LoopChangeCallback     m_onLoopChange;

    // Snap grid
    Snap m_snap = Snap::Beat;

    // Loop range
    double m_loopStart = 0.0;
    double m_loopEnd   = 0.0;
    bool   m_loopEnabled = false;

    // Auto-scroll playhead
    bool m_autoScroll = true;

    // Loop range drag state
    enum class LoopDragMode { None, DragStart, DragEnd, DragBoth };
    LoopDragMode m_loopDragMode = LoopDragMode::None;
    double m_loopDragOrigStart = 0.0;
    double m_loopDragOrigEnd = 0.0;
    float  m_loopDragMouseStart = 0.0f;

    // Clip selection
    int m_selClipTrack = -1;
    int m_selClipIdx   = -1;

    // Clip drag state
    enum class DragMode { None, MoveClip, ResizeLeft, ResizeRight, AutoPoint, ResizeTrackH };
    DragMode m_dragMode      = DragMode::None;
    double   m_dragStartBeat = 0.0;
    double   m_dragOrigStart = 0.0;
    double   m_dragOrigLength = 0.0;
    double   m_dragOrigOffset = 0.0;
    int      m_dragOrigTrack  = -1;

    // Track resize drag
    int   m_resizeTrack = -1;
    float m_resizeOrigH = 0.0f;
    float m_resizeMouseStart = 0.0f;
    bool  m_hoverResize = false;

    // Per-track custom heights
    std::map<int, float> m_trackHeights;

    // Automation lane expansion
    std::set<int> m_expandedTracks;

    // Automation breakpoint drag
    int   m_autoPointDragTrack = -1;
    int   m_autoPointDragLane  = -1;
    int   m_autoPointDragIdx   = -1;
    float m_autoPointDragOrigVal  = 0.0f;
    double m_autoPointDragOrigTime = 0.0;

    // ─── Paint helpers (implemented in ArrangementPanel.cpp) ───────────

#ifdef YAWN_TEST_BUILD
    void paintRuler(Renderer2D&, Font&, float, float, float) {}
    void paintTrackHeaders(Renderer2D&, Font&, float, float, float) {}
    void paintClipTimeline(Renderer2D&, Font&, float, float, float, float) {}
    void paintAutoLanes(Renderer2D&, Font&, float, float, float, float) {}
    void paintPlayhead(Renderer2D&, float, float, float, float) {}
    void paintScrollbar(Renderer2D&, float, float, float) {}
#else
    void paintRuler(Renderer2D& r, Font& f, float x, float y, float w);
    void paintTrackHeaders(Renderer2D& r, Font& f, float x, float y, float h);
    void paintClipTimeline(Renderer2D& r, Font& f, float x, float y, float w, float h);
    void paintAutoLanes(Renderer2D& r, Font& f, float x, float y, float w, float h);
    void paintPlayhead(Renderer2D& r, float x, float y, float w, float h);
    void paintScrollbar(Renderer2D& r, float x, float y, float w);
#endif
};

} // namespace fw
} // namespace ui
} // namespace yawn
