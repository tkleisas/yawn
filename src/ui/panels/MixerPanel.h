#pragma once
// MixerPanel — UI v2 mixer view.
//
// Migrated from v1 fw::Widget to fw2::Widget. All child controls are
// native fw2 widgets (FwButton, FwToggle, FwDropDown, FwKnob, FwFader,
// FwPan, FwMeter, FwScrollBar) so they dispatch directly through the
// fw2 gesture SM — no V1EventBridge / m_v2Dragging tracking. Labels
// are painted inline via `ctx.textMetrics->drawText`.
//
// Integration into the v1 ContentGrid is via `fw::MixerPanelWrapper`
// (see PanelWrappers.h) which bridges v1 mouse events to fw2's
// dispatchMouse* API.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/Fader.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/Meter.h"
#include "ui/framework/v2/Pan.h"
#include "ui/framework/v2/ScrollBar.h"
#include "ui/framework/v2/UIContext.h"
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
namespace yawn { namespace midi  { class MidiEngine; } }
#include "core/Constants.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>

namespace yawn {
namespace ui {
namespace fw2 {

struct MixerMeter {
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class MixerPanel : public Widget {
public:
    MixerPanel() {
        // Left-margin show-toggles. v2 FwToggle (Button variant) with
        // a soft blue accent so "on" reads as highlighted without
        // being loud.
        const Color kSoftBlue{60, 80, 110, 255};
        m_ioToggle.setLabel("I/O");
        m_ioToggle.setAccentColor(kSoftBlue);
        m_ioToggle.setState(m_showIO);
        m_ioToggle.setOnChange([this](bool on) { m_showIO = on; });

        m_sendToggle.setLabel("S");
        m_sendToggle.setAccentColor(kSoftBlue);
        m_sendToggle.setState(m_showSends);
        m_sendToggle.setOnChange([this](bool on) { m_showSends = on; });

        m_returnToggle.setLabel("R");
        m_returnToggle.setAccentColor(kSoftBlue);
        m_returnToggle.setState(m_showReturns);
        m_returnToggle.setOnChange([this](bool on) {
            m_showReturns = on;
            if (m_onReturnToggle) m_onReturnToggle(on);
        });

        m_scrollbar.setOnScroll([this](float pos) {
            m_scrollX = pos;
            if (m_onScrollChanged) m_onScrollChanged(pos);
        });

        for (int t = 0; t < kMaxTracks; ++t) {
            setupStripCallbacks(t);
        }

        setFocusable(false);
        setRelayoutBoundary(true);
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
    void setOnReturnToggle(std::function<void(bool)> cb) { m_onReturnToggle = std::move(cb); }
    void setOnTrackSelected(std::function<void(int)> cb) { m_onTrackSelected = std::move(cb); }
    bool showReturns() const { return m_showReturns; }

    // Panel constants — used internally + by the wrapper.
    static constexpr float kMixerHeight  = 420.0f;

protected:
    // ─── fw2 Widget overrides ───────────────────────────────────────
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, kMixerHeight});
    }

    void onLayout(Rect, UIContext&) override {}

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&)     override { return false; }
    bool onMouseMove(MouseMoveEvent&) override { return false; }
    bool onMouseUp(MouseEvent&)       override { return false; }
    bool onScroll(ScrollEvent&)       override { return false; }
#else
    bool onMouseDown(MouseEvent& e) override;

    bool onMouseMove(MouseMoveEvent& e) override {
        if (Widget* cap = Widget::capturedWidget()) {
            const auto& b = cap->bounds();
            MouseMoveEvent ev = e;
            ev.lx = e.x - b.x;
            ev.ly = e.y - b.y;
            cap->dispatchMouseMove(ev);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (Widget* cap = Widget::capturedWidget()) {
            const auto& b = cap->bounds();
            MouseEvent ev = e;
            ev.lx = e.x - b.x;
            ev.ly = e.y - b.y;
            cap->dispatchMouseUp(ev);
            return true;
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_project) return false;
        const float gridW = m_bounds.w - ::yawn::ui::Theme::kSceneLabelWidth;
        const float contentW = m_project->numTracks() * ::yawn::ui::Theme::kTrackWidth;
        const float maxScroll = std::max(0.0f, contentW - gridW);
        m_scrollX = std::clamp(m_scrollX - e.dx * 30.0f, 0.0f, maxScroll);
        m_scrollbar.setScrollPos(m_scrollX);
        if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
        return true;
    }
#endif

public:
#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override;
#endif

private:
    struct TrackStrip {
        FwButton  stopBtn;
        FwToggle  muteBtn;
        FwToggle  soloBtn;
        FwToggle  armBtn;
        FwButton  monBtn;   // 3-state cycle Off/In/Auto
        FwButton  autoBtn;  // 4-state cycle Off/Read/Touch/Latch
        FwDropDown audioInputDrop;
        FwToggle  monoBtn;
        FwDropDown midiInDrop;
        FwDropDown midiInChDrop;
        FwDropDown midiOutDrop;
        FwDropDown midiOutChDrop;
        FwDropDown sidechainDrop;
        FwPan     pan;
        FwFader   fader;
        FwMeter   meter;
        FwKnob    sendKnobs[kMaxReturnBuses];
    };

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
    void paintAudioIO(UIContext&, TrackStrip&, const Track&, int,
                       float, float, float, float) {}
#else
    void paintAudioIO(UIContext& ctx, TrackStrip& s, const Track& track,
                       int idx, float ioX, float ioW, float ioY, float ioH);
#endif

#ifdef YAWN_TEST_BUILD
    void paintMidiIO(UIContext&, TrackStrip&, const Track&, int,
                      float, float, float, float) {}
#else
    void paintMidiIO(UIContext& ctx, TrackStrip& s, const Track& track,
                      int idx, float ioX, float ioW, float ioY, float ioH);
#endif

    // ─── Data ───────────────────────────────────────────────────────
    Project*            m_project    = nullptr;
    audio::AudioEngine* m_engine     = nullptr;
    midi::MidiEngine*   m_midiEngine = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;
    midi::MidiLearnManager* m_learnManager = nullptr;

    MixerMeter m_trackMeters[kMaxTracks] = {};
    TrackStrip m_strips[kMaxTracks];
    FwScrollBar m_scrollbar;
    FwToggle    m_ioToggle;
    FwToggle    m_sendToggle;
    FwToggle    m_returnToggle;

    int   m_selectedTrack = 0;
    float m_scrollX       = 0.0f;
    bool  m_showIO        = false;
    bool  m_showSends     = false;
    bool  m_showReturns   = true;

    static constexpr float kMeterWidth   = 6.0f;
    static constexpr float kFaderWidth   = 20.0f;
    static constexpr float kButtonHeight = 20.0f;
    static constexpr float kButtonWidth  = 28.0f;
    static constexpr float kIOHeight     = 20.0f;
    static constexpr float kScrollbarH   = 12.0f;

    std::function<void(float)>        m_onScrollChanged;
    std::function<void(int, bool)>    m_onTrackArmed;
    std::function<void(bool)>         m_onReturnToggle;
    std::function<void(int)>          m_onTrackSelected;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
