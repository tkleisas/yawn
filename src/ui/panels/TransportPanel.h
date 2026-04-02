#pragma once
// TransportPanel — Standalone transport bar widget.
//
// Ableton-style layout: BPM + TimeSig + TAP left, centered Stop|Play|Record,
// Metronome toggle, position display right-aligned.
// Uses proper triangle/circle renderer primitives for button icons.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "audio/AudioEngine.h"

namespace yawn { class Project; }
#include <cmath>
#include <algorithm>
#include <string>

namespace yawn {
namespace ui {
namespace fw {

class TransportPanel : public Widget {
public:
    TransportPanel() {
        m_tapBtn.setLabel("TAP");
        m_tapBtn.setOnClick([this]() { tapTempo(); });

        m_bpmInput.setRange(20.0f, 999.0f);
        m_bpmInput.setFormat("%.2f");
        m_bpmInput.setSensitivity(0.1f);
        m_bpmInput.setOnChange([this](float v) {
            if (m_engine)
                m_engine->sendCommand(audio::TransportSetBPMMsg{static_cast<double>(v)});
        });

        m_tsNumInput.setRange(1.0f, 32.0f);
        m_tsNumInput.setFormat("%.0f");
        m_tsNumInput.setSensitivity(0.3f);
        m_tsNumInput.setOnChange([this](float v) {
            int val = static_cast<int>(v);
            if (m_engine)
                m_engine->sendCommand(
                    audio::TransportSetTimeSignatureMsg{val, m_transportDenominator});
        });

        m_tsDenInput.setRange(1.0f, 32.0f);
        m_tsDenInput.setFormat("%.0f");
        m_tsDenInput.setSensitivity(0.3f);
        m_tsDenInput.setOnChange([this](float v) {
            int val = static_cast<int>(v);
            if (val <= 1) val = 1;
            else if (val <= 2) val = 2;
            else if (val <= 4) val = 4;
            else if (val <= 8) val = 8;
            else if (val <= 16) val = 16;
            else val = 32;
            if (m_engine)
                m_engine->sendCommand(
                    audio::TransportSetTimeSignatureMsg{m_transportNumerator, val});
        });

        m_posLabel.setAlign(TextAlign::Right);
        m_countInLabel.setAlign(TextAlign::Right);
        m_bpmLabel.setText("BPM");
        m_bpmLabel.setAlign(TextAlign::Left);

        m_metroBtn.setLabel("MET");
        m_metroBtn.setOnClick([this]() {
            m_metronomeOn = !m_metronomeOn;
            if (m_engine)
                m_engine->sendCommand(audio::MetronomeToggleMsg{m_metronomeOn});
        });
    }

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
        if (engine) m_metronomeOn = engine->metronome().enabled();
    }

    // ─── State updates (called from App) ────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void setTransportState(bool, double, double, int = 4, int = 4) {}
#else
    void setTransportState(bool playing, double beats, double bpm,
                           int numerator = 4, int denominator = 4);
#endif

    void setRecordState(bool recording, bool countingIn, double progress) {
        m_recording = recording;
        m_countingIn = countingIn;
        m_countInProgress = progress;
    }

    void setSelectedScene(int scene) { m_selectedScene = scene; }
    void setCountInBars(int bars) { m_countInBars = bars; }

    bool isEditing() const {
        return m_bpmInput.isEditing() || m_tsNumInput.isEditing() || m_tsDenInput.isEditing();
    }

    // ─── Double-click to edit BPM / time signature ──────────────────────

    bool handleDoubleClick(float mx, float my) {
        if (mx >= m_bpmBoxX && mx <= m_bpmBoxX + m_bpmBoxW &&
            my >= m_bpmBoxY && my <= m_bpmBoxY + m_bpmBoxH) {
            m_bpmInput.beginEdit();
            return true;
        }
        if (mx >= m_tsNumBoxX && mx <= m_tsNumBoxX + m_tsNumBoxW &&
            my >= m_tsNumBoxY && my <= m_tsNumBoxY + m_tsNumBoxH) {
            m_tsNumInput.beginEdit();
            return true;
        }
        if (mx >= m_tsDenBoxX && mx <= m_tsDenBoxX + m_tsDenBoxW &&
            my >= m_tsDenBoxY && my <= m_tsDenBoxY + m_tsDenBoxH) {
            m_tsDenInput.beginEdit();
            return true;
        }
        return false;
    }

