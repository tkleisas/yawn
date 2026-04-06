#pragma once
// MixerPanel — Framework widget replacement for MixerView.
//
// Uses framework widgets (FwButton, FwFader, MeterWidget, PanWidget,
// ScrollBar, Label, FwDropDown) for all controls. I/O routing section
// shows audio input + mono for Audio tracks, MIDI in/out for MIDI tracks.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "ui/ContextMenu.h"
#include "audio/Mixer.h"
#include "app/Project.h"
#include "automation/AutomationTypes.h"
#include "midi/MidiMapping.h"
#include "util/UndoManager.h"

namespace yawn { namespace audio { class AudioEngine; } }
namespace yawn { namespace midi { class MidiEngine; } }
#include "core/Constants.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

struct MixerMeter {
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class MixerPanel : public Widget {
public:
    MixerPanel() {
        m_mixLabel.setText("MIX");
        m_mixLabel.setColor(Theme::textDim);

        m_ioToggle.setLabel("I/O");
        m_ioToggle.setTextColor(Theme::textDim);
        m_ioToggle.setColor(Theme::clipSlotEmpty);
        m_ioToggle.setOnClick([this]() {
            m_showIO = !m_showIO;
        });

        m_sendToggle.setLabel("S");
        m_sendToggle.setTextColor(Theme::textDim);
        m_sendToggle.setColor(Theme::clipSlotEmpty);
        m_sendToggle.setOnClick([this]() {
            m_showSends = !m_showSends;
        });

        m_scrollbar.setOnScroll([this](float pos) {
            m_scrollX = pos;
            if (m_onScrollChanged) m_onScrollChanged(pos);
        });

        for (int t = 0; t < kMaxTracks; ++t) {
            setupStripCallbacks(t);
        }
    }

    void init(Project* project, audio::AudioEngine* engine,
              midi::MidiEngine* midiEngine = nullptr,
              undo::UndoManager* undoMgr = nullptr) {
        m_project = project;
        m_engine  = engine;
        m_midiEngine = midiEngine;
        m_undoManager = undoMgr;
    }

    void setMidiEngine(midi::MidiEngine* me) { m_midiEngine = me; }
    void setLearnManager(midi::MidiLearnManager* mgr) { m_learnManager = mgr; }

    void updateMeter(int trackIndex, float peakL, float peakR) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackMeters[trackIndex] = {peakL, peakR};
        }
    }

    void setSelectedTrack(int track) { m_selectedTrack = track; }
    int  selectedTrack() const { return m_selectedTrack; }
    bool isDragging() const { return Widget::capturedWidget() != nullptr; }
    float preferredHeight() const { return kMixerHeight; }
    float scrollX() const { return m_scrollX; }
    void setScrollX(float sx) { m_scrollX = sx; }
    void setOnScrollChanged(std::function<void(float)> cb) { m_onScrollChanged = std::move(cb); }
    void setOnTrackArmedChanged(std::function<void(int, bool)> cb) { m_onTrackArmed = std::move(cb); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, kMixerHeight});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&) override { return false; }
#else
    bool onMouseDown(MouseEvent& e) override;
#endif

    bool onMouseMove(MouseMoveEvent& e) override {
        // Context menu hover tracking
        if (m_contextMenu.isOpen()) {
            m_contextMenu.handleMouseMove(e.x, e.y);
            return true;
        }

        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseMove(e);
        }

        // Forward mouse move to open dropdowns for hover tracking
        if (m_project) {
            float x = m_bounds.x;
            float gridX = x + Theme::kSceneLabelWidth;
            float gridW = m_bounds.w - Theme::kSceneLabelWidth;
            for (int t = 0; t < m_project->numTracks(); ++t) {
                float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
                if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
                auto& s = m_strips[t];
                if (m_project->track(t).type == Track::Type::Audio) {
                    s.audioInputDrop.onMouseMove(e);
                } else {
                    s.midiInDrop.onMouseMove(e);
                    s.midiInChDrop.onMouseMove(e);
                    s.midiOutDrop.onMouseMove(e);
                    s.midiOutChDrop.onMouseMove(e);
                }
            }
        }

        float sbY = m_bounds.y + m_bounds.h - kScrollbarH;
        if (e.y >= sbY && e.y < sbY + kScrollbarH) {
            m_scrollbar.onMouseMove(e);
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseUp(e);
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_project) return false;
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        float maxScroll = std::max(0.0f, contentW - gridW);
        m_scrollX = std::clamp(m_scrollX - e.dx * 30.0f, 0.0f, maxScroll);
        m_scrollbar.setScrollPos(m_scrollX);
        if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
        return true;
    }

