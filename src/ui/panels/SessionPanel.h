#pragma once
// SessionPanel — Framework widget replacement for SessionView.
//
// fw::Widget that renders the session grid (transport bar, track headers,
// scene labels, clip slots) and handles click events.  Text input, key
// events and scroll are still forwarded directly from App.cpp because
// the widget tree doesn't dispatch those yet.

#include "ui/framework/Widget.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "app/Project.h"

namespace yawn { namespace audio { class AudioEngine; class Clip; } }
#include "core/Constants.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

struct ClipSlotUIState {
    bool    playing      = false;
    bool    queued       = false;
    bool    recording    = false;
    int     recordingScene = -1;  // which scene is recording (-1 = none)
    int     playingScene = -1;    // which scene is playing (-1 = none)
    int64_t playPosition = 0;
    float   midiPlayFrac = 0.0f;
    bool    isMidiPlaying = false;
};

class SessionPanel : public Widget {
public:
    SessionPanel() = default;

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
    }

    // ─── State updates (called from App) ────────────────────────────────

    void updateClipState(int trackIndex, bool playing, int64_t playPos,
                         int playingScene = -1,
                         bool isMidi = false, double clipLengthBeats = 0.0) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackStates[trackIndex].playing      = playing;
            m_trackStates[trackIndex].playPosition  = playPos;
            m_trackStates[trackIndex].playingScene  = playingScene;
            if (isMidi && clipLengthBeats > 0.0) {
                double beats = static_cast<double>(playPos) / 1000000.0;
                m_trackStates[trackIndex].midiPlayFrac =
                    static_cast<float>(std::fmod(beats, clipLengthBeats) / clipLengthBeats);
                m_trackStates[trackIndex].isMidiPlaying = playing;
            } else {
                m_trackStates[trackIndex].isMidiPlaying = false;
            }
        }
    }

    void setSelectedTrack(int t) { m_selectedTrack = t; }
    int  selectedTrack() const   { return m_selectedTrack; }
    void setSelectedScene(int s) { m_selectedScene = s; }
    int  selectedScene() const   { return m_selectedScene; }
    float scrollX() const        { return m_scrollX; }
    float scrollY() const        { return m_scrollY; }
    void setScrollX(float sx)    { m_scrollX = sx; }

    void setGlobalRecordArmed(bool armed) { m_globalRecordArmed = armed; }
    void setTrackRecording(int trackIndex, bool recording, int recordingScene = -1) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackStates[trackIndex].recording = recording;
            m_trackStates[trackIndex].recordingScene = recording ? recordingScene : -1;
        }
    }
    void updateAnimTimer(float dt) { m_animTimer += dt; }

    float preferredHeight() const {
        int scenes = m_project
            ? std::min(m_project->numScenes(), kVisibleScenes) : kVisibleScenes;
        return Theme::kTrackHeaderHeight
             + scenes * Theme::kClipSlotHeight;
    }

    // ─── Scroll callback ───────────────────────────────────────────────
    void setOnScrollChanged(std::function<void(float)> cb) { m_onScrollChanged = std::move(cb); }

    // ─── Slot query (used by App for double-click → piano roll) ─────────

    bool getSlotAt(float mx, float my, int& trackOut, int& sceneOut) const {
        if (!m_project) return false;
        float gridX = m_bounds.x + Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + Theme::kTrackHeaderHeight;
        if (mx < gridX || my < gridY) return false;
        float cmx = mx + m_scrollX;
        float cmy = my + m_scrollY;
        int track = static_cast<int>((cmx - gridX) / Theme::kTrackWidth);
        int scene = static_cast<int>((cmy - gridY) / Theme::kClipSlotHeight);
        if (track < 0 || track >= m_project->numTracks()) return false;
        if (scene < 0 || scene >= m_project->numScenes()) return false;
        trackOut = track;
        sceneOut = scene;
        return true;
    }

    // ─── Scroll (forwarded from App) ────────────────────────────────────

    void handleScroll(float dx, float dy) {
        if (!m_project) return;
        float gridH = m_bounds.h - Theme::kTrackHeaderHeight - kScrollbarH;
        float contentH = m_project->numScenes() * Theme::kClipSlotHeight;
        float maxY = std::max(0.0f, contentH - gridH);
        m_scrollY = std::clamp(m_scrollY - dy * 30.0f, 0.0f, maxY);

        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        float maxX = std::max(0.0f, contentW - gridW);
        m_scrollX = std::clamp(m_scrollX - dx * 30.0f, 0.0f, maxX);
        if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
    }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, preferredHeight()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    // ─── Mouse events ───────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&) override { return false; }
#else
    bool onMouseDown(MouseEvent& e) override;
