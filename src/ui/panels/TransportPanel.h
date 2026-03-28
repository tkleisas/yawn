#pragma once
// TransportPanel — Standalone transport bar widget.
//
// Refactored to use framework widgets (FwButton, FwNumberInput, Label)
// for BPM, time signature, tap tempo, and position display.
// Play/stop/record buttons still use manual rendering for custom icons.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdlib>
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
        m_bpmInput.setSensitivity(0.5f);
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

        m_posLabel.setAlign(TextAlign::Left);
        m_countInLabel.setAlign(TextAlign::Left);
        m_bpmLabel.setText("BPM");
        m_bpmLabel.setAlign(TextAlign::Left);
    }

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
    }

    // ─── State updates (called from App) ────────────────────────────────

    void setTransportState(bool playing, double beats, double bpm,
                           int numerator = 4, int denominator = 4) {
        m_transportPlaying     = playing;
        m_transportBeats       = beats;
        m_transportBPM         = bpm;
        m_transportNumerator   = numerator;
        m_transportDenominator = denominator;

        m_bpmInput.setValue(static_cast<float>(bpm));
        m_tsNumInput.setValue(static_cast<float>(numerator));
        m_tsDenInput.setValue(static_cast<float>(denominator));

        int bpb  = std::max(1, numerator);
        int bar  = static_cast<int>(beats / bpb) + 1;
        double beatInBar = std::fmod(beats, static_cast<double>(bpb));
        int beat = static_cast<int>(beatInBar) + 1;
        int sub  = static_cast<int>((beatInBar - std::floor(beatInBar)) * 100.0) % 100;
        char posBuf[32];
        std::snprintf(posBuf, sizeof(posBuf), "%d . %d . %02d", bar, beat, sub);
        m_posLabel.setText(posBuf);

        m_countInLabel.setText("");
    }

    void setRecordState(bool recording, bool countingIn, double progress) {
        m_recording = recording;
        m_countingIn = countingIn;
        m_countInProgress = progress;
    }

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

    bool handleKeyDown(int keycode) {
        if (keycode == 13 || keycode == 9) { // Enter or Tab
            if (m_bpmInput.isEditing()) {
                m_bpmInput.commitEdit();
                if (keycode == 9) {
                    m_tsNumInput.beginEdit();
                    return true;
                }
            } else if (m_tsNumInput.isEditing()) {
                m_tsNumInput.commitEdit();
                if (keycode == 9) {
                    m_tsDenInput.beginEdit();
                    return true;
                }
            } else if (m_tsDenInput.isEditing()) {
                m_tsDenInput.commitEdit();
            }
            return true;
        }
        if (keycode == 27) {
            if (m_bpmInput.isEditing()) { m_bpmInput.cancelEdit(); return true; }
            if (m_tsNumInput.isEditing()) { m_tsNumInput.cancelEdit(); return true; }
            if (m_tsDenInput.isEditing()) { m_tsDenInput.cancelEdit(); return true; }
            return true;
        }
        if (keycode == 8) {
            if (m_bpmInput.isEditing()) { KeyEvent e; e.keyCode = keycode; return m_bpmInput.onKeyDown(e); }
            if (m_tsNumInput.isEditing()) { KeyEvent e; e.keyCode = keycode; return m_tsNumInput.onKeyDown(e); }
            if (m_tsDenInput.isEditing()) { KeyEvent e; e.keyCode = keycode; return m_tsDenInput.onKeyDown(e); }
        }
        return true;
    }

    // ─── Measure / Layout ───────────────────────────────────────────────

    float preferredHeight() const { return Theme::kTransportBarHeight; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, Theme::kTransportBarHeight});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;

        constexpr float btnSize = 38.0f;
        constexpr float btnGap  = 2.0f;
        float y = bounds.y;
        float h = Theme::kTransportBarHeight;
        float btnY = y + (h - btnSize) * 0.5f;
        float boxH = btnSize;

        // Stop/Play/Record button positions (manual)
        float x = bounds.x + 8.0f;
        m_stopBtnX = x; m_stopBtnY = btnY; m_stopBtnW = btnSize; m_stopBtnH = btnSize;
        x += btnSize + btnGap;
        m_playBtnX = x; m_playBtnY = btnY; m_playBtnW = btnSize; m_playBtnH = btnSize;
        x += btnSize + btnGap;
        m_recButtonX = x; m_recButtonY = btnY; m_recButtonW = btnSize; m_recButtonH = btnSize;

        // BPM input
        float bpmX = bounds.x + 140.0f, bpmW = 100.0f;
        m_bpmBoxX = bpmX; m_bpmBoxY = btnY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;
        m_bpmInput.layout(Rect{bpmX, btnY, bpmW, boxH}, ctx);

        // BPM label
        m_bpmLabel.layout(Rect{bpmX + bpmW + 4, btnY, 36, boxH}, ctx);

        // Time signature
        float tsX = bpmX + bpmW + 50.0f, tsBoxW = 30.0f;
        m_tsNumBoxX = tsX; m_tsNumBoxY = btnY; m_tsNumBoxW = tsBoxW; m_tsNumBoxH = boxH;
        m_tsNumInput.layout(Rect{tsX, btnY, tsBoxW, boxH}, ctx);

        // Slash
        m_slashLabel.setText("/");
        float slashX = tsX + tsBoxW + 4;
        m_slashLabel.layout(Rect{slashX, btnY, 10, boxH}, ctx);

        // Denominator
        float denX = slashX + 10 + 4;
        m_tsDenBoxX = denX; m_tsDenBoxY = btnY; m_tsDenBoxW = tsBoxW; m_tsDenBoxH = boxH;
        m_tsDenInput.layout(Rect{denX, btnY, tsBoxW, boxH}, ctx);

        // Tap tempo button
        float tapX = denX + tsBoxW + 16.0f, tapW = 48.0f;
        m_tapButtonX = tapX; m_tapButtonY = btnY; m_tapButtonW = tapW; m_tapButtonH = boxH;
        m_tapBtn.layout(Rect{tapX, btnY, tapW, boxH}, ctx);

        // Position display
        float posX = tapX + tapW + 20.0f;
        m_posLabel.layout(Rect{posX, btnY, bounds.w - (posX - bounds.x), boxH}, ctx);
        m_posLabel.setColor(Theme::transportAccent);
        m_posLabel.setFontScale(0);

        // Count-in indicator
        m_countInLabel.layout(Rect{posX + 120, btnY, 60, boxH}, ctx);
        m_countInLabel.setColor(Theme::textDim);
        m_countInLabel.setFontScale(0);
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
        if (!m_engine) return;
        auto& r = *ctx.renderer;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w;
        float h = Theme::kTransportBarHeight;

        r.drawRect(x, y, w, h, Theme::transportBg);
        r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);

        paintTransportButtons(r);

        // Separator after transport buttons
        float sepX = m_recButtonX + m_recButtonW + 10.0f;
        r.drawRect(sepX, y + 6, 1, h - 12, Theme::clipSlotBorder);

        // BPM input widget
        m_bpmInput.paint(ctx);

        // BPM label
        m_bpmLabel.paint(ctx);

        // Time sig inputs
        m_tsNumInput.paint(ctx);
        m_slashLabel.paint(ctx);
        m_tsDenInput.paint(ctx);

        // Tap tempo button (with flash color)
        m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
        uint8_t fR = static_cast<uint8_t>(40 + m_tapFlash * 60);
        uint8_t fG = static_cast<uint8_t>(40 + m_tapFlash * 80);
        m_tapBtn.setColor(Color{fR, fG, 50});
        m_tapBtn.paint(ctx);

        // Separator after tempo section
        float sep2X = m_tapButtonX + m_tapButtonW + 10.0f;
        r.drawRect(sep2X, y + 6, 1, h - 12, Theme::clipSlotBorder);

        // Position display
        m_posLabel.paint(ctx);

        // Count-in indicator
        if (m_countInBars > 0) {
            char ciBuf[16];
            std::snprintf(ciBuf, sizeof(ciBuf), "CI:%d", m_countInBars);
            m_countInLabel.setText(ciBuf);
            m_countInLabel.paint(ctx);
        }
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_engine) return false;
        float mx = e.x, my = e.y;
        bool rightClick = (e.button == MouseButton::Right);

        // Stop button
        if (mx >= m_stopBtnX && mx <= m_stopBtnX + m_stopBtnW &&
            my >= m_stopBtnY && my <= m_stopBtnY + m_stopBtnH) {
            m_engine->sendCommand(audio::TransportStopMsg{});
            m_engine->sendCommand(audio::TransportSetPositionMsg{0});
            return true;
        }

        // Play button
        if (mx >= m_playBtnX && mx <= m_playBtnX + m_playBtnW &&
            my >= m_playBtnY && my <= m_playBtnY + m_playBtnH) {
            if (m_transportPlaying)
                m_engine->sendCommand(audio::TransportStopMsg{});
            else
                m_engine->sendCommand(audio::TransportPlayMsg{});
            return true;
        }

        // Record button
        if (mx >= m_recButtonX && mx <= m_recButtonX + m_recButtonW &&
            my >= m_recButtonY && my <= m_recButtonY + m_recButtonH) {
            if (rightClick) {
                static const int countInValues[] = {0, 1, 2, 4};
                int idx = 0;
                for (int i = 0; i < 4; ++i) {
                    if (countInValues[i] == m_countInBars) { idx = (i + 1) % 4; break; }
                }
                m_countInBars = countInValues[idx];
                m_engine->sendCommand(audio::TransportSetCountInMsg{m_countInBars});
            } else {
                bool newState = !m_recording;
                m_engine->sendCommand(audio::TransportRecordMsg{newState});
            }
            return true;
        }

        // Try child widgets
        if (m_bpmInput.isEditing() || hitTestChild(m_bpmInput, mx, my)) {
            m_bpmInput.onMouseDown(e);
            return true;
        }
        if (m_tsNumInput.isEditing() || hitTestChild(m_tsNumInput, mx, my)) {
            m_tsNumInput.onMouseDown(e);
            return true;
        }
        if (m_tsDenInput.isEditing() || hitTestChild(m_tsDenInput, mx, my)) {
            m_tsDenInput.onMouseDown(e);
            return true;
        }
        if (hitTestChild(m_tapBtn, mx, my)) {
            m_tapBtn.onMouseDown(e);
            return true;
        }

        // Click anywhere dismisses edit mode
        if (isEditing()) {
            if (m_bpmInput.isEditing()) m_bpmInput.commitEdit();
            if (m_tsNumInput.isEditing()) m_tsNumInput.commitEdit();
            if (m_tsDenInput.isEditing()) m_tsDenInput.commitEdit();
        }
        return true;
    }

    bool onMouseUp(MouseEvent& e) override {
        m_tapBtn.onMouseUp(e);
        m_bpmInput.onMouseUp(e);
        m_tsNumInput.onMouseUp(e);
        m_tsDenInput.onMouseUp(e);
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        m_bpmInput.onMouseMove(e);
        m_tsNumInput.onMouseMove(e);
        m_tsDenInput.onMouseMove(e);
        return false;
    }

