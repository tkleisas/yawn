#pragma once
// TransportPanel — standalone transport bar widget (UI v2 version).
//
// Ableton-style layout: BPM + TimeSig + TAP left, centered
// Stop|Play|Record, Metronome toggle, position display right-aligned.
//
// Migrated from v1 fw::Widget to fw2::Widget. Now a proper fw2 widget
// whose children (FwNumberInput, FwButton, FwToggle) use the fw2
// gesture SM directly — no V1EventBridge / m_v2Dragging tracking
// required. Integration into the v1 rootLayout is via
// `fw::TransportPanelWrapper` (see PanelWrappers.h), which bridges
// v1 mouse events into fw2's dispatchMouse* API.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/NumberInput.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/UIContext.h"
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
namespace fw2 {

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

        m_metroBtn.setLabel("MET");
        m_metroBtn.setOnChange([this](bool on) {
            m_metronomeOn = on;
            if (m_engine)
                m_engine->sendCommand(audio::MetronomeToggleMsg{m_metronomeOn});
        });

        // Panel is a container — it doesn't capture gestures itself,
        // its children do. Keep it non-click-only so child dispatch
        // can do its own gesture handling.
        setFocusable(false);
        setRelayoutBoundary(true);
        // Own-dispatch container — see fw2::Widget gotcha note.
        setAutoCaptureOnUnhandledPress(false);
    }

    void init(Project* project, audio::AudioEngine* engine,
              undo::UndoManager* undoMgr = nullptr) {
        m_project = project;
        m_engine  = engine;
        m_undoManager = undoMgr;
        if (engine) m_metronomeOn = engine->metronome().enabled();
    }

    // ─── State updates (called from App) ────────────────────────────
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

    // ─── External commands (App dispatches these from raw SDL) ──────

    // Double-click on BPM / time-sig boxes enters inline edit mode.
    // Called by App's MOUSE_BUTTON_DOWN handler when clicks == 2.
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

    // Tap tempo — callable from controller scripts.
#ifdef YAWN_TEST_BUILD
    void tapTempo() {}
#else
    void tapTempo();
#endif

    float preferredHeight() const { return ::yawn::ui::Theme::kTransportBarHeight; }

protected:
    // ─── fw2 Widget overrides ───────────────────────────────────────
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, ::yawn::ui::Theme::kTransportBarHeight});
    }

#ifdef YAWN_TEST_BUILD
    void onLayout(Rect, UIContext&) override {}
#else
    void onLayout(Rect bounds, UIContext& ctx) override;
#endif

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&)     override { return false; }
    bool onMouseMove(MouseMoveEvent&) override { return false; }
#else
    bool onMouseDown(MouseEvent& e)     override;
    bool onMouseMove(MouseMoveEvent& e) override;
#endif

public:
    // Public render override: paints chrome + delegates to child
    // widgets. Overrides default Widget::render so we can paint the
    // panel background, transport buttons, meters, etc. BEFORE child
    // widgets, then recurse into children at the end.
#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override;
#endif

private:
    bool hitBtn(float bx, float by, float bw, float bh, float mx, float my) const {
        return mx >= bx && mx <= bx + bw && my >= by && my <= by + bh;
    }

#ifdef YAWN_TEST_BUILD
    void paintTransportButtons(Renderer2D&) {}
#else
    void paintTransportButtons(Renderer2D& r);
#endif

    void openTransportLearnMenu(float mx, float my,
                                const automation::AutomationTarget& target,
                                float paramMin, float paramMax,
                                std::function<void()> resetAction);

    // ─── Data ───────────────────────────────────────────────────────
    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;
    undo::UndoManager*  m_undoManager = nullptr;
    int                 m_selectedScene = 0;

    bool   m_transportPlaying     = false;
    double m_transportBeats       = 0.0;
    double m_transportBPM         = 120.0;
    int    m_transportNumerator   = 4;
    int    m_transportDenominator = 4;

    // Child widgets (value-typed). All fw2, no bridging required.
    FwNumberInput m_bpmInput;
    FwNumberInput m_tsNumInput;
    FwNumberInput m_tsDenInput;
    FwButton      m_tapBtn;
    FwToggle      m_metroBtn;

    // Cached label text (generated per-frame).
    std::string m_posText;

    // Audio-engine toggle (far-left of the transport bar).
    float m_audioBtnX = 0, m_audioBtnY = 0, m_audioBtnW = 0, m_audioBtnH = 0;

    // Link sync toggle (right side, before CPU meter).
    float m_linkBtnX = 0, m_linkBtnY = 0, m_linkBtnW = 0, m_linkBtnH = 0;

    // Centered transport button positions.
    float m_homeBtnX = 0, m_homeBtnY = 0, m_homeBtnW = 0, m_homeBtnH = 0;
    float m_stopBtnX = 0, m_stopBtnY = 0, m_stopBtnW = 0, m_stopBtnH = 0;
    float m_playBtnX = 0, m_playBtnY = 0, m_playBtnW = 0, m_playBtnH = 0;
    float m_recBtnX  = 0, m_recBtnY  = 0, m_recBtnW  = 0, m_recBtnH  = 0;

    // BPM/TimeSig box positions — used by handleDoubleClick + MIDI-Learn
    // right-click hit-testing.
    float m_bpmBoxX = 0, m_bpmBoxY = 0, m_bpmBoxW = 0, m_bpmBoxH = 0;
    float m_tsNumBoxX = 0, m_tsNumBoxY = 0, m_tsNumBoxW = 0, m_tsNumBoxH = 0;
    float m_tsDenBoxX = 0, m_tsDenBoxY = 0, m_tsDenBoxW = 0, m_tsDenBoxH = 0;

    // Hover state (transport buttons).
    int m_hoveredBtn = -1;  // 0=stop, 1=play, 2=record, 3=home, -1=none

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
    float  m_metroDotX = 0.0f;
    float  m_metroDotY = 0.0f;

    // Ableton Link state (updated per-frame from engine)
    bool m_linkEnabled = false;
    int  m_linkPeers = 0;

    // "Last seen" engine values — see setTransportState's comment for
    // the rubber-banding rationale.
    double m_lastSeenEngineBpm         = 0.0;
    int    m_lastSeenEngineNumerator   = 0;
    int    m_lastSeenEngineDenominator = 0;
    bool   m_lastSeenEngineMetronome   = false;
    bool   m_bpmSynced                 = false;
    bool   m_tsSynced                  = false;
    bool   m_metSynced                 = false;

    // Performance meters (smoothed).
    float  m_cpuLoad = 0.0f;
    float  m_memoryMB = 0.0f;
    int    m_meterUpdateCounter = 0;

    midi::MidiLearnManager* m_learnManager = nullptr;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
