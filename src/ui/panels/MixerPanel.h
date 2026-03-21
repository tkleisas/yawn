#pragma once
// MixerPanel — Framework widget replacement for MixerView.
//
// This is a fw::Widget that renders the mixer and handles events internally.
// It replaces both MixerView (the panel) and MixerViewWrapper (the wrapper).
// Only included from App.cpp — never compiled in test builds.

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

// Meter data from the audio thread
struct MixerMeter {
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class MixerPanel : public Widget {
public:
    MixerPanel() = default;

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
    }

    void updateMeter(int trackIndex, float peakL, float peakR) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackMeters[trackIndex] = {peakL, peakR};
        } else if (trackIndex == -1) {
            m_masterMeter = {peakL, peakR};
        } else if (trackIndex >= -5 && trackIndex <= -2) {
            int busIdx = -(trackIndex + 2);
            m_returnMeters[busIdx] = {peakL, peakR};
        }
    }

    void setSelectedTrack(int track) { m_selectedTrack = track; }
    int  selectedTrack() const { return m_selectedTrack; }
    bool isDragging() const { return m_dragging; }
    float preferredHeight() const { return kMixerHeight; }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, kMixerHeight});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
        if (!m_project || !m_engine) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w,  h = m_bounds.h;

        // Background
        r.drawRect(x, y, w, h, Theme::panelBg);
        r.drawRect(x, y, w, 1, Theme::clipSlotBorder);

        float stripY = y + 2;
        float stripH = h - 4;

        // "MIX" label in scene gutter
        float scale = fontScale(f) * 0.85f;
        drawText(r, f, "MIX", x + 6, y + stripH * 0.5f - 8, scale, Theme::textDim);

        // Track channel strips
        float gridX = x + Theme::kSceneLabelWidth;
        for (int t = 0; t < m_project->numTracks(); ++t) {
            float sx = gridX + t * Theme::kTrackWidth;
            paintChannelStrip(r, f, t, sx, stripY, Theme::kTrackWidth, stripH);
        }

        // Separator after tracks
        float afterTracks = gridX + m_project->numTracks() * Theme::kTrackWidth;
        r.drawRect(afterTracks, y + 4, kSeparatorWidth, h - 8, Theme::clipSlotBorder);
        float retX = afterTracks + kSeparatorWidth + 4;

        // Return bus strips
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            paintReturnStrip(r, f, b, retX, stripY, kRetStripW, stripH);
            retX += kRetStripW + kStripPadding;
        }

        // Separator before master
        r.drawRect(retX, y + 4, kSeparatorWidth, h - 8, Theme::clipSlotBorder);
        retX += kSeparatorWidth + 4;

        // Master strip
        paintMasterStrip(r, f, retX, stripY, kRetStripW, stripH);
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_project || !m_engine) return false;

        float mx = e.x, my = e.y;
        bool rightClick = (e.button == MouseButton::Right);
        float x = m_bounds.x, y = m_bounds.y;
        float gridX = x + Theme::kSceneLabelWidth;

        // --- Track channel strips ---
        for (int t = 0; t < m_project->numTracks(); ++t) {
            float sx = gridX + t * Theme::kTrackWidth;
            float ix = sx + Theme::kSlotPadding;
            float iw = Theme::kTrackWidth - Theme::kSlotPadding * 2;
            if (mx < sx || mx >= sx + Theme::kTrackWidth) continue;

            // Mute / Solo buttons
            float btnY = y + 2 + 30;
            float btnW = std::min((iw - 12) * 0.5f, kButtonWidth);
            if (my >= btnY && my < btnY + kButtonHeight) {
                if (mx >= ix + 4 && mx < ix + 4 + btnW) {
                    bool cur = m_engine->mixer().trackChannel(t).muted;
                    m_engine->sendCommand(audio::SetTrackMuteMsg{t, !cur});
                    return true;
                }
                if (mx >= ix + 4 + btnW + 2 && mx < ix + 4 + 2 * btnW + 2) {
                    bool cur = m_engine->mixer().trackChannel(t).soloed;
                    m_engine->sendCommand(audio::SetTrackSoloMsg{t, !cur});
                    return true;
                }
            }

            // Pan bar
            float panY = y + 60;
            float panW = iw - 8;
            if (my >= panY && my < panY + 8 && mx >= ix + 4 && mx < ix + 4 + panW) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetTrackPanMsg{t, 0.0f});
                    return true;
                }
                beginDrag(DragType::Pan, t, mx, m_engine->mixer().trackChannel(t).pan);
                return true;
            }

            // Fader
            float faderY = y + 84;
            float faderBottom = y + m_bounds.h - 2 - 22;
            if (my >= faderY && my < faderBottom && mx >= ix + 4 && mx < ix + 4 + kFaderWidth) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetTrackVolumeMsg{t, 1.0f});
                    return true;
                }
                beginDrag(DragType::Fader, t, my, m_engine->mixer().trackChannel(t).volume);
                return true;
            }
        }

        // --- Return bus strips ---
        float afterTracks = gridX + m_project->numTracks() * Theme::kTrackWidth;
        float retX = afterTracks + kSeparatorWidth + 4;
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            float rx = retX + b * (kRetStripW + kStripPadding);
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
            float faderBottom = y + m_bounds.h - 2 - 22;
            if (my >= faderY && my < faderBottom && mx >= rx + 4 && mx < rx + 4 + kFaderWidth) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetReturnVolumeMsg{b, 1.0f});
                    return true;
                }
                beginDrag(DragType::Fader, kDragReturn0 + b, my, rb.volume);
                return true;
            }
        }

        // --- Master strip ---
        float masterX = retX + kMaxReturnBuses * (kRetStripW + kStripPadding) + kSeparatorWidth + 4;
        float masterFaderY = y + 2 + 30;
        float masterFaderBottom = y + m_bounds.h - 2 - 22;
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
            float faderH = m_bounds.h - 4 - 84 - 22;
            if (faderH < 20) faderH = 20;
            float deltaY = m_dragStartPos - my;
            float deltaVol = (deltaY / faderH) * 2.0f;
            float newVol = std::clamp(m_dragStartValue + deltaVol, 0.0f, 2.0f);

            if (m_dragTarget >= 0)
                m_engine->sendCommand(audio::SetTrackVolumeMsg{m_dragTarget, newVol});
            else if (m_dragTarget == kDragMaster)
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

            if (m_dragTarget >= 0)
                m_engine->sendCommand(audio::SetTrackPanMsg{m_dragTarget, newPan});
            else if (m_dragTarget <= kDragReturn0)
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

    void paintChannelStrip(Renderer2D& r, Font& f,
                           int idx, float x, float y, float w, float h) {
        float pad = Theme::kSlotPadding;
        float ix = x + pad, iw = w - pad * 2;
        float scale = fontScale(f);
        float smallScale = scale * 0.8f;

        r.drawRect(ix, y, iw, h, Theme::background);
        if (idx == m_selectedTrack)
            r.drawRect(ix, y, iw, h, Color{50, 55, 65, 255});

        Color col = Theme::trackColors[m_project->track(idx).colorIndex % Theme::kNumTrackColors];
        const auto& ch = m_engine->mixer().trackChannel(idx);

        // Color bar
        r.drawRect(ix, y, iw, 3, col);

        // Track number
        float curY = y + 5;
        char name[16];
        std::snprintf(name, sizeof(name), "%d", idx + 1);
        drawText(r, f, name, ix + 4, curY, scale, Theme::textPrimary);

        // Mute / Solo
        curY = y + 30;
        Color muteCol = ch.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty;
        Color soloCol = ch.soloed ? Color{255, 200, 50} : Theme::clipSlotEmpty;
        float btnW = std::min((iw - 12) * 0.5f, kButtonWidth);
        paintButton(r, f, ix + 4, curY, btnW, kButtonHeight, "M", muteCol,
                    ch.muted ? Color{0,0,0} : Theme::textSecondary);
        paintButton(r, f, ix + 4 + btnW + 2, curY, btnW, kButtonHeight, "S", soloCol,
                    ch.soloed ? Color{0,0,0} : Theme::textSecondary);

        // Pan bar
        curY += kButtonHeight + 6;
        float panW = iw - 8, panH = 16;
        r.drawRect(ix + 4, curY, panW, panH, Theme::clipSlotEmpty);
        float panCenter = ix + 4 + panW * 0.5f;
        float panPos = panCenter + ch.pan * (panW * 0.5f - 4);
        r.drawRect(panPos - 4, curY, 8, panH, col);

        // Send dots
        curY += panH + 4;
        int maxDots = std::min(kMaxReturnBuses, static_cast<int>((panW + 2) / 10));
        for (int s = 0; s < maxDots; ++s) {
            const auto& send = ch.sends[s];
            Color dotCol = (send.enabled && send.level > 0.01f)
                ? Color{100, 180, 255}.withAlpha(static_cast<uint8_t>(100 + send.level * 155))
                : Theme::clipSlotEmpty;
            r.drawRect(ix + 4 + s * 10, curY, 7, 7, dotCol);
        }

        // Fader + Meter
        curY += 12;
        float faderBottom = y + h - 22;
        float faderH = std::max(20.0f, faderBottom - curY);
        paintFader(r, ix + 4, curY, kFaderWidth, faderH, ch.volume, col);
        paintMeter(r, ix + 4 + kFaderWidth + 3, curY, kMeterWidth * 2, faderH,
                   m_trackMeters[idx].peakL, m_trackMeters[idx].peakR);

        // dB label
        float db = ch.volume > 0.001f ? 20.0f * std::log10(ch.volume) : -60.0f;
        char dbText[16];
        if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
        else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
        drawText(r, f, dbText, ix + 4, y + h - 18, smallScale, Theme::textDim);

        // Column border
        r.drawRect(x + w - 1, y, 1, h, Theme::clipSlotBorder);
    }

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

    MixerMeter m_trackMeters[kMaxTracks] = {};
    MixerMeter m_returnMeters[kMaxReturnBuses] = {};
    MixerMeter m_masterMeter = {};

    bool     m_dragging       = false;
    DragType m_dragType       = DragType::None;
    int      m_dragTarget     = -1;
    float    m_dragStartPos   = 0;
    float    m_dragStartValue = 0;
    int      m_selectedTrack  = 0;

    // Layout constants
    static constexpr float kMixerHeight  = 280.0f;
    static constexpr float kStripWidth   = 80.0f;
    static constexpr float kMeterWidth   = 6.0f;
    static constexpr float kFaderWidth   = 20.0f;
    static constexpr float kButtonHeight = 22.0f;
    static constexpr float kButtonWidth  = 28.0f;
    static constexpr float kStripPadding = 2.0f;
    static constexpr float kSeparatorWidth = 2.0f;
    static constexpr float kRetStripW    = 70.0f;

    static constexpr int kDragMaster  = -1;
    static constexpr int kDragReturn0 = -100;
};

} // namespace fw
} // namespace ui
} // namespace yawn
