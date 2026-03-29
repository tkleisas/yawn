#pragma once
// ReturnMasterPanel — Returns + Master channel strip panel.
//
// Uses framework widgets (FwButton, FwFader, MeterWidget, PanWidget, Label)
// for all controls.  Only the separator and strip backgrounds remain manual.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
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
    ReturnMasterPanel() {
        static const char* retNames[] = {
            "Ret A","Ret B","Ret C","Ret D","Ret E","Ret F","Ret G","Ret H"};

        Color busCol{100, 180, 255};
        Color masterCol = Theme::transportAccent;

        m_stopAllBtn.setOnClick([this]() {
            if (!m_engine) return;
            int numTracks = m_project ? m_project->numTracks() : kMaxTracks;
            for (int t = 0; t < numTracks; ++t) {
                m_engine->sendCommand(audio::StopClipMsg{t});
                m_engine->sendCommand(audio::StopMidiClipMsg{t});
            }
        });

        for (int b = 0; b < kMaxReturnBuses; ++b) {
            auto& rs = m_returnStrips[b];

            rs.nameLabel.setText(retNames[b]);
            rs.nameLabel.setColor(Theme::textPrimary);

            rs.muteBtn.setLabel("M");
            rs.muteBtn.setOnClick([this, b]() {
                if (!m_engine) return;
                bool cur = m_engine->mixer().returnBus(b).muted;
                m_engine->sendCommand(audio::SetReturnMuteMsg{b, !cur});
            });

            rs.pan.setThumbColor(busCol);
            rs.pan.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnPanMsg{b, v});
            });

            rs.fader.setRange(0.0f, 2.0f);
            rs.fader.setTrackColor(busCol);
            rs.fader.setOnChange([this, b](float v) {
                if (!m_engine) return;
                m_engine->sendCommand(audio::SetReturnVolumeMsg{b, v});
            });
        }

        m_masterStrip.nameLabel.setText("MASTER");
        m_masterStrip.nameLabel.setColor(Theme::textPrimary);

        m_masterStrip.fader.setRange(0.0f, 2.0f);
        m_masterStrip.fader.setTrackColor(masterCol);
        m_masterStrip.fader.setOnChange([this](float v) {
            if (!m_engine) return;
            m_engine->sendCommand(audio::SetMasterVolumeMsg{v});
        });
    }

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

    bool isDragging() const { return Widget::capturedWidget() != nullptr; }

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

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;

        r.drawRect(x, y, w, h, Theme::panelBg);
        r.drawRect(x, y, w, 1, Theme::clipSlotBorder);

        float stripY = y + 2;
        float stripH = h - 4;

        float curX = x + 4;
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            if (curX + kRetStripW > x + w) break;
            paintReturnStrip(ctx, b, curX, stripY, kRetStripW, stripH);
            curX += kRetStripW + kStripPadding;
        }

        r.drawRect(curX, y + 4, kSeparatorWidth, h - 8, Theme::clipSlotBorder);
        curX += kSeparatorWidth + 4;

        if (curX + kRetStripW <= x + w)
            paintMasterStrip(ctx, curX, stripY, kRetStripW, stripH);
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_engine) return false;
        float mx = e.x, my = e.y;
        bool rightClick = (e.button == MouseButton::Right);
        float x = m_bounds.x, y = m_bounds.y;

        float curX = x + 4;
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            float rx = curX + b * (kRetStripW + kStripPadding);
            if (mx < rx || mx >= rx + kRetStripW) continue;

            auto& rs = m_returnStrips[b];

            if (!rightClick && hitWidget(rs.muteBtn, mx, my)) {
                return rs.muteBtn.onMouseDown(e);
            }
            if (hitWidget(rs.pan, mx, my)) {
                return rs.pan.onMouseDown(e);
            }
            if (hitWidget(rs.fader, mx, my)) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetReturnVolumeMsg{b, 1.0f});
                    return true;
                }
                return rs.fader.onMouseDown(e);
            }
        }

        float masterX = x + 4
            + kMaxReturnBuses * (kRetStripW + kStripPadding)
            + kSeparatorWidth + 4;

        if (mx >= masterX && mx < masterX + kRetStripW) {
            if (!rightClick && hitWidget(m_stopAllBtn, mx, my)) {
                return m_stopAllBtn.onMouseDown(e);
            }
            if (hitWidget(m_masterStrip.fader, mx, my)) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetMasterVolumeMsg{1.0f});
                    return true;
                }
                return m_masterStrip.fader.onMouseDown(e);
            }
        }

        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseMove(e);
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseUp(e);
        }
        return false;
    }