private:
    bool hitTestChild(Widget& child, float mx, float my) {
        auto& b = child.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    void paintTransportButtons(Renderer2D& r) {
        constexpr float btnSize = 38.0f;
        float x = m_bounds.x + 8.0f;
        float y = m_bounds.y;
        float h = Theme::kTransportBarHeight;
        float btnY = y + (h - btnSize) * 0.5f;

        // Stop button
        {
            Color bg = m_transportPlaying ? Color{38,38,41} : Color{50,50,55};
            r.drawRect(m_stopBtnX, btnY, btnSize, btnSize, bg);
            r.drawRectOutline(m_stopBtnX, btnY, btnSize, btnSize, Theme::clipSlotBorder);
            Color ic = m_transportPlaying ? Color{120,120,120} : Color{255,255,255};
            float cx = m_stopBtnX + (btnSize - 10) * 0.5f;
            float cy = btnY + (btnSize - 10) * 0.5f;
            r.drawRect(cx, cy, 10, 10, ic);
        }

        // Play button
        {
            Color bg = m_transportPlaying ? Color{30,60,30} : Color{38,38,41};
            r.drawRect(m_playBtnX, btnY, btnSize, btnSize, bg);
            r.drawRectOutline(m_playBtnX, btnY, btnSize, btnSize, Theme::clipSlotBorder);
            Color ic = m_transportPlaying ? Color{80,230,80} : Color{120,120,120};
            float triCx = m_playBtnX + btnSize * 0.5f - 1.0f;
            float triCy = btnY + btnSize * 0.5f;
            r.drawRect(triCx - 4, triCy - 6, 2, 12, ic);
            r.drawRect(triCx - 2, triCy - 5, 2, 10, ic);
            r.drawRect(triCx,     triCy - 4, 2,  8, ic);
            r.drawRect(triCx + 2, triCy - 3, 2,  6, ic);
            r.drawRect(triCx + 4, triCy - 2, 2,  4, ic);
            r.drawRect(triCx + 6, triCy - 1, 2,  2, ic);
        }

        // Record button
        {
            Color bg = m_recording ? Color{200,40,40} : Color{60,30,30};
            if (m_countingIn) {
                m_recPulse += 0.1f;
                float pulse = (std::sin(m_recPulse * 6.0f) + 1.0f) * 0.5f;
                bg = Color{static_cast<uint8_t>(120 + static_cast<int>(pulse * 80)), 30, 30};
            }
            r.drawRect(m_recButtonX, btnY, btnSize, btnSize, bg);
            r.drawRectOutline(m_recButtonX, btnY, btnSize, btnSize,
                m_recording ? Color{255,80,80} : Theme::clipSlotBorder);
            Color dc = m_recording ? Color{255,100,100} : Color{200,60,60};
            float cx = m_recButtonX + btnSize * 0.5f;
            float cy = btnY + btnSize * 0.5f;
            r.drawRect(cx - 3, cy - 5, 6,  1, dc);
            r.drawRect(cx - 4, cy - 4, 8,  1, dc);
            r.drawRect(cx - 5, cy - 3, 10, 6, dc);
            r.drawRect(cx - 4, cy + 3, 8,  1, dc);
            r.drawRect(cx - 3, cy + 4, 6,  1, dc);
            if (m_countingIn) {
                float progW = btnSize * static_cast<float>(m_countInProgress);
                r.drawRect(m_recButtonX, btnY + btnSize - 3, progW, 3, Color{255,100,100});
            }
        }
    }

    void tapTempo() {
        double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
        m_tapFlash = 1.0f;
        if (m_tapCount > 0) {
            double last = m_tapTimes[(m_tapCount - 1) % kTapHistorySize];
            if (now - last > 2.0) m_tapCount = 0;
        }
        m_tapTimes[m_tapCount % kTapHistorySize] = now;
        m_tapCount++;
        if (m_tapCount >= 2) {
            int n = std::min(m_tapCount - 1, kTapHistorySize - 1);
            int si = m_tapCount - n - 1;
            double total = m_tapTimes[(m_tapCount - 1) % kTapHistorySize]
                         - m_tapTimes[si % kTapHistorySize];
            double avg = total / n;
            double bpm = std::clamp(std::round(60.0 / avg * 100.0) / 100.0, 20.0, 999.0);
            if (m_engine)
                m_engine->sendCommand(audio::TransportSetBPMMsg{bpm});
        }
    }

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

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
    Label         m_posLabel;
    Label         m_countInLabel;
    Label         m_bpmLabel;
    Label         m_slashLabel;

    // Transport button positions (manual)
    float m_stopBtnX = 0, m_stopBtnY = 0, m_stopBtnW = 0, m_stopBtnH = 0;
    float m_playBtnX = 0, m_playBtnY = 0, m_playBtnW = 0, m_playBtnH = 0;
    float m_recButtonX = 0, m_recButtonY = 0, m_recButtonW = 0, m_recButtonH = 0;

    // Widget box positions (for double-click detection in App)
    float m_bpmBoxX = 0, m_bpmBoxY = 0, m_bpmBoxW = 0, m_bpmBoxH = 0;
    float m_tsNumBoxX = 0, m_tsNumBoxY = 0, m_tsNumBoxW = 0, m_tsNumBoxH = 0;
    float m_tsDenBoxX = 0, m_tsDenBoxY = 0, m_tsDenBoxW = 0, m_tsDenBoxH = 0;
    float m_tapButtonX = 0, m_tapButtonY = 0, m_tapButtonW = 0, m_tapButtonH = 0;

    static constexpr int kTapHistorySize = 4;
    double m_tapTimes[kTapHistorySize] = {};
    int    m_tapCount = 0;
    float  m_tapFlash = 0.0f;

    bool   m_recording = false;
    bool   m_countingIn = false;
    double m_countInProgress = 0.0;
    int    m_countInBars = 0;
    float  m_recPulse = 0.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
