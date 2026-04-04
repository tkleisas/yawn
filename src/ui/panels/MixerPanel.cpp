// MixerPanel.cpp — rendering and event implementations.
// Split from MixerPanel.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "MixerPanel.h"
#include "audio/AudioEngine.h"
#include "midi/MidiEngine.h"
#include "../Renderer.h"
#include "../Font.h"

namespace yawn {
namespace ui {
namespace fw {

void MixerPanel::paint(UIContext& ctx) {
    if (!m_project || !m_engine) return;
    auto& r = *ctx.renderer;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w,  h = m_bounds.h;

    r.drawRect(x, y, w, h, Theme::panelBg);
    r.drawRect(x, y, w, 1, Theme::clipSlotBorder);

    float stripY = y + 2;
    float stripH = h - 4 - kScrollbarH;

    float labelScale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    m_mixLabel.setFontScale(labelScale);
    m_mixLabel.layout(Rect{x + 6, stripY + 4, 30, 14}, ctx);
    m_mixLabel.paint(ctx);

    // I/O and Send toggle buttons in left margin
    float toggleW = 30.0f, toggleH = 18.0f;
    float toggleX = x + 6;
    float toggleY = stripY + 22;

    m_ioToggle.setColor(m_showIO ? Color{60, 80, 110} : Theme::clipSlotEmpty);
    m_ioToggle.setTextColor(m_showIO ? Color{150, 200, 255} : Theme::textDim);
    m_ioToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, ctx);
    m_ioToggle.paint(ctx);

    toggleY += toggleH + 2;
    m_sendToggle.setColor(m_showSends ? Color{60, 80, 110} : Theme::clipSlotEmpty);
    m_sendToggle.setTextColor(m_showSends ? Color{150, 200, 255} : Theme::textDim);
    m_sendToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, ctx);
    m_sendToggle.paint(ctx);

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
    if (m_showIO) {
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
    }

    float contentW = m_project->numTracks() * Theme::kTrackWidth;
    m_scrollbar.setContentSize(contentW);
    m_scrollbar.setScrollPos(m_scrollX);
    m_scrollbar.layout(Rect{gridX, y + h - kScrollbarH, gridW, kScrollbarH}, ctx);
    m_scrollbar.paint(ctx);
}

bool MixerPanel::onMouseDown(MouseEvent& e) {
    if (!m_project || !m_engine) return false;
    float mx = e.x, my = e.y;
    bool rightClick = (e.button == MouseButton::Right);

    if (hitWidget(m_scrollbar, mx, my)) {
        return m_scrollbar.onMouseDown(e);
    }

    // Toggle buttons in left margin
    if (!rightClick && hitWidget(m_ioToggle, mx, my))
        return m_ioToggle.onMouseDown(e);
    if (!rightClick && hitWidget(m_sendToggle, mx, my))
        return m_sendToggle.onMouseDown(e);

    float x = m_bounds.x, y = m_bounds.y;
    float gridX = x + Theme::kSceneLabelWidth;
    float gridW = m_bounds.w - Theme::kSceneLabelWidth;

    // First pass: check if any open dropdown popup was clicked
    if (m_showIO) {
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
                auto oldMode = m_project->track(t).monitorMode;
                m_project->track(t).monitorMode = Track::MonitorMode::Auto;
                m_engine->sendCommand(
                    audio::SetTrackMonitorMsg{t,
                        static_cast<uint8_t>(Track::MonitorMode::Auto)});
                if (m_undoManager) {
                    m_undoManager->push({"Reset Monitor",
                        [this, t, oldMode]{ m_project->track(t).monitorMode = oldMode;
                            m_engine->sendCommand(audio::SetTrackMonitorMsg{t, static_cast<uint8_t>(oldMode)}); },
                        [this, t]{ m_project->track(t).monitorMode = Track::MonitorMode::Auto;
                            m_engine->sendCommand(audio::SetTrackMonitorMsg{t, static_cast<uint8_t>(Track::MonitorMode::Auto)}); },
                        ""});
                }
                return true;
            }
            return s.monBtn.onMouseDown(e);
        }

