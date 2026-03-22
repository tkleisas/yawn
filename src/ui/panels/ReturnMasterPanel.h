#pragma once
// ReturnMasterPanel — Returns + Master channel strip panel.
//
// Renders return bus strips and the master strip in the bottom-right
// quadrant of the ContentGrid.  Handles fader/pan drag for returns
// and master volume.

#include "ui/framework/Widget.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
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
    ReturnMasterPanel() = default;

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

    bool isDragging() const { return m_dragging; }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
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
        float w = m_bounds.w, h = m_bounds.h;

        r.drawRect(x, y, w, h, Theme::panelBg);
        r.drawRect(x, y, w, 1, Theme::clipSlotBorder);

        float stripY = y + 2;
        float stripH = h - 4;

        // Return bus strips
        float curX = x + 4;
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            if (curX + kRetStripW > x + w) break;
            paintReturnStrip(r, f, b, curX, stripY, kRetStripW, stripH);
            curX += kRetStripW + kStripPadding;
        }

        // Separator
        r.drawRect(curX, y + 4, kSeparatorWidth, h - 8, Theme::clipSlotBorder);
        curX += kSeparatorWidth + 4;

        // Master strip
        if (curX + kRetStripW <= x + w)
            paintMasterStrip(r, f, curX, stripY, kRetStripW, stripH);
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_engine) return false;
        float mx = e.x, my = e.y;
        bool rightClick = (e.button == MouseButton::Right);
        float x = m_bounds.x, y = m_bounds.y;
        float h = m_bounds.h;

        // Return bus strips
        float curX = x + 4;
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            float rx = curX + b * (kRetStripW + kStripPadding);
            if (mx < rx || mx >= rx + kRetStripW) continue;
            const auto& rb = m_engine->mixer().returnBus(b);

            float muteY = y + 2 + 26;
            if (my >= muteY && my < muteY + kButtonHeight && mx >= rx + 4 && mx < rx + 4 + kButtonWidth) {
                m_engine->sendCommand(audio::SetReturnMuteMsg{b, !rb.muted});
                return true;
            }
            float panY = y + 56;
            float panW = kRetStripW - 8;
            if (my >= panY && my < panY + 8 && mx >= rx + 4 && mx < rx + 4 + panW) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetReturnPanMsg{b, 0.0f});
                    return true;
                }
                beginDrag(DragType::Pan, kDragReturn0 + b, mx, rb.pan);
                return true;
            }
            float faderY = y + 74;
            float faderBottom = y + h - 2 - 22;
            if (my >= faderY && my < faderBottom && mx >= rx + 4 && mx < rx + 4 + kFaderWidth) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetReturnVolumeMsg{b, 1.0f});
                    return true;
                }
                beginDrag(DragType::Fader, kDragReturn0 + b, my, rb.volume);
                return true;
            }
        }

        // Master strip
        float masterX = x + 4 + kMaxReturnBuses * (kRetStripW + kStripPadding) + kSeparatorWidth + 4;
        float masterFaderY = y + 2 + 30;
        float masterFaderBottom = y + h - 2 - 22;
        if (mx >= masterX + 4 && mx < masterX + 4 + kFaderWidth + 2 &&
            my >= masterFaderY && my < masterFaderBottom) {
            if (rightClick) {
                m_engine->sendCommand(audio::SetMasterVolumeMsg{1.0f});
                return true;
            }
            beginDrag(DragType::Fader, kDragMaster, my, m_engine->mixer().master().volume);
            return true;
        }

        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_dragging || !m_engine) return false;
        float mx = e.x, my = e.y;

        if (m_dragType == DragType::Fader) {
            float faderH = m_bounds.h - 4 - 74 - 22;
            if (faderH < 20) faderH = 20;
            float deltaY = m_dragStartPos - my;
            float deltaVol = (deltaY / faderH) * 2.0f;
            float newVol = std::clamp(m_dragStartValue + deltaVol, 0.0f, 2.0f);

            if (m_dragTarget == kDragMaster)
                m_engine->sendCommand(audio::SetMasterVolumeMsg{newVol});
            else if (m_dragTarget <= kDragReturn0)
                m_engine->sendCommand(audio::SetReturnVolumeMsg{m_dragTarget - kDragReturn0, newVol});
            return true;
        }

        if (m_dragType == DragType::Pan) {
            float panW = 100.0f;
            float deltaX = mx - m_dragStartPos;
            float deltaPan = (deltaX / panW) * 2.0f;
            float newPan = std::clamp(m_dragStartValue + deltaPan, -1.0f, 1.0f);

            if (m_dragTarget <= kDragReturn0)
                m_engine->sendCommand(audio::SetReturnPanMsg{m_dragTarget - kDragReturn0, newPan});
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_dragging) {
            m_dragging = false;
            m_dragType = DragType::None;
            m_dragTarget = -1;
            releaseMouse();
            return true;
        }
        return false;
    }