    bool handleTextInput(const char* text) {
        TextInputEvent ev;
        std::strncpy(ev.text, text, sizeof(ev.text) - 1);
        if (m_bpmInput.isEditing()) return m_bpmInput.onTextInput(ev);
        if (m_tsNumInput.isEditing()) return m_tsNumInput.onTextInput(ev);
        if (m_tsDenInput.isEditing()) return m_tsDenInput.onTextInput(ev);
        return false;
    }

#ifdef YAWN_TEST_BUILD
    bool handleKeyDown(int) { return false; }
#else
    bool handleKeyDown(int keycode);
#endif

    // ─── Measure / Layout ───────────────────────────────────────────────

    float preferredHeight() const { return Theme::kTransportBarHeight; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, Theme::kTransportBarHeight});
    }

#ifdef YAWN_TEST_BUILD
    void layout(const Rect& bounds, const UIContext&) override { m_bounds = bounds; }
#else
    void layout(const Rect& bounds, const UIContext& ctx) override;
#endif

    // ─── Rendering ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    // ─── Mouse events ───────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&) override { return false; }
    bool onMouseMove(MouseMoveEvent&) override { return false; }
#else
    bool onMouseDown(MouseEvent& e) override;
    bool onMouseMove(MouseMoveEvent& e) override;
#endif

    bool onMouseUp(MouseEvent& e) override {
        m_tapBtn.onMouseUp(e);
        m_metroBtn.onMouseUp(e);
        m_bpmInput.onMouseUp(e);
        m_tsNumInput.onMouseUp(e);
        m_tsDenInput.onMouseUp(e);
        return false;
    }

private:
    bool hitTestChild(Widget& child, float mx, float my) {
        auto& b = child.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    bool hitBtn(float bx, float by, float bw, float bh, float mx, float my) {
        return mx >= bx && mx <= bx + bw && my >= by && my <= by + bh;
    }

#ifdef YAWN_TEST_BUILD
    void paintTransportButtons(Renderer2D&) {}
    void tapTempo() {}
#else
    void paintTransportButtons(Renderer2D& r);
    void tapTempo();
#endif

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;
    int                 m_selectedScene = 0;

    bool   m_transportPlaying     = false;
    double m_transportBeats       = 0.0;
    double m_transportBPM         = 120.0;
    int    m_transportNumerator   = 4;
    int    m_transportDenominator = 4;

    // Child widgets
    FwNumberInput m_bpmInput;
    FwNumberInput m_tsNumInput;
    FwNumberInput m_tsDenInput;
    FwButton      m_tapBtn;
    FwButton      m_metroBtn;
    Label         m_posLabel;
    Label         m_countInLabel;
    Label         m_bpmLabel;
    Label         m_slashLabel;

    // Centered transport button positions
    float m_stopBtnX = 0, m_stopBtnY = 0, m_stopBtnW = 0, m_stopBtnH = 0;
    float m_playBtnX = 0, m_playBtnY = 0, m_playBtnW = 0, m_playBtnH = 0;
    float m_recBtnX  = 0, m_recBtnY  = 0, m_recBtnW  = 0, m_recBtnH  = 0;

    // Widget box positions (for double-click detection)
    float m_bpmBoxX = 0, m_bpmBoxY = 0, m_bpmBoxW = 0, m_bpmBoxH = 0;
    float m_tsNumBoxX = 0, m_tsNumBoxY = 0, m_tsNumBoxW = 0, m_tsNumBoxH = 0;
    float m_tsDenBoxX = 0, m_tsDenBoxY = 0, m_tsDenBoxW = 0, m_tsDenBoxH = 0;

    // Hover state
    int m_hoveredBtn = -1;  // 0=stop, 1=play, 2=record, -1=none

    static constexpr int kTapHistorySize = 4;
    double m_tapTimes[kTapHistorySize] = {};
    int    m_tapCount = 0;
    float  m_tapFlash = 0.0f;

    bool   m_recording = false;
    bool   m_countingIn = false;
    double m_countInProgress = 0.0;
    int    m_countInBars = 0;
    float  m_recPulse = 0.0f;
    bool   m_metronomeOn = false;
};

} // namespace fw
} // namespace ui
} // namespace yawn
