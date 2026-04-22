// MixerPanel.cpp — rendering and event implementations.
// Split from MixerPanel.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "MixerPanel.h"
#include "audio/AudioEngine.h"
#include "midi/MidiEngine.h"
#include "../Renderer.h"
#include "../Font.h"
#include "ui/framework/v2/V1MenuBridge.h"

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
    float toggleW = 36.0f, toggleH = 18.0f;
    float toggleX = x + 4;
    float toggleY = stripY + 22;

    // v2 toggles — setState drives the accent-fill visual; no per-paint
    // colour juggling needed. Sync state from the panel's bools each
    // paint (cheap no-op when unchanged).
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
    m_ioToggle.setState(m_showIO);
    m_ioToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, v2ctx);
    m_ioToggle.render(v2ctx);

    toggleY += toggleH + 2;
    m_sendToggle.setState(m_showSends);
    m_sendToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, v2ctx);
    m_sendToggle.render(v2ctx);

    toggleY += toggleH + 2;
    m_returnToggle.setState(m_showReturns);
    m_returnToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, v2ctx);
    m_returnToggle.render(v2ctx);

    float gridX = x + Theme::kSceneLabelWidth;
    float gridW = w - Theme::kSceneLabelWidth;

    r.pushClip(gridX, y, gridW, h - kScrollbarH);
    for (int t = 0; t < m_project->numTracks(); ++t) {
        float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
        if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
        paintStrip(ctx, t, sx, stripY, Theme::kTrackWidth, stripH);
    }
    r.popClip();

    // v2 dropdown popups paint via LayerStack::paintLayers in App's
    // render loop — above every panel, with drop shadow. No per-panel
    // overlay pass needed.

    float contentW = m_project->numTracks() * Theme::kTrackWidth;
    m_scrollbar.setContentSize(contentW);
    m_scrollbar.setScrollPos(m_scrollX);
    m_scrollbar.layout(Rect{gridX, y + h - kScrollbarH, gridW, kScrollbarH}, ctx);
    m_scrollbar.paint(ctx);

    // v1 context menu retired — fw2::ContextMenu paints via LayerStack
    // in App's render loop.
}

