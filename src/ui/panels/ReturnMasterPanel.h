#pragma once
// ReturnMasterPanel — Returns + Master channel strip panel (UI v2).
//
// Migrated from v1 fw::Widget to fw2::Widget. All children are native
// fw2 widgets (FwToggle, FwPan, FwFader, FwMeter, FwButton) and the
// strip name / dB labels are painted inline. Integration into the v1
// ContentGrid is via `fw::ReturnMasterPanelWrapper` in
// PanelWrappers.h, which bridges v1 mouse events to fw2's dispatch.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/Fader.h"
#include "ui/framework/v2/Meter.h"
#include "ui/framework/v2/Pan.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/UIContext.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "audio/Mixer.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"
#include "core/Constants.h"
#include "util/UndoManager.h"
#include "ui/ContextMenu.h"
#include "automation/AutomationTypes.h"
#include "midi/MidiMapping.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

struct ReturnMeter {
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class ReturnMasterPanel : public Widget {
public:
    ReturnMasterPanel() {
        static const char* retNames[] = {
            "Ret A","Ret B","Ret C","Ret D","Ret E","Ret F","Ret G","Ret H"};

        const Color busCol{100, 180, 255, 255};
        const Color masterCol = ::yawn::ui::Theme::transportAccent;

        m_stopAllBtn.setOnClick([this]() {
            if (!m_engine) return;
            const int numTracks = m_project ? m_project->numTracks() : kMaxTracks;
            for (int t = 0; t < numTracks; ++t) {
                m_engine->sendCommand(audio::StopClipMsg{t});
                m_engine->sendCommand(audio::StopMidiClipMsg{t});
            }
        });

        for (int b = 0; b < kMaxReturnBuses; ++b) {
            auto& rs = m_returnStrips[b];
            m_returnNames[b] = retNames[b];

            rs.muteBtn.setLabel("M");
            rs.muteBtn.setAccentColor(Color{255, 80, 80, 255});
            rs.muteBtn.setOnChange([this, b](bool on) {
                if (!m_engine) return;
                const bool cur = !on;
                m_engine->sendCommand(audio::SetReturnMuteMsg{b, on});
                if (m_undoManager) {
                    m_undoManager->push({"Toggle Return Mute",
                        [this, b, cur]{ m_engine->sendCommand(audio::SetReturnMuteMsg{b, cur}); },
                        [this, b, on]{ m_engine->sendCommand(audio::SetReturnMuteMsg{b, on}); },
                        ""});
                }
            });

            rs.pan.setThumbColor(busCol);
            rs.pan.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnPanMsg{b, v});
            });
            rs.pan.setOnDragEnd([this, b](float oldV, float newV) {
                if (!m_undoManager) return;
                if (oldV == newV) return;
                m_undoManager->push({"Change Return Pan",
                    [this, b, oldV]{ m_returnStrips[b].pan.setValue(oldV);
                        if (m_engine) m_engine->sendCommand(audio::SetReturnPanMsg{b, oldV}); },
                    [this, b, newV]{ m_returnStrips[b].pan.setValue(newV);
                        if (m_engine) m_engine->sendCommand(audio::SetReturnPanMsg{b, newV}); },
                    "return.pan." + std::to_string(b)});
            });

