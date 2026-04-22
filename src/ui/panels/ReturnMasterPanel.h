#pragma once
// ReturnMasterPanel — Returns + Master channel strip panel.
//
// Uses framework widgets (FwButton, FwFader, MeterWidget, PanWidget, Label)
// for all controls.  Only the separator and strip backgrounds remain manual.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
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

namespace yawn {
namespace ui {
namespace fw {

struct ReturnMeter {
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class ReturnMasterPanel : public Widget {
public:
    ReturnMasterPanel() {
        static const char* retNames[] = {
            "Ret A","Ret B","Ret C","Ret D","Ret E","Ret F","Ret G","Ret H"};

        Color busCol{100, 180, 255};
        Color masterCol = Theme::transportAccent;

        m_stopAllBtn.setOnClick([this]() {
            if (!m_engine) return;
            int numTracks = m_project ? m_project->numTracks() : kMaxTracks;
            for (int t = 0; t < numTracks; ++t) {
                m_engine->sendCommand(audio::StopClipMsg{t});
                m_engine->sendCommand(audio::StopMidiClipMsg{t});
            }
        });

        for (int b = 0; b < kMaxReturnBuses; ++b) {
            auto& rs = m_returnStrips[b];

            rs.nameLabel.setText(retNames[b]);
            rs.nameLabel.setColor(Theme::textPrimary);

            rs.muteBtn.setLabel("M");
            rs.muteBtn.setOnClick([this, b]() {
                if (!m_engine) return;
                bool cur = m_engine->mixer().returnBus(b).muted;
                bool next = !cur;
                m_engine->sendCommand(audio::SetReturnMuteMsg{b, next});
                if (m_undoManager) {
                    m_undoManager->push({"Toggle Return Mute",
                        [this, b, cur]{ m_engine->sendCommand(audio::SetReturnMuteMsg{b, cur}); },
                        [this, b, next]{ m_engine->sendCommand(audio::SetReturnMuteMsg{b, next}); },
                        ""});
                }
            });

            rs.pan.setThumbColor(busCol);
            rs.pan.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnPanMsg{b, v});
            });
            rs.pan.setOnTouch([this, b](bool start) {
                if (!m_undoManager) return;
                if (start) {
                    m_returnPanStart[b] = m_returnStrips[b].pan.value();
                } else {
                    float oldV = m_returnPanStart[b];
                    float newV = m_returnStrips[b].pan.value();
                    m_undoManager->push({"Change Return Pan",
                        [this, b, oldV]{ m_returnStrips[b].pan.setValue(oldV);
                            if (m_engine) m_engine->sendCommand(audio::SetReturnPanMsg{b, oldV}); },
                        [this, b, newV]{ m_returnStrips[b].pan.setValue(newV);
                            if (m_engine) m_engine->sendCommand(audio::SetReturnPanMsg{b, newV}); },
                        "return.pan." + std::to_string(b)});
                }
            });

            rs.fader.setRange(0.0f, 2.0f);
            rs.fader.setTrackColor(busCol);
            rs.fader.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnVolumeMsg{b, v});
            });
            rs.fader.setOnDragEnd([this, b](float oldVal) {
                if (!m_undoManager) return;
                float newVal = m_returnStrips[b].fader.value();
                m_undoManager->push({"Change Return Volume",
                    [this, b, oldVal]{ m_returnStrips[b].fader.setValue(oldVal);
                        if (m_engine) m_engine->sendCommand(audio::SetReturnVolumeMsg{b, oldVal}); },
                    [this, b, newVal]{ m_returnStrips[b].fader.setValue(newVal);
                        if (m_engine) m_engine->sendCommand(audio::SetReturnVolumeMsg{b, newVal}); },
                    "return.vol." + std::to_string(b)});
            });
        }

        m_masterStrip.nameLabel.setText("MASTER");
        m_masterStrip.nameLabel.setColor(Theme::textPrimary);

        m_masterStrip.fader.setRange(0.0f, 2.0f);
        m_masterStrip.fader.setTrackColor(masterCol);
        m_masterStrip.fader.setOnChange([this](float v) {
            if (!m_engine) return;
            m_engine->sendCommand(audio::SetMasterVolumeMsg{v});
        });
        m_masterStrip.fader.setOnDragEnd([this](float oldVal) {
            if (!m_undoManager) return;
            float newVal = m_masterStrip.fader.value();
            m_undoManager->push({"Change Master Volume",
                [this, oldVal]{ m_masterStrip.fader.setValue(oldVal);
                    if (m_engine) m_engine->sendCommand(audio::SetMasterVolumeMsg{oldVal}); },
                [this, newVal]{ m_masterStrip.fader.setValue(newVal);
                    if (m_engine) m_engine->sendCommand(audio::SetMasterVolumeMsg{newVal}); },
                "master.volume"});
        });
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
            int busIdx = -(trackIndex + 2);
            m_returnMeters[busIdx] = {peakL, peakR};
        }
    }

    bool isDragging() const { return Widget::capturedWidget() != nullptr; }

    void setLearnManager(midi::MidiLearnManager* mgr) { m_learnManager = mgr; }

    // Callbacks for opening detail panel on strip click
    void setOnReturnClick(std::function<void(int)> cb) { m_onReturnClick = std::move(cb); }
    void setOnMasterClick(std::function<void()> cb)    { m_onMasterClick = std::move(cb); }

    // Right-click callbacks for showing "Add Effect" context menu
    void setOnReturnRightClick(std::function<void(int, float, float)> cb) { m_onReturnRightClick = std::move(cb); }
    void setOnMasterRightClick(std::function<void(float, float)> cb)     { m_onMasterRightClick = std::move(cb); }

    void setShowReturns(bool show) { m_showReturns = show; }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
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

    bool onMouseMove(MouseMoveEvent& e) override {
        // v1 context menu retired — fw2 handles hover via LayerStack.
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseMove(e);
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseUp(e);
        }
        return false;
    }

private:
    struct StripWidgets {
        FwButton   muteBtn;
        PanWidget  pan;
        FwFader    fader;
        MeterWidget meter;
        Label      nameLabel;
        Label      dbLabel;
    };

    bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

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

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;
    float m_returnPanStart[kMaxReturnBuses] = {};

    ReturnMeter m_returnMeters[kMaxReturnBuses] = {};
    ReturnMeter m_masterMeter = {};

    StripWidgets m_returnStrips[kMaxReturnBuses];
    StripWidgets m_masterStrip;
    FwButton     m_stopAllBtn;

    bool m_showReturns = true;
    midi::MidiLearnManager* m_learnManager = nullptr;
    std::function<void(int)> m_onReturnClick;
    std::function<void()>    m_onMasterClick;
    std::function<void(int, float, float)> m_onReturnRightClick;
    std::function<void(float, float)>      m_onMasterRightClick;

#ifndef YAWN_TEST_BUILD
    void openMidiLearnMenu(float mx, float my,
                           const automation::AutomationTarget& target,
                           float paramMin, float paramMax,
                           std::function<void()> resetAction);
#endif

    static constexpr float kMeterWidth     = 6.0f;
    static constexpr float kFaderWidth     = 20.0f;
    static constexpr float kButtonHeight   = 22.0f;
    static constexpr float kButtonWidth    = 28.0f;
    static constexpr float kStripPadding   = 2.0f;
    static constexpr float kSeparatorWidth = 2.0f;
    static constexpr float kRetStripW      = 70.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
