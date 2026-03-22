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

        float textY = y + 6;
        float scale = Theme::kFontSize / f.pixelHeight();

        // Play/Stop indicator
        Color stateCol = m_transportPlaying ? Theme::playing : Theme::stopped;
        const char* stateTxt = m_transportPlaying ? "PLAYING" : "STOPPED";
        float circX = x + 12, circY = y + h * 0.5f - 5;
        r.drawRect(circX, circY, 10, 10, stateCol);
        drawText(r, f, stateTxt, circX + 18, textY, scale, stateCol);

        // BPM box
        float bpmX = x + 160, boxH = h - 8, boxY = y + 4, bpmW = 100;
        m_bpmBoxX = bpmX; m_bpmBoxY = boxY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;
        bool editBpm = (m_editMode == EditMode::BPM);
        Color bpmBg = editBpm ? Color{60,60,80,255} : Color{40,40,50,255};
        r.drawRect(bpmX, boxY, bpmW, boxH, bpmBg);
        r.drawRectOutline(bpmX, boxY, bpmW, boxH,
                          editBpm ? Theme::transportAccent : Theme::clipSlotBorder);
        char bpmBuf[32];
        if (editBpm) std::snprintf(bpmBuf, sizeof(bpmBuf), "%s_", m_editBuffer.c_str());
        else         std::snprintf(bpmBuf, sizeof(bpmBuf), "%.2f", m_transportBPM);
        drawTextRet(r, f, bpmBuf, bpmX + 6, textY, scale,
                    editBpm ? Theme::transportAccent : Theme::transportText);
        drawText(r, f, "BPM", bpmX + bpmW + 4, textY, scale * 0.85f, Theme::textSecondary);

        // Time signature
        float tsX = bpmX + bpmW + 50, tsBoxW = 30, slashGap = 4;

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

        // Tap tempo
        float tapX = denX + tsBoxW + 16, tapW = 48;
        m_tapButtonX = tapX; m_tapButtonY = boxY; m_tapButtonW = tapW; m_tapButtonH = boxH;
        m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
        uint8_t fR = static_cast<uint8_t>(40 + m_tapFlash * 60);
        uint8_t fG = static_cast<uint8_t>(40 + m_tapFlash * 80);
        Color tapBg = {fR, fG, 50, 255};
        r.drawRect(tapX, boxY, tapW, boxH, tapBg);
        r.drawRectOutline(tapX, boxY, tapW, boxH, Theme::clipSlotBorder);
        drawText(r, f, "TAP", tapX + 6, textY, scale * 0.85f, Theme::transportText);

        // Bar.Beat position
        int bpb = std::max(1, m_transportNumerator);
        int bar  = static_cast<int>(m_transportBeats / bpb) + 1;
        int beat = static_cast<int>(std::fmod(m_transportBeats, (double)bpb)) + 1;
        char posBuf[32];
        std::snprintf(posBuf, sizeof(posBuf), "%d . %d", bar, beat);
        drawText(r, f, posBuf, tapX + tapW + 20, textY, scale, Theme::transportAccent);
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_engine) return false;
        float mx = e.x, my = e.y;

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

    float m_bpmBoxX = 0, m_bpmBoxY = 0, m_bpmBoxW = 0, m_bpmBoxH = 0;
    float m_tsNumBoxX = 0, m_tsNumBoxY = 0, m_tsNumBoxW = 0, m_tsNumBoxH = 0;
    float m_tsDenBoxX = 0, m_tsDenBoxY = 0, m_tsDenBoxW = 0, m_tsDenBoxH = 0;
    float m_tapButtonX = 0, m_tapButtonY = 0, m_tapButtonW = 0, m_tapButtonH = 0;

    static constexpr int kTapHistorySize = 4;
    double m_tapTimes[kTapHistorySize] = {};
    int    m_tapCount = 0;
    float  m_tapFlash = 0.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
