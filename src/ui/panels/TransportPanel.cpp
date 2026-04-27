// TransportPanel.cpp — UI v2 rendering + event implementations.

#include "TransportPanel.h"
#include "app/Project.h"
#include "../Renderer.h"
#include "../Font.h"
#include "ui/framework/v2/V1MenuBridge.h"
#include "ui/framework/v2/Theme.h"
#include "util/SystemInfo.h"
#include <SDL3/SDL.h>

namespace yawn {
namespace ui {
namespace fw2 {

void TransportPanel::setTransportState(bool playing, double beats, double bpm,
                                       int numerator, int denominator) {
    m_transportPlaying     = playing;
    m_transportBeats       = beats;
    m_transportBPM         = bpm;
    m_transportNumerator   = numerator;
    m_transportDenominator = denominator;

    // Sync from engine — but ONLY when the engine value CHANGED since
    // the last frame. See TransportPanel's header comment + the
    // "last seen" state block for the rubber-banding rationale that
    // made this necessary.
    if (bpm != m_lastSeenEngineBpm || !m_bpmSynced) {
        m_bpmInput.setValue(static_cast<float>(bpm), ValueChangeSource::Automation);
        m_lastSeenEngineBpm = bpm;
        m_bpmSynced = true;
    }
    if (numerator != m_lastSeenEngineNumerator || !m_tsSynced) {
        m_tsNumInput.setValue(static_cast<float>(numerator), ValueChangeSource::Automation);
        m_lastSeenEngineNumerator = numerator;
    }
    if (denominator != m_lastSeenEngineDenominator || !m_tsSynced) {
        m_tsDenInput.setValue(static_cast<float>(denominator), ValueChangeSource::Automation);
        m_lastSeenEngineDenominator = denominator;
    }
    m_tsSynced = true;

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
    m_posText = posBuf;
}

bool TransportPanel::handleKeyDown(int keycode) {
    // SDL keycodes: 13 = Enter, 27 = Escape, 8 = Backspace, 9 = Tab.
    auto* editing = m_bpmInput.isEditing()   ? &m_bpmInput
                  : m_tsNumInput.isEditing() ? &m_tsNumInput
                  : m_tsDenInput.isEditing() ? &m_tsDenInput
                  : nullptr;
    if (!editing) return false;

    if (keycode == 9) {
        editing->endEdit(/*commit*/true);
        if (editing == &m_bpmInput)        m_tsNumInput.beginEdit();
        else if (editing == &m_tsNumInput) m_tsDenInput.beginEdit();
        return true;
    }

    KeyEvent ke{};
    switch (keycode) {
        case 27: ke.key = Key::Escape;    break;
        case 13: ke.key = Key::Enter;     break;
        case 8:  ke.key = Key::Backspace; break;
        default: return true;
    }
    editing->dispatchKeyDown(ke);
    return true;
}

// ─── Layout ───────────────────────────────────────────────────────

void TransportPanel::onLayout(Rect bounds, UIContext& ctx) {
    constexpr float btnSize = 36.0f;
    constexpr float btnGap  = 4.0f;
    const float y = bounds.y;
    const float h = ::yawn::ui::Theme::kTransportBarHeight;
    const float btnY = y + (h - btnSize) * 0.5f;
    const float boxH = btnSize;

    // Audio-engine toggle — far-left pill that suspends/resumes the
    // PortAudio stream without tearing it down. Red when the engine is
    // suspended (device lost or manually off), green when running.
    {
        constexpr float audioW = 54.0f;
        m_audioBtnX = bounds.x + 12.0f;
        m_audioBtnY = btnY;
        m_audioBtnW = audioW;
        m_audioBtnH = boxH;
    }

    // ── Left group: BPM + TimeSig + TAP ──
    float lx = bounds.x + 12.0f + m_audioBtnW + 12.0f;

    // BPM input
    const float bpmW = 80.0f;
    m_bpmBoxX = lx; m_bpmBoxY = btnY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;
    m_bpmInput.measure(Constraints::tight(bpmW, boxH), ctx);
    m_bpmInput.layout(Rect{lx, btnY, bpmW, boxH}, ctx);
    lx += bpmW + 4.0f;

    // BPM label ("BPM" static text — painted inline in render())
    lx += 32.0f + 8.0f;

    // Separator
    lx += 12.0f;

    // Time sig numerator
    const float tsBoxW = 28.0f;
    m_tsNumBoxX = lx; m_tsNumBoxY = btnY; m_tsNumBoxW = tsBoxW; m_tsNumBoxH = boxH;
    m_tsNumInput.measure(Constraints::tight(tsBoxW, boxH), ctx);
    m_tsNumInput.layout(Rect{lx, btnY, tsBoxW, boxH}, ctx);
    lx += tsBoxW + 2.0f;

    // "/" separator (painted inline)
    lx += 10.0f + 2.0f;

    // Denominator
    m_tsDenBoxX = lx; m_tsDenBoxY = btnY; m_tsDenBoxW = tsBoxW; m_tsDenBoxH = boxH;
    m_tsDenInput.measure(Constraints::tight(tsBoxW, boxH), ctx);
    m_tsDenInput.layout(Rect{lx, btnY, tsBoxW, boxH}, ctx);
    lx += tsBoxW + 8.0f;

    // Separator
    lx += 12.0f;

    // TAP button
    const float tapW = 42.0f;
    m_tapBtn.measure(Constraints::tight(tapW, boxH), ctx);
    m_tapBtn.layout(Rect{lx, btnY, tapW, boxH}, ctx);
    lx += tapW + 8.0f;

    // Metronome toggle
    const float metW = (m_countInBars > 0) ? 62.0f : 42.0f;
    m_metroBtn.measure(Constraints::tight(metW, boxH), ctx);
    m_metroBtn.layout(Rect{lx, btnY, metW, boxH}, ctx);
    m_metroDotX = lx + metW + 6.0f;
    m_metroDotY = btnY + boxH * 0.5f;

    // ── Center group: Home | Stop | Play | Record ──
    const float totalBtnW = 4 * btnSize + 3 * btnGap;
    const float centerX = bounds.x + (bounds.w - totalBtnW) * 0.5f;

    m_homeBtnX = centerX;                          m_homeBtnY = btnY;
    m_homeBtnW = btnSize;                          m_homeBtnH = btnSize;
    m_stopBtnX = centerX + btnSize + btnGap;       m_stopBtnY = btnY;
    m_stopBtnW = btnSize;                          m_stopBtnH = btnSize;
    m_playBtnX = centerX + 2 * (btnSize + btnGap); m_playBtnY = btnY;
    m_playBtnW = btnSize;                          m_playBtnH = btnSize;
    m_recBtnX  = centerX + 3 * (btnSize + btnGap); m_recBtnY  = btnY;
    m_recBtnW  = btnSize;                          m_recBtnH  = btnSize;

    // ── Right side: Link toggle (rightmost) ──
    const float linkW = 52.0f;
    m_linkBtnX = bounds.x + bounds.w - 12.0f - linkW;
    m_linkBtnY = btnY;
    m_linkBtnW = linkW;
    m_linkBtnH = boxH;
}

// ─── Render ────────────────────────────────────────────────────────

void TransportPanel::render(UIContext& ctx) {
    if (!m_visible) return;
    if (!ctx.renderer || !ctx.textMetrics) return;
    if (!m_engine) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    const float x = m_bounds.x, y = m_bounds.y;
    const float w = m_bounds.w;
    const float h = ::yawn::ui::Theme::kTransportBarHeight;

    // Background
    r.drawRect(x, y, w, h, ::yawn::ui::Theme::transportBg);
    r.drawRect(x, y + h - 1, w, 1, ::yawn::ui::Theme::clipSlotBorder);

    // Audio-engine toggle (far-left pill). Green when running, red when
    // suspended. Label "AUDIO". Click to flip — auto-suspends on
    // device-removed events from SDL; manual resume tells the engine
    // to start processing again (assumes the stream is still open).
    {
        const bool running = m_engine->isRunning();
        const bool hovered = (m_hoveredBtn == 4);
        const Color bg = running
            ? (hovered ? Color{50, 130, 60, 255} : Color{40, 110, 50, 255})
            : (hovered ? Color{180, 60, 60, 255} : Color{150, 50, 50, 255});
        r.drawRoundedRect(m_audioBtnX, m_audioBtnY, m_audioBtnW, m_audioBtnH,
                           4.0f, bg);
        const float fs = theme().metrics.fontSizeSmall;
        const char* lbl = "AUDIO";
        const float tw = tm.textWidth(lbl, fs);
        const float lh = tm.lineHeight(fs);
        tm.drawText(r, lbl,
                     m_audioBtnX + (m_audioBtnW - tw) * 0.5f,
                     m_audioBtnY + (m_audioBtnH - lh) * 0.5f - lh * 0.15f,
                     fs, Color{240, 240, 245, 255});
    }

    // Transport buttons (center)
    paintTransportButtons(r);

    // CC labels for mapped transport controls
    if (m_learnManager) {
        const float ccSize = 7.0f;
        const Color ccCol{100, 180, 255, 255};
        auto drawCCLabel = [&](float bx, float by, float bw, float bh,
                               const automation::AutomationTarget& target) {
            auto* mapping = m_learnManager->findByTarget(target);
            if (!mapping) return;
            auto lbl = mapping->label();
            const float tw = tm.textWidth(lbl, ccSize);
            const float lh = tm.lineHeight(ccSize);
            tm.drawText(r, lbl, bx + (bw - tw) * 0.5f, by - lh - 1, ccSize, ccCol);
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

    // Inline text sizes pulled from the fw2 theme so they track the
    // UI font-scale setting (Preferences → Theme) like everything
    // else.
    const ThemeMetrics& tmet = theme().metrics;
    const float textScale = tmet.fontSizeSmall;
    const Color textCol = ::yawn::ui::Theme::textPrimary;
    const Color dimCol  = ::yawn::ui::Theme::clipSlotBorder;

    // Left-group widgets.
    m_bpmInput.render(ctx);

    // "BPM" static label next to the input.
    {
        const float lh = tm.lineHeight(textScale);
        tm.drawText(r, "BPM",
                    m_bpmBoxX + m_bpmBoxW + 4.0f,
                    m_bpmBoxY + (m_bpmBoxH - lh) * 0.5f,
                    textScale, textCol);
    }

    // Separator after BPM block.
    {
        const float sepX = m_bpmBoxX + m_bpmBoxW + 36.0f;
        r.drawRect(sepX, y + 8, 1, h - 16, dimCol);
    }

    // Time sig inputs + slash.
    m_tsNumInput.render(ctx);
    {
        const float slashSize = tmet.fontSize;
        const float lh = tm.lineHeight(slashSize);
        tm.drawText(r, "/",
                    m_tsNumBoxX + m_tsNumBoxW + 4.0f,
                    m_tsNumBoxY + (m_tsNumBoxH - lh) * 0.5f,
                    slashSize, textCol);
    }
    m_tsDenInput.render(ctx);

    // Separator after time sig.
    {
        const float sepX = m_tsDenBoxX + m_tsDenBoxW + 8.0f;
        r.drawRect(sepX, y + 8, 1, h - 16, dimCol);
    }

    // TAP button with flash effect.
    m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
    if (m_tapFlash > 0.01f) {
        uint8_t fR = static_cast<uint8_t>(40 + m_tapFlash * 60);
        uint8_t fG = static_cast<uint8_t>(40 + m_tapFlash * 80);
        m_tapBtn.setAccentColor(Color{fR, fG, 50});
        m_tapBtn.setHighlighted(true);
    } else {
        m_tapBtn.setHighlighted(false);
    }
    m_tapBtn.render(ctx);

    // Metronome toggle (label + state).
    if (m_countInBars > 0) {
        char metLabel[16];
        std::snprintf(metLabel, sizeof(metLabel), "MET %d", m_countInBars);
        m_metroBtn.setLabel(metLabel);
    } else {
        m_metroBtn.setLabel("MET");
    }
    m_metroBtn.setAccentColor(Color{60, 90, 140, 255});
    m_metroBtn.setState(m_metronomeOn);
    m_metroBtn.render(ctx);

    // Visual metronome dots / number next to MET.
    if (m_metronomeOn && (m_transportPlaying || m_countingIn)) {
        const int bpb = std::max(1, m_transportNumerator);
        const double beats = m_countingIn ? m_countInBeats : m_transportBeats;
        const int currentBeat = static_cast<int>(std::fmod(beats, static_cast<double>(bpb)));
        const double beatFrac = std::fmod(beats, 1.0);
        const float flash = std::max(0.0f, 1.0f - static_cast<float>(beatFrac) * 4.0f);

        if (m_metroVisualStyle == 1) {
            char numBuf[16];
            std::snprintf(numBuf, sizeof(numBuf), "%d", currentBeat + 1);
            const float numSize = 22.0f;
            const float numW = tm.textWidth(numBuf, numSize);
            (void)numW;
            const float numH = tm.lineHeight(numSize);
            const float nx = m_metroDotX + 2;
            const float ny = m_metroDotY - numH * 0.5f;
            const uint8_t bright = static_cast<uint8_t>(140 + flash * 115);
            const Color numCol = (currentBeat == 0)
                ? Color{bright, static_cast<uint8_t>(bright / 2), 50, 255}
                : Color{80, static_cast<uint8_t>(bright), bright, 255};
            tm.drawText(r, numBuf, nx, ny, numSize, numCol);
        } else {
            const float dotR = 4.0f;
            const float dotGap = 12.0f;
            for (int i = 0; i < bpb && i < 16; ++i) {
                const float dx = m_metroDotX + i * dotGap;
                const float dy = m_metroDotY;
                if (i == currentBeat) {
                    uint8_t bright = static_cast<uint8_t>(120 + flash * 135);
                    Color col = (i == 0)
                        ? Color{bright, static_cast<uint8_t>(bright / 2), 50, 255}
                        : Color{80, static_cast<uint8_t>(bright), bright, 255};
                    r.drawFilledCircle(dx, dy, dotR + flash * 2.0f, col, 12);
                } else {
                    Color dim = (i == 0) ? Color{80, 50, 30, 255} : Color{50, 60, 65, 255};
                    r.drawFilledCircle(dx, dy, dotR, dim, 12);
                }
            }
        }
    }

    // Right-aligned position display.
    if (!m_posText.empty()) {
        const float posSize = tmet.fontSizeLarge;
        const float tw = tm.textWidth(m_posText, posSize);
        const float lh = tm.lineHeight(posSize);
        const float posX = m_bounds.x + m_bounds.w - 160.0f - 12.0f + (160.0f - tw);
        tm.drawText(r, m_posText, posX, y + (h - lh) * 0.5f,
                    posSize, ::yawn::ui::Theme::transportAccent);
    }

    // Ableton Link indicator and toggle
    {
        m_linkEnabled = m_engine->linkManager().enabled();
        m_linkPeers = m_engine->linkManager().numPeers();

        const bool hovered = (m_hoveredBtn == 5);
        const bool active = m_linkEnabled && m_linkPeers > 0;
        const Color bg = active
            ? (hovered ? Color{50, 130, 170, 255} : Color{40, 110, 150, 255})
            : m_linkEnabled
                ? (hovered ? Color{70, 70, 80, 255} : Color{55, 55, 65, 255})
                : (hovered ? Color{55, 55, 65, 255} : Color{40, 40, 50, 255});
        r.drawRoundedRect(m_linkBtnX, m_linkBtnY, m_linkBtnW, m_linkBtnH,
                           4.0f, bg);

        const float fs = tmet.fontSizeSmall;
        const Color textCol = active ? Color{180, 220, 255, 255}
                             : m_linkEnabled ? Color{140, 150, 160, 255}
                             : Color{100, 105, 110, 255};
        const char* lbl = m_linkEnabled
            ? (m_linkPeers > 0 ? "LINK" : "LINK")
            : "LINK";

        const float tw = tm.textWidth(lbl, fs);
        const float lh = tm.lineHeight(fs);

        // Show peer count when active
        if (m_linkEnabled && m_linkPeers > 0) {
            char peerBuf[16];
            std::snprintf(peerBuf, sizeof(peerBuf), "%d", m_linkPeers);
            const float peerW = tm.textWidth(peerBuf, fs * 0.75f);
            const float totalW = tw + peerW + 4.0f;
            const float startX = m_linkBtnX + (m_linkBtnW - totalW) * 0.5f;

            tm.drawText(r, lbl, startX,
                        m_linkBtnY + (m_linkBtnH - lh) * 0.5f - lh * 0.15f,
                        fs, textCol);
            tm.drawText(r, peerBuf, startX + tw + 4.0f,
                        m_linkBtnY + (m_linkBtnH - lh) * 0.5f - 2.0f,
                        fs * 0.75f,
                        Color{120, 200, 140, 255});
        } else {
            tm.drawText(r, lbl,
                        m_linkBtnX + (m_linkBtnW - tw) * 0.5f,
                        m_linkBtnY + (m_linkBtnH - lh) * 0.5f - lh * 0.15f,
                        fs, textCol);
        }
    }

    // Performance meters (CPU / MEM).
    {
        if (++m_meterUpdateCounter >= 30) {
            m_meterUpdateCounter = 0;
            const float rawCpu = static_cast<float>(m_engine->cpuLoad());
            const float rawMem = util::processMemoryMB();
            m_cpuLoad  = m_cpuLoad  * 0.7f + rawCpu * 0.3f;
            m_memoryMB = m_memoryMB * 0.7f + rawMem * 0.3f;
        }

        const float meterSize = tmet.fontSizeSmall;
        const float lineH = tm.lineHeight(meterSize);
        const float rightEdge = m_linkBtnX - 8.0f;

        char cpuBuf[16];
        const int cpuPct = static_cast<int>(m_cpuLoad * 100.0f + 0.5f);
        std::snprintf(cpuBuf, sizeof(cpuBuf), "CPU %d%%", cpuPct);
        const Color cpuCol = (cpuPct > 80) ? Color{255, 80, 60, 255}
                           : (cpuPct > 50) ? Color{255, 200, 60, 255}
                           :                 Color{120, 130, 140, 255};
        const float cpuW = tm.textWidth(cpuBuf, meterSize);
        const float meterX = rightEdge - cpuW - 16.0f;
        const float meterY = y + (h - lineH * 2 - 2) * 0.5f;
        tm.drawText(r, cpuBuf, meterX, meterY, meterSize, cpuCol);

        char memBuf[24];
        if (m_memoryMB >= 1024.0f)
            std::snprintf(memBuf, sizeof(memBuf), "MEM %.1fG", m_memoryMB / 1024.0f);
        else
            std::snprintf(memBuf, sizeof(memBuf), "MEM %.0fM", m_memoryMB);
        const float memW = tm.textWidth(memBuf, meterSize);
        const float memX = meterX + (cpuW - memW) * 0.5f;
        tm.drawText(r, memBuf, memX, meterY + lineH + 2, meterSize,
                    Color{120, 130, 140, 255});
    }
}

// ─── Transport Buttons (immediate-mode) ────────────────────────────

void TransportPanel::paintTransportButtons(Renderer2D& r) {
    constexpr float cornerR = 4.0f;

    // Home — return playhead to 0. Icon: a left-pointing triangle plus
    // a short vertical bar ("⏮") scaled down to fit the button.
    {
        const bool hovered = (m_hoveredBtn == 3);
        const Color bg = hovered ? Color{60,60,65,255} : Color{50,50,55,255};
        r.drawRoundedRect(m_homeBtnX, m_homeBtnY, m_homeBtnW, m_homeBtnH, cornerR, bg);
        const Color ic{255,255,255,255};
        const float cx = m_homeBtnX + m_homeBtnW * 0.5f;
        const float cy = m_homeBtnY + m_homeBtnH * 0.5f;
        const float triH = 10.0f, triW = 8.0f;
        // Vertical bar (left side)
        r.drawRect(cx - triW * 0.6f - 3.0f, cy - triH * 0.5f,
                   2.0f, triH, ic);
        // Left-pointing triangle (right of bar)
        r.drawTriangle(cx + triW * 0.4f, cy - triH * 0.5f,
                        cx + triW * 0.4f, cy + triH * 0.5f,
                        cx - triW * 0.6f, cy, ic);
    }

    // Stop
    {
        const bool hovered = (m_hoveredBtn == 0);
        const Color bg = m_transportPlaying
            ? (hovered ? Color{48,48,52,255} : Color{38,38,41,255})
            : (hovered ? Color{60,60,65,255} : Color{50,50,55,255});
        r.drawRoundedRect(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH, cornerR, bg);
        const Color ic = m_transportPlaying ? Color{140,140,140,255}
                                             : Color{255,255,255,255};
        const float cx = m_stopBtnX + m_stopBtnW * 0.5f;
        const float cy = m_stopBtnY + m_stopBtnH * 0.5f;
        const float half = 5.0f;
        r.drawRect(cx - half, cy - half, half * 2, half * 2, ic);
    }

    // Play
    {
        const bool hovered = (m_hoveredBtn == 1);
        const Color bg = m_transportPlaying
            ? (hovered ? Color{35,75,35,255} : Color{30,60,30,255})
            : (hovered ? Color{48,48,52,255} : Color{38,38,41,255});
        r.drawRoundedRect(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH, cornerR, bg);
        const Color ic = m_transportPlaying ? Color{80,230,80,255}
                                             : Color{160,160,160,255};
        const float cx = m_playBtnX + m_playBtnW * 0.5f;
        const float cy = m_playBtnY + m_playBtnH * 0.5f;
        const float triH = 12.0f, triW = 10.0f;
        r.drawTriangle(cx - triW * 0.4f, cy - triH * 0.5f,
                        cx - triW * 0.4f, cy + triH * 0.5f,
                        cx + triW * 0.6f, cy, ic);
    }

    // Record
    {
        const bool hovered = (m_hoveredBtn == 2);
        Color bg;
        if (m_countingIn) {
            m_recPulse += 0.1f;
            const float pulse = (std::sin(m_recPulse * 6.0f) + 1.0f) * 0.5f;
            bg = Color{static_cast<uint8_t>(120 + static_cast<int>(pulse * 80)), 30, 30, 255};
        } else if (m_recording) {
            bg = hovered ? Color{220,50,50,255} : Color{200,40,40,255};
        } else {
            bg = hovered ? Color{75,35,35,255} : Color{60,30,30,255};
        }
        r.drawRoundedRect(m_recBtnX, m_recBtnY, m_recBtnW, m_recBtnH, cornerR, bg);
        const Color dc = m_recording ? Color{255,100,100,255} : Color{200,60,60,255};
        const float cx = m_recBtnX + m_recBtnW * 0.5f;
        const float cy = m_recBtnY + m_recBtnH * 0.5f;
        r.drawFilledCircle(cx, cy, 6.0f, dc, 20);
        if (m_countingIn) {
            const float progW = m_recBtnW * static_cast<float>(m_countInProgress);
            r.drawRect(m_recBtnX, m_recBtnY + m_recBtnH - 3, progW, 3,
                        Color{255,100,100,255});
        }
    }
}

// ─── Mouse events ──────────────────────────────────────────────────

bool TransportPanel::onMouseDown(MouseEvent& e) {
    if (!m_engine) return false;
    const float mx = e.x, my = e.y;
    const bool rightClick = (e.button == MouseButton::Right);

    // Right-click on transport buttons → MIDI Learn menu.
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

    if (rightClick) return false;

    // Audio-engine toggle.
    if (hitBtn(m_audioBtnX, m_audioBtnY, m_audioBtnW, m_audioBtnH, mx, my)) {
        if (m_engine->isRunning()) m_engine->suspend();
        else                        m_engine->resume();
        return true;
    }

    // Link toggle.
    if (hitBtn(m_linkBtnX, m_linkBtnY, m_linkBtnW, m_linkBtnH, mx, my)) {
        m_linkEnabled = !m_linkEnabled;
        m_engine->linkManager().enable(m_linkEnabled);
        return true;
    }

    // Transport buttons.
    if (hitBtn(m_homeBtnX, m_homeBtnY, m_homeBtnW, m_homeBtnH, mx, my)) {
        // Return-to-zero — leaves play state untouched so a user can
        // "rewind to start while playing" and keep going.
        m_engine->sendCommand(audio::TransportSetPositionMsg{0});
        return true;
    }
    if (hitBtn(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH, mx, my)) {
        m_engine->sendCommand(audio::TransportStopMsg{});
        m_engine->sendCommand(audio::TransportSetPositionMsg{0});
        return true;
    }
    if (hitBtn(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH, mx, my)) {
        if (m_transportPlaying)
            m_engine->sendCommand(audio::TransportStopMsg{});
        else
            m_engine->sendCommand(audio::TransportPlayMsg{});
        return true;
    }
    if (hitBtn(m_recBtnX, m_recBtnY, m_recBtnW, m_recBtnH, mx, my)) {
        const bool newState = !m_recording;
        m_engine->sendCommand(audio::TransportRecordMsg{newState, m_selectedScene});
        return true;
    }

    // Route clicks on v2 children directly to them — they own gesture
    // state (capture + drag), we just find the right one and hand off.
    auto tryChild = [&](Widget& w) -> bool {
        const auto& b = w.bounds();
        if (mx < b.x || mx >= b.x + b.w) return false;
        if (my < b.y || my >= b.y + b.h) return false;
        MouseEvent ev = e;
        ev.lx = mx - b.x;
        ev.ly = my - b.y;
        w.dispatchMouseDown(ev);
        return true;
    };
    if (tryChild(m_bpmInput))   return true;
    if (tryChild(m_tsNumInput)) return true;
    if (tryChild(m_tsDenInput)) return true;
    if (tryChild(m_tapBtn))     return true;
    if (tryChild(m_metroBtn))   return true;

    // Click anywhere else — commit any active edit.
    if (isEditing()) {
        if (m_bpmInput.isEditing())   m_bpmInput.endEdit(/*commit*/true);
        if (m_tsNumInput.isEditing()) m_tsNumInput.endEdit(/*commit*/true);
        if (m_tsDenInput.isEditing()) m_tsDenInput.endEdit(/*commit*/true);
    }
    return false;
}

bool TransportPanel::onMouseMove(MouseMoveEvent& e) {
    // If a v2 child has captured the mouse, route all moves there.
    if (Widget* cap = Widget::capturedWidget()) {
        const auto& b = cap->bounds();
        MouseMoveEvent ev = e;
        ev.lx = e.x - b.x;
        ev.ly = e.y - b.y;
        cap->dispatchMouseMove(ev);
        return true;
    }

    // Update hover state for transport buttons.
    const float mx = e.x, my = e.y;
    const int prev = m_hoveredBtn;
    m_hoveredBtn = -1;
    if      (hitBtn(m_stopBtnX, m_stopBtnY, m_stopBtnW, m_stopBtnH, mx, my)) m_hoveredBtn = 0;
    else if (hitBtn(m_playBtnX, m_playBtnY, m_playBtnW, m_playBtnH, mx, my)) m_hoveredBtn = 1;
    else if (hitBtn(m_recBtnX,  m_recBtnY,  m_recBtnW,  m_recBtnH,  mx, my)) m_hoveredBtn = 2;
    else if (hitBtn(m_homeBtnX, m_homeBtnY, m_homeBtnW, m_homeBtnH, mx, my)) m_hoveredBtn = 3;
    else if (hitBtn(m_audioBtnX, m_audioBtnY, m_audioBtnW, m_audioBtnH, mx, my)) m_hoveredBtn = 4;
    else if (hitBtn(m_linkBtnX, m_linkBtnY, m_linkBtnW, m_linkBtnH, mx, my)) m_hoveredBtn = 5;
    return (m_hoveredBtn != prev);
}

void TransportPanel::tapTempo() {
    const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
    m_tapFlash = 1.0f;
    if (m_tapCount > 0) {
        const double last = m_tapTimes[(m_tapCount - 1) % kTapHistorySize];
        if (now - last > 2.0) m_tapCount = 0;
    }
    m_tapTimes[m_tapCount % kTapHistorySize] = now;
    m_tapCount++;
    if (m_tapCount >= 2) {
        const int n = std::min(m_tapCount - 1, kTapHistorySize - 1);
        const int si = m_tapCount - n - 1;
        const double total = m_tapTimes[(m_tapCount - 1) % kTapHistorySize]
                           - m_tapTimes[si % kTapHistorySize];
        const double avg = total / n;
        const double bpm = std::clamp(std::round(60.0 / avg * 100.0) / 100.0, 20.0, 999.0);
        const float oldBpm = m_bpmInput.value();
        const float newBpm = static_cast<float>(bpm);
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

    const bool hasMapping = m_learnManager && m_learnManager->findByTarget(target) != nullptr;
    const bool isLearning = m_learnManager && m_learnManager->isLearning() &&
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

    ContextMenu::show(v1ItemsToFw2(std::move(items)),
                      Point{mx, my});
}

} // namespace fw2
} // namespace ui
} // namespace yawn