#endif

    // Track selected by last click (for App to sync selection)
    int lastClickTrack() const { return m_lastClickTrack; }
    int lastClickScene() const { return m_lastClickScene; }
    void clearLastClickTrack() { m_lastClickTrack = -1; m_lastClickScene = -1; }

    // Right-click on clip slot (for App to show context menu)
    int lastRightClickTrack() const { return m_lastRightClickTrack; }
    int lastRightClickScene() const { return m_lastRightClickScene; }
    void clearRightClick() { m_lastRightClickTrack = -1; m_lastRightClickScene = -1; }

    bool onMouseMove(MouseMoveEvent& e) override {
        float mx = e.x, my = e.y;
        if (m_hsbDragging && m_project) {
            float gridW = m_bounds.w - Theme::kSceneLabelWidth;
            float contentW = m_project->numTracks() * Theme::kTrackWidth;
            float maxScroll = std::max(0.0f, contentW - gridW);
            float delta = mx - m_hsbDragStartX;
            float scrollDelta = delta * (contentW / std::max(1.0f, gridW));
            m_scrollX = std::clamp(m_hsbDragStartScroll + scrollDelta, 0.0f, maxScroll);
            if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
            return true;
        }
        // Track scrollbar hover
        float gridX = m_bounds.x + Theme::kSceneLabelWidth;
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + Theme::kTrackHeaderHeight;
        float gridH = m_bounds.h - Theme::kTrackHeaderHeight - kScrollbarH;
        float sbY = gridY + gridH;
        m_hsbHovered = (my >= sbY && my < sbY + kScrollbarH && mx >= gridX && mx < gridX + gridW);

        // Automation dropdown hover tracking
        if (m_autoModeOpenTrack >= 0 && m_project) {
            int t = m_autoModeOpenTrack;
            float tx = gridX + t * Theme::kTrackWidth - m_scrollX;
            float tw = Theme::kTrackWidth;
            float popupW = 50.0f, itemH = 18.0f;
            float popupX = tx + tw - popupW - 4;
            float popupY = m_bounds.y + Theme::kTrackHeaderHeight;
            if (mx >= popupX && mx < popupX + popupW && my >= popupY && my < popupY + 4 * itemH) {
                m_autoModeHoverItem = static_cast<int>((my - popupY) / itemH);
            } else {
                m_autoModeHoverItem = -1;
            }
        }

        // Track hover over clip slot icon zone
        if (my >= gridY && my < gridY + gridH && mx >= gridX) {
            float cmx = mx + m_scrollX;
            float cmy = my + m_scrollY;
            int ti = static_cast<int>((cmx - gridX) / Theme::kTrackWidth);
            int si = static_cast<int>((cmy - gridY) / Theme::kClipSlotHeight);
            float slotLocalX = cmx - gridX - ti * Theme::kTrackWidth;
            if (ti >= 0 && ti < m_project->numTracks() &&
                si >= 0 && si < m_project->numScenes()) {
                m_hoveredTrack = ti;
                m_hoveredScene = si;
                m_hoveredIcon = (slotLocalX < kIconZoneW + Theme::kSlotPadding);
            } else {
                m_hoveredTrack = -1;
                m_hoveredScene = -1;
                m_hoveredIcon = false;
            }
        } else {
            m_hoveredTrack = -1;
            m_hoveredScene = -1;
            m_hoveredIcon = false;
        }

        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_hsbDragging) {
            m_hsbDragging = false;
            releaseMouse();
            return true;
        }
        return false;
    }

private:
    // ─── Text helper ────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void drawText(Renderer2D&, Font&, const char*, float, float, float, Color) {}
#else
    void drawText(Renderer2D& r, Font& f, const char* text,
                  float x, float y, float scale, Color color);
#endif

#ifdef YAWN_TEST_BUILD
    float drawTextRet(Renderer2D&, Font&, const char*, float x, float, float, Color) { return x; }
#else
    float drawTextRet(Renderer2D& r, Font& f, const char* text,
                      float x, float y, float scale, Color color);
#endif

    // ─── Track Headers ──────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintTrackHeaders(Renderer2D&, Font&, float, float, float) {}
#else
    void paintTrackHeaders(Renderer2D& r, Font& f, float x, float y, float w);
#endif

    // ─── Scene Labels ───────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintSceneLabels(Renderer2D&, Font&, float, float, float) {}
#else
    void paintSceneLabels(Renderer2D& r, Font& f, float x, float y, float h);
#endif

    // ─── Clip Grid ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintClipGrid(Renderer2D&, Font&, float, float, float, float) {}
#else
    void paintClipGrid(Renderer2D& r, Font& f, float x, float y, float w, float h);
#endif

#ifdef YAWN_TEST_BUILD
    void paintHScrollbar(Renderer2D&, float, float, float) {}
#else
    void paintHScrollbar(Renderer2D& r, float x, float y, float w);
#endif

    static constexpr float kIconZoneW = 18.0f;

#ifdef YAWN_TEST_BUILD
    void paintClipSlot(Renderer2D&, Font&, int, int, float, float, float, float) {}
#else
    void paintClipSlot(Renderer2D& r, Font& f, int ti, int si,
                       float x, float y, float w, float h);
#endif

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

    ClipSlotUIState m_trackStates[kMaxTracks] = {};

    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;

    bool  m_hsbDragging      = false;
    bool  m_hsbHovered       = false;
    float m_hsbDragStartX    = 0;
    float m_hsbDragStartScroll = 0;

    int   m_activeScene    = -1;
    int   m_selectedTrack  = 0;
    int   m_selectedScene  = 0;
    int   m_lastClickTrack = -1;
    int   m_lastClickScene = -1;
    int   m_lastRightClickTrack = -1;
    int   m_lastRightClickScene = -1;
    float m_animTimer      = 0.0f;
    bool  m_globalRecordArmed = false;

    // Hover tracking for clip slot icons
    int   m_hoveredTrack = -1;
    int   m_hoveredScene = -1;
    bool  m_hoveredIcon  = false;

    // Automation mode dropdown state
    int   m_autoModeOpenTrack = -1;   // which track's dropdown is open (-1 = none)
    int   m_autoModeHoverItem = -1;   // hovered item in open dropdown (0-3)

    std::function<void(float)> m_onScrollChanged;

    static constexpr float kScrollbarH    = 12.0f;
    static constexpr int kVisibleScenes  = 8;
};

} // namespace fw
} // namespace ui
} // namespace yawn