            rs.fader.setRange(0.0f, 2.0f);
            rs.fader.setTrackColor(busCol);
            rs.fader.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnVolumeMsg{b, v});
            });
            rs.fader.setOnDragEnd([this, b](float oldVal, float newVal) {
                if (!m_undoManager) return;
                m_undoManager->push({"Change Return Volume",
                    [this, b, oldVal]{ m_returnStrips[b].fader.setValue(oldVal);
                        if (m_engine) m_engine->sendCommand(audio::SetReturnVolumeMsg{b, oldVal}); },
                    [this, b, newVal]{ m_returnStrips[b].fader.setValue(newVal);
                        if (m_engine) m_engine->sendCommand(audio::SetReturnVolumeMsg{b, newVal}); },
                    "return.vol." + std::to_string(b)});
            });
        }

        m_masterStrip.fader.setRange(0.0f, 2.0f);
        m_masterStrip.fader.setTrackColor(masterCol);
        m_masterStrip.fader.setOnChange([this](float v) {
            if (!m_engine) return;
            m_engine->sendCommand(audio::SetMasterVolumeMsg{v});
        });
        m_masterStrip.fader.setOnDragEnd([this](float oldVal, float newVal) {
            if (!m_undoManager) return;
            m_undoManager->push({"Change Master Volume",
                [this, oldVal]{ m_masterStrip.fader.setValue(oldVal);
                    if (m_engine) m_engine->sendCommand(audio::SetMasterVolumeMsg{oldVal}); },
                [this, newVal]{ m_masterStrip.fader.setValue(newVal);
                    if (m_engine) m_engine->sendCommand(audio::SetMasterVolumeMsg{newVal}); },
                "master.volume"});
        });

        setFocusable(false);
        setRelayoutBoundary(true);
    }

    void init(Project* project, audio::AudioEngine* engine,
              undo::UndoManager* undoMgr = nullptr) {
        m_project = project;
        m_engine  = engine;
        m_undoManager = undoMgr;
    }

    void updateMeter(int trackIndex, float peakL, float peakR) {
        if (trackIndex == -1) {
            m_masterMeter = {peakL, peakR};
        } else if (trackIndex >= -5 && trackIndex <= -2) {
            const int busIdx = -(trackIndex + 2);
            m_returnMeters[busIdx] = {peakL, peakR};
        }
    }

    bool isDragging() const { return Widget::capturedWidget() != nullptr; }

    void setLearnManager(midi::MidiLearnManager* mgr) { m_learnManager = mgr; }

    void setOnReturnClick(std::function<void(int)> cb) { m_onReturnClick = std::move(cb); }
    void setOnMasterClick(std::function<void()> cb)    { m_onMasterClick = std::move(cb); }
    void setOnReturnRightClick(std::function<void(int, float, float)> cb) {
        m_onReturnRightClick = std::move(cb);
    }
    void setOnMasterRightClick(std::function<void(float, float)> cb) {
        m_onMasterRightClick = std::move(cb);
    }
    void setShowReturns(bool show) { m_showReturns = show; }

protected:
    // ─── fw2 Widget overrides ───────────────────────────────────────
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void onLayout(Rect, UIContext&) override {}

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&)     override { return false; }
    bool onMouseMove(MouseMoveEvent&) override { return false; }
    bool onMouseUp(MouseEvent&)       override { return false; }
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
#endif

public:
#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override;
#endif

private:
    struct StripWidgets {
        FwToggle muteBtn;
        FwPan    pan;
        FwFader  fader;
        FwMeter  meter;
    };

#ifdef YAWN_TEST_BUILD
    void paintStripCommon(UIContext&, StripWidgets&,
                           const char*, float, float,
                           float, float, Color,
                           float, float, float,
                           bool) {}
#else
    void paintStripCommon(UIContext& ctx, StripWidgets& sw,
                           const char* name, float x, float y,
                           float w, float h, Color col,
                           float volume, float peakL, float peakR,
                           bool muted);
#endif

#ifdef YAWN_TEST_BUILD
    void paintReturnStrip(UIContext&, int, float, float,
                           float, float) {}
#else
    void paintReturnStrip(UIContext& ctx, int idx, float x, float y,
                           float w, float h);
#endif

#ifdef YAWN_TEST_BUILD
    void paintMasterStrip(UIContext&, float, float,
                           float, float) {}
#else
    void paintMasterStrip(UIContext& ctx, float x, float y,
                           float w, float h);
#endif

#ifndef YAWN_TEST_BUILD
    void openMidiLearnMenu(float mx, float my,
                           const automation::AutomationTarget& target,
                           float paramMin, float paramMax,
                           std::function<void()> resetAction);
#endif

    // ─── Data ───────────────────────────────────────────────────────
    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;

    ReturnMeter m_returnMeters[kMaxReturnBuses] = {};
    ReturnMeter m_masterMeter = {};

    StripWidgets m_returnStrips[kMaxReturnBuses];
    StripWidgets m_masterStrip;
    FwButton     m_stopAllBtn;

    std::string  m_returnNames[kMaxReturnBuses];

    bool m_showReturns = true;
    midi::MidiLearnManager* m_learnManager = nullptr;
    std::function<void(int)> m_onReturnClick;
    std::function<void()>    m_onMasterClick;
    std::function<void(int, float, float)> m_onReturnRightClick;
    std::function<void(float, float)>      m_onMasterRightClick;

    static constexpr float kMeterWidth     = 6.0f;
    static constexpr float kFaderWidth     = 20.0f;
    static constexpr float kButtonHeight   = 22.0f;
    static constexpr float kButtonWidth    = 28.0f;
    static constexpr float kStripPadding   = 2.0f;
    static constexpr float kSeparatorWidth = 2.0f;
    static constexpr float kRetStripW      = 70.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
