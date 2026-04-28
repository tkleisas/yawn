#pragma once
// SessionPanel — UI v2 session grid panel.
//
// Migrated from v1 fw::Widget to fw2::Widget. All paint is
// immediate-mode (no fw2 child widgets), all events are inline
// hit-tests. Double-click detection is handled via an explicit
// onMouseDownWithClicks(e, clickCount) entry point threaded from App
// (the fw2 MouseEvent has no clickCount field; that information lives
// in the fw2 gesture SM but is hidden from raw onMouseDown).

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "app/Project.h"
#include "util/UndoManager.h"

namespace yawn { namespace audio { class AudioEngine; struct Clip; } }
#include "core/Constants.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <functional>
#include <unordered_set>

namespace yawn {
namespace ui {
namespace fw2 {

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

    void init(Project* project, audio::AudioEngine* engine,
              undo::UndoManager* undoMgr = nullptr) {
        m_project = project;
        m_engine  = engine;
        m_undoManager = undoMgr;
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

    // Auto-scroll so the selected clip slot is visible in the grid viewport
    void ensureSelectionVisible() {
        if (!m_project) return;
        float gridX = m_bounds.x + ::yawn::ui::Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + ::yawn::ui::Theme::kTrackHeaderHeight;
        float gridW = m_bounds.w - ::yawn::ui::Theme::kSceneLabelWidth;
        float gridH = m_bounds.h - ::yawn::ui::Theme::kTrackHeaderHeight - kScrollbarH;
        (void)gridX; (void)gridY;

        float slotX = m_selectedTrack * ::yawn::ui::Theme::kTrackWidth;
        float slotY = m_selectedScene * ::yawn::ui::Theme::kClipSlotHeight;

        // Horizontal
        if (slotX < m_scrollX)
            m_scrollX = slotX;
        else if (slotX + ::yawn::ui::Theme::kTrackWidth > m_scrollX + gridW)
            m_scrollX = slotX + ::yawn::ui::Theme::kTrackWidth - gridW;

        // Vertical
        if (slotY < m_scrollY)
            m_scrollY = slotY;
        else if (slotY + ::yawn::ui::Theme::kClipSlotHeight > m_scrollY + gridH)
            m_scrollY = slotY + ::yawn::ui::Theme::kClipSlotHeight - gridH;

        float maxScrollX = std::max(0.0f,
            m_project->numTracks() * ::yawn::ui::Theme::kTrackWidth - gridW);
        float maxScrollY = std::max(0.0f,
            m_project->numScenes() * ::yawn::ui::Theme::kClipSlotHeight - gridH);
        m_scrollX = std::clamp(m_scrollX, 0.0f, maxScrollX);
        m_scrollY = std::clamp(m_scrollY, 0.0f, maxScrollY);
        if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
    }

    void setGlobalRecordArmed(bool armed) { m_globalRecordArmed = armed; }
    void setTrackRecording(int trackIndex, bool recording, int recordingScene = -1) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackStates[trackIndex].recording = recording;
            m_trackStates[trackIndex].recordingScene = recording ? recordingScene : -1;
        }
    }
    void updateAnimTimer(float dt) { m_animTimer += dt; }

    // ─── Controller grid region ─────────────────────────────────────────
    void toggleGridRegion()        { m_showGridRegion = !m_showGridRegion; }
    bool gridRegionVisible() const { return m_showGridRegion; }

    void setControllerGridRegion(int trackOff, int sceneOff, bool active) {
        m_gridOriginTrack = trackOff;
        m_gridOriginScene = sceneOff;
        m_showGridRegion  = active;
    }

    void moveGridRegion(int dTrack, int dScene) {
        if (!m_project) return;
        m_gridOriginTrack = std::clamp(m_gridOriginTrack + dTrack,
            0, std::max(0, m_project->numTracks() - m_gridCols));
        m_gridOriginScene = std::clamp(m_gridOriginScene + dScene,
            0, std::max(0, m_project->numScenes() - m_gridRows));
    }

    int gridOriginTrack() const { return m_gridOriginTrack; }
    int gridOriginScene() const { return m_gridOriginScene; }
    int gridCols() const        { return m_gridCols; }
    int gridRows() const        { return m_gridRows; }

    float preferredHeight() const {
        int scenes = m_project
            ? std::min(m_project->numScenes(), kVisibleScenes) : kVisibleScenes;
        return ::yawn::ui::Theme::kTrackHeaderHeight
             + scenes * ::yawn::ui::Theme::kClipSlotHeight;
    }

