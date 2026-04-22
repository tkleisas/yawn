// TransportPanel.cpp — rendering and event implementations.
// Ableton-style layout: BPM+TimeSig left, centered Stop|Play|Record, position right.

#include "TransportPanel.h"
#include "app/Project.h"
#include "../Renderer.h"
#include "../Font.h"
#include "ui/framework/v2/V1MenuBridge.h"
#include "util/SystemInfo.h"
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

    // Sync from engine — but ONLY when the engine value CHANGED since
    // the last frame. The tap/metronome feedback loop happens because
    // sendCommand is async: when the UI sets BPM=180 and sends a
    // command, the engine still reports bpm()=120 for 1-2 frames
    // until the queue drains. If we unconditionally `setValue(120)`
    // each frame, it (a) visually reverts the tap and (b) fires
    // onChange → sendCommand(120), overwriting the tap at the audio
    // thread too. Tracking `last seen engine value` means we only
    // push engine → UI when some OTHER source moved the engine
    // (automation, MIDI controller, MIDI Learn, scripting). Between
    // those events the widget's own state wins.
    //
    // Use ::yawn::ui::fw2::ValueChangeSource::Automation belt-and-suspenders so even
    // if a sync does fire, it doesn't feedback-send another command.
    if (bpm != m_lastSeenEngineBpm || !m_bpmSynced) {
        m_bpmInput.setValue(static_cast<float>(bpm), ::yawn::ui::fw2::ValueChangeSource::Automation);
        m_lastSeenEngineBpm = bpm;
        m_bpmSynced = true;
    }
    if (numerator != m_lastSeenEngineNumerator || !m_tsSynced) {
        m_tsNumInput.setValue(static_cast<float>(numerator), ::yawn::ui::fw2::ValueChangeSource::Automation);
        m_lastSeenEngineNumerator = numerator;
    }
    if (denominator != m_lastSeenEngineDenominator || !m_tsSynced) {
        m_tsDenInput.setValue(static_cast<float>(denominator), ::yawn::ui::fw2::ValueChangeSource::Automation);
        m_lastSeenEngineDenominator = denominator;
    }
    m_tsSynced = true;

    // Metronome sync — same approach.
    if (m_engine) {
        const bool engineMet = m_engine->metronome().enabled();
        if (engineMet != m_lastSeenEngineMetronome || !m_metSynced) {
            m_metronomeOn = engineMet;
            m_lastSeenEngineMetronome = engineMet;
            m_metSynced = true;
        }
    }

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
    // SDL keycodes: 13 = Enter, 27 = Escape, 8 = Backspace, 9 = Tab.
    // v2 FwNumberInput absorbs Enter/Escape/Backspace internally; Tab
    // is panel-level policy (move focus to the next field) so we
    // handle it here.
    auto* editing = m_bpmInput.isEditing()   ? &m_bpmInput
                  : m_tsNumInput.isEditing() ? &m_tsNumInput
                  : m_tsDenInput.isEditing() ? &m_tsDenInput
                  : nullptr;
    if (!editing) return false;

    if (keycode == 9) {
        // Tab: commit current field, jump to the next in the chain.
        editing->endEdit(/*commit*/true);
        if (editing == &m_bpmInput)        m_tsNumInput.beginEdit();
        else if (editing == &m_tsNumInput) m_tsDenInput.beginEdit();
        // Else: already on the last field — just commit, no advance.
        return true;
    }

    ::yawn::ui::fw2::KeyEvent ke;
    switch (keycode) {
        case 27: ke.key = ::yawn::ui::fw2::Key::Escape;    break;
        case 13: ke.key = ::yawn::ui::fw2::Key::Enter;     break;
        case 8:  ke.key = ::yawn::ui::fw2::Key::Backspace; break;
        default: return true;   // swallow other keys while editing
    }
    editing->dispatchKeyDown(ke);
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

    // All numeric inputs + the TAP/MET buttons are v2 widgets; layout
    // them against the fw2 UIContext.
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();

    // BPM input
    float bpmW = 80.0f;
    m_bpmBoxX = lx; m_bpmBoxY = btnY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;
    m_bpmInput.layout(Rect{lx, btnY, bpmW, boxH}, v2ctx);
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
    m_tsNumInput.layout(Rect{lx, btnY, tsBoxW, boxH}, v2ctx);
    lx += tsBoxW + 2.0f;

    // Slash
    m_slashLabel.setText("/");
    m_slashLabel.layout(Rect{lx, btnY, 10, boxH}, ctx);
    lx += 10 + 2.0f;

    // Denominator
    m_tsDenBoxX = lx; m_tsDenBoxY = btnY; m_tsDenBoxW = tsBoxW; m_tsDenBoxH = boxH;
    m_tsDenInput.layout(Rect{lx, btnY, tsBoxW, boxH}, v2ctx);
    lx += tsBoxW + 8.0f;

    // Separator
    float sep2X = lx;
    lx += 12.0f;

    // TAP button — v2 FwButton, layout via fw2 UIContext.
    float tapW = 42.0f;
    m_tapBtn.layout(Rect{lx, btnY, tapW, boxH}, v2ctx);
    lx += tapW + 8.0f;

    // Metronome toggle — v2 FwToggle.
    float metW = (m_countInBars > 0) ? 62.0f : 42.0f;
    m_metroBtn.layout(Rect{lx, btnY, metW, boxH}, v2ctx);
    m_metroDotX = lx + metW + 6.0f;
    m_metroDotY = btnY + boxH * 0.5f;

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

    // CC labels for mapped transport controls
    if (m_learnManager) {
        auto& font = *ctx.font;
        float ccScale = 7.0f / Theme::kFontSize;
        Color ccCol{100, 180, 255};

        auto drawCCLabel = [&](float bx, float by, float bw, float bh,
                               const automation::AutomationTarget& target) {
            auto* mapping = m_learnManager->findByTarget(target);
            if (mapping) {
                auto lbl = mapping->label();
                float tw = font.textWidth(lbl.c_str(), ccScale);
                float labelH = Theme::kFontSize * ccScale;
                font.drawText(r, lbl.c_str(), bx + (bw - tw) * 0.5f, by - labelH - 1, ccScale, ccCol);
            }
        };

        using TP = automation::TransportParam;
        drawCCLabel(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH,
                    automation::AutomationTarget::transport(TP::Stop));
        drawCCLabel(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH,
                    automation::AutomationTarget::transport(TP::Play));
        drawCCLabel(m_recBtnX, m_recBtnY, m_recBtnW, m_recBtnH,
                    automation::AutomationTarget::transport(TP::Record));
        drawCCLabel(m_bpmBoxX, m_bpmBoxY, m_bpmBoxW, m_bpmBoxH,
                    automation::AutomationTarget::transport(TP::BPM));
    }

    // v2 inputs + buttons render through the fw2 UIContext.
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();

    // Left group widgets
    m_bpmInput.render(v2ctx);
    m_bpmLabel.paint(ctx);

    // Separator after BPM
    float sep1X = m_bpmBoxX + m_bpmBoxW + 36.0f;
    r.drawRect(sep1X, y + 8, 1, h - 16, Theme::clipSlotBorder);

    // Time sig
    m_tsNumInput.render(v2ctx);
    m_slashLabel.paint(ctx);
    m_tsDenInput.render(v2ctx);

    // Separator after time sig
    float sep2X = m_tsDenBoxX + m_tsDenBoxW + 8.0f;
    r.drawRect(sep2X, y + 8, 1, h - 16, Theme::clipSlotBorder);

    // TAP button — v2 FwButton with flash. Flash expressed via accent
    // highlight (pulse on tap). render through the fw2 UIContext.
    m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
    if (m_tapFlash > 0.01f) {
        uint8_t fR = static_cast<uint8_t>(40 + m_tapFlash * 60);
        uint8_t fG = static_cast<uint8_t>(40 + m_tapFlash * 80);
        m_tapBtn.setAccentColor(Color{fR, fG, 50});
        m_tapBtn.setHighlighted(true);
    } else {
        m_tapBtn.setHighlighted(false);
    }
    m_tapBtn.render(v2ctx);

    // Metronome toggle — v2 FwToggle. Label includes count-in count.
    if (m_countInBars > 0) {
        char metLabel[16];
        std::snprintf(metLabel, sizeof(metLabel), "MET %d", m_countInBars);
        m_metroBtn.setLabel(metLabel);
    } else {
        m_metroBtn.setLabel("MET");
    }
    m_metroBtn.setAccentColor(Color{60, 90, 140});
    m_metroBtn.setState(m_metronomeOn);
    m_metroBtn.render(v2ctx);

    // Visual metronome next to MET button
    if (m_metronomeOn && (m_transportPlaying || m_countingIn)) {
        int bpb = std::max(1, m_transportNumerator);
        double beats = m_countingIn ? m_countInBeats : m_transportBeats;
        int currentBeat = static_cast<int>(std::fmod(beats, static_cast<double>(bpb)));
        double beatFrac = std::fmod(beats, 1.0);
        float flash = std::max(0.0f, 1.0f - static_cast<float>(beatFrac) * 4.0f);

        if (m_metroVisualStyle == 1) {
            // Big beat number display
            char numBuf[4];
            std::snprintf(numBuf, sizeof(numBuf), "%d", currentBeat + 1);
            float numScale = 22.0f / Theme::kFontSize;
            float numW = ctx.font->textWidth(numBuf, numScale);
            float numH = ctx.font->lineHeight(numScale);
            float nx = m_metroDotX + 2;
            float ny = m_metroDotY - numH * 0.5f;
            uint8_t bright = static_cast<uint8_t>(140 + flash * 115);
            Color numCol = (currentBeat == 0)
                ? Color{bright, static_cast<uint8_t>(bright / 2), 50}
                : Color{80, static_cast<uint8_t>(bright), bright};
            ctx.font->drawText(r, numBuf, nx, ny, numScale, numCol);
        } else {
            // Dots style
            float dotR = 4.0f;
            float dotGap = 12.0f;
            for (int i = 0; i < bpb && i < 16; ++i) {
                float dx = m_metroDotX + i * dotGap;
                float dy = m_metroDotY;
                if (i == currentBeat) {
                    uint8_t bright = static_cast<uint8_t>(120 + flash * 135);
                    Color col = (i == 0)
                        ? Color{bright, static_cast<uint8_t>(bright / 2), 50}
                        : Color{80, static_cast<uint8_t>(bright), bright};
                    r.drawFilledCircle(dx, dy, dotR + flash * 2.0f, col, 12);
                } else {
                    Color dim = (i == 0) ? Color{80, 50, 30} : Color{50, 60, 65};
                    r.drawFilledCircle(dx, dy, dotR, dim, 12);
                }
            }
        }
    }

    // Right group
    m_posLabel.paint(ctx);

    // ── Performance meters (CPU / MEM) ──
    {
        // Update readings every ~30 frames to avoid per-frame system calls
        if (++m_meterUpdateCounter >= 30) {
            m_meterUpdateCounter = 0;
            float rawCpu = static_cast<float>(m_engine->cpuLoad());
            float rawMem = util::processMemoryMB();
            // Smooth with exponential moving average
            m_cpuLoad  = m_cpuLoad  * 0.7f + rawCpu * 0.3f;
            m_memoryMB = m_memoryMB * 0.7f + rawMem * 0.3f;
        }

        float meterScale = 9.0f / Theme::kFontSize;
        float lineH = ctx.font->lineHeight(meterScale);
        float rightEdge = m_bounds.x + m_bounds.w - 160.0f - 12.0f;  // posLabel X

        // CPU meter
        char cpuBuf[16];
        int cpuPct = static_cast<int>(m_cpuLoad * 100.0f + 0.5f);
        std::snprintf(cpuBuf, sizeof(cpuBuf), "CPU %d%%", cpuPct);
        Color cpuCol = (cpuPct > 80) ? Color{255, 80, 60, 255}
                     : (cpuPct > 50) ? Color{255, 200, 60, 255}
                     :                 Color{120, 130, 140, 255};
        float cpuW = ctx.font->textWidth(cpuBuf, meterScale);
        float meterX = rightEdge - cpuW - 16.0f;
        float meterY = y + (h - lineH * 2 - 2) * 0.5f;
        ctx.font->drawText(r, cpuBuf, meterX, meterY, meterScale, cpuCol);

        // Memory meter
        char memBuf[24];
        if (m_memoryMB >= 1024.0f)
            std::snprintf(memBuf, sizeof(memBuf), "MEM %.1fG", m_memoryMB / 1024.0f);
        else
            std::snprintf(memBuf, sizeof(memBuf), "MEM %.0fM", m_memoryMB);
        float memW = ctx.font->textWidth(memBuf, meterScale);
        float memX = meterX + (cpuW - memW) * 0.5f;
        ctx.font->drawText(r, memBuf, memX, meterY + lineH + 2, meterScale,
                           Color{120, 130, 140, 255});
    }

    // v1 context menu retired — fw2::ContextMenu paints via LayerStack.
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

    // v1 context menu retired — LayerStack intercepts upstream.

    // Right-click on transport buttons opens MIDI Learn menu
    if (rightClick && m_learnManager) {
        using namespace automation;
        if (hitBtn(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH, mx, my)) {
            openTransportLearnMenu(mx, my, AutomationTarget::transport(TransportParam::Stop),
                0.0f, 1.0f, nullptr);
            return true;
        }
        if (hitBtn(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH, mx, my)) {
            openTransportLearnMenu(mx, my, AutomationTarget::transport(TransportParam::Play),
                0.0f, 1.0f, nullptr);
            return true;
        }
        if (hitBtn(m_recBtnX, m_recBtnY, m_recBtnW, m_recBtnH, mx, my)) {
            openTransportLearnMenu(mx, my, AutomationTarget::transport(TransportParam::Record),
                0.0f, 1.0f, nullptr);
            return true;
        }
        if (mx >= m_bpmBoxX && mx < m_bpmBoxX + m_bpmBoxW &&
            my >= m_bpmBoxY && my < m_bpmBoxY + m_bpmBoxH) {
            openTransportLearnMenu(mx, my, AutomationTarget::transport(TransportParam::BPM),
                0.0f, 1.0f, nullptr);
            return true;
        }
    }

    // Left-click only below this point
    if (rightClick) return false;

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
        bool newState = !m_recording;
        m_engine->sendCommand(audio::TransportRecordMsg{newState, m_selectedScene});
        return true;
    }

    // Numeric inputs — v2 widgets. Route through the v1→v2 bridge so
    // the gesture SM gets down/up for drag, and double-click fires
    // from dispatchMouseUp for inline edit mode.
    auto routeV2Input = [&](::yawn::ui::fw2::FwNumberInput& w) -> bool {
        const auto& b = w.bounds();
        if (mx < b.x || mx >= b.x + b.w) return false;
        if (my < b.y || my >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        w.dispatchMouseDown(ev);
        m_v2Dragging = &w;
        captureMouse();
        return true;
    };
    if (routeV2Input(m_bpmInput))   return true;
    if (routeV2Input(m_tsNumInput)) return true;
    if (routeV2Input(m_tsDenInput)) return true;
    {
        const auto& b = m_tapBtn.bounds();
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
            m_tapBtn.dispatchMouseDown(ev);
            m_v2Dragging = &m_tapBtn;
            captureMouse();
            return true;
        }
    }
    {
        const auto& b = m_metroBtn.bounds();
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
            m_metroBtn.dispatchMouseDown(ev);
            m_v2Dragging = &m_metroBtn;
            captureMouse();
            return true;
        }
    }

    // Click anywhere else dismisses edit mode (commits current buffer).
    // Returning false lets the App continue dispatching to other panels
    // (contentGrid / session / mixer) — this panel only consumes clicks
    // that actually hit one of its interactive elements.
    if (isEditing()) {
        if (m_bpmInput.isEditing())   m_bpmInput.endEdit(/*commit*/true);
        if (m_tsNumInput.isEditing()) m_tsNumInput.endEdit(/*commit*/true);
        if (m_tsDenInput.isEditing()) m_tsDenInput.endEdit(/*commit*/true);
    }
    return false;
}

