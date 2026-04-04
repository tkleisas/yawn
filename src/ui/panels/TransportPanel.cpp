// TransportPanel.cpp — rendering and event implementations.
// Ableton-style layout: BPM+TimeSig left, centered Stop|Play|Record, position right.

#include "TransportPanel.h"
#include "app/Project.h"
#include "../Renderer.h"
#include "../Font.h"
#include <SDL3/SDL.h>

namespace yawn {
namespace ui {
namespace fw {

void TransportPanel::setTransportState(bool playing, double beats, double bpm,
                                       int numerator, int denominator) {
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

bool TransportPanel::handleKeyDown(int keycode) {
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

// ─── Layout ─────────────────────────────────────────────────────────────────

void TransportPanel::layout(const Rect& bounds, const UIContext& ctx) {
    m_bounds = bounds;

    constexpr float btnSize = 36.0f;
    constexpr float btnGap  = 4.0f;
    float y = bounds.y;
    float h = Theme::kTransportBarHeight;
    float btnY = y + (h - btnSize) * 0.5f;
    float boxH = btnSize;

    // ── Left group: BPM + TimeSig + TAP ──
    float lx = bounds.x + 12.0f;

    // BPM input
    float bpmW = 80.0f;
    m_bpmBoxX = lx; m_bpmBoxY = btnY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;
    m_bpmInput.layout(Rect{lx, btnY, bpmW, boxH}, ctx);
    lx += bpmW + 4.0f;

    // BPM label
    m_bpmLabel.layout(Rect{lx, btnY, 32, boxH}, ctx);
    lx += 32 + 8.0f;

    // Separator
    float sep1X = lx;
    lx += 12.0f;

    // Time sig numerator
    float tsBoxW = 28.0f;
    m_tsNumBoxX = lx; m_tsNumBoxY = btnY; m_tsNumBoxW = tsBoxW; m_tsNumBoxH = boxH;
    m_tsNumInput.layout(Rect{lx, btnY, tsBoxW, boxH}, ctx);
    lx += tsBoxW + 2.0f;

    // Slash
    m_slashLabel.setText("/");
    m_slashLabel.layout(Rect{lx, btnY, 10, boxH}, ctx);
    lx += 10 + 2.0f;

    // Denominator
    m_tsDenBoxX = lx; m_tsDenBoxY = btnY; m_tsDenBoxW = tsBoxW; m_tsDenBoxH = boxH;
    m_tsDenInput.layout(Rect{lx, btnY, tsBoxW, boxH}, ctx);
    lx += tsBoxW + 8.0f;

    // Separator
    float sep2X = lx;
    lx += 12.0f;

    // TAP button
    float tapW = 42.0f;
    m_tapBtn.layout(Rect{lx, btnY, tapW, boxH}, ctx);
    lx += tapW + 8.0f;

    // Metronome toggle
    float metW = 42.0f;
    m_metroBtn.layout(Rect{lx, btnY, metW, boxH}, ctx);

    // ── Center group: Stop | Play | Record (centered in full width) ──
    float totalBtnW = 3 * btnSize + 2 * btnGap;
    float centerX = bounds.x + (bounds.w - totalBtnW) * 0.5f;

    m_stopBtnX = centerX;
    m_stopBtnY = btnY;
    m_stopBtnW = btnSize;
    m_stopBtnH = btnSize;

    m_playBtnX = centerX + btnSize + btnGap;
    m_playBtnY = btnY;
    m_playBtnW = btnSize;
    m_playBtnH = btnSize;

    m_recBtnX = centerX + 2 * (btnSize + btnGap);
    m_recBtnY = btnY;
    m_recBtnW = btnSize;
    m_recBtnH = btnSize;

    // ── Right group: Position display ──
    float posW = 160.0f;
    float posX = bounds.x + bounds.w - posW - 12.0f;
    m_posLabel.layout(Rect{posX, btnY, posW, boxH}, ctx);
    m_posLabel.setColor(Theme::transportAccent);
    m_posLabel.setFontScale(0);

    // Count-in indicator
    m_countInLabel.layout(Rect{posX - 60, btnY, 54, boxH}, ctx);
    m_countInLabel.setColor(Theme::textDim);
    m_countInLabel.setFontScale(0);
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void TransportPanel::paint(UIContext& ctx) {
    if (!m_engine) return;
    auto& r = *ctx.renderer;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w;
    float h = Theme::kTransportBarHeight;

    // Background
    r.drawRect(x, y, w, h, Theme::transportBg);
    r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);

    // Transport buttons (center)
    paintTransportButtons(r);

    // Left group widgets
    m_bpmInput.paint(ctx);
    m_bpmLabel.paint(ctx);

    // Separator after BPM
    float sep1X = m_bpmBoxX + m_bpmBoxW + 36.0f;
    r.drawRect(sep1X, y + 8, 1, h - 16, Theme::clipSlotBorder);

    // Time sig
    m_tsNumInput.paint(ctx);
    m_slashLabel.paint(ctx);
    m_tsDenInput.paint(ctx);

    // Separator after time sig
    float sep2X = m_tsDenBoxX + m_tsDenBoxW + 8.0f;
    r.drawRect(sep2X, y + 8, 1, h - 16, Theme::clipSlotBorder);

    // TAP button with flash
    m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
    uint8_t fR = static_cast<uint8_t>(40 + m_tapFlash * 60);
    uint8_t fG = static_cast<uint8_t>(40 + m_tapFlash * 80);
    m_tapBtn.setColor(Color{fR, fG, 50});
    m_tapBtn.paint(ctx);

    // Metronome button
    Color metroBg = m_metronomeOn ? Color{60, 90, 140} : Color{45, 45, 50};
    m_metroBtn.setColor(metroBg);
    m_metroBtn.paint(ctx);

    // Right group
    m_posLabel.paint(ctx);

    // Count-in indicator
    if (m_countInBars > 0) {
        char ciBuf[16];
        std::snprintf(ciBuf, sizeof(ciBuf), "CI:%d", m_countInBars);
        m_countInLabel.setText(ciBuf);
        m_countInLabel.paint(ctx);
    }
}

// ─── Transport Buttons ──────────────────────────────────────────────────────

void TransportPanel::paintTransportButtons(Renderer2D& r) {
    constexpr float cornerR = 4.0f;

    // ── Stop button ──
    {
        bool hovered = (m_hoveredBtn == 0);
        Color bg = m_transportPlaying
            ? (hovered ? Color{48,48,52} : Color{38,38,41})
            : (hovered ? Color{60,60,65} : Color{50,50,55});
        r.drawRoundedRect(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH, cornerR, bg);
        Color ic = m_transportPlaying ? Color{140,140,140} : Color{255,255,255};
        float cx = m_stopBtnX + m_stopBtnW * 0.5f;
        float cy = m_stopBtnY + m_stopBtnH * 0.5f;
        float half = 5.0f;
        r.drawRect(cx - half, cy - half, half * 2, half * 2, ic);
    }

    // ── Play button ──
    {
        bool hovered = (m_hoveredBtn == 1);
        Color bg = m_transportPlaying
            ? (hovered ? Color{35,75,35} : Color{30,60,30})
            : (hovered ? Color{48,48,52} : Color{38,38,41});
        r.drawRoundedRect(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH, cornerR, bg);
        Color ic = m_transportPlaying ? Color{80,230,80} : Color{160,160,160};
        float cx = m_playBtnX + m_playBtnW * 0.5f;
        float cy = m_playBtnY + m_playBtnH * 0.5f;
        float triH = 12.0f;
        float triW = 10.0f;
        r.drawTriangle(cx - triW * 0.4f, cy - triH * 0.5f,
                        cx - triW * 0.4f, cy + triH * 0.5f,
                        cx + triW * 0.6f, cy, ic);
    }

    // ── Record button ──
    {
        bool hovered = (m_hoveredBtn == 2);
        Color bg;
        if (m_countingIn) {
            m_recPulse += 0.1f;
            float pulse = (std::sin(m_recPulse * 6.0f) + 1.0f) * 0.5f;
            bg = Color{static_cast<uint8_t>(120 + static_cast<int>(pulse * 80)), 30, 30};
        } else if (m_recording) {
            bg = hovered ? Color{220,50,50} : Color{200,40,40};
        } else {
            bg = hovered ? Color{75,35,35} : Color{60,30,30};
        }
        r.drawRoundedRect(m_recBtnX, m_recBtnY, m_recBtnW, m_recBtnH, cornerR, bg);

        Color dc = m_recording ? Color{255,100,100} : Color{200,60,60};
        float cx = m_recBtnX + m_recBtnW * 0.5f;
        float cy = m_recBtnY + m_recBtnH * 0.5f;
        r.drawFilledCircle(cx, cy, 6.0f, dc, 20);

        if (m_countingIn) {
            float progW = m_recBtnW * static_cast<float>(m_countInProgress);
            r.drawRect(m_recBtnX, m_recBtnY + m_recBtnH - 3, progW, 3, Color{255,100,100});
        }
    }
}

// ─── Mouse events ───────────────────────────────────────────────────────────

bool TransportPanel::onMouseDown(MouseEvent& e) {
    if (!m_engine) return false;
    float mx = e.x, my = e.y;
    bool rightClick = (e.button == MouseButton::Right);

    // Stop
    if (hitBtn(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH, mx, my)) {
        m_engine->sendCommand(audio::TransportStopMsg{});
        m_engine->sendCommand(audio::TransportSetPositionMsg{0});
        return true;
    }

    // Play
    if (hitBtn(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH, mx, my)) {
        if (m_transportPlaying)
            m_engine->sendCommand(audio::TransportStopMsg{});
        else
            m_engine->sendCommand(audio::TransportPlayMsg{});
        return true;
    }

    // Record
    if (hitBtn(m_recBtnX, m_recBtnY, m_recBtnW, m_recBtnH, mx, my)) {
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
            m_engine->sendCommand(audio::TransportRecordMsg{newState, m_selectedScene});
        }
        return true;
    }

    // Child widgets
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
    if (hitTestChild(m_metroBtn, mx, my)) {
        m_metroBtn.onMouseDown(e);
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

bool TransportPanel::onMouseMove(MouseMoveEvent& e) {
    m_bpmInput.onMouseMove(e);
    m_tsNumInput.onMouseMove(e);
    m_tsDenInput.onMouseMove(e);

    // Update hover state for transport buttons
    float mx = e.x, my = e.y;
    int prev = m_hoveredBtn;
    m_hoveredBtn = -1;
    if (hitBtn(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH, mx, my)) m_hoveredBtn = 0;
    else if (hitBtn(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH, mx, my)) m_hoveredBtn = 1;
    else if (hitBtn(m_recBtnX, m_recBtnY, m_recBtnW, m_recBtnH, mx, my)) m_hoveredBtn = 2;
    return (m_hoveredBtn != prev);
}

void TransportPanel::tapTempo() {
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
        float oldBpm = m_bpmInput.value();
        float newBpm = static_cast<float>(bpm);
        if (m_engine)
            m_engine->sendCommand(audio::TransportSetBPMMsg{bpm});
        m_bpmInput.setValue(newBpm);
        if (m_undoManager) {
            m_undoManager->push({"Tap Tempo",
                [this, oldBpm]{ m_bpmInput.setValue(oldBpm);
                    if (m_engine) m_engine->sendCommand(audio::TransportSetBPMMsg{static_cast<double>(oldBpm)}); },
                [this, newBpm]{ m_bpmInput.setValue(newBpm);
                    if (m_engine) m_engine->sendCommand(audio::TransportSetBPMMsg{static_cast<double>(newBpm)}); },
                "bpm"});
        }
    }
}

} // namespace fw
} // namespace ui
} // namespace yawn