bool MixerPanel::onMouseDown(MouseEvent& e) {
    if (!m_project || !m_engine) return false;
    float mx = e.x, my = e.y;
    bool rightClick = (e.button == MouseButton::Right);

    // v1 context menu retired — LayerStack dispatch in App::pollEvents
    // intercepts clicks while the fw2 menu is open.

    if (hitWidget(m_scrollbar, mx, my)) {
        return m_scrollbar.onMouseDown(e);
    }

    // Toggle buttons in left margin — v2 FwToggle, routed through the
    // v1→v2 event bridge + m_v2Dragging capture (same shape the strip
    // buttons use below).
    {
        auto routePanelToggle = [&](::yawn::ui::fw2::FwToggle& t) -> bool {
            if (rightClick) return false;
            const auto& b = t.bounds();
            if (mx < b.x || mx >= b.x + b.w) return false;
            if (my < b.y || my >= b.y + b.h) return false;
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
            t.dispatchMouseDown(ev);
            m_v2Dragging = &t;
            captureMouse();
            return true;
        };
        if (routePanelToggle(m_ioToggle))     return true;
        if (routePanelToggle(m_sendToggle))   return true;
        if (routePanelToggle(m_returnToggle)) return true;
    }

    float x = m_bounds.x, y = m_bounds.y;
    float gridX = x + Theme::kSceneLabelWidth;
    float gridW = m_bounds.w - Theme::kSceneLabelWidth;

    // v2 dropdowns route open-state clicks through LayerStack
    // (App::pollEvents dispatches before reaching the panel).
    // Outside-click dismiss also happens upstream — no per-strip
    // loop needed here.

    for (int t = 0; t < m_project->numTracks(); ++t) {
        float sx = gridX + t * Theme::kTrackWidth - m_scrollX;
        if (sx + Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
        if (mx < sx || mx >= sx + Theme::kTrackWidth) continue;

        auto& s = m_strips[t];

        // v2 button/toggle hit-tests — route through dispatchMouseDown +
        // m_v2Dragging so the fw2 gesture SM gets its down/up pair via
        // v1 capture. Uses the fw2 widget's own bounds() for hit-test.
        auto routeV2Btn = [&](::yawn::ui::fw2::Widget& w) -> bool {
            const auto& b = w.bounds();
            if (mx < b.x || mx >= b.x + b.w) return false;
            if (my < b.y || my >= b.y + b.h) return false;
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
            w.dispatchMouseDown(ev);
            m_v2Dragging = &w;
            captureMouse();
            return true;
        };

        if (!rightClick && routeV2Btn(s.stopBtn))  return true;
        if (!rightClick && routeV2Btn(s.muteBtn))  return true;
        if (!rightClick && routeV2Btn(s.soloBtn))  return true;
        if (!rightClick && routeV2Btn(s.armBtn))   return true;

        // Monitor button — right-click resets to Auto (keep v1 undo),
        // otherwise forward to v2 for the cycle.
        {
            const auto& b = s.monBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
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
                return routeV2Btn(s.monBtn);
            }
        }

        // Auto button — right-click resets to Off, otherwise cycle.
        {
            const auto& b = s.autoBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                if (rightClick) {
                    auto oldMode = m_project->track(t).autoMode;
                    m_project->track(t).autoMode = automation::AutoMode::Off;
                    m_engine->sendCommand(audio::SetAutoModeMsg{t, 0});
                    if (m_undoManager) {
                        m_undoManager->push({"Reset Auto Mode",
                            [this, t, oldMode]{ m_project->track(t).autoMode = oldMode;
                                m_engine->sendCommand(audio::SetAutoModeMsg{t, static_cast<uint8_t>(oldMode)}); },
                            [this, t]{ m_project->track(t).autoMode = automation::AutoMode::Off;
                                m_engine->sendCommand(audio::SetAutoModeMsg{t, 0}); },
                            ""});
                    }
                    return true;
                }
                return routeV2Btn(s.autoBtn);
            }
        }

        if (m_showIO && m_project->track(t).type == Track::Type::Audio) {
            // v2 dropdowns: toggle on mouseDown directly (v1 App loop
            // doesn't forward mouseUp to v2 widgets).
            {
                const auto& b = s.audioInputDrop.bounds();
                if (!rightClick && mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    s.audioInputDrop.toggle();
                    return true;
                }
            }
            if (!rightClick && routeV2Btn(s.monoBtn)) return true;
        } else if (m_showIO && m_project->track(t).type == Track::Type::Midi) {
            auto tryToggle = [&](::yawn::ui::fw2::FwDropDown& d) -> bool {
                const auto& b = d.bounds();
                if (!rightClick && mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    d.toggle();
                    return true;
                }
                return false;
            };
            if (tryToggle(s.midiInDrop))    return true;
            if (tryToggle(s.midiInChDrop))  return true;
            if (tryToggle(s.midiOutDrop))   return true;
            if (tryToggle(s.midiOutChDrop)) return true;
            if (tryToggle(s.sidechainDrop)) return true;
        }

        // Send knobs — v2 widgets hosted inside the v1 panel. Drag
        // uses the fw2 gesture SM, so we translate the event, capture
        // v1 mouse so moves route back here, and stash the widget in
        // m_v2Dragging.
        if (m_showSends) {
            for (int d = 0; d < kMaxReturnBuses; ++d) {
                auto& kn = s.sendKnobs[d];
                const auto& kb = kn.bounds();
                if (mx < kb.x || mx >= kb.x + kb.w) continue;
                if (my < kb.y || my >= kb.y + kb.h) continue;
                auto ev = ::yawn::ui::fw2::toFw2Mouse(e, kb);
                kn.dispatchMouseDown(ev);
                m_v2Dragging = &kn;
                captureMouse();
                return true;
            }
        }

        // Pan: right-click opens MIDI Learn context menu
        if (hitWidget(s.pan, mx, my)) {
            if (rightClick) {
                openMidiLearnMenu(mx, my,
                    automation::AutomationTarget::mixer(t, automation::MixerParam::Pan),
                    -1.0f, 1.0f,
                    [this, t]() {
                        float oldVal = m_strips[t].pan.value();
                        m_strips[t].pan.setValue(0.0f);
                        m_engine->sendCommand(audio::SetTrackPanMsg{t, 0.0f});
                        if (m_undoManager) {
                            m_undoManager->push({"Reset Pan",
                                [this, t, oldVal]{ m_strips[t].pan.setValue(oldVal);
                                    m_engine->sendCommand(audio::SetTrackPanMsg{t, oldVal}); },
                                [this, t]{ m_strips[t].pan.setValue(0.0f);
                                    m_engine->sendCommand(audio::SetTrackPanMsg{t, 0.0f}); },
                                ""});
                        }
                    });
                return true;
            }
            return s.pan.onMouseDown(e);
        }

        // Fader — v2 FwFader, same v1→v2 event bridge pattern as knobs
        // and buttons. Right-click opens the MIDI Learn context menu
        // (handled separately from the drag dispatch).
        {
            const auto& fb = s.fader.bounds();
            if (mx >= fb.x && mx < fb.x + fb.w &&
                my >= fb.y && my < fb.y + fb.h) {
                if (rightClick) {
                    openMidiLearnMenu(mx, my,
                        automation::AutomationTarget::mixer(t, automation::MixerParam::Volume),
                        0.0f, 2.0f,
                        [this, t]() {
                            float oldVal = m_strips[t].fader.value();
                            m_strips[t].fader.setValue(1.0f);
                            m_engine->sendCommand(audio::SetTrackVolumeMsg{t, 1.0f});
                            if (m_undoManager) {
                                m_undoManager->push({"Reset Volume",
                                    [this, t, oldVal]{ m_strips[t].fader.setValue(oldVal);
                                        m_engine->sendCommand(audio::SetTrackVolumeMsg{t, oldVal}); },
                                    [this, t]{ m_strips[t].fader.setValue(1.0f);
                                        m_engine->sendCommand(audio::SetTrackVolumeMsg{t, 1.0f}); },
                                    ""});
                            }
                        });
                    return true;
                }
                auto ev = ::yawn::ui::fw2::toFw2Mouse(e, fb);
                s.fader.dispatchMouseDown(ev);
                m_v2Dragging = &s.fader;
                captureMouse();
                return true;
            }
        }

        // Click on strip background → select this track
        if (!rightClick) {
            m_selectedTrack = t;
            if (m_onTrackSelected) m_onTrackSelected(t);
            return true;
        }
    }
    return false;
}