bool TransportPanel::onMouseMove(MouseMoveEvent& e) {
    // v1 context menu retired — fw2 handles hover via LayerStack.

    // v2 button/toggle drag in progress — forward translated events.
    if (m_v2Dragging) {
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, m_v2Dragging->bounds());
        m_v2Dragging->dispatchMouseMove(ev);
        return true;
    }

    // v2 number inputs route hover internally; no per-move forward.

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

void TransportPanel::openTransportLearnMenu(float mx, float my,
                                             const automation::AutomationTarget& target,
                                             float paramMin, float paramMax,
                                             std::function<void()> resetAction) {
    using Item = ui::ContextMenu::Item;
    std::vector<Item> items;

    bool hasMapping = m_learnManager && m_learnManager->findByTarget(target) != nullptr;
    bool isLearning = m_learnManager && m_learnManager->isLearning() &&
                      m_learnManager->learnTarget() == target;

    if (isLearning) {
        Item cancelItem;
        cancelItem.label = "Cancel Learn";
        cancelItem.action = [this]() {
            if (m_learnManager) m_learnManager->cancelLearn();
        };
        items.push_back(std::move(cancelItem));
    } else {
        Item learnItem;
        learnItem.label = "MIDI Learn";
        learnItem.action = [this, target, paramMin, paramMax]() {
            if (m_learnManager)
                m_learnManager->startLearn(target, paramMin, paramMax);
        };
        items.push_back(std::move(learnItem));
    }

    if (hasMapping) {
        auto* mapping = m_learnManager->findByTarget(target);
        Item removeItem;
        removeItem.label = "Remove " + mapping->label();
        removeItem.action = [this, target]() {
            if (m_learnManager) m_learnManager->removeByTarget(target);
        };
        items.push_back(std::move(removeItem));
    }

    if (resetAction) {
        Item sep;
        sep.separator = true;
        items.push_back(std::move(sep));

        Item resetItem;
        resetItem.label = "Reset to Default";
        resetItem.action = std::move(resetAction);
        items.push_back(std::move(resetItem));
    }

    ::yawn::ui::fw2::ContextMenu::show(
        ::yawn::ui::fw2::v1ItemsToFw2(std::move(items)),
        Point{mx, my});
}

} // namespace fw
} // namespace ui
} // namespace yawn
