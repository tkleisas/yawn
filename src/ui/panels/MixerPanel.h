#pragma once
// MixerPanel — Framework widget replacement for MixerView.
//
// Uses framework widgets (FwButton, FwFader, MeterWidget, PanWidget,
// ScrollBar, Label, FwDropDown) for all controls. I/O routing section
// shows audio input + mono for Audio tracks, MIDI in/out for MIDI tracks.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/Fader.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/Meter.h"
#include "ui/framework/v2/Pan.h"
#include "ui/framework/v2/ScrollBar.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/V1EventBridge.h"
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

        // Left-margin show-toggles. v2 FwToggle (Button variant) with
        // a soft blue accent so the "on" state reads as highlighted
        // without being loud.
        const Color kSoftBlue{60, 80, 110};
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
        // v1 context menu retired — fw2 handles its own hover via
        // LayerStack dispatch in App::pollEvents. v2 dropdowns also
        // get their popup hover via LayerStack while open — no
        // per-strip forwarding needed.
        // v2 knob drag in progress — forward translated events to
        // the gesture SM. Handled BEFORE v1 capture because v2 widgets
        // don't participate in v1's capturedWidget() mechanism.
        if (m_v2Dragging) {
            auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, m_v2Dragging->bounds());
            m_v2Dragging->dispatchMouseMove(ev);
            return true;
        }
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseMove(e);
        }

        // v2 scrollbar tracks hover via its own painter state; no
        // per-move forward needed. Drag routes through m_v2Dragging.
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_v2Dragging) {
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, m_v2Dragging->bounds());
            m_v2Dragging->dispatchMouseUp(ev);
            m_v2Dragging = nullptr;
            releaseMouse();
            return true;
        }
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseUp(e);
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_project) return false;
        // v2 dropdown popups handle their own wheel-scroll via
        // LayerStack. Panel-level scroll just pans the horizontal
        // track view.
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
        ::yawn::ui::fw2::FwButton stopBtn;
        ::yawn::ui::fw2::FwToggle muteBtn;
        ::yawn::ui::fw2::FwToggle soloBtn;
        ::yawn::ui::fw2::FwToggle armBtn;
        ::yawn::ui::fw2::FwButton monBtn;   // 3-state cycle Off/In/Auto
        ::yawn::ui::fw2::FwButton autoBtn;  // 4-state cycle Off/Read/Touch/Latch
        ::yawn::ui::fw2::FwDropDown audioInputDrop;
        ::yawn::ui::fw2::FwToggle monoBtn;
        ::yawn::ui::fw2::FwDropDown midiInDrop;
        ::yawn::ui::fw2::FwDropDown midiInChDrop;
        ::yawn::ui::fw2::FwDropDown midiOutDrop;
        ::yawn::ui::fw2::FwDropDown midiOutChDrop;
        ::yawn::ui::fw2::FwDropDown sidechainDrop;
        Label midiRxLabel;
        Label midiTxLabel;
        Label sidechainLabel;
        ::yawn::ui::fw2::FwPan pan;
        ::yawn::ui::fw2::FwFader fader;
        ::yawn::ui::fw2::FwMeter meter;
        Label nameLabel;
        Label dbLabel;
        ::yawn::ui::fw2::FwKnob sendKnobs[kMaxReturnBuses];
        // (sendDragStart retired — v2's setOnDragEnd delivers
        // (startValue, endValue) directly, no manual capture needed.)
    };

    // Tracks which v2 widget currently owns a drag. v2 widgets drive
    // their own gesture SM and do NOT participate in v1 capturedWidget()
    // — so the panel keeps its own pointer, sets it on mouseDown, and
    // clears on mouseUp. Null = no v2 drag.
    ::yawn::ui::fw2::Widget* m_v2Dragging = nullptr;

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

    MixerMeter m_trackMeters[kMaxTracks] = {};
    TrackStrip m_strips[kMaxTracks];
    ::yawn::ui::fw2::FwScrollBar m_scrollbar;
    Label      m_mixLabel;
    ::yawn::ui::fw2::FwToggle m_ioToggle;
    ::yawn::ui::fw2::FwToggle m_sendToggle;
    ::yawn::ui::fw2::FwToggle m_returnToggle;

    int   m_selectedTrack = 0;
    float m_scrollX       = 0.0f;
    bool  m_showIO        = false;
    bool  m_showSends     = false;
    bool  m_showReturns   = true;

    static constexpr float kMixerHeight  = 420.0f;
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

} // namespace fw
} // namespace ui
} // namespace yawn