private:
    // ─── Helpers ────────────────────────────────────────────────────────

    float fontScale(Font& f) const {
        float pixH = f.pixelHeight();
        return (pixH < 1.0f) ? Theme::kSmallFontSize : Theme::kSmallFontSize / pixH;
    }

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

    enum class DragType { None, Fader, Pan };

    void beginDrag(DragType type, int target, float startPos, float startValue) {
        m_dragging = true;
        m_dragType = type;
        m_dragTarget = target;
        m_dragStartPos = startPos;
        m_dragStartValue = startValue;
        captureMouse();
    }

    // ─── Strip Rendering ────────────────────────────────────────────────

    void paintReturnStrip(Renderer2D& r, Font& f,
                          int idx, float x, float y, float w, float h) {
        r.drawRect(x, y, w, h, Theme::background);
        Color busCol{100, 180, 255};
        float scale = fontScale(f);
        float smallScale = scale * 0.8f;
        const auto& rb = m_engine->mixer().returnBus(idx);

        r.drawRect(x, y, w, 3, busCol);
        const char* names[] = {"Ret A","Ret B","Ret C","Ret D","Ret E","Ret F","Ret G","Ret H"};
        drawText(r, f, names[idx], x + 4, y + 5, scale, Theme::textPrimary);

        float curY = y + 26;
        Color muteCol = rb.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty;
        paintButton(r, f, x + 4, curY, kButtonWidth, kButtonHeight, "M", muteCol,
                    rb.muted ? Color{0,0,0} : Theme::textSecondary);

        curY += kButtonHeight + 6;
        float panW = w - 8, panH = 16;
        r.drawRect(x + 4, curY, panW, panH, Theme::clipSlotEmpty);
        float panCenter = x + 4 + panW * 0.5f;
        r.drawRect(panCenter + rb.pan * (panW * 0.5f - 4) - 4, curY, 8, panH, busCol);

        curY += panH + 8;
        float faderBottom = y + h - 22;
        float faderH = std::max(20.0f, faderBottom - curY);
        paintFader(r, x + 4, curY, kFaderWidth, faderH, rb.volume, busCol);
        paintMeter(r, x + 4 + kFaderWidth + 3, curY, kMeterWidth * 2, faderH,
                   m_returnMeters[idx].peakL, m_returnMeters[idx].peakR);

        float db = rb.volume > 0.001f ? 20.0f * std::log10(rb.volume) : -60.0f;
        char dbText[16];
        if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
        else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
        drawText(r, f, dbText, x + 4, y + h - 18, smallScale, Theme::textDim);
    }

    void paintMasterStrip(Renderer2D& r, Font& f,
                          float x, float y, float w, float h) {
        r.drawRect(x, y, w, h, Color{35, 35, 40});
        Color masterCol = Theme::transportAccent;
        float scale = fontScale(f);
        float smallScale = scale * 0.8f;
        const auto& master = m_engine->mixer().master();

        r.drawRect(x, y, w, 3, masterCol);
        drawText(r, f, "MASTER", x + 4, y + 5, scale, Theme::textPrimary);

        float curY = y + 30;
        float faderBottom = y + h - 22;
        float faderH = std::max(20.0f, faderBottom - curY);
        paintFader(r, x + 4, curY, kFaderWidth + 2, faderH, master.volume, masterCol);
        paintMeter(r, x + kFaderWidth + 10, curY, kMeterWidth * 2 + 2, faderH,
                   m_masterMeter.peakL, m_masterMeter.peakR);

        float db = master.volume > 0.001f ? 20.0f * std::log10(master.volume) : -60.0f;
        char dbText[16];
        if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
        else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
        drawText(r, f, dbText, x + 4, y + h - 18, smallScale, Theme::textDim);
    }

    void paintFader(Renderer2D& r, float x, float y, float w, float h,
                    float value, Color col) {
        float trackX = x + w * 0.5f - 1;
        r.drawRect(trackX, y, 2, h, Theme::clipSlotBorder);
        float frac = std::min(value / 2.0f, 1.0f);
        float knobH = 32;
        float knobY = y + h - frac * h - knobH * 0.5f;
        r.drawRect(x, knobY, w, knobH, col);
        r.drawRect(x + 1, knobY + 2, w - 2, knobH - 4, Color{200, 200, 200});
        float unityY = y + h - 0.5f * h;
        r.drawRect(x - 2, unityY, w + 4, 1, Theme::textDim);
    }

    void paintMeter(Renderer2D& r, float x, float y, float w, float h,
                    float peakL, float peakR) {
        float halfW = w * 0.5f - 1;
        r.drawRect(x, y, halfW, h, Color{20, 20, 22});
        r.drawRect(x + halfW + 2, y, halfW, h, Color{20, 20, 22});

        auto dbToH = [](float peak) -> float {
            if (peak < 0.001f) return 0.0f;
            float db = 20.0f * std::log10(peak);
            return std::max(0.0f, std::min(1.0f, (db + 60.0f) / 60.0f));
        };
        auto meterCol = [](float peak) -> Color {
            if (peak > 1.0f) return {255, 40, 40};
            if (peak > 0.7f) return {255, 200, 50};
            return {80, 220, 80};
        };
        float hL = dbToH(peakL) * h;
        float hR = dbToH(peakR) * h;
        r.drawRect(x, y + h - hL, halfW, hL, meterCol(peakL));
        r.drawRect(x + halfW + 2, y + h - hR, halfW, hR, meterCol(peakR));
    }

    void paintButton(Renderer2D& r, Font& f, float x, float y, float w, float h,
                     const char* label, Color bg, Color text) {
        r.drawRect(x, y, w, h, bg);
        r.drawRectOutline(x, y, w, h, Theme::clipSlotBorder);
        float sc = Theme::kSmallFontSize / f.pixelHeight() * 0.85f;
        float tw = f.textWidth(label, sc);
        drawText(r, f, label, x + (w - tw) * 0.5f, y + 1, sc, text);
    }

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

    ReturnMeter m_returnMeters[kMaxReturnBuses] = {};
    ReturnMeter m_masterMeter = {};

    bool     m_dragging       = false;
    DragType m_dragType       = DragType::None;
    int      m_dragTarget     = -1;
    float    m_dragStartPos   = 0;
    float    m_dragStartValue = 0;

    // Constants
    static constexpr float kMeterWidth     = 6.0f;
    static constexpr float kFaderWidth     = 20.0f;
    static constexpr float kButtonHeight   = 22.0f;
    static constexpr float kButtonWidth    = 28.0f;
    static constexpr float kStripPadding   = 2.0f;
    static constexpr float kSeparatorWidth = 2.0f;
    static constexpr float kRetStripW      = 70.0f;

    static constexpr int kDragMaster  = -1;
    static constexpr int kDragReturn0 = -100;
};

} // namespace fw
} // namespace ui
} // namespace yawn