    // ─── Scroll callback ───────────────────────────────────────────────
    void setOnScrollChanged(std::function<void(float)> cb) { m_onScrollChanged = std::move(cb); }

    // ─── Track rename ──────────────────────────────────────────────────
    using RenameCallback = std::function<void(int track, const std::string& oldName, const std::string& newName)>;
    void setOnTrackRenamed(RenameCallback cb) { m_onTrackRenamed = std::move(cb); }

    // ─── Visual clip launch (loads a shader) ───────────────────────────
    // App owns VisualEngine; SessionPanel just asks it to switch shaders
    // via this callback when a visual clip is launched.
    using VisualLaunchCallback = std::function<void(int track, int scene, const std::string& shaderPath)>;
    void setOnLaunchVisualClip(VisualLaunchCallback cb) { m_onLaunchVisualClip = std::move(cb); }

    // Fired when the user clicks an active visual clip (stop gesture)
    // or scene-launches over an empty slot on a visual track. App
    // wires it to VisualEngine::clearLayer + launch-state reset.
    using VisualStopCallback = std::function<void(int track)>;
    void setOnStopVisualClip(VisualStopCallback cb) { m_onStopVisualClip = std::move(cb); }

    // ─── Live input status query ───────────────────────────────────────
    // App wires this so the grid can colour the per-clip "LIVE" pip by
    // the engine's current connection state. Return values encode
    // LiveVideoSource::State:
    //   0 = Stopped, 1 = Connecting, 2 = Connected, 3 = Failed.
    // Return -1 if the clip at (track, scene) isn't the launched one on
    // its track (so we paint a neutral grey indicator instead).
    using VisualLiveStateCallback = std::function<int(int track, int scene)>;
    void setOnQueryLiveState(VisualLiveStateCallback cb) {
        m_onQueryLiveState = std::move(cb);
    }

    // ─── Importing overlay (for background video transcodes) ───────────
    void setSlotImporting(int track, int scene, bool importing) {
        int64_t key = static_cast<int64_t>(track) * 10000 + scene;
        if (importing) m_importingSlots.insert(key);
        else         { m_importingSlots.erase(key); m_importProgress.erase(key); }
    }
    bool isSlotImporting(int track, int scene) const {
        int64_t key = static_cast<int64_t>(track) * 10000 + scene;
        return m_importingSlots.count(key) > 0;
    }
    void setSlotImportProgress(int track, int scene, float progress) {
        int64_t key = static_cast<int64_t>(track) * 10000 + scene;
        m_importProgress[key] = progress;
    }
    float slotImportProgress(int track, int scene) const {
        int64_t key = static_cast<int64_t>(track) * 10000 + scene;
        auto it = m_importProgress.find(key);
        return it != m_importProgress.end() ? it->second : 0.0f;
    }

    void startTrackRename(int track) {
        if (!m_project || track < 0 || track >= m_project->numTracks()) return;
        m_renameTrack = track;
        m_renameText = m_project->track(track).name;
        m_renameSavedText = m_renameText;
        m_renameCursor = static_cast<int>(m_renameText.size());
    }

    void cancelTrackRename() { m_renameTrack = -1; }

    bool isRenamingTrack() const { return m_renameTrack >= 0; }

    bool handleRenameKeyDown(int keyCode) {
        if (m_renameTrack < 0) return false;
        if (keyCode == 0x0D || keyCode == 0x4000009C) { commitRename(); return true; }
        if (keyCode == 0x1B) { cancelTrackRename(); return true; }
        if (keyCode == 0x08 && m_renameCursor > 0) {
            int len = ::yawn::ui::utf8PrevCharOffset(m_renameText, m_renameCursor);
            m_renameText.erase(m_renameCursor - len, len);
            m_renameCursor -= len;
            return true;
        }
        if (keyCode == 0x7F && m_renameCursor < static_cast<int>(m_renameText.size())) {
            int len = ::yawn::ui::utf8CharLen(&m_renameText[m_renameCursor]);
            m_renameText.erase(m_renameCursor, len);
            return true;
        }
        return true;
    }

    bool handleRenameTextInput(const char* text) {
        if (m_renameTrack < 0) return false;
        m_renameText.insert(m_renameCursor, text);
        m_renameCursor += static_cast<int>(std::strlen(text));
        return true;
    }

    // ─── Slot query (used by App for double-click → piano roll) ─────────