private:
    struct StripWidgets {
        FwButton   muteBtn;
        PanWidget  pan;
        FwFader    fader;
        MeterWidget meter;
        Label      nameLabel;
        Label      dbLabel;
    };

    bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    void paintStripCommon(UIContext& ctx, StripWidgets& sw,
                           const char* name, float x, float y,
                           float w, float h, Color col,
                           float volume, float peakL, float peakR,
                           bool muted) {
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        float pixH = f.pixelHeight();
        float scale = (pixH < 1.0f) ? Theme::kSmallFontSize
                     : Theme::kSmallFontSize / pixH;
        float smallScale = scale * 0.8f;

        sw.nameLabel.setFontScale(scale);
        sw.nameLabel.layout(Rect{x + 4, y + 5, w - 8, 14}, ctx);
        sw.nameLabel.paint(ctx);

        float curY = y + 26;

        if (&sw.muteBtn != nullptr) {
            sw.muteBtn.setColor(muted ? Color{255, 80, 80} : Theme::clipSlotEmpty);
            sw.muteBtn.setTextColor(muted ? Color{0, 0, 0} : Theme::textSecondary);
            sw.muteBtn.layout(Rect{x + 4, curY, kButtonWidth, kButtonHeight}, ctx);
            sw.muteBtn.paint(ctx);
        }

        curY += kButtonHeight + 6;
        sw.pan.layout(Rect{x + 4, curY, w - 8, 16}, ctx);
        sw.pan.paint(ctx);

        curY += 16 + 8;
        float faderBottom = y + h - 22;
        float faderH = std::max(20.0f, faderBottom - curY);

        sw.fader.setValue(volume);
        sw.fader.layout(Rect{x + 4, curY, kFaderWidth, faderH}, ctx);
        sw.fader.paint(ctx);

        sw.meter.setPeak(peakL, peakR);
        sw.meter.layout(Rect{x + 4 + kFaderWidth + 3, curY,
                             kMeterWidth * 2, faderH}, ctx);
        sw.meter.paint(ctx);

        float db = volume > 0.001f ? 20.0f * std::log10(volume) : -60.0f;
        char dbText[16];
        if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
        else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
        sw.dbLabel.setText(dbText);
        sw.dbLabel.setColor(Theme::textDim);
        sw.dbLabel.setFontScale(smallScale);
        sw.dbLabel.layout(Rect{x + 4, y + h - 18, w - 8, 14}, ctx);
        sw.dbLabel.paint(ctx);
    }

    void paintReturnStrip(UIContext& ctx, int idx, float x, float y,
                           float w, float h) {
        auto& r = *ctx.renderer;
        Color busCol{100, 180, 255};
        auto& rs = m_returnStrips[idx];
        const auto& rb = m_engine->mixer().returnBus(idx);

        r.drawRect(x, y, w, h, Theme::background);
        r.drawRect(x, y, w, 3, busCol);

        rs.pan.setValue(rb.pan);
        rs.fader.setTrackColor(busCol);

        paintStripCommon(ctx, rs, nullptr, x, y, w, h, busCol,
                         rb.volume,
                         m_returnMeters[idx].peakL,
                         m_returnMeters[idx].peakR,
                         rb.muted);
    }

    void paintMasterStrip(UIContext& ctx, float x, float y,
                           float w, float h) {
        auto& r = *ctx.renderer;
        Color masterCol = Theme::transportAccent;
        const auto& master = m_engine->mixer().master();

        r.drawRect(x, y, w, h, Color{35, 35, 40});
        r.drawRect(x, y, w, 3, masterCol);

        m_masterStrip.fader.setValue(master.volume);

        m_masterStrip.nameLabel.layout(Rect{x + 4, y + 5, w - 8, 14}, ctx);
        m_masterStrip.nameLabel.paint(ctx);

        m_stopAllBtn.setColor(Theme::clipSlotEmpty);
        m_stopAllBtn.setTextColor(Theme::textSecondary);
        m_stopAllBtn.layout(Rect{x + 4, y + 22, w - 8, kButtonHeight}, ctx);
        m_stopAllBtn.paint(ctx);
        {
            auto& sb = m_stopAllBtn.bounds();
            float iconSize = 8.0f;
            float iconX = sb.x + (sb.w - iconSize) * 0.5f;
            float iconY = sb.y + (sb.h - iconSize) * 0.5f;
            r.drawRect(iconX, iconY, iconSize, iconSize, Theme::textSecondary);
        }

        float curY = y + 22 + kButtonHeight + 4;
        float faderBottom = y + h - 22;
        float faderH = std::max(20.0f, faderBottom - curY);

        m_masterStrip.fader.layout(Rect{x + 4, curY, kFaderWidth + 2, faderH}, ctx);
        m_masterStrip.fader.paint(ctx);

        m_masterStrip.meter.setPeak(m_masterMeter.peakL, m_masterMeter.peakR);
        m_masterStrip.meter.layout(Rect{x + kFaderWidth + 10, curY,
                                        kMeterWidth * 2 + 2, faderH}, ctx);
        m_masterStrip.meter.paint(ctx);

        auto& f = *ctx.font;
        float pixH = f.pixelHeight();
        float smallScale = (pixH < 1.0f) ? Theme::kSmallFontSize * 0.8f
                         : Theme::kSmallFontSize * 0.8f / pixH;
        float db = master.volume > 0.001f ? 20.0f * std::log10(master.volume) : -60.0f;
        char dbText[16];
        if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
        else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
        m_masterStrip.dbLabel.setText(dbText);
        m_masterStrip.dbLabel.setColor(Theme::textDim);
        m_masterStrip.dbLabel.setFontScale(smallScale);
        m_masterStrip.dbLabel.layout(Rect{x + 4, y + h - 18, w - 8, 14}, ctx);
        m_masterStrip.dbLabel.paint(ctx);
    }

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

    ReturnMeter m_returnMeters[kMaxReturnBuses] = {};
    ReturnMeter m_masterMeter = {};

    StripWidgets m_returnStrips[kMaxReturnBuses];
    StripWidgets m_masterStrip;
    FwButton     m_stopAllBtn;

    static constexpr float kMeterWidth     = 6.0f;
    static constexpr float kFaderWidth     = 20.0f;
    static constexpr float kButtonHeight   = 22.0f;
    static constexpr float kButtonWidth    = 28.0f;
    static constexpr float kStripPadding   = 2.0f;
    static constexpr float kSeparatorWidth = 2.0f;
    static constexpr float kRetStripW      = 70.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