void MixerPanel::openMidiLearnMenu(float mx, float my,
                                    const automation::AutomationTarget& target,
                                    float paramMin, float paramMax,
                                    std::function<void()> resetAction) {
    using Item = ContextMenu::Item;
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

    Item sep;
    sep.separator = true;
    items.push_back(std::move(sep));

    Item resetItem;
    resetItem.label = "Reset to Default";
    resetItem.action = std::move(resetAction);
    items.push_back(std::move(resetItem));

    ::yawn::ui::fw2::ContextMenu::show(
        ::yawn::ui::fw2::v1ItemsToFw2(std::move(items)),
        Point{mx, my});
}

void MixerPanel::setupStripCallbacks(int t) {
    auto& s = m_strips[t];

    s.stopBtn.setOnClick([this, t]() {
        if (!m_engine) return;
        m_engine->sendCommand(audio::StopClipMsg{t});
        m_engine->sendCommand(audio::StopMidiClipMsg{t});
    });

    // Mute — v2 FwToggle. Red accent carries the muted state.
    s.muteBtn.setLabel("M");
    s.muteBtn.setAccentColor(Color{255, 80, 80});
    s.muteBtn.setOnChange([this, t](bool on) {
        if (!m_engine) return;
        bool cur = !on;   // state before this click
        m_engine->sendCommand(audio::SetTrackMuteMsg{t, on});
        if (m_undoManager) {
            m_undoManager->push({"Toggle Mute",
                [this, t, cur]{ m_engine->sendCommand(audio::SetTrackMuteMsg{t, cur}); },
                [this, t, on]{ m_engine->sendCommand(audio::SetTrackMuteMsg{t, on}); },
                ""});
        }
    });

    // Solo — v2 FwToggle. Yellow accent carries the soloed state.
    s.soloBtn.setLabel("S");
    s.soloBtn.setAccentColor(Color{255, 200, 50});
    s.soloBtn.setOnChange([this, t](bool on) {
        if (!m_engine) return;
        bool cur = !on;
        m_engine->sendCommand(audio::SetTrackSoloMsg{t, on});
        if (m_undoManager) {
            m_undoManager->push({"Toggle Solo",
                [this, t, cur]{ m_engine->sendCommand(audio::SetTrackSoloMsg{t, cur}); },
                [this, t, on]{ m_engine->sendCommand(audio::SetTrackSoloMsg{t, on}); },
                ""});
        }
    });

    // Arm — v2 FwToggle. Deeper red accent carries the armed state.
    s.armBtn.setLabel("R");
    s.armBtn.setAccentColor(Color{200, 40, 40});
    s.armBtn.setOnChange([this, t](bool on) {
        if (!m_project || !m_engine) return;
        bool cur = !on;
        m_project->track(t).armed = on;
        m_engine->sendCommand(audio::SetTrackArmedMsg{t, on});
        if (m_onTrackArmed) m_onTrackArmed(t, on);
        if (m_undoManager) {
            m_undoManager->push({"Toggle Arm",
                [this, t, cur]{ m_project->track(t).armed = cur;
                    m_engine->sendCommand(audio::SetTrackArmedMsg{t, cur}); },
                [this, t, on]{ m_project->track(t).armed = on;
                    m_engine->sendCommand(audio::SetTrackArmedMsg{t, on}); },
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

    s.autoBtn.setOnClick([this, t]() {
        if (!m_project || !m_engine) return;
        auto& track = m_project->track(t);
        auto oldMode = track.autoMode;
        int next = (static_cast<int>(oldMode) + 1) % 4;
        auto newMode = static_cast<automation::AutoMode>(next);
        track.autoMode = newMode;
        m_engine->sendCommand(audio::SetAutoModeMsg{t, static_cast<uint8_t>(next)});
        if (m_undoManager) {
            m_undoManager->push({"Change Auto Mode",
                [this, t, oldMode]{ m_project->track(t).autoMode = oldMode;
                    m_engine->sendCommand(audio::SetAutoModeMsg{t, static_cast<uint8_t>(oldMode)}); },
                [this, t, newMode]{ m_project->track(t).autoMode = newMode;
                    m_engine->sendCommand(audio::SetAutoModeMsg{t, static_cast<uint8_t>(newMode)}); },
                ""});
        }
    });

    s.audioInputDrop.setOnChange([this, t](int idx, const std::string&) {
        if (!m_project || !m_engine) return;
        // Decode: indices 0..N are hardware inputs, then come resample tracks
        // The item list is built in paintAudioIO; resample items start after hw inputs
        int hwCount = 10; // None + 9 hardware input options
        int oldAudioCh = m_project->track(t).audioInputCh;
        int oldResample = m_project->track(t).resampleSource;
        if (idx < hwCount) {
            // Hardware input selected — clear resample
            m_project->track(t).audioInputCh = idx;
            m_project->track(t).resampleSource = -1;
            m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, idx});
            m_engine->sendCommand(audio::SetResampleSourceMsg{t, -1});
        } else {
            // Resample from track
            int srcTrack = idx - hwCount;
            // Adjust: skip self (we excluded it when building the list)
            if (srcTrack >= t) srcTrack++;
            m_project->track(t).audioInputCh = 0; // no hardware input
            m_project->track(t).resampleSource = srcTrack;
            m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, 0});
            m_engine->sendCommand(audio::SetResampleSourceMsg{t, srcTrack});
        }
        if (m_undoManager) {
            int newAudioCh = m_project->track(t).audioInputCh;
            int newResample = m_project->track(t).resampleSource;
            m_undoManager->push({"Change Audio Input",
                [this, t, oldAudioCh, oldResample]{
                    m_project->track(t).audioInputCh = oldAudioCh;
                    m_project->track(t).resampleSource = oldResample;
                    m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, oldAudioCh});
                    m_engine->sendCommand(audio::SetResampleSourceMsg{t, oldResample});
                },
                [this, t, newAudioCh, newResample]{
                    m_project->track(t).audioInputCh = newAudioCh;
                    m_project->track(t).resampleSource = newResample;
                    m_engine->sendCommand(audio::SetTrackAudioInputChMsg{t, newAudioCh});
                    m_engine->sendCommand(audio::SetResampleSourceMsg{t, newResample});
                },
                ""});
        }
    });

    // Mono — v2 FwToggle. Green accent for the mono-on state (matches
    // the v1 green highlight). Label is set per-paint to Mono/Stereo.
    s.monoBtn.setLabel("S");
    s.monoBtn.setAccentColor(Color{60, 100, 60});
    s.monoBtn.setOnChange([this, t](bool on) {
        if (!m_project || !m_engine) return;
        bool cur = !on;
        m_project->track(t).mono = on;
        m_engine->sendCommand(audio::SetTrackMonoMsg{t, on});
        if (m_undoManager) {
            m_undoManager->push({"Toggle Mono",
                [this, t, cur]{ m_project->track(t).mono = cur;
                    m_engine->sendCommand(audio::SetTrackMonoMsg{t, cur}); },
                [this, t, on]{ m_project->track(t).mono = on;
                    m_engine->sendCommand(audio::SetTrackMonoMsg{t, on}); },
                ""});
        }
    });

    s.sidechainDrop.setOnChange([this, t](int idx, const std::string&) {
        if (!m_project || !m_engine) return;
        int oldVal = m_project->track(t).sidechainSource;
        int newVal = (idx == 0) ? -1 : -1; // decode below
        if (idx > 0) {
            // Map dropdown index to track index (skipping self)
            int trackIdx = idx - 1;
            if (trackIdx >= t) trackIdx++;
            newVal = trackIdx;
        }
        m_project->track(t).sidechainSource = newVal;
        m_engine->sendCommand(audio::SetSidechainSourceMsg{t, newVal});
        if (m_undoManager) {
            m_undoManager->push({"Change Sidechain Source",
                [this, t, oldVal]{
                    m_project->track(t).sidechainSource = oldVal;
                    m_engine->sendCommand(audio::SetSidechainSourceMsg{t, oldVal});
                },
                [this, t, newVal]{
                    m_project->track(t).sidechainSource = newVal;
                    m_engine->sendCommand(audio::SetSidechainSourceMsg{t, newVal});
                },
                ""});
        }
    });

    s.midiInDrop.setOnChange([this, t](int idx, const std::string&) {
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

    s.midiInChDrop.setOnChange([this, t](int idx, const std::string&) {
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

    s.midiOutDrop.setOnChange([this, t](int idx, const std::string&) {
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

    s.midiOutChDrop.setOnChange([this, t](int idx, const std::string&) {
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
    // v2 FwFader's setOnDragEnd delivers both start and end values —
    // no need to re-read fader.value() after release.
    s.fader.setOnDragEnd([this, t](float dragStartVal, float newVal) {
        if (!m_undoManager || !m_engine) return;
        if (dragStartVal != newVal) {
            m_undoManager->push({"Change Volume",
                [this, t, dragStartVal]{ m_strips[t].fader.setValue(dragStartVal);
                    m_engine->sendCommand(audio::SetTrackVolumeMsg{t, dragStartVal}); },
                [this, t, newVal]{ m_strips[t].fader.setValue(newVal);
                    m_engine->sendCommand(audio::SetTrackVolumeMsg{t, newVal}); },
                ""});
        }
    });

    // Send knobs — one per return bus. fw2 widgets; small discs with
    // a single-letter label.
    static const char* sendLabels[] = {"A","B","C","D","E","F","G","H"};
    for (int d = 0; d < kMaxReturnBuses; ++d) {
        auto& knob = s.sendKnobs[d];
        knob.setLabel(sendLabels[d]);
        knob.setShowLabel(true);
        knob.setShowValue(false);                // tiny knob — no value row
        knob.setDiameter(20.0f);
        knob.setRange(0.0f, 1.0f);
        knob.setDefaultValue(0.0f);
        // Small knob → much lower pixelsPerFullRange. Sends are trim
        // controls users "flick" rather than precision-set, so 30 px
        // (~1.5× the disc diameter) for full 0→1 sweep feels right.
        // Shift still available for fine (×0.1) if the user needs it.
        knob.setPixelsPerFullRange(30.0f);
        knob.setOnChange([this, t, d](float v) {
            if (!m_engine) return;
            m_engine->sendCommand(audio::SetSendLevelMsg{t, d, v});
            // Auto-enable send when level > 0, disable when 0
            bool shouldEnable = (v > 0.001f);
            m_engine->sendCommand(audio::SetSendEnabledMsg{t, d, shouldEnable});
        });
        // Drag-end undo. v2 passes (startValue, endValue) so the old
        // "onTouch with manually-captured sendDragStart" dance isn't
        // needed any more.
        knob.setOnDragEnd([this, t, d](float oldVal, float newVal) {
            if (!m_undoManager || !m_engine) return;
            if (oldVal == newVal) return;
            m_undoManager->push({"Change Send Level",
                [this, t, d, oldVal]{
                    m_strips[t].sendKnobs[d].setValue(oldVal);
                    m_engine->sendCommand(audio::SetSendLevelMsg{t, d, oldVal});
                    m_engine->sendCommand(audio::SetSendEnabledMsg{t, d, oldVal > 0.001f});
                },
                [this, t, d, newVal]{
                    m_strips[t].sendKnobs[d].setValue(newVal);
                    m_engine->sendCommand(audio::SetSendLevelMsg{t, d, newVal});
                    m_engine->sendCommand(audio::SetSendEnabledMsg{t, d, newVal > 0.001f});
                },
                "send." + std::to_string(t) + "." + std::to_string(d)});
        });
    }
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
    if (idx == m_selectedTrack) {
        r.drawRect(ix, stripY, iw, stripH, Color{50, 55, 65, 255});
        // Selected track gets a thicker, brighter color bar at top
        r.drawRect(ix, stripY, iw, 4, col);
        // Subtle side borders
        r.drawRect(ix, stripY, 1, stripH, Color{col.r, col.g, col.b, 80});
        r.drawRect(ix + iw - 1, stripY, 1, stripH, Color{col.r, col.g, col.b, 80});
    } else {
        r.drawRect(ix, stripY, iw, 3, col);
    }

    char nameBuf[32];
    const auto& trackName = m_project->track(idx).name;
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", trackName.c_str());
    s.nameLabel.setText(nameBuf);
    s.nameLabel.layout(Rect{ix + 4, stripY + 5, iw - 8, 14}, ctx);
    s.nameLabel.paint(ctx);

    float curY = stripY + 24;
    float btnW = std::min((iw - 16) / 3.0f, kButtonWidth);
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();

    // Stop / Mute / Solo row — all v2 widgets.
    // Stop: FwButton with a small stop-icon square overlaid on top.
    {
        s.stopBtn.layout(Rect{ix + 4, curY, btnW, kButtonHeight}, v2ctx);
        s.stopBtn.render(v2ctx);
        auto& sb = s.stopBtn.bounds();
        float iconSize = 6.0f;
        float iconX = sb.x + (sb.w - iconSize) * 0.5f;
        float iconY = sb.y + (sb.h - iconSize) * 0.5f;
        r.drawRect(iconX, iconY, iconSize, iconSize, Theme::textSecondary);
    }

    // Mute: FwToggle. setState drives the red accent fill.
    s.muteBtn.setState(ch.muted);
    s.muteBtn.layout(Rect{ix + 4 + btnW + 2, curY, btnW, kButtonHeight}, v2ctx);
    s.muteBtn.render(v2ctx);

    // Solo: FwToggle. setState drives the yellow accent fill.
    s.soloBtn.setState(ch.soloed);
    s.soloBtn.layout(Rect{ix + 4 + (btnW + 2) * 2, curY, btnW, kButtonHeight}, v2ctx);
    s.soloBtn.render(v2ctx);

    // Arm + Monitor row
    curY += kButtonHeight + 2;
    bool armed = track.armed;
    s.armBtn.setLabel("R");
    s.armBtn.setState(armed);
    s.armBtn.layout(Rect{ix + 4, curY, btnW, kButtonHeight}, v2ctx);
    s.armBtn.render(v2ctx);

    // Monitor — v2 FwButton, cycles Off/In/Auto. Accent color reflects
    // current state and setHighlighted(true) shows the fill. Auto is
    // the "no highlight" default (matches v1's clipSlotEmpty look).
    auto mode = track.monitorMode;
    const char* monLabel = "Auto";
    Color monAccent = Color{40, 80, 40};
    bool monHL = false;
    if (mode == Track::MonitorMode::In) {
        monLabel = "In";
        monAccent = Color{40, 80, 40};
        monHL = true;
    } else if (mode == Track::MonitorMode::Off) {
        monLabel = "Off";
        monAccent = Color{80, 40, 40};
        monHL = true;
    }
    s.monBtn.setLabel(monLabel);
    s.monBtn.setAccentColor(monAccent);
    s.monBtn.setHighlighted(monHL);
    float monX = ix + 4 + btnW + 2;
    float monW = iw - 8 - btnW - 2;          // fill remaining row width
    s.monBtn.layout(Rect{monX, curY, monW, kButtonHeight}, v2ctx);
    s.monBtn.render(v2ctx);

    curY += kButtonHeight + 2;

    // Automation mode — v2 FwButton, 4-state cycle Off/Read/Touch/Latch.
    // Same pattern as monBtn: colored accent + highlight only when on.
    {
        auto am = track.autoMode;
        const char* autoLabel = "Off";
        Color autoAccent = Theme::clipSlotEmpty;
        bool autoHL = false;
        if (am == automation::AutoMode::Read) {
            autoLabel = "Read";
            autoAccent = Color{40, 80, 40};
            autoHL = true;
        } else if (am == automation::AutoMode::Touch) {
            autoLabel = "Touch";
            autoAccent = Color{100, 70, 20};
            autoHL = true;
        } else if (am == automation::AutoMode::Latch) {
            autoLabel = "Latch";
            autoAccent = Color{100, 30, 30};
            autoHL = true;
        }
        s.autoBtn.setLabel(autoLabel);
        s.autoBtn.setAccentColor(autoAccent);
        s.autoBtn.setHighlighted(autoHL);
        float autoW = iw - 8;
        s.autoBtn.layout(Rect{ix + 4, curY, autoW, kButtonHeight}, v2ctx);
        s.autoBtn.render(v2ctx);
    }

    curY += kButtonHeight + 4;

    // Pan
    s.pan.setValue(ch.pan);
    s.pan.setThumbColor(col);
    s.pan.layout(Rect{ix + 4, curY, iw - 8, 16}, ctx);
    s.pan.paint(ctx);

    // CC label for pan mapping
    if (m_learnManager) {
        auto panTarget = automation::AutomationTarget::mixer(idx, automation::MixerParam::Pan);
        auto* panMap = m_learnManager->findByTarget(panTarget);
        if (panMap) {
            auto lbl = panMap->label();
            float ccScale = 8.0f / Theme::kFontSize;
            ctx.font->drawText(*ctx.renderer, lbl.c_str(),
                ix + iw - 4 - ctx.font->textWidth(lbl.c_str(), ccScale),
                curY + 2, ccScale, Color{100, 180, 255});
        }
    }

    curY += 16 + 4;

    // Send knobs (only when toggled on)
    if (m_showSends) {
        // Layout send knobs in a 4x2 grid
        constexpr float knobW = 28.0f, knobH = 32.0f;
        constexpr int cols = 4;
        for (int d = 0; d < kMaxReturnBuses; ++d) {
            int col = d % cols;
            int row = d / cols;
            float kx = ix + 2 + col * knobW;
            float ky = curY + row * knobH;

            auto& knob = s.sendKnobs[d];
            const auto& send = ch.sends[d];
            // Sync from engine state, BUT NOT while the user is mid-
            // drag — the engine is async and applies send-level
            // commands a frame or two later than we issue them, so
            // an unconditional setValue would rubber-band the knob
            // back to the stale engine value and the drag would look
            // "jumpy". Same gate the v1 mixer had via isDragging().
            if (!knob.isDragging())
                knob.setValue(send.level);

            // Color feedback: off → green → yellow → red
            float v = knob.value();
            Color arcCol;
            if (v < 0.01f) {
                arcCol = Color{50, 50, 55, 255};  // off
            } else if (v < 0.5f) {
                // green to yellow
                float t = v / 0.5f;
                arcCol = Color{
                    static_cast<uint8_t>(40 + t * 215),
                    static_cast<uint8_t>(180 + t * 40),
                    static_cast<uint8_t>(40 * (1.0f - t)),
                    255};
            } else {
                // yellow to red
                float t = (v - 0.5f) / 0.5f;
                arcCol = Color{255,
                    static_cast<uint8_t>(220 * (1.0f - t)),
                    0, 255};
            }
            knob.setAccentColor(arcCol);

            // Hosted v2 widget → render through the fw2 global UIContext.
            auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
            knob.layout(Rect{kx, ky, knobW, knobH}, v2ctx);
            knob.render(v2ctx);
        }
        int rows = (kMaxReturnBuses + cols - 1) / cols;
        curY += rows * knobH + 2;
    }

    // Fader + Meter
    curY += 12;
    float faderBottom = stripY + stripH - 22;
    float faderH = std::max(20.0f, faderBottom - curY);

    // v2 fader — skip sync while user is mid-drag so the engine's
    // (async) volume echo can't rubber-band the knob. Same gate the
    // mixer send knobs use.
    if (!s.fader.isDragging())
        s.fader.setValue(ch.volume);
    s.fader.setTrackColor(col);
    {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        s.fader.layout(Rect{ix + 4, curY, kFaderWidth, faderH}, v2ctx);
        s.fader.render(v2ctx);
    }

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
        } else if (track.type == Track::Type::Midi) {
            paintMidiIO(ctx, s, track, idx, ioX, ioW, curY, faderH);
        }
        // Visual tracks have no audio/MIDI I/O — leave the region blank.
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

    // CC label for volume mapping
    if (m_learnManager) {
        auto volTarget = automation::AutomationTarget::mixer(idx, automation::MixerParam::Volume);
        auto* volMap = m_learnManager->findByTarget(volTarget);
        if (volMap) {
            auto lbl = volMap->label();
            float ccScale = 8.0f / Theme::kFontSize;
            ctx.font->drawText(*ctx.renderer, lbl.c_str(),
                ix + 4, stripY + stripH - 30, ccScale, Color{100, 180, 255});
        }
    }

    r.drawRect(sx + stripW - 1, stripY, 1, stripH, Theme::clipSlotBorder);
}

void MixerPanel::paintAudioIO(UIContext& ctx, TrackStrip& s, const Track& track,
                   int idx, float ioX, float ioW, float ioY, float ioH) {
    float dropH = kIOHeight;
    float curY = ioY;
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();

    // Audio input dropdown: hardware inputs + resample from other tracks
    std::vector<std::string> inputItems = {"None", "In 1", "In 2", "In 1+2",
                                            "In 3", "In 3+4", "In 5", "In 5+6",
                                            "In 7", "In 7+8"};
    int hwCount = static_cast<int>(inputItems.size());
    // Add resample sources (all tracks except self)
    if (m_project) {
        for (int i = 0; i < m_project->numTracks(); ++i) {
            if (i == idx) continue;
            inputItems.push_back(m_project->track(i).name);
        }
    }
    s.audioInputDrop.setItems(inputItems);
    int sel = 0;
    if (track.resampleSource >= 0 && m_project) {
        // Find the dropdown index for this resample source
        int dropIdx = hwCount;
        for (int i = 0; i < m_project->numTracks(); ++i) {
            if (i == idx) continue;
            if (i == track.resampleSource) { sel = dropIdx; break; }
            dropIdx++;
        }
    } else {
        sel = std::clamp(track.audioInputCh, 0, hwCount - 1);
    }
    s.audioInputDrop.setSelectedIndex(sel);
    s.audioInputDrop.layout(Rect{ioX, curY, ioW, dropH}, v2ctx);
    s.audioInputDrop.render(v2ctx);

    // Mono toggle — v2 FwToggle. setState drives the green accent fill.
    curY += dropH + 2;
    s.monoBtn.setLabel(track.mono ? "Mono" : "Stereo");
    s.monoBtn.setState(track.mono);
    s.monoBtn.layout(Rect{ioX, curY, ioW, dropH}, v2ctx);
    s.monoBtn.render(v2ctx);
}

void MixerPanel::paintMidiIO(UIContext& ctx, TrackStrip& s, const Track& track,
                  int idx, float ioX, float ioW, float ioY, float ioH) {
    float dropH = kIOHeight;
    float labelH = 10.0f;
    float labelScale = Theme::kSmallFontSize / Theme::kFontSize * 0.45f;
    float curY = ioY;
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();

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
    s.midiInDrop.setSelectedIndex(std::clamp(inPortSel, 0, static_cast<int>(inPortItems.size()) - 1));
    s.midiInDrop.layout(Rect{ioX, curY, ioW, dropH}, v2ctx);
    s.midiInDrop.render(v2ctx);
    curY += dropH + 2;

    // MIDI Input channel
    std::vector<std::string> chItems = {"All"};
    for (int c = 1; c <= 16; ++c) chItems.push_back(std::to_string(c));
    s.midiInChDrop.setItems(chItems);
    int inChSel = (track.midiInputChannel < 0) ? 0 : track.midiInputChannel + 1;
    s.midiInChDrop.setSelectedIndex(std::clamp(inChSel, 0, 16));
    s.midiInChDrop.layout(Rect{ioX, curY, ioW, dropH}, v2ctx);
    s.midiInChDrop.render(v2ctx);
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
    s.midiOutDrop.setSelectedIndex(std::clamp(outPortSel, 0, static_cast<int>(outPortItems.size()) - 1));
    s.midiOutDrop.layout(Rect{ioX, curY, ioW, dropH}, v2ctx);
    s.midiOutDrop.render(v2ctx);
    curY += dropH + 2;

    // MIDI Output channel
    s.midiOutChDrop.setItems(chItems);
    int outChSel = (track.midiOutputChannel < 0) ? 0 : track.midiOutputChannel + 1;
    s.midiOutChDrop.setSelectedIndex(std::clamp(outChSel, 0, 16));
    s.midiOutChDrop.layout(Rect{ioX, curY, ioW, dropH}, v2ctx);
    s.midiOutChDrop.render(v2ctx);
    curY += dropH + 4;

    // Sidechain source dropdown (only if instrument supports it)
    bool showSidechain = false;
    if (m_engine) {
        auto* inst = m_engine->instrument(idx);
        if (inst && inst->supportsSidechain()) showSidechain = true;
    }
    if (showSidechain && m_project) {
        s.sidechainLabel.setText("SC");
        s.sidechainLabel.setColor(Theme::textDim);
        s.sidechainLabel.setFontScale(labelScale);
        s.sidechainLabel.setAlign(TextAlign::Left);
        s.sidechainLabel.layout(Rect{ioX, curY, ioW, labelH}, ctx);
        s.sidechainLabel.paint(ctx);
        curY += labelH;

        std::vector<std::string> scItems = {"None"};
        for (int i = 0; i < m_project->numTracks(); ++i) {
            if (i == idx) continue;
            scItems.push_back(m_project->track(i).name);
        }
        s.sidechainDrop.setItems(scItems);
        int scSel = 0;
        if (track.sidechainSource >= 0) {
            int dropIdx = 1;
            for (int i = 0; i < m_project->numTracks(); ++i) {
                if (i == idx) continue;
                if (i == track.sidechainSource) { scSel = dropIdx; break; }
                dropIdx++;
            }
        }
        s.sidechainDrop.setSelectedIndex(scSel);
        s.sidechainDrop.layout(Rect{ioX, curY, ioW, dropH}, v2ctx);
        s.sidechainDrop.render(v2ctx);
    }
}

} // namespace fw
} // namespace ui
} // namespace yawn
