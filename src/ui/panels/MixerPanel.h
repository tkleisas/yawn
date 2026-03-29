#pragma once
// MixerPanel — Framework widget replacement for MixerView.
//
// Uses framework widgets (FwButton, FwFader, MeterWidget, PanWidget,
// ScrollBar, Label, FwDropDown) for all controls. I/O routing section
// shows audio input + mono for Audio tracks, MIDI in/out for MIDI tracks.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "audio/Mixer.h"
#include "audio/AudioEngine.h"
#include "midi/MidiEngine.h"
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

    void init(Project* project, audio::AudioEngine* engine, midi::MidiEngine* midiEngine = nullptr) {
        m_project = project;
        m_engine  = engine;
        m_midiEngine = midiEngine;
    }

    void setMidiEngine(midi::MidiEngine* me) { m_midiEngine = me; }

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

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, kMixerHeight});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

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

        // Paint open dropdown overlays on top (outside clip so they're not cut off)
        for (int t = 0; t < m_project->numTracks(); ++t) {
            float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
            if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
            auto& s = m_strips[t];
            if (m_project->track(t).type == Track::Type::Audio) {
                s.audioInputDrop.paintOverlay(ctx);
            } else {
                s.midiInDrop.paintOverlay(ctx);
                s.midiInChDrop.paintOverlay(ctx);
                s.midiOutDrop.paintOverlay(ctx);
                s.midiOutChDrop.paintOverlay(ctx);
            }
        }

        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        m_scrollbar.setContentSize(contentW);
        m_scrollbar.setScrollPos(m_scrollX);
        m_scrollbar.layout(Rect{gridX, y + h - kScrollbarH, gridW, kScrollbarH}, ctx);
        m_scrollbar.paint(ctx);
    }

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

        // First pass: check if any open dropdown popup was clicked
        for (int t = 0; t < m_project->numTracks(); ++t) {
            float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
            if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
            auto& s = m_strips[t];
            if (m_project->track(t).type == Track::Type::Audio) {
                if (s.audioInputDrop.hitPopup(mx, my))
                    return s.audioInputDrop.handlePopupClick(mx, my);
            } else {
                if (s.midiInDrop.hitPopup(mx, my))
                    return s.midiInDrop.handlePopupClick(mx, my);
                if (s.midiInChDrop.hitPopup(mx, my))
                    return s.midiInChDrop.handlePopupClick(mx, my);
                if (s.midiOutDrop.hitPopup(mx, my))
                    return s.midiOutDrop.handlePopupClick(mx, my);
                if (s.midiOutChDrop.hitPopup(mx, my))
                    return s.midiOutChDrop.handlePopupClick(mx, my);
            }
        }

        // Close any open dropdowns if click is outside all popups
        for (int t = 0; t < m_project->numTracks(); ++t) {
            auto& s = m_strips[t];
            if (m_project->track(t).type == Track::Type::Audio) {
                if (s.audioInputDrop.isOpen()) { s.audioInputDrop.close(); }
            } else {
                if (s.midiInDrop.isOpen()) s.midiInDrop.close();
                if (s.midiInChDrop.isOpen()) s.midiInChDrop.close();
                if (s.midiOutDrop.isOpen()) s.midiOutDrop.close();
                if (s.midiOutChDrop.isOpen()) s.midiOutChDrop.close();
            }
        }

        for (int t = 0; t < m_project->numTracks(); ++t) {
            float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
            if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
            if (mx < sx || mx >= sx + Theme::kTrackWidth) continue;

            auto& s = m_strips[t];

            if (!rightClick && hitWidget(s.stopBtn, mx, my))
                return s.stopBtn.onMouseDown(e);
            if (!rightClick && hitWidget(s.muteBtn, mx, my))
                return s.muteBtn.onMouseDown(e);
            if (!rightClick && hitWidget(s.soloBtn, mx, my))
                return s.soloBtn.onMouseDown(e);
            if (!rightClick && hitWidget(s.armBtn, mx, my))
                return s.armBtn.onMouseDown(e);
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

            if (m_project->track(t).type == Track::Type::Audio) {
                if (!rightClick && hitWidget(s.audioInputDrop, mx, my))
                    return s.audioInputDrop.onMouseDown(e);
                if (!rightClick && hitWidget(s.monoBtn, mx, my))
                    return s.monoBtn.onMouseDown(e);
            } else {
                if (!rightClick && hitWidget(s.midiInDrop, mx, my))
                    return s.midiInDrop.onMouseDown(e);
                if (!rightClick && hitWidget(s.midiInChDrop, mx, my))
                    return s.midiInChDrop.onMouseDown(e);
                if (!rightClick && hitWidget(s.midiOutDrop, mx, my))
                    return s.midiOutDrop.onMouseDown(e);
                if (!rightClick && hitWidget(s.midiOutChDrop, mx, my))
                    return s.midiOutChDrop.onMouseDown(e);
            }

            if (hitWidget(s.pan, mx, my))
                return s.pan.onMouseDown(e);
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

        // Forward mouse move to open dropdowns for hover tracking
        if (m_project) {
            float x = m_bounds.x;
            float gridX = x + Theme::kSceneLabelWidth;
            float gridW = m_bounds.w - Theme::kSceneLabelWidth;
            for (int t = 0; t < m_project->numTracks(); ++t) {
                float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
                if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
                auto& s = m_strips[t];
                if (m_project->track(t).type == Track::Type::Audio) {
                    s.audioInputDrop.onMouseMove(e);
                } else {
                    s.midiInDrop.onMouseMove(e);
                    s.midiInChDrop.onMouseMove(e);
                    s.midiOutDrop.onMouseMove(e);
                    s.midiOutChDrop.onMouseMove(e);
                }
            }
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
        FwButton stopBtn;
        FwButton muteBtn;
        FwButton soloBtn;
        FwButton armBtn;
        FwButton monBtn;
        FwDropDown audioInputDrop;
        FwButton monoBtn;
        FwDropDown midiInDrop;
        FwDropDown midiInChDrop;
        FwDropDown midiOutDrop;
        FwDropDown midiOutChDrop;
        Label midiRxLabel;
        Label midiTxLabel;
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

        s.stopBtn.setOnClick([this, t]() {
            if (!m_engine) return;
            m_engine->sendCommand(audio::StopClipMsg{t});
            m_engine->sendCommand(audio::StopMidiClipMsg{t});
        });

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

        s.audioInputDrop.setOnChange([this, t](int idx) {
            if (!m_project || !m_engine) return;
            m_project->track(t).audioInputCh = idx;
            m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, idx});
        });

        s.monoBtn.setLabel("S");
        s.monoBtn.setOnClick([this, t]() {
            if (!m_project || !m_engine) return;
            bool cur = m_project->track(t).mono;
            m_project->track(t).mono = !cur;
            m_engine->sendCommand(audio::SetTrackMonoMsg{t, !cur});
        });

        s.midiInDrop.setOnChange([this, t](int idx) {
            if (!m_project) return;
            if (idx == 0) m_project->track(t).midiInputPort = -1;
            else if (idx == 1) m_project->track(t).midiInputPort = -2;
            else m_project->track(t).midiInputPort = idx - 2;
        });

        s.midiInChDrop.setOnChange([this, t](int idx) {
            if (!m_project) return;
            m_project->track(t).midiInputChannel = (idx == 0) ? -1 : idx - 1;
        });

        s.midiOutDrop.setOnChange([this, t](int idx) {
            if (!m_project) return;
            m_project->track(t).midiOutputPort = (idx == 0) ? -1 : idx - 1;
            if (m_engine) m_engine->sendCommand(
                audio::SetTrackMidiOutputMsg{t, m_project->track(t).midiOutputPort,
                                              m_project->track(t).midiOutputChannel});
        });

        s.midiOutChDrop.setOnChange([this, t](int idx) {
            if (!m_project) return;
            m_project->track(t).midiOutputChannel = (idx == 0) ? -1 : idx - 1;
            if (m_engine) m_engine->sendCommand(
                audio::SetTrackMidiOutputMsg{t, m_project->track(t).midiOutputPort,
                                              m_project->track(t).midiOutputChannel});
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
        const auto& track = m_project->track(idx);
        Color col = Theme::trackColors[track.colorIndex % Theme::kNumTrackColors];

        r.drawRect(ix, stripY, iw, stripH, Theme::background);
        if (idx == m_selectedTrack)
            r.drawRect(ix, stripY, iw, stripH, Color{50, 55, 65, 255});

        r.drawRect(ix, stripY, iw, 3, col);

        char nameBuf[16];
        std::snprintf(nameBuf, sizeof(nameBuf), "%d", idx + 1);
        s.nameLabel.setText(nameBuf);
        s.nameLabel.layout(Rect{ix + 4, stripY + 5, iw - 8, 14}, ctx);
        s.nameLabel.paint(ctx);

        float curY = stripY + 24;
        float btnW = std::min((iw - 16) / 3.0f, kButtonWidth);

        // Stop / Mute / Solo row
        {
            auto& sb = s.stopBtn.bounds();
            s.stopBtn.setColor(Theme::clipSlotEmpty);
            s.stopBtn.setTextColor(Theme::textSecondary);
            s.stopBtn.layout(Rect{ix + 4, curY, btnW, kButtonHeight}, ctx);
            s.stopBtn.paint(ctx);
            float iconSize = 6.0f;
            float iconX = sb.x + (sb.w - iconSize) * 0.5f;
            float iconY = sb.y + (sb.h - iconSize) * 0.5f;
            r.drawRect(iconX, iconY, iconSize, iconSize, Theme::textSecondary);
        }

        s.muteBtn.setColor(ch.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty);
        s.muteBtn.setTextColor(ch.muted ? Color{0, 0, 0} : Theme::textSecondary);
        s.muteBtn.layout(Rect{ix + 4 + btnW + 2, curY, btnW, kButtonHeight}, ctx);
        s.muteBtn.paint(ctx);

        s.soloBtn.setColor(ch.soloed ? Color{255, 200, 50} : Theme::clipSlotEmpty);
        s.soloBtn.setTextColor(ch.soloed ? Color{0, 0, 0} : Theme::textSecondary);
        s.soloBtn.layout(Rect{ix + 4 + (btnW + 2) * 2, curY, btnW, kButtonHeight}, ctx);
        s.soloBtn.paint(ctx);

        // Arm + Monitor row
        curY += kButtonHeight + 2;
        bool armed = track.armed;
        s.armBtn.setColor(armed ? Color{200, 40, 40} : Theme::clipSlotEmpty);
        s.armBtn.setTextColor(armed ? Color{255, 255, 255} : Theme::textSecondary);
        s.armBtn.setLabel("R");
        s.armBtn.layout(Rect{ix + 4, curY, btnW, kButtonHeight}, ctx);
        s.armBtn.paint(ctx);

        auto mode = track.monitorMode;
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
        s.monBtn.layout(Rect{ix + 4 + btnW + 2, curY, btnW, kButtonHeight}, ctx);
        s.monBtn.paint(ctx);

        // ─── I/O Section ─────────────────────────────────────────────
        curY += kButtonHeight + 4;

        if (track.type == Track::Type::Audio) {
            paintAudioIO(ctx, s, track, idx, ix, iw, curY);
        } else {
            paintMidiIO(ctx, s, track, idx, ix, iw, curY);
        }

        // ─── Separator ───────────────────────────────────────────────
        if (track.type == Track::Type::Audio) {
            curY += kIOHeight + 2 + 6;
        } else {
            curY += kIOHeight * 2 + 12.0f + 6;
        }

        // Pan
        s.pan.setValue(ch.pan);
        s.pan.setThumbColor(col);
        s.pan.layout(Rect{ix + 4, curY, iw - 8, 16}, ctx);
        s.pan.paint(ctx);

        // Send dots
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

        // Fader + Meter
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

    void paintAudioIO(UIContext& ctx, TrackStrip& s, const Track& track,
                       int idx, float ix, float iw, float curY) {
        // Audio input dropdown
        std::vector<std::string> inputItems = {"None", "In 1", "In 2", "In 1+2",
                                                "In 3", "In 3+4", "In 5", "In 5+6",
                                                "In 7", "In 7+8"};
        s.audioInputDrop.setItems(inputItems);
        int sel = std::clamp(track.audioInputCh, 0, static_cast<int>(inputItems.size()) - 1);
        s.audioInputDrop.setSelected(sel);
        s.audioInputDrop.layout(Rect{ix + 4, curY, iw - 8, kIOHeight}, ctx);
        s.audioInputDrop.paint(ctx);

        // Mono toggle
        curY += kIOHeight + 2;
        s.monoBtn.setLabel(track.mono ? "Mono" : "Stereo");
        s.monoBtn.setColor(track.mono ? Color{60, 100, 60} : Theme::clipSlotEmpty);
        s.monoBtn.setTextColor(track.mono ? Color{140, 240, 140} : Theme::textSecondary);
        s.monoBtn.layout(Rect{ix + 4, curY, iw - 8, kIOHeight}, ctx);
        s.monoBtn.paint(ctx);
    }

    void paintMidiIO(UIContext& ctx, TrackStrip& s, const Track& track,
                      int idx, float ix, float iw, float curY) {
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        float halfW = (iw - 10) * 0.5f;
        float labelScale = Theme::kSmallFontSize / Theme::kFontSize * 0.5f;
        float labelH = 12.0f;

        // "MIDI RX" label
        s.midiRxLabel.setText("MIDI RX");
        s.midiRxLabel.setColor(Theme::textDim);
        s.midiRxLabel.setFontScale(labelScale);
        s.midiRxLabel.setAlign(TextAlign::Center);
        s.midiRxLabel.layout(Rect{ix + 4, curY, iw - 8, labelH}, ctx);
        s.midiRxLabel.paint(ctx);
        curY += labelH;

        // MIDI Input port
        std::vector<std::string> inPortItems = {"All", "None"};
        if (m_midiEngine) {
            for (int i = 0; i < m_midiEngine->availableInputCount(); ++i)
                inPortItems.push_back(m_midiEngine->availableInputName(i));
        }
        s.midiInDrop.setItems(inPortItems);
        int inPortSel = 0;
        if (track.midiInputPort == -2) inPortSel = 1;
        else if (track.midiInputPort >= 0) inPortSel = track.midiInputPort + 2;
        s.midiInDrop.setSelected(std::clamp(inPortSel, 0, static_cast<int>(inPortItems.size()) - 1));
        s.midiInDrop.layout(Rect{ix + 4, curY, halfW, kIOHeight}, ctx);
        s.midiInDrop.paint(ctx);

        // MIDI Input channel
        std::vector<std::string> chItems = {"All"};
        for (int c = 1; c <= 16; ++c) chItems.push_back(std::to_string(c));
        s.midiInChDrop.setItems(chItems);
        int inChSel = (track.midiInputChannel < 0) ? 0 : track.midiInputChannel + 1;
        s.midiInChDrop.setSelected(std::clamp(inChSel, 0, 16));
        s.midiInChDrop.layout(Rect{ix + 4 + halfW + 2, curY, halfW, kIOHeight}, ctx);
        s.midiInChDrop.paint(ctx);

        // "MIDI TX" label
        curY += kIOHeight + 2;
        s.midiTxLabel.setText("MIDI TX");
        s.midiTxLabel.setColor(Theme::textDim);
        s.midiTxLabel.setFontScale(labelScale);
        s.midiTxLabel.setAlign(TextAlign::Center);
        s.midiTxLabel.layout(Rect{ix + 4, curY, iw - 8, labelH}, ctx);
        s.midiTxLabel.paint(ctx);
        curY += labelH;

        // MIDI Output port
        std::vector<std::string> outPortItems = {"None"};
        if (m_midiEngine) {
            for (int i = 0; i < m_midiEngine->availableOutputCount(); ++i)
                outPortItems.push_back(m_midiEngine->availableOutputName(i));
        }
        s.midiOutDrop.setItems(outPortItems);
        int outPortSel = (track.midiOutputPort < 0) ? 0 : track.midiOutputPort + 1;
        s.midiOutDrop.setSelected(std::clamp(outPortSel, 0, static_cast<int>(outPortItems.size()) - 1));
        s.midiOutDrop.layout(Rect{ix + 4, curY, halfW, kIOHeight}, ctx);
        s.midiOutDrop.paint(ctx);

        // MIDI Output channel
        s.midiOutChDrop.setItems(chItems);
        int outChSel = (track.midiOutputChannel < 0) ? 0 : track.midiOutputChannel + 1;
        s.midiOutChDrop.setSelected(std::clamp(outChSel, 0, 16));
        s.midiOutChDrop.layout(Rect{ix + 4 + halfW + 2, curY, halfW, kIOHeight}, ctx);
        s.midiOutChDrop.paint(ctx);
    }

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project    = nullptr;
    audio::AudioEngine* m_engine     = nullptr;
    midi::MidiEngine*   m_midiEngine = nullptr;

    MixerMeter m_trackMeters[kMaxTracks] = {};
    TrackStrip m_strips[kMaxTracks];
    ScrollBar  m_scrollbar;
    Label      m_mixLabel;

    int   m_selectedTrack = 0;
    float m_scrollX       = 0.0f;

    static constexpr float kMixerHeight  = 420.0f;
    static constexpr float kMeterWidth   = 6.0f;
    static constexpr float kFaderWidth   = 20.0f;
    static constexpr float kButtonHeight = 20.0f;
    static constexpr float kButtonWidth  = 28.0f;
    static constexpr float kIOHeight     = 20.0f;
    static constexpr float kScrollbarH   = 12.0f;

    std::function<void(float)>        m_onScrollChanged;
    std::function<void(int, bool)>    m_onTrackArmed;
};

} // namespace fw
} // namespace ui
} // namespace yawn