private:
    struct TrackStrip {
        FwButton stopBtn;
        FwButton muteBtn;
        FwButton soloBtn;
        FwButton armBtn;
        FwButton monBtn;
        FwButton autoBtn;   // Automation mode: Off/Read/Touch/Latch
        FwDropDown audioInputDrop;
        FwButton monoBtn;
        FwDropDown midiInDrop;
        FwDropDown midiInChDrop;
        FwDropDown midiOutDrop;
        FwDropDown midiOutChDrop;
        Label midiRxLabel;
        Label midiTxLabel;
        PanWidget pan;
        FwFader fader;
        MeterWidget meter;
        Label nameLabel;
        Label dbLabel;
        float panDragStart = 0.0f;  // captured on touch start for undo
    };

    bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    void openMidiLearnMenu(float mx, float my,
                           const automation::AutomationTarget& target,
                           float paramMin, float paramMax,
                           std::function<void()> resetAction);

#ifdef YAWN_TEST_BUILD
    void setupStripCallbacks(int) {}
#else
    void setupStripCallbacks(int t);
#endif

#ifdef YAWN_TEST_BUILD
    void paintStrip(UIContext&, int, float, float, float, float) {}
#else
    void paintStrip(UIContext& ctx, int idx, float sx, float stripY,
                     float stripW, float stripH);
#endif

#ifdef YAWN_TEST_BUILD
    void paintAudioIO(UIContext&, TrackStrip&, const Track&, int, float, float, float, float) {}
#else
    void paintAudioIO(UIContext& ctx, TrackStrip& s, const Track& track,
                       int idx, float ioX, float ioW, float ioY, float ioH);
#endif

#ifdef YAWN_TEST_BUILD
    void paintMidiIO(UIContext&, TrackStrip&, const Track&, int, float, float, float, float) {}
#else
    void paintMidiIO(UIContext& ctx, TrackStrip& s, const Track& track,
                      int idx, float ioX, float ioW, float ioY, float ioH);
#endif

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project    = nullptr;
    audio::AudioEngine* m_engine     = nullptr;
    midi::MidiEngine*   m_midiEngine = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;
    midi::MidiLearnManager* m_learnManager = nullptr;
    ContextMenu         m_contextMenu;

    MixerMeter m_trackMeters[kMaxTracks] = {};
    TrackStrip m_strips[kMaxTracks];
    ScrollBar  m_scrollbar;
    Label      m_mixLabel;
    FwButton   m_ioToggle;
    FwButton   m_sendToggle;

    int   m_selectedTrack = 0;
    float m_scrollX       = 0.0f;
    bool  m_showIO        = false;
    bool  m_showSends     = false;

    static constexpr float kMixerHeight  = 420.0f;
    static constexpr float kMeterWidth   = 6.0f;
    static constexpr float kFaderWidth   = 20.0f;
    static constexpr float kButtonHeight = 20.0f;
    static constexpr float kButtonWidth  = 28.0f;
    static constexpr float kIOHeight     = 20.0f;
    static constexpr float kScrollbarH   = 12.0f;

    std::function<void(float)>        m_onScrollChanged;
    std::function<void(int, bool)>    m_onTrackArmed;
};

} // namespace fw
} // namespace ui
} // namespace yawn