        if (m_showIO && m_project->track(t).type == Track::Type::Audio) {
            if (!rightClick && hitWidget(s.audioInputDrop, mx, my))
                return s.audioInputDrop.onMouseDown(e);
            if (!rightClick && hitWidget(s.monoBtn, mx, my))
                return s.monoBtn.onMouseDown(e);
        } else if (m_showIO) {
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
                float oldVal = s.fader.value();
                s.fader.setValue(1.0f);
                m_engine->sendCommand(audio::SetTrackVolumeMsg{t, 1.0f});
                if (m_undoManager) {
                    m_undoManager->push({"Reset Volume",
                        [this, t, oldVal]{ m_strips[t].fader.setValue(oldVal);
                            m_engine->sendCommand(audio::SetTrackVolumeMsg{t, oldVal}); },
                        [this, t]{ m_strips[t].fader.setValue(1.0f);
                            m_engine->sendCommand(audio::SetTrackVolumeMsg{t, 1.0f}); },
                        ""});
                }
                return true;
            }
            return s.fader.onMouseDown(e);
        }
    }
    return false;
}

void MixerPanel::setupStripCallbacks(int t) {
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
        if (m_undoManager) {
            bool newVal = !cur;
            m_undoManager->push({"Toggle Mute",
                [this, t, cur]{ m_engine->sendCommand(audio::SetTrackMuteMsg{t, cur}); },
                [this, t, newVal]{ m_engine->sendCommand(audio::SetTrackMuteMsg{t, newVal}); },
                ""});
        }
    });

    s.soloBtn.setLabel("S");
    s.soloBtn.setOnClick([this, t]() {
        if (!m_engine) return;
        bool cur = m_engine->mixer().trackChannel(t).soloed;
        m_engine->sendCommand(audio::SetTrackSoloMsg{t, !cur});
        if (m_undoManager) {
            bool newVal = !cur;
            m_undoManager->push({"Toggle Solo",
                [this, t, cur]{ m_engine->sendCommand(audio::SetTrackSoloMsg{t, cur}); },
                [this, t, newVal]{ m_engine->sendCommand(audio::SetTrackSoloMsg{t, newVal}); },
                ""});
        }
    });

    s.armBtn.setLabel("R");
    s.armBtn.setOnClick([this, t]() {
        if (!m_project || !m_engine) return;
        bool cur = m_project->track(t).armed;
        m_project->track(t).armed = !cur;
        m_engine->sendCommand(audio::SetTrackArmedMsg{t, !cur});
        if (m_onTrackArmed) m_onTrackArmed(t, !cur);
        if (m_undoManager) {
            bool newVal = !cur;
            m_undoManager->push({"Toggle Arm",
                [this, t, cur]{ m_project->track(t).armed = cur;
                    m_engine->sendCommand(audio::SetTrackArmedMsg{t, cur}); },
                [this, t, newVal]{ m_project->track(t).armed = newVal;
                    m_engine->sendCommand(audio::SetTrackArmedMsg{t, newVal}); },
                ""});
        }
    });

    s.monBtn.setOnClick([this, t]() {
        if (!m_project || !m_engine) return;
        auto& track = m_project->track(t);
        auto oldMode = track.monitorMode;
        if (track.monitorMode == Track::MonitorMode::Auto)
            track.monitorMode = Track::MonitorMode::In;
        else if (track.monitorMode == Track::MonitorMode::In)
            track.monitorMode = Track::MonitorMode::Off;
        else
            track.monitorMode = Track::MonitorMode::Auto;
        auto newMode = track.monitorMode;
        m_engine->sendCommand(
            audio::SetTrackMonitorMsg{t,
                static_cast<uint8_t>(newMode)});
        if (m_undoManager) {
            m_undoManager->push({"Change Monitor",
                [this, t, oldMode]{ m_project->track(t).monitorMode = oldMode;
                    m_engine->sendCommand(audio::SetTrackMonitorMsg{t, static_cast<uint8_t>(oldMode)}); },
                [this, t, newMode]{ m_project->track(t).monitorMode = newMode;
                    m_engine->sendCommand(audio::SetTrackMonitorMsg{t, static_cast<uint8_t>(newMode)}); },
                ""});
        }
    });

    s.audioInputDrop.setOnChange([this, t](int idx) {
        if (!m_project || !m_engine) return;
        int oldVal = m_project->track(t).audioInputCh;
        m_project->track(t).audioInputCh = idx;
        m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, idx});
        if (m_undoManager) {
            m_undoManager->push({"Change Audio Input",
                [this, t, oldVal]{ m_project->track(t).audioInputCh = oldVal;
                    m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, oldVal}); },
                [this, t, idx]{ m_project->track(t).audioInputCh = idx;
                    m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, idx}); },
                ""});
        }
    });

    s.monoBtn.setLabel("S");
    s.monoBtn.setOnClick([this, t]() {
        if (!m_project || !m_engine) return;
        bool cur = m_project->track(t).mono;
        m_project->track(t).mono = !cur;
        m_engine->sendCommand(audio::SetTrackMonoMsg{t, !cur});
        if (m_undoManager) {
            bool newVal = !cur;
            m_undoManager->push({"Toggle Mono",
                [this, t, cur]{ m_project->track(t).mono = cur;
                    m_engine->sendCommand(audio::SetTrackMonoMsg{t, cur}); },
                [this, t, newVal]{ m_project->track(t).mono = newVal;
                    m_engine->sendCommand(audio::SetTrackMonoMsg{t, newVal}); },
                ""});
        }
    });

    s.midiInDrop.setOnChange([this, t](int idx) {
        if (!m_project) return;
        int oldVal = m_project->track(t).midiInputPort;
        if (idx == 0) m_project->track(t).midiInputPort = -1;
        else if (idx == 1) m_project->track(t).midiInputPort = -2;
        else m_project->track(t).midiInputPort = idx - 2;
        int newVal = m_project->track(t).midiInputPort;
        if (m_undoManager) {
            m_undoManager->push({"Change MIDI Input",
                [this, t, oldVal]{ m_project->track(t).midiInputPort = oldVal; },
                [this, t, newVal]{ m_project->track(t).midiInputPort = newVal; },
                ""});
        }
    });

    s.midiInChDrop.setOnChange([this, t](int idx) {
        if (!m_project) return;
        int oldVal = m_project->track(t).midiInputChannel;
        m_project->track(t).midiInputChannel = (idx == 0) ? -1 : idx - 1;
        int newVal = m_project->track(t).midiInputChannel;
        if (m_undoManager) {
            m_undoManager->push({"Change MIDI Input Ch",
                [this, t, oldVal]{ m_project->track(t).midiInputChannel = oldVal; },
                [this, t, newVal]{ m_project->track(t).midiInputChannel = newVal; },
                ""});
        }
    });

    s.midiOutDrop.setOnChange([this, t](int idx) {
        if (!m_project) return;
        int oldPort = m_project->track(t).midiOutputPort;
        m_project->track(t).midiOutputPort = (idx == 0) ? -1 : idx - 1;
        int newPort = m_project->track(t).midiOutputPort;
        int ch = m_project->track(t).midiOutputChannel;
        if (m_engine) m_engine->sendCommand(
            audio::SetTrackMidiOutputMsg{t, newPort, ch});
        if (m_undoManager) {
            m_undoManager->push({"Change MIDI Output",
                [this, t, oldPort, ch]{ m_project->track(t).midiOutputPort = oldPort;
                    if (m_engine) m_engine->sendCommand(audio::SetTrackMidiOutputMsg{t, oldPort, ch}); },
                [this, t, newPort, ch]{ m_project->track(t).midiOutputPort = newPort;
                    if (m_engine) m_engine->sendCommand(audio::SetTrackMidiOutputMsg{t, newPort, ch}); },
                ""});
        }
    });

    s.midiOutChDrop.setOnChange([this, t](int idx) {
        if (!m_project) return;
        int oldCh = m_project->track(t).midiOutputChannel;
        m_project->track(t).midiOutputChannel = (idx == 0) ? -1 : idx - 1;
        int newCh = m_project->track(t).midiOutputChannel;
        int port = m_project->track(t).midiOutputPort;
        if (m_engine) m_engine->sendCommand(
            audio::SetTrackMidiOutputMsg{t, port, newCh});
        if (m_undoManager) {
            m_undoManager->push({"Change MIDI Output Ch",
                [this, t, oldCh, port]{ m_project->track(t).midiOutputChannel = oldCh;
                    if (m_engine) m_engine->sendCommand(audio::SetTrackMidiOutputMsg{t, port, oldCh}); },
                [this, t, newCh, port]{ m_project->track(t).midiOutputChannel = newCh;
                    if (m_engine) m_engine->sendCommand(audio::SetTrackMidiOutputMsg{t, port, newCh}); },
                ""});
        }
    });

    // Pan knob — uses onTouch for undo capture
    s.pan.setOnChange([this, t](float v) {
        if (!m_engine) return;
        m_engine->sendCommand(audio::SetTrackPanMsg{t, v});
    });
    s.pan.setOnTouch([this, t](bool touching) {
        if (!m_undoManager || !m_engine) return;
        if (touching) {
            m_strips[t].panDragStart = m_strips[t].pan.value();
        } else {
            float oldVal = m_strips[t].panDragStart;
            float newVal = m_strips[t].pan.value();
            if (oldVal != newVal) {
                m_undoManager->push({"Change Pan",
                    [this, t, oldVal]{ m_strips[t].pan.setValue(oldVal);
                        m_engine->sendCommand(audio::SetTrackPanMsg{t, oldVal}); },
                    [this, t, newVal]{ m_strips[t].pan.setValue(newVal);
                        m_engine->sendCommand(audio::SetTrackPanMsg{t, newVal}); },
                    ""});
            }
        }
    });

    // Volume fader — uses onDragEnd for undo capture
    s.fader.setRange(0.0f, 2.0f);
    s.fader.setOnChange([this, t](float v) {
        if (!m_engine) return;
        m_engine->sendCommand(audio::SetTrackVolumeMsg{t, v});
    });
    s.fader.setOnDragEnd([this, t](float dragStartVal) {
        if (!m_undoManager || !m_engine) return;
        float newVal = m_strips[t].fader.value();
        if (dragStartVal != newVal) {
            m_undoManager->push({"Change Volume",
                [this, t, dragStartVal]{ m_strips[t].fader.setValue(dragStartVal);
                    m_engine->sendCommand(audio::SetTrackVolumeMsg{t, dragStartVal}); },
                [this, t, newVal]{ m_strips[t].fader.setValue(newVal);
                    m_engine->sendCommand(audio::SetTrackVolumeMsg{t, newVal}); },
                ""});
        }
    });
}

