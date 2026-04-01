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
#include <cstdio>
#include <cmath>
#include <algorithm>

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
                m_engine->sendCommand(audio::SetReturnMuteMsg{b, !cur});
            });

            rs.pan.setThumbColor(busCol);
            rs.pan.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnPanMsg{b, v});
            });

            rs.fader.setRange(0.0f, 2.0f);
            rs.fader.setTrackColor(busCol);
            rs.fader.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnVolumeMsg{b, v});
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
    }

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
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

    ReturnMeter m_returnMeters[kMaxReturnBuses] = {};
    ReturnMeter m_masterMeter = {};

    StripWidgets m_returnStrips[kMaxReturnBuses];
    StripWidgets m_masterStrip;
    FwButton     m_stopAllBtn;

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
