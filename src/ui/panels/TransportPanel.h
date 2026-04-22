#pragma once
// TransportPanel — Standalone transport bar widget.
//
// Ableton-style layout: BPM + TimeSig + TAP left, centered Stop|Play|Record,
// Metronome toggle, position display right-aligned.
// Uses proper triangle/circle renderer primitives for button icons.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/NumberInput.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/V1EventBridge.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "ui/ContextMenu.h"
#include "audio/AudioEngine.h"
#include "automation/AutomationTypes.h"
#include "midi/MidiMapping.h"
#include "util/UndoManager.h"

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
        // v2 FwNumberInput delivers (startValue, endValue) directly.
        m_bpmInput.setOnDragEnd([this](float oldVal, float newVal) {
            if (!m_undoManager) return;
            if (oldVal == newVal) return;
            m_undoManager->push({"Change BPM",
                [this, oldVal]{ m_bpmInput.setValue(oldVal);
                    if (m_engine) m_engine->sendCommand(audio::TransportSetBPMMsg{static_cast<double>(oldVal)}); },
                [this, newVal]{ m_bpmInput.setValue(newVal);
                    if (m_engine) m_engine->sendCommand(audio::TransportSetBPMMsg{static_cast<double>(newVal)}); },
                "bpm"});
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
        m_tsNumInput.setOnDragEnd([this](float oldVal, float newVal) {
            if (!m_undoManager) return;
            int oldNum = static_cast<int>(oldVal);
            int newNum = static_cast<int>(newVal);
            if (oldNum == newNum) return;
            int den = m_transportDenominator;
            m_undoManager->push({"Change Time Signature",
                [this, oldNum, den]{ m_tsNumInput.setValue(static_cast<float>(oldNum));
                    if (m_engine) m_engine->sendCommand(audio::TransportSetTimeSignatureMsg{oldNum, den}); },
                [this, newNum, den]{ m_tsNumInput.setValue(static_cast<float>(newNum));
                    if (m_engine) m_engine->sendCommand(audio::TransportSetTimeSignatureMsg{newNum, den}); },
                ""});
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
        m_tsDenInput.setOnDragEnd([this](float oldVal, float newVal) {
            if (!m_undoManager) return;
            int num = m_transportNumerator;
            int oldDen = static_cast<int>(oldVal);
            int newDen = static_cast<int>(newVal);
            if (oldDen == newDen) return;
            m_undoManager->push({"Change Time Signature",
                [this, num, oldDen]{ m_tsDenInput.setValue(static_cast<float>(oldDen));
                    if (m_engine) m_engine->sendCommand(audio::TransportSetTimeSignatureMsg{num, oldDen}); },
                [this, num, newDen]{ m_tsDenInput.setValue(static_cast<float>(newDen));
                    if (m_engine) m_engine->sendCommand(audio::TransportSetTimeSignatureMsg{num, newDen}); },
                ""});
        });

        m_posLabel.setAlign(TextAlign::Right);
        m_countInLabel.setAlign(TextAlign::Right);
        m_bpmLabel.setText("BPM");
        m_bpmLabel.setAlign(TextAlign::Left);

        m_metroBtn.setLabel("MET");
        m_metroBtn.setOnChange([this](bool on) {
            m_metronomeOn = on;
            if (m_engine)
                m_engine->sendCommand(audio::MetronomeToggleMsg{m_metronomeOn});
        });
    }

    void init(Project* project, audio::AudioEngine* engine,
              undo::UndoManager* undoMgr = nullptr) {
        m_project = project;
        m_engine  = engine;
        m_undoManager = undoMgr;
        if (engine) m_metronomeOn = engine->metronome().enabled();
    }

    // ─── State updates (called from App) ────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void setTransportState(bool, double, double, int = 4, int = 4) {}
#else
    void setTransportState(bool playing, double beats, double bpm,
                           int numerator = 4, int denominator = 4);
#endif

    void setRecordState(bool recording, bool countingIn, double progress,
                        double countInBeats = 0.0) {
        m_recording = recording;
        m_countingIn = countingIn;
        m_countInProgress = progress;
        m_countInBeats = countInBeats;
    }

    void setSelectedScene(int scene) { m_selectedScene = scene; }
    void setCountInBars(int bars) { m_countInBars = bars; }
    void setMetronomeVisualStyle(int style) { m_metroVisualStyle = style; }
    void setLearnManager(midi::MidiLearnManager* lm) { m_learnManager = lm; }

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
        const std::string t = text ? text : "";
        if (m_bpmInput.isEditing())   { m_bpmInput.takeTextInput(t);   return true; }
        if (m_tsNumInput.isEditing()) { m_tsNumInput.takeTextInput(t); return true; }
        if (m_tsDenInput.isEditing()) { m_tsDenInput.takeTextInput(t); return true; }
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
        // All v2 widgets (button, toggle, number inputs) release
        // through the m_v2Dragging path.
        if (m_v2Dragging) {
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, m_v2Dragging->bounds());
            m_v2Dragging->dispatchMouseUp(ev);
            m_v2Dragging = nullptr;
            releaseMouse();
            return true;
        }
        return false;
    }

    // Tap tempo — callable from controller scripts
#ifdef YAWN_TEST_BUILD
    void tapTempo() {}
#else
    void tapTempo();
#endif

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
#else
    void paintTransportButtons(Renderer2D& r);
#endif

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;
    int                 m_selectedScene = 0;

    bool   m_transportPlaying     = false;
    double m_transportBeats       = 0.0;
    double m_transportBPM         = 120.0;
    int    m_transportNumerator   = 4;
    int    m_transportDenominator = 4;

    // Child widgets
    ::yawn::ui::fw2::FwNumberInput m_bpmInput;
    ::yawn::ui::fw2::FwNumberInput m_tsNumInput;
    ::yawn::ui::fw2::FwNumberInput m_tsDenInput;
    ::yawn::ui::fw2::FwButton m_tapBtn;
    ::yawn::ui::fw2::FwToggle m_metroBtn;
    Label         m_posLabel;
    Label         m_countInLabel;
    Label         m_bpmLabel;
    Label         m_slashLabel;

    // Tracks which v2 widget currently owns a drag — set in
    // onMouseDown, used by onMouseMove/Up to forward translated
    // events back to the fw2 gesture SM. Null when no v2 drag.
    ::yawn::ui::fw2::Widget* m_v2Dragging = nullptr;

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
    double m_countInBeats = 0.0;
    int    m_countInBars = 0;
    float  m_recPulse = 0.0f;
    bool   m_metronomeOn = false;
    int    m_metroVisualStyle = 0; // 0=Dots, 1=Beat Number
    float  m_metroDotX = 0.0f;  // X position for visual metronome dots
    float  m_metroDotY = 0.0f;

    // Performance meters
    float  m_cpuLoad = 0.0f;       // smoothed CPU load (0-1)
    float  m_memoryMB = 0.0f;      // smoothed memory in MB
    int    m_meterUpdateCounter = 0;

    // MIDI Learn
    midi::MidiLearnManager* m_learnManager = nullptr;

    void openTransportLearnMenu(float mx, float my,
                                const automation::AutomationTarget& target,
                                float paramMin, float paramMax,
                                std::function<void()> resetAction);
};

} // namespace fw
} // namespace ui
} // namespace yawn