void MixerPanel::paintStrip(UIContext& ctx, int idx, float sx, float stripY,
                 float stripW, float stripH) {
    auto& r = *ctx.renderer;
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
    float monX = ix + 4 + btnW + 2;
    float monW = iw - 8 - btnW - 2;          // fill remaining row width
    s.monBtn.layout(Rect{monX, curY, monW, kButtonHeight}, ctx);
    s.monBtn.paint(ctx);

    curY += kButtonHeight + 4;

    // Pan
    s.pan.setValue(ch.pan);
    s.pan.setThumbColor(col);
    s.pan.layout(Rect{ix + 4, curY, iw - 8, 16}, ctx);
    s.pan.paint(ctx);

    curY += 16 + 4;

    // Send dots (only when toggled on)
    if (m_showSends) {
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
    }

    // Fader + Meter
    curY += 12;
    float faderBottom = stripY + stripH - 22;
    float faderH = std::max(20.0f, faderBottom - curY);

    s.fader.setValue(ch.volume);
    s.fader.setTrackColor(col);
    s.fader.layout(Rect{ix + 4, curY, kFaderWidth, faderH}, ctx);
    s.fader.paint(ctx);

    float meterX = ix + 4 + kFaderWidth + 3;
    s.meter.setPeak(m_trackMeters[idx].peakL, m_trackMeters[idx].peakR);
    s.meter.layout(Rect{meterX, curY, kMeterWidth * 2, faderH}, ctx);
    s.meter.paint(ctx);

    // I/O controls alongside fader (in space to the right of meter)
    if (m_showIO) {
        float ioX = meterX + kMeterWidth * 2 + 4;
        float ioW = ix + iw - ioX - 2;
        if (track.type == Track::Type::Audio) {
            paintAudioIO(ctx, s, track, idx, ioX, ioW, curY, faderH);
        } else {
            paintMidiIO(ctx, s, track, idx, ioX, ioW, curY, faderH);
        }
    }

    float db = ch.volume > 0.001f ? 20.0f * std::log10(ch.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    s.dbLabel.setText(dbText);
    s.dbLabel.setColor(Theme::textDim);
    float dbScale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    s.dbLabel.setFontScale(dbScale);
    s.dbLabel.layout(Rect{ix + 4, stripY + stripH - 18, iw - 8, 14}, ctx);
    s.dbLabel.paint(ctx);

    r.drawRect(sx + stripW - 1, stripY, 1, stripH, Theme::clipSlotBorder);
}

void MixerPanel::paintAudioIO(UIContext& ctx, TrackStrip& s, const Track& track,
                   int idx, float ioX, float ioW, float ioY, float ioH) {
    float dropH = kIOHeight;
    float curY = ioY;

    // Audio input dropdown
    std::vector<std::string> inputItems = {"None", "In 1", "In 2", "In 1+2",
                                            "In 3", "In 3+4", "In 5", "In 5+6",
                                            "In 7", "In 7+8"};
    s.audioInputDrop.setItems(inputItems);
    int sel = std::clamp(track.audioInputCh, 0, static_cast<int>(inputItems.size()) - 1);
    s.audioInputDrop.setSelected(sel);
    s.audioInputDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.audioInputDrop.paint(ctx);

    // Mono toggle
    curY += dropH + 2;
    s.monoBtn.setLabel(track.mono ? "Mono" : "Stereo");
    s.monoBtn.setColor(track.mono ? Color{60, 100, 60} : Theme::clipSlotEmpty);
    s.monoBtn.setTextColor(track.mono ? Color{140, 240, 140} : Theme::textSecondary);
    s.monoBtn.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.monoBtn.paint(ctx);
}

void MixerPanel::paintMidiIO(UIContext& ctx, TrackStrip& s, const Track& track,
                  int idx, float ioX, float ioW, float ioY, float ioH) {
    float dropH = kIOHeight;
    float labelH = 10.0f;
    float labelScale = Theme::kSmallFontSize / Theme::kFontSize * 0.45f;
    float curY = ioY;

    // "RX" label
    s.midiRxLabel.setText("RX");
    s.midiRxLabel.setColor(Theme::textDim);
    s.midiRxLabel.setFontScale(labelScale);
    s.midiRxLabel.setAlign(TextAlign::Left);
    s.midiRxLabel.layout(Rect{ioX, curY, ioW, labelH}, ctx);
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
    s.midiInDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiInDrop.paint(ctx);
    curY += dropH + 2;

    // MIDI Input channel
    std::vector<std::string> chItems = {"All"};
    for (int c = 1; c <= 16; ++c) chItems.push_back(std::to_string(c));
    s.midiInChDrop.setItems(chItems);
    int inChSel = (track.midiInputChannel < 0) ? 0 : track.midiInputChannel + 1;
    s.midiInChDrop.setSelected(std::clamp(inChSel, 0, 16));
    s.midiInChDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiInChDrop.paint(ctx);
    curY += dropH + 4;

    // "TX" label
    s.midiTxLabel.setText("TX");
    s.midiTxLabel.setColor(Theme::textDim);
    s.midiTxLabel.setFontScale(labelScale);
    s.midiTxLabel.setAlign(TextAlign::Left);
    s.midiTxLabel.layout(Rect{ioX, curY, ioW, labelH}, ctx);
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
    s.midiOutDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiOutDrop.paint(ctx);
    curY += dropH + 2;

    // MIDI Output channel
    s.midiOutChDrop.setItems(chItems);
    int outChSel = (track.midiOutputChannel < 0) ? 0 : track.midiOutputChannel + 1;
    s.midiOutChDrop.setSelected(std::clamp(outChSel, 0, 16));
    s.midiOutChDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiOutChDrop.paint(ctx);
}

} // namespace fw
} // namespace ui
} // namespace yawn
