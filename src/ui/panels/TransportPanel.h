#pragma once
// TransportPanel — Standalone transport bar widget.
//
// Extracted from SessionPanel to allow independent positioning in the
// layout.  Renders play/stop indicator, BPM, time signature, tap tempo
// and bar.beat position.  Handles double-click editing, text input and
// key events for the transport fields.

#include "ui/framework/Widget.h"
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
    TransportPanel() = default;

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
    }

    void setRecordState(bool recording, bool countingIn, double progress) {
        m_recording = recording;
        m_countingIn = countingIn;
        m_countInProgress = progress;
    }

    bool isEditing() const { return m_editMode != EditMode::None; }

    // ─── Double-click to edit BPM / time signature ──────────────────────

    bool handleDoubleClick(float mx, float my) {
        if (mx >= m_bpmBoxX && mx <= m_bpmBoxX + m_bpmBoxW &&
            my >= m_bpmBoxY && my <= m_bpmBoxY + m_bpmBoxH) {
            m_editMode = EditMode::BPM;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.2f", m_transportBPM);
            m_editBuffer = buf;
            return true;
        }
        if (mx >= m_tsNumBoxX && mx <= m_tsNumBoxX + m_tsNumBoxW &&
            my >= m_tsNumBoxY && my <= m_tsNumBoxY + m_tsNumBoxH) {
            m_editMode = EditMode::TimeSigNum;
            m_editBuffer = std::to_string(m_transportNumerator);
            return true;
        }
        if (mx >= m_tsDenBoxX && mx <= m_tsDenBoxX + m_tsDenBoxW &&
            my >= m_tsDenBoxY && my <= m_tsDenBoxY + m_tsDenBoxH) {
            m_editMode = EditMode::TimeSigDen;
            m_editBuffer = std::to_string(m_transportDenominator);
            return true;
        }
        return false;
    }

    bool handleTextInput(const char* text) {
        if (m_editMode == EditMode::None) return false;
        for (const char* p = text; *p; ++p) {
            char c = *p;
            if (m_editMode == EditMode::BPM) {
                if ((c >= '0' && c <= '9') ||
                    (c == '.' && m_editBuffer.find('.') == std::string::npos))
                    m_editBuffer += c;
            } else {
                if (c >= '0' && c <= '9')
                    m_editBuffer += c;
            }
        }
        return true;
    }

    bool handleKeyDown(int keycode) {
        if (m_editMode == EditMode::None) return false;
        if (keycode == 13 || keycode == 9) { // Enter or Tab
            if (m_editMode == EditMode::BPM) {
                double val = std::atof(m_editBuffer.c_str());
                val = std::clamp(val, 20.0, 999.0);
                if (m_engine)
                    m_engine->sendCommand(audio::TransportSetBPMMsg{val});
                if (keycode == 9) {
                    m_editMode = EditMode::TimeSigNum;
                    m_editBuffer = std::to_string(m_transportNumerator);
                    return true;
                }
            } else if (m_editMode == EditMode::TimeSigNum) {
                int val = std::atoi(m_editBuffer.c_str());
                val = std::clamp(val, 1, 32);
                if (m_engine)
                    m_engine->sendCommand(
                        audio::TransportSetTimeSignatureMsg{val, m_transportDenominator});
                if (keycode == 9) {
                    m_editMode = EditMode::TimeSigDen;
                    m_editBuffer = std::to_string(m_transportDenominator);
                    return true;
                }
            } else if (m_editMode == EditMode::TimeSigDen) {
                int val = std::atoi(m_editBuffer.c_str());
                if (val < 1) val = 1;
                else if (val <= 1) val = 1;
                else if (val <= 2) val = 2;
                else if (val <= 4) val = 4;
                else if (val <= 8) val = 8;
                else if (val <= 16) val = 16;
                else val = 32;
                if (m_engine)
                    m_engine->sendCommand(
                        audio::TransportSetTimeSignatureMsg{m_transportNumerator, val});
            }
            m_editMode = EditMode::None;
            m_editBuffer.clear();
            return true;
        }
        if (keycode == 27) { m_editMode = EditMode::None; m_editBuffer.clear(); return true; }
        if (keycode == 8)  { if (!m_editBuffer.empty()) m_editBuffer.pop_back(); return true; }
        return true; // consume all keys while editing
    }

    // ─── Measure / Layout ───────────────────────────────────────────────

    float preferredHeight() const { return Theme::kTransportBarHeight; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, Theme::kTransportBarHeight});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
        if (!m_engine) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w;
        float h = Theme::kTransportBarHeight;

        r.drawRect(x, y, w, h, Theme::transportBg);
        r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);

        float scale = Theme::kFontSize / f.pixelHeight();
        float textY = y + (h - Theme::kFontSize) * 0.5f;

        // ── Stop / Play / Record buttons ────────────────────────────────
        constexpr float btnSize = 38.0f;
        constexpr float btnGap  = 2.0f;
        float btnY = y + (h - btnSize) * 0.5f;
        float btnX = x + 8.0f;

        // Stop button
        m_stopBtnX = btnX; m_stopBtnY = btnY; m_stopBtnW = btnSize; m_stopBtnH = btnSize;
        {
            Color bg = m_transportPlaying ? Color{38,38,41} : Color{50,50,55};
            r.drawRect(btnX, btnY, btnSize, btnSize, bg);
            r.drawRectOutline(btnX, btnY, btnSize, btnSize, Theme::clipSlotBorder);
            Color ic = m_transportPlaying ? Color{120,120,120} : Color{255,255,255};
            float cx = btnX + (btnSize - 10) * 0.5f;
            float cy = btnY + (btnSize - 10) * 0.5f;
            r.drawRect(cx, cy, 10, 10, ic);
        }
        btnX += btnSize + btnGap;

        // Play button
        m_playBtnX = btnX; m_playBtnY = btnY; m_playBtnW = btnSize; m_playBtnH = btnSize;
        {
            Color bg = m_transportPlaying ? Color{30,60,30} : Color{38,38,41};
            r.drawRect(btnX, btnY, btnSize, btnSize, bg);
            r.drawRectOutline(btnX, btnY, btnSize, btnSize, Theme::clipSlotBorder);
            Color ic = m_transportPlaying ? Color{80,230,80} : Color{120,120,120};
            // Play triangle: stacked horizontal rects approximating ▶
            float triCx = btnX + btnSize * 0.5f - 1.0f;
            float triCy = btnY + btnSize * 0.5f;
            r.drawRect(triCx - 4, triCy - 6, 2, 12, ic);
            r.drawRect(triCx - 2, triCy - 5, 2, 10, ic);
            r.drawRect(triCx,     triCy - 4, 2,  8, ic);
            r.drawRect(triCx + 2, triCy - 3, 2,  6, ic);
            r.drawRect(triCx + 4, triCy - 2, 2,  4, ic);
            r.drawRect(triCx + 6, triCy - 1, 2,  2, ic);
        }
        btnX += btnSize + btnGap;

        // Record button
        float recBtnX = btnX;
        m_recButtonX = btnX; m_recButtonY = btnY; m_recButtonW = btnSize; m_recButtonH = btnSize;
        {
            Color bg = m_recording ? Color{200,40,40} : Color{60,30,30};
            if (m_countingIn) {
                m_recPulse += 0.1f;
                float pulse = (std::sin(m_recPulse * 6.0f) + 1.0f) * 0.5f;
                bg = Color{static_cast<uint8_t>(120 + static_cast<int>(pulse * 80)), 30, 30};
            }
            r.drawRect(btnX, btnY, btnSize, btnSize, bg);
            r.drawRectOutline(btnX, btnY, btnSize, btnSize,
                m_recording ? Color{255,80,80} : Theme::clipSlotBorder);
            // Record circle: cross-shaped pattern of rects approximating ●
            Color dc = m_recording ? Color{255,100,100} : Color{200,60,60};
            float cx = btnX + btnSize * 0.5f;
            float cy = btnY + btnSize * 0.5f;
            r.drawRect(cx - 3, cy - 5, 6,  1, dc);
            r.drawRect(cx - 4, cy - 4, 8,  1, dc);
            r.drawRect(cx - 5, cy - 3, 10, 6, dc);
            r.drawRect(cx - 4, cy + 3, 8,  1, dc);
            r.drawRect(cx - 3, cy + 4, 6,  1, dc);
            // Count-in progress bar
            if (m_countingIn) {
                float progW = btnSize * static_cast<float>(m_countInProgress);
                r.drawRect(btnX, btnY + btnSize - 3, progW, 3, Color{255,100,100});
            }
        }

        // Separator after transport buttons
        float sepX = recBtnX + btnSize + 10.0f;
        r.drawRect(sepX, y + 6, 1, h - 12, Theme::clipSlotBorder);

        // ── BPM box ─────────────────────────────────────────────────────
        float bpmX = x + 140.0f, bpmW = 100.0f;
        float boxH = btnSize, boxY = btnY;
        m_bpmBoxX = bpmX; m_bpmBoxY = boxY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;
        bool editBpm = (m_editMode == EditMode::BPM);
        Color bpmBg = editBpm ? Color{60,60,80,255} : Color{40,40,50,255};
        r.drawRect(bpmX, boxY, bpmW, boxH, bpmBg);
        r.drawRectOutline(bpmX, boxY, bpmW, boxH,
                          editBpm ? Theme::transportAccent : Theme::clipSlotBorder);
        char bpmBuf[32];
        if (editBpm) std::snprintf(bpmBuf, sizeof(bpmBuf), "%s_", m_editBuffer.c_str());
        else         std::snprintf(bpmBuf, sizeof(bpmBuf), "%.2f", m_transportBPM);
        float bpmTextEnd = drawTextRet(r, f, bpmBuf, bpmX + 6, textY, scale,
                    editBpm ? Theme::transportAccent : Theme::transportText);
        (void)bpmTextEnd;
        drawText(r, f, "BPM", bpmX + bpmW + 4, textY, scale * 0.85f, Theme::textSecondary);

        // ── Time signature ──────────────────────────────────────────────
        float tsX = bpmX + bpmW + 50.0f, tsBoxW = 30.0f, slashGap = 4.0f;

        bool editNum = (m_editMode == EditMode::TimeSigNum);
        m_tsNumBoxX = tsX; m_tsNumBoxY = boxY; m_tsNumBoxW = tsBoxW; m_tsNumBoxH = boxH;
        Color numBg = editNum ? Color{60,60,80,255} : Color{40,40,50,255};
        r.drawRect(tsX, boxY, tsBoxW, boxH, numBg);
        r.drawRectOutline(tsX, boxY, tsBoxW, boxH,
                          editNum ? Theme::transportAccent : Theme::clipSlotBorder);
        char numBuf[8];
        if (editNum) std::snprintf(numBuf, sizeof(numBuf), "%s_", m_editBuffer.c_str());
        else         std::snprintf(numBuf, sizeof(numBuf), "%d", m_transportNumerator);
        drawText(r, f, numBuf, tsX + 6, textY, scale,
                 editNum ? Theme::transportAccent : Theme::transportText);

        float slashX = tsX + tsBoxW + slashGap;
        slashX = drawTextRet(r, f, "/", slashX, textY, scale, Theme::textSecondary);

        float denX = slashX + slashGap;
        bool editDen = (m_editMode == EditMode::TimeSigDen);
        m_tsDenBoxX = denX; m_tsDenBoxY = boxY; m_tsDenBoxW = tsBoxW; m_tsDenBoxH = boxH;
        Color denBg = editDen ? Color{60,60,80,255} : Color{40,40,50,255};
        r.drawRect(denX, boxY, tsBoxW, boxH, denBg);
        r.drawRectOutline(denX, boxY, tsBoxW, boxH,
                          editDen ? Theme::transportAccent : Theme::clipSlotBorder);
        char denBuf[8];
        if (editDen) std::snprintf(denBuf, sizeof(denBuf), "%s_", m_editBuffer.c_str());
        else         std::snprintf(denBuf, sizeof(denBuf), "%d", m_transportDenominator);
        drawText(r, f, denBuf, denX + 6, textY, scale,
                 editDen ? Theme::transportAccent : Theme::transportText);

        // ── Tap tempo ───────────────────────────────────────────────────
        float tapX = denX + tsBoxW + 16.0f, tapW = 48.0f;
        m_tapButtonX = tapX; m_tapButtonY = boxY; m_tapButtonW = tapW; m_tapButtonH = boxH;
        m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
        uint8_t fR = static_cast<uint8_t>(40 + m_tapFlash * 60);
        uint8_t fG = static_cast<uint8_t>(40 + m_tapFlash * 80);
        Color tapBg = {fR, fG, 50, 255};
        r.drawRect(tapX, boxY, tapW, boxH, tapBg);
        r.drawRectOutline(tapX, boxY, tapW, boxH, Theme::clipSlotBorder);
        drawText(r, f, "TAP", tapX + 6, textY, scale * 0.85f, Theme::transportText);

        // Separator after tempo section
        float sep2X = tapX + tapW + 10.0f;
        r.drawRect(sep2X, y + 6, 1, h - 12, Theme::clipSlotBorder);

        // ── Bar . Beat . Sub position ───────────────────────────────────
        int bpb  = std::max(1, m_transportNumerator);
        int bar  = static_cast<int>(m_transportBeats / bpb) + 1;
        double beatInBar = std::fmod(m_transportBeats, static_cast<double>(bpb));
        int beat = static_cast<int>(beatInBar) + 1;
        int sub  = static_cast<int>((beatInBar - std::floor(beatInBar)) * 100.0) % 100;
        char posBuf[32];
        std::snprintf(posBuf, sizeof(posBuf), "%d . %d . %02d", bar, beat, sub);
        float posX = sep2X + 10.0f;
        float posEnd = drawTextRet(r, f, posBuf, posX, textY, scale, Theme::transportAccent);

        // ── Count-in indicator ──────────────────────────────────────────
        if (m_countInBars > 0) {
            char ciBuf[16];
            std::snprintf(ciBuf, sizeof(ciBuf), "CI:%d", m_countInBars);
            drawText(r, f, ciBuf, posEnd + 14.0f, textY, scale * 0.8f, Theme::textDim);
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

        // Play button (toggle: if playing, stop; else play)
        if (mx >= m_playBtnX && mx <= m_playBtnX + m_playBtnW &&
            my >= m_playBtnY && my <= m_playBtnY + m_playBtnH) {
            if (m_transportPlaying)
                m_engine->sendCommand(audio::TransportStopMsg{});
            else
                m_engine->sendCommand(audio::TransportPlayMsg{});
            return true;
        }

        // Record button (right-click cycles count-in)
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

        // Tap tempo button
        if (mx >= m_tapButtonX && mx <= m_tapButtonX + m_tapButtonW &&
            my >= m_tapButtonY && my <= m_tapButtonY + m_tapButtonH) {
            tapTempo();
            return true;
        }

        // Click anywhere in transport dismisses edit mode
        if (m_editMode != EditMode::None) {
            m_editMode = EditMode::None;
            m_editBuffer.clear();
        }
        return true;
    }

