#pragma once
// MixerPanel — Framework widget replacement for MixerView.
//
// Uses framework widgets (FwButton, FwFader, MeterWidget, PanWidget,
// ScrollBar, Label) for all controls.  Only the color bar, send dots,
// column borders, and strip background remain as manual rendering.

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
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

struct MixerMeter {
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class MixerPanel : public Widget {
public:
    MixerPanel() {
        m_mixLabel.setText("MIX");
        m_mixLabel.setColor(Theme::textDim);

        m_scrollbar.setOnScroll([this](float pos) {
            m_scrollX = pos;
            if (m_onScrollChanged) m_onScrollChanged(pos);
        });

        for (int t = 0; t < kMaxTracks; ++t) {
            setupStripCallbacks(t);
        }
    }

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
    }

    void updateMeter(int trackIndex, float peakL, float peakR) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackMeters[trackIndex] = {peakL, peakR};
        }
    }

    void setSelectedTrack(int track) { m_selectedTrack = track; }
    int  selectedTrack() const { return m_selectedTrack; }
    bool isDragging() const { return Widget::capturedWidget() != nullptr; }
    float preferredHeight() const { return kMixerHeight; }
    float scrollX() const { return m_scrollX; }
    void setScrollX(float sx) { m_scrollX = sx; }
    void setOnScrollChanged(std::function<void(float)> cb) { m_onScrollChanged = std::move(cb); }
    void setOnTrackArmedChanged(std::function<void(int, bool)> cb) { m_onTrackArmed = std::move(cb); }

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

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w,  h = m_bounds.h;

        r.drawRect(x, y, w, h, Theme::panelBg);
        r.drawRect(x, y, w, 1, Theme::clipSlotBorder);

        float stripY = y + 2;
        float stripH = h - 4 - kScrollbarH;

        float pixH = ctx.font ? ctx.font->pixelHeight() : 0;
        float labelScale = (pixH < 1.0f) ? Theme::kSmallFontSize
                         : Theme::kSmallFontSize / pixH;
        labelScale *= 0.85f;
        m_mixLabel.setFontScale(labelScale);
        m_mixLabel.layout(Rect{x + 6, y + stripH * 0.5f - 8, 30, 16}, ctx);
        m_mixLabel.paint(ctx);

        float gridX = x + Theme::kSceneLabelWidth;
        float gridW = w - Theme::kSceneLabelWidth;

        r.pushClip(gridX, y, gridW, h - kScrollbarH);
        for (int t = 0; t < m_project->numTracks(); ++t) {
            float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
            if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
            paintStrip(ctx, t, sx, stripY, Theme::kTrackWidth, stripH);
        }
        r.popClip();

        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        m_scrollbar.setContentSize(contentW);
        m_scrollbar.setScrollPos(m_scrollX);
        m_scrollbar.layout(Rect{gridX, y + h - kScrollbarH, gridW, kScrollbarH}, ctx);
        m_scrollbar.paint(ctx);
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_project || !m_engine) return false;
        float mx = e.x, my = e.y;
        bool rightClick = (e.button == MouseButton::Right);

        if (hitWidget(m_scrollbar, mx, my)) {
            return m_scrollbar.onMouseDown(e);
        }

        float x = m_bounds.x, y = m_bounds.y;
        float gridX = x + Theme::kSceneLabelWidth;
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;

        for (int t = 0; t < m_project->numTracks(); ++t) {
            float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
            if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
            if (mx < sx || mx >= sx + Theme::kTrackWidth) continue;

            auto& s = m_strips[t];

            if (!rightClick && hitWidget(s.muteBtn, mx, my)) {
                return s.muteBtn.onMouseDown(e);
            }
            if (!rightClick && hitWidget(s.soloBtn, mx, my)) {
                return s.soloBtn.onMouseDown(e);
            }
            if (!rightClick && hitWidget(s.armBtn, mx, my)) {
                return s.armBtn.onMouseDown(e);
            }
            if (hitWidget(s.monBtn, mx, my)) {
                if (rightClick) {
                    m_project->track(t).monitorMode = Track::MonitorMode::Auto;
                    m_engine->sendCommand(
                        audio::SetTrackMonitorMsg{t,
                            static_cast<uint8_t>(Track::MonitorMode::Auto)});
                    return true;
                }
                return s.monBtn.onMouseDown(e);
            }
            if (hitWidget(s.pan, mx, my)) {
                return s.pan.onMouseDown(e);
            }
            if (hitWidget(s.fader, mx, my)) {
                if (rightClick) {
                    m_engine->sendCommand(audio::SetTrackVolumeMsg{t, 1.0f});
                    return true;
                }
                return s.fader.onMouseDown(e);
            }
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseMove(e);
        }

        float sbY = m_bounds.y + m_bounds.h - kScrollbarH;
        if (e.y >= sbY && e.y < sbY + kScrollbarH) {
            m_scrollbar.onMouseMove(e);
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (auto* cap = Widget::capturedWidget()) {
            return cap->onMouseUp(e);
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_project) return false;
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        float maxScroll = std::max(0.0f, contentW - gridW);
        m_scrollX = std::clamp(m_scrollX - e.dx * 30.0f, 0.0f, maxScroll);
        m_scrollbar.setScrollPos(m_scrollX);
        if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
        return true;
    }