    bool getSlotAt(float mx, float my, int& trackOut, int& sceneOut) const {
        if (!m_project) return false;
        float gridX = m_bounds.x + ::yawn::ui::Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + ::yawn::ui::Theme::kTrackHeaderHeight;
        if (mx < gridX || my < gridY) return false;
        float cmx = mx + m_scrollX;
        float cmy = my + m_scrollY;
        int track = static_cast<int>((cmx - gridX) / ::yawn::ui::Theme::kTrackWidth);
        int scene = static_cast<int>((cmy - gridY) / ::yawn::ui::Theme::kClipSlotHeight);
        if (track < 0 || track >= m_project->numTracks()) return false;
        if (scene < 0 || scene >= m_project->numScenes()) return false;
        trackOut = track;
        sceneOut = scene;
        return true;
    }

    // Launch or stop the clip at (track, scene). Returns true if action was taken.
    bool launchOrStopSlot(int ti, int si);
    void launchSlotAt(int ti, int si);

    // Public accessors for controller integration
    const ClipSlotUIState& trackState(int track) const {
        static const ClipSlotUIState kEmpty{};
        if (track < 0 || track >= kMaxTracks) return kEmpty;
        return m_trackStates[track];
    }

    // Launch clip slot (handles armed tracks → record, playing → stop, etc.)
    void launchClipSlot(int track, int scene) {
        launchOrStopSlot(track, scene);
    }

    // Launch all clips in a scene row
    void launchScene(int scene);

    // ─── Scroll (forwarded from App) ────────────────────────────────────

    void handleScroll(float dx, float dy) {
        if (!m_project) return;
        float gridH = m_bounds.h - ::yawn::ui::Theme::kTrackHeaderHeight - kScrollbarH;
        float contentH = m_project->numScenes() * ::yawn::ui::Theme::kClipSlotHeight;
        float maxY = std::max(0.0f, contentH - gridH);
        m_scrollY = std::clamp(m_scrollY - dy * 30.0f, 0.0f, maxY);

        float gridW = m_bounds.w - ::yawn::ui::Theme::kSceneLabelWidth;
        float contentW = m_project->numTracks() * ::yawn::ui::Theme::kTrackWidth;
        float maxX = std::max(0.0f, contentW - gridW);
        m_scrollX = std::clamp(m_scrollX - dx * 30.0f, 0.0f, maxX);
        if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
    }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, preferredHeight()});
    }

    void onLayout(Rect, UIContext&) override {
        // Widget::layout() stores m_bounds for us before calling this.
    }

    // ─── Rendering ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override;
#endif

    // ─── Mouse events ───────────────────────────────────────────────────
    //
    // fw2 MouseEvent has no clickCount field (the gesture SM tracks
    // double-clicks internally and surfaces them via onDoubleClick).
    // SessionPanel's click flow is too tangled (drag-start pending,
    // right-click, scrollbar hit, scene-label launch, …) to split into
    // onClick/onDoubleClick overrides cleanly, so App threads the v1
    // click count through onMouseDownWithClicks.

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&) override { return false; }
    bool onMouseDownWithClicks(MouseEvent&, int) { return false; }
#else
    // clickCount is now carried by MouseEvent::clickCount (populated by
    // ContentGridWrapper from SDL event.button.clicks). The old int-param
    // overload is kept so any remaining direct call sites still compile.
    bool onMouseDown(MouseEvent& e) override {
        return onMouseDownWithClicks(e, e.clickCount);
    }
    bool onMouseDownWithClicks(MouseEvent& e, int clickCount);