private:
    // ─── Text helpers ───────────────────────────────────────────────────

    void drawText(Renderer2D& r, Font& f, const char* text,
                  float x, float y, float scale, Color color) {
        if (!f.isLoaded()) return;
        float tx = x;
        for (const char* p = text; *p; ++p) {
            auto g = f.getGlyph(*p, tx, y, scale);
            r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                               g.u0, g.v0, g.u1, g.v1, color, f.textureId());
            tx += g.xAdvance;
        }
    }

    float drawTextRet(Renderer2D& r, Font& f, const char* text,
                      float x, float y, float scale, Color color) {
        if (!f.isLoaded()) return x;
        float tx = x;
        for (const char* p = text; *p; ++p) {
            auto g = f.getGlyph(*p, tx, y, scale);
            r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                               g.u0, g.v0, g.u1, g.v1, color, f.textureId());
            tx += g.xAdvance;
        }
        return tx;
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

    enum class EditMode { None, BPM, TimeSigNum, TimeSigDen };
    EditMode    m_editMode = EditMode::None;
    std::string m_editBuffer;

    float m_stopBtnX = 0, m_stopBtnY = 0, m_stopBtnW = 0, m_stopBtnH = 0;
    float m_playBtnX = 0, m_playBtnY = 0, m_playBtnW = 0, m_playBtnH = 0;
    float m_bpmBoxX = 0, m_bpmBoxY = 0, m_bpmBoxW = 0, m_bpmBoxH = 0;
    float m_tsNumBoxX = 0, m_tsNumBoxY = 0, m_tsNumBoxW = 0, m_tsNumBoxH = 0;
    float m_tsDenBoxX = 0, m_tsDenBoxY = 0, m_tsDenBoxW = 0, m_tsDenBoxH = 0;
    float m_tapButtonX = 0, m_tapButtonY = 0, m_tapButtonW = 0, m_tapButtonH = 0;
    float m_recButtonX = 0, m_recButtonY = 0, m_recButtonW = 0, m_recButtonH = 0;

    static constexpr int kTapHistorySize = 4;
    double m_tapTimes[kTapHistorySize] = {};
    int    m_tapCount = 0;
    float  m_tapFlash = 0.0f;

    // Recording state
    bool   m_recording = false;
    bool   m_countingIn = false;
    double m_countInProgress = 0.0;
    int    m_countInBars = 0;
    float  m_recPulse = 0.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