private:
    struct TrackStrip {
        FwButton muteBtn;
        FwButton soloBtn;
        FwButton armBtn;
        FwButton monBtn;
        PanWidget pan;
        FwFader fader;
        MeterWidget meter;
        Label nameLabel;
        Label dbLabel;
    };

    bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    void setupStripCallbacks(int t) {
        auto& s = m_strips[t];

        s.muteBtn.setLabel("M");
        s.muteBtn.setOnClick([this, t]() {
            if (!m_engine) return;
            bool cur = m_engine->mixer().trackChannel(t).muted;
            m_engine->sendCommand(audio::SetTrackMuteMsg{t, !cur});
        });

        s.soloBtn.setLabel("S");
        s.soloBtn.setOnClick([this, t]() {
            if (!m_engine) return;
            bool cur = m_engine->mixer().trackChannel(t).soloed;
            m_engine->sendCommand(audio::SetTrackSoloMsg{t, !cur});
        });

        s.armBtn.setLabel("R");
        s.armBtn.setOnClick([this, t]() {
            if (!m_project || !m_engine) return;
            bool cur = m_project->track(t).armed;
            m_project->track(t).armed = !cur;
            m_engine->sendCommand(audio::SetTrackArmedMsg{t, !cur});
            if (m_onTrackArmed) m_onTrackArmed(t, !cur);
        });

        s.monBtn.setOnClick([this, t]() {
            if (!m_project || !m_engine) return;
            auto& track = m_project->track(t);
            if (track.monitorMode == Track::MonitorMode::Auto)
                track.monitorMode = Track::MonitorMode::In;
            else if (track.monitorMode == Track::MonitorMode::In)
                track.monitorMode = Track::MonitorMode::Off;
            else
                track.monitorMode = Track::MonitorMode::Auto;
            m_engine->sendCommand(
                audio::SetTrackMonitorMsg{t,
                    static_cast<uint8_t>(track.monitorMode)});
        });

        s.pan.setOnChange([this, t](float v) {
            if (!m_engine) return;
            m_engine->sendCommand(audio::SetTrackPanMsg{t, v});
        });

        s.fader.setRange(0.0f, 2.0f);
        s.fader.setOnChange([this, t](float v) {
            if (!m_engine) return;
            m_engine->sendCommand(audio::SetTrackVolumeMsg{t, v});
        });
    }

    void paintStrip(UIContext& ctx, int idx, float sx, float stripY,
                     float stripW, float stripH) {
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        auto& s = m_strips[idx];
        float pad = Theme::kSlotPadding;
        float ix = sx + pad, iw = stripW - pad * 2;
        const auto& ch = m_engine->mixer().trackChannel(idx);
        Color col = Theme::trackColors[
            m_project->track(idx).colorIndex % Theme::kNumTrackColors];

        r.drawRect(ix, stripY, iw, stripH, Theme::background);
        if (idx == m_selectedTrack)
            r.drawRect(ix, stripY, iw, stripH, Color{50, 55, 65, 255});

        r.drawRect(ix, stripY, iw, 3, col);

        char nameBuf[16];
        std::snprintf(nameBuf, sizeof(nameBuf), "%d", idx + 1);
        s.nameLabel.setText(nameBuf);
        s.nameLabel.layout(Rect{ix + 4, stripY + 5, iw - 8, 14}, ctx);
        s.nameLabel.paint(ctx);

        float curY = stripY + 30;
        float btnW = std::min((iw - 12) * 0.5f, kButtonWidth);

        s.muteBtn.setColor(ch.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty);
        s.muteBtn.setTextColor(ch.muted ? Color{0, 0, 0} : Theme::textSecondary);
        s.muteBtn.layout(Rect{ix + 4, curY, btnW, kButtonHeight}, ctx);
        s.muteBtn.paint(ctx);

        s.soloBtn.setColor(ch.soloed ? Color{255, 200, 50} : Theme::clipSlotEmpty);
        s.soloBtn.setTextColor(ch.soloed ? Color{0, 0, 0} : Theme::textSecondary);
        s.soloBtn.layout(Rect{ix + 4 + btnW + 2, curY, btnW, kButtonHeight}, ctx);
        s.soloBtn.paint(ctx);

        curY += kButtonHeight + 2;
        bool armed = m_project->track(idx).armed;
        s.armBtn.setColor(armed ? Color{200, 40, 40} : Theme::clipSlotEmpty);
        s.armBtn.setTextColor(armed ? Color{255, 255, 255} : Theme::textSecondary);
        s.armBtn.layout(Rect{ix + 4, curY, iw - 8, kButtonHeight}, ctx);
        s.armBtn.paint(ctx);

        curY += kButtonHeight + 2;
        auto mode = m_project->track(idx).monitorMode;
        const char* monLabel = "Auto";
        Color monBg = Theme::clipSlotEmpty;
        if (mode == Track::MonitorMode::In) {
            monLabel = "In";
            monBg = Color{40, 80, 40};
        } else if (mode == Track::MonitorMode::Off) {
            monLabel = "Off";
            monBg = Color{50, 40, 40};
        }
        s.monBtn.setLabel(monLabel);
        s.monBtn.setColor(monBg);
        s.monBtn.setTextColor((mode == Track::MonitorMode::In) ? Color{120, 230, 120}
                                                                : Theme::textSecondary);
        s.monBtn.layout(Rect{ix + 4, curY, iw - 8, kButtonHeight}, ctx);
        s.monBtn.paint(ctx);

        curY += kButtonHeight + 6;
        s.pan.setValue(ch.pan);
        s.pan.setThumbColor(col);
        s.pan.layout(Rect{ix + 4, curY, iw - 8, 16}, ctx);
        s.pan.paint(ctx);

        curY += 16 + 4;
        int maxDots = std::min(kMaxReturnBuses,
                               static_cast<int>((iw - 8 + 2) / 10));
        for (int d = 0; d < maxDots; ++d) {
            const auto& send = ch.sends[d];
            Color dotCol = (send.enabled && send.level > 0.01f)
                ? Color{100, 180, 255}.withAlpha(
                      static_cast<uint8_t>(100 + send.level * 155))
                : Theme::clipSlotEmpty;
            r.drawRect(ix + 4 + d * 10, curY, 7, 7, dotCol);
        }

        curY += 12;
        float faderBottom = stripY + stripH - 22;
        float faderH = std::max(20.0f, faderBottom - curY);

        s.fader.setValue(ch.volume);
        s.fader.setTrackColor(col);
        s.fader.layout(Rect{ix + 4, curY, kFaderWidth, faderH}, ctx);
        s.fader.paint(ctx);

        s.meter.setPeak(m_trackMeters[idx].peakL, m_trackMeters[idx].peakR);
        s.meter.layout(Rect{ix + 4 + kFaderWidth + 3, curY,
                            kMeterWidth * 2, faderH}, ctx);
        s.meter.paint(ctx);

        float db = ch.volume > 0.001f ? 20.0f * std::log10(ch.volume) : -60.0f;
        char dbText[16];
        if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
        else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
        s.dbLabel.setText(dbText);
        s.dbLabel.setColor(Theme::textDim);
        float pixH = f.pixelHeight();
        float smallScale = (pixH < 1.0f) ? Theme::kSmallFontSize * 0.8f
                           : Theme::kSmallFontSize * 0.8f / pixH;
        s.dbLabel.setFontScale(smallScale);
        s.dbLabel.layout(Rect{ix + 4, stripY + stripH - 18, iw - 8, 14}, ctx);
        s.dbLabel.paint(ctx);

        r.drawRect(sx + stripW - 1, stripY, 1, stripH, Theme::clipSlotBorder);
    }

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

    MixerMeter m_trackMeters[kMaxTracks] = {};
    TrackStrip m_strips[kMaxTracks];
    ScrollBar  m_scrollbar;
    Label      m_mixLabel;

    int   m_selectedTrack = 0;
    float m_scrollX       = 0.0f;

    static constexpr float kMixerHeight  = 280.0f;
    static constexpr float kMeterWidth   = 6.0f;
    static constexpr float kFaderWidth   = 20.0f;
    static constexpr float kButtonHeight = 22.0f;
    static constexpr float kButtonWidth  = 28.0f;
    static constexpr float kScrollbarH   = 12.0f;

    std::function<void(float)>        m_onScrollChanged;
    std::function<void(int, bool)>    m_onTrackArmed;
};

} // namespace fw
} // namespace ui
} // namespace yawn