#endif

    // Track selected by last click (for App to sync selection)
    int lastClickTrack() const { return m_lastClickTrack; }
    int lastClickScene() const { return m_lastClickScene; }
    void clearLastClickTrack() { m_lastClickTrack = -1; m_lastClickScene = -1; }

    // Right-click on clip slot (for App to show context menu)
    int lastRightClickTrack() const { return m_lastRightClickTrack; }
    int lastRightClickScene() const { return m_lastRightClickScene; }
    void clearRightClick() { m_lastRightClickTrack = -1; m_lastRightClickScene = -1; m_rightClickSceneLabel = -1; }

    // Right-click on scene label (for App to show scene context menu)
    int rightClickSceneLabel() const { return m_rightClickSceneLabel; }
    void clearRightClickSceneLabel() { m_rightClickSceneLabel = -1; }

    // Clip drag-and-drop results (for App to handle move/copy)
    bool clipDragCompleted() const { return m_clipDragCompleted; }
    int  dragSourceTrack()   const { return m_dragSourceTrack; }
    int  dragSourceScene()   const { return m_dragSourceScene; }
    int  dragTargetTrack()   const { return m_dragTargetTrack; }
    int  dragTargetScene()   const { return m_dragTargetScene; }
    bool dragIsCopy()        const { return m_clipDragIsCopy; }
    bool isDraggingClip()    const { return m_clipDragging; }
    void clearDragResult()         { m_clipDragCompleted = false; }

    bool onMouseMove(MouseMoveEvent& e) override {
        float mx = e.x, my = e.y;
        if (m_hsbDragging && m_project) {
            float gridW = m_bounds.w - ::yawn::ui::Theme::kSceneLabelWidth;
            float contentW = m_project->numTracks() * ::yawn::ui::Theme::kTrackWidth;
            float maxScroll = std::max(0.0f, contentW - gridW);
            float delta = mx - m_hsbDragStartX;
            float scrollDelta = delta * (contentW / std::max(1.0f, gridW));
            m_scrollX = std::clamp(m_hsbDragStartScroll + scrollDelta, 0.0f, maxScroll);
            if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
            return true;
        }

        // Clip drag: check threshold then track target
        if (m_clipDragPending && !m_clipDragging) {
            float dx = mx - m_dragStartX;
            float dy = my - m_dragStartY;
            if (dx * dx + dy * dy > kDragThreshold * kDragThreshold) {
                m_clipDragging = true;
            }
        }
        if (m_clipDragging && m_project) {
            m_dragCurrentX = mx;
            m_dragCurrentY = my;
            m_clipDragIsCopy = (e.modifiers & ::yawn::ui::fw2::ModifierKey::Ctrl) != 0;
            float gridX = m_bounds.x + ::yawn::ui::Theme::kSceneLabelWidth;
            float gridY = m_bounds.y + ::yawn::ui::Theme::kTrackHeaderHeight;
            float cmx = mx + m_scrollX;
            float cmy = my + m_scrollY;
            int ti = static_cast<int>((cmx - gridX) / ::yawn::ui::Theme::kTrackWidth);
            int si = static_cast<int>((cmy - gridY) / ::yawn::ui::Theme::kClipSlotHeight);
            if (ti >= 0 && ti < m_project->numTracks() &&
                si >= 0 && si < m_project->numScenes()) {
                m_dragTargetTrack = ti;
                m_dragTargetScene = si;
            } else {
                m_dragTargetTrack = -1;
                m_dragTargetScene = -1;
            }
            return true;
        }

        // Track scrollbar hover
        float gridX = m_bounds.x + ::yawn::ui::Theme::kSceneLabelWidth;
        float gridW = m_bounds.w - ::yawn::ui::Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + ::yawn::ui::Theme::kTrackHeaderHeight;
        float gridH = m_bounds.h - ::yawn::ui::Theme::kTrackHeaderHeight - kScrollbarH;
        float sbY = gridY + gridH;
        m_hsbHovered = (my >= sbY && my < sbY + kScrollbarH && mx >= gridX && mx < gridX + gridW);

        // Track hover over clip slot icon zone
        if (m_project && my >= gridY && my < gridY + gridH && mx >= gridX) {
            float cmx = mx + m_scrollX;
            float cmy = my + m_scrollY;
            int ti = static_cast<int>((cmx - gridX) / ::yawn::ui::Theme::kTrackWidth);
            int si = static_cast<int>((cmy - gridY) / ::yawn::ui::Theme::kClipSlotHeight);
            float slotLocalX = cmx - gridX - ti * ::yawn::ui::Theme::kTrackWidth;
            if (ti >= 0 && ti < m_project->numTracks() &&
                si >= 0 && si < m_project->numScenes()) {
                m_hoveredTrack = ti;
                m_hoveredScene = si;
                m_hoveredIcon = (slotLocalX < kIconZoneW + ::yawn::ui::Theme::kSlotPadding);
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

    bool onMouseUp(MouseEvent& e) override {
        if (m_hsbDragging) {
            m_hsbDragging = false;
            releaseMouse();
            return true;
        }
        if (m_clipDragging) {
            // Finalize drag — compute final target
            m_clipDragIsCopy = (e.modifiers & ::yawn::ui::fw2::ModifierKey::Ctrl) != 0;
            if (m_dragTargetTrack >= 0 && m_dragTargetScene >= 0 &&
                !(m_dragTargetTrack == m_dragSourceTrack &&
                  m_dragTargetScene == m_dragSourceScene)) {
                m_clipDragCompleted = true;
            }
            m_clipDragging = false;
            m_clipDragPending = false;
            releaseMouse();
            return true;
        }
        if (m_clipDragPending) {
            // Didn't exceed drag threshold — treat as a select (not launch)
            m_clipDragPending = false;
            releaseMouse();
            return true;
        }
        return false;
    }

private:
    // ─── Track Headers ──────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintTrackHeaders(Renderer2D&, TextMetrics&, float, float, float) {}
#else
    void paintTrackHeaders(Renderer2D& r, TextMetrics& tm, float x, float y, float w);
#endif

    // ─── Scene Labels ───────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintSceneLabels(Renderer2D&, TextMetrics&, float, float, float) {}
#else
    void paintSceneLabels(Renderer2D& r, TextMetrics& tm, float x, float y, float h);
#endif

    // ─── Clip Grid ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintClipGrid(Renderer2D&, TextMetrics&, float, float, float, float) {}
#else
    void paintClipGrid(Renderer2D& r, TextMetrics& tm, float x, float y, float w, float h);
#endif

#ifdef YAWN_TEST_BUILD
    void paintHScrollbar(Renderer2D&, float, float, float) {}
#else
    void paintHScrollbar(Renderer2D& r, float x, float y, float w);
#endif

    static constexpr float kIconZoneW = 18.0f;

#ifdef YAWN_TEST_BUILD
    void paintClipSlot(Renderer2D&, TextMetrics&, int, int, float, float, float, float) {}
#else
    void paintClipSlot(Renderer2D& r, TextMetrics& tm, int ti, int si,
                       float x, float y, float w, float h);
#endif

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;

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
    int   m_rightClickSceneLabel = -1;
    float m_animTimer      = 0.0f;
    bool  m_globalRecordArmed = false;

    // Hover tracking for clip slot icons
    int   m_hoveredTrack = -1;
    int   m_hoveredScene = -1;
    bool  m_hoveredIcon  = false;

    // Controller grid region (for future Push/Move mapping)
    int   m_gridOriginTrack = 0;
    int   m_gridOriginScene = 0;
    int   m_gridCols        = 8;    // controller pad columns
    int   m_gridRows        = 8;    // controller pad rows
    bool  m_showGridRegion  = false;

    // Set of (track*10000 + scene) currently importing a video file.
    std::unordered_set<int64_t> m_importingSlots;
    std::unordered_map<int64_t, float> m_importProgress;

    // Cache of loaded video thumbnails: path → GL texture id. Entries are
    // created lazily in paintClipSlot the first time a slot references a
    // thumbnail. Never freed in MVP — typical sessions have tens of clips.
    mutable std::unordered_map<std::string, unsigned> m_thumbnailCache;
    unsigned getThumbnailTexture(const std::string& path) const;

    // Clip drag-and-drop state
    bool  m_clipDragPending   = false;  // mouse down, waiting to exceed threshold
    bool  m_clipDragging      = false;  // actively dragging a clip
    int   m_dragSourceTrack   = -1;
    int   m_dragSourceScene   = -1;
    int   m_dragTargetTrack   = -1;
    int   m_dragTargetScene   = -1;
    float m_dragStartX        = 0;
    float m_dragStartY        = 0;
    float m_dragCurrentX      = 0;
    float m_dragCurrentY      = 0;
    bool  m_clipDragCompleted = false;
    bool  m_clipDragIsCopy    = false;
    static constexpr float kDragThreshold = 5.0f;

    std::function<void(float)> m_onScrollChanged;
    RenameCallback m_onTrackRenamed;
    VisualLaunchCallback m_onLaunchVisualClip;
    VisualStopCallback   m_onStopVisualClip;
    VisualLiveStateCallback m_onQueryLiveState;

    // Inline track rename state
    int         m_renameTrack = -1;
    std::string m_renameText;
    std::string m_renameSavedText;
    int         m_renameCursor = 0;

    void commitRename() {
        if (m_renameTrack < 0 || !m_project) { m_renameTrack = -1; return; }
        // Trim whitespace
        auto trimmed = m_renameText;
        while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(0, 1);
        while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        if (trimmed.empty()) trimmed = m_renameSavedText;
        m_renameText = trimmed;
        if (m_renameText != m_renameSavedText) {
            m_project->track(m_renameTrack).name = m_renameText;
            if (m_onTrackRenamed)
                m_onTrackRenamed(m_renameTrack, m_renameSavedText, m_renameText);
        }
        m_renameTrack = -1;
    }

    static constexpr float kScrollbarH    = 12.0f;
    static constexpr int kVisibleScenes  = 8;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
