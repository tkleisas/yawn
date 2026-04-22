// MixerPanel.cpp — UI v2 rendering + event implementations.
//
// Migrated from v1 fw::Widget to fw2::Widget. All child controls are
// native fw2 widgets that dispatch directly through the fw2 gesture
// SM — no V1EventBridge / m_v2Dragging tracking. Labels that used to
// be v1 fw::Label widgets are now painted inline via
// `ctx.textMetrics->drawText`.

#include "MixerPanel.h"
#include "audio/AudioEngine.h"
#include "midi/MidiEngine.h"
#include "../Renderer.h"
#include "../Font.h"
#include "ui/framework/v2/V1MenuBridge.h"
#include "ui/framework/v2/Theme.h"

namespace yawn {
namespace ui {
namespace fw2 {

void MixerPanel::render(UIContext& ctx) {
    if (!m_project || !m_engine) return;
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w,  h = m_bounds.h;

    r.drawRect(x, y, w, h, ::yawn::ui::Theme::panelBg);
    r.drawRect(x, y, w, 1, ::yawn::ui::Theme::clipSlotBorder);

    float stripY = y + 2;
    float stripH = h - 4 - kScrollbarH;

    // "MIX" label — theme small size, scales with the font-scale
    // preference like the rest of the fw2 UI.
    {
        const float mixSize = theme().metrics.fontSizeSmall;
        const float lh = tm.lineHeight(mixSize);
        tm.drawText(r, "MIX",
                    x + 6,
                    stripY + 4 + (14.0f - lh) * 0.5f,
                    mixSize, ::yawn::ui::Theme::textPrimary);
    }

    // I/O and Send toggle buttons in left margin
    float toggleW = 36.0f, toggleH = 18.0f;
    float toggleX = x + 4;
    float toggleY = stripY + 22;

    // v2 toggles — setState drives the accent-fill visual. Sync state
    // from the panel's bools each frame using Automation so the
    // setOnChange callback only fires for real user clicks.
    m_ioToggle.setState(m_showIO, ValueChangeSource::Automation);
    m_ioToggle.measure(Constraints::tight(toggleW, toggleH), ctx);
    m_ioToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, ctx);
    m_ioToggle.render(ctx);

    toggleY += toggleH + 2;
    m_sendToggle.setState(m_showSends, ValueChangeSource::Automation);
    m_sendToggle.measure(Constraints::tight(toggleW, toggleH), ctx);
    m_sendToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, ctx);
    m_sendToggle.render(ctx);

    toggleY += toggleH + 2;
    m_returnToggle.setState(m_showReturns, ValueChangeSource::Automation);
    m_returnToggle.measure(Constraints::tight(toggleW, toggleH), ctx);
    m_returnToggle.layout(Rect{toggleX, toggleY, toggleW, toggleH}, ctx);
    m_returnToggle.render(ctx);

    float gridX = x + ::yawn::ui::Theme::kSceneLabelWidth;
    float gridW = w - ::yawn::ui::Theme::kSceneLabelWidth;

    r.pushClip(gridX, y, gridW, h - kScrollbarH);
    for (int t = 0; t < m_project->numTracks(); ++t) {
        float sx = gridX + t * ::yawn::ui::Theme::kTrackWidth - m_scrollX;
        if (sx + ::yawn::ui::Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
        paintStrip(ctx, t, sx, stripY, ::yawn::ui::Theme::kTrackWidth, stripH);
    }
    r.popClip();

    // v2 dropdown popups paint via LayerStack::paintLayers in App's
    // render loop — above every panel, with drop shadow. No per-panel
    // overlay pass needed.

    float contentW = m_project->numTracks() * ::yawn::ui::Theme::kTrackWidth;
    m_scrollbar.setContentSize(contentW);
    m_scrollbar.setScrollPos(m_scrollX);
    m_scrollbar.measure(Constraints::tight(gridW, kScrollbarH), ctx);
    m_scrollbar.layout(Rect{gridX, y + h - kScrollbarH, gridW, kScrollbarH}, ctx);
    m_scrollbar.render(ctx);

    // v1 context menu retired — fw2::ContextMenu paints via LayerStack
    // in App's render loop.
}

bool MixerPanel::onMouseDown(MouseEvent& e) {
    if (!m_project || !m_engine) return false;
    float mx = e.x, my = e.y;
    bool rightClick = (e.button == MouseButton::Right);

    // Route helper — direct dispatch to a fw2 child at its own bounds.
    // The fw2 gesture SM handles capture via Widget::capturedWidget()
    // internally, so onMouseMove/onMouseUp in the header will forward
    // moves and ups to the captured widget for us.
    auto hitChild = [&](Widget& wgt) -> bool {
        const auto& b = wgt.bounds();
        if (mx < b.x || mx >= b.x + b.w) return false;
        if (my < b.y || my >= b.y + b.h) return false;
        MouseEvent ev = e;
        ev.lx = mx - b.x;
        ev.ly = my - b.y;
        wgt.dispatchMouseDown(ev);
        return true;
    };

    // v2 scrollbar — route through the gesture SM for drag + release.
    {
        const auto& sb = m_scrollbar.bounds();
        if (mx >= sb.x && mx < sb.x + sb.w && my >= sb.y && my < sb.y + sb.h) {
            MouseEvent ev = e;
            ev.lx = mx - sb.x;
            ev.ly = my - sb.y;
            m_scrollbar.dispatchMouseDown(ev);
            return true;
        }
    }

    // Toggle buttons in left margin — v2 FwToggle.
    if (!rightClick) {
        if (hitChild(m_ioToggle))     return true;
        if (hitChild(m_sendToggle))   return true;
        if (hitChild(m_returnToggle)) return true;
    }

    float x = m_bounds.x;
    float gridX = x + ::yawn::ui::Theme::kSceneLabelWidth;
    float gridW = m_bounds.w - ::yawn::ui::Theme::kSceneLabelWidth;

    // v2 dropdowns route open-state clicks through LayerStack
    // (App::pollEvents dispatches before reaching the panel).
    // Outside-click dismiss also happens upstream — no per-strip
    // loop needed here.

    for (int t = 0; t < m_project->numTracks(); ++t) {
        float sx = gridX + t * ::yawn::ui::Theme::kTrackWidth - m_scrollX;
        if (sx + ::yawn::ui::Theme::kTrackWidth < gridX || sx > gridX + gridW) continue;
        if (mx < sx || mx >= sx + ::yawn::ui::Theme::kTrackWidth) continue;

        auto& s = m_strips[t];

        if (!rightClick && hitChild(s.stopBtn))  return true;
        if (!rightClick && hitChild(s.muteBtn))  return true;
        if (!rightClick && hitChild(s.soloBtn))  return true;
        if (!rightClick && hitChild(s.armBtn))   return true;

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
                return hitChild(s.monBtn);
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
                return hitChild(s.autoBtn);
            }
        }

        if (m_showIO && m_project->track(t).type == Track::Type::Audio) {
            // v2 dropdowns: toggle on mouseDown directly (we'd rather
            // open-on-press than route through hitChild, since the
            // dropdown's picker is hosted on the LayerStack).
            {
                const auto& b = s.audioInputDrop.bounds();
                if (!rightClick && mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    s.audioInputDrop.toggle();
                    return true;
                }
            }
            if (!rightClick && hitChild(s.monoBtn)) return true;
        } else if (m_showIO && m_project->track(t).type == Track::Type::Midi) {
            auto tryToggle = [&](FwDropDown& d) -> bool {
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

        // Send knobs — v2 widgets hosted inside the panel. Drag uses
        // the fw2 gesture SM.
        if (m_showSends) {
            for (int d = 0; d < kMaxReturnBuses; ++d) {
                auto& kn = s.sendKnobs[d];
                const auto& kb = kn.bounds();
                if (mx < kb.x || mx >= kb.x + kb.w) continue;
                if (my < kb.y || my >= kb.y + kb.h) continue;
                MouseEvent ev = e;
                ev.lx = mx - kb.x;
                ev.ly = my - kb.y;
                kn.dispatchMouseDown(ev);
                return true;
            }
        }

        // Pan — v2 FwPan. Right-click opens the MIDI Learn context
        // menu (handled before dispatch so the widget's internal
        // reset doesn't also fire).
        {
            const auto& pb = s.pan.bounds();
            if (mx >= pb.x && mx < pb.x + pb.w &&
                my >= pb.y && my < pb.y + pb.h) {
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
                MouseEvent ev = e;
                ev.lx = mx - pb.x;
                ev.ly = my - pb.y;
                s.pan.dispatchMouseDown(ev);
                return true;
            }
        }

        // Fader — v2 FwFader. Right-click opens the MIDI Learn
        // context menu (handled separately from the drag dispatch).
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
                MouseEvent ev = e;
                ev.lx = mx - fb.x;
                ev.ly = my - fb.y;
                s.fader.dispatchMouseDown(ev);
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
    using Item = ::yawn::ui::ContextMenu::Item;
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

    ContextMenu::show(v1ItemsToFw2(std::move(items)),
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
    s.muteBtn.setAccentColor(Color{255, 80, 80, 255});
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
    s.soloBtn.setAccentColor(Color{255, 200, 50, 255});
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
    s.armBtn.setAccentColor(Color{200, 40, 40, 255});
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
    s.monoBtn.setAccentColor(Color{60, 100, 60, 255});
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

    // Pan — v2 FwPan delivers (startValue, endValue) in one callback.
    s.pan.setOnChange([this, t](float v) {
        if (!m_engine) return;
        m_engine->sendCommand(audio::SetTrackPanMsg{t, v});
    });
    s.pan.setOnDragEnd([this, t](float oldVal, float newVal) {
        if (!m_undoManager || !m_engine) return;
        if (oldVal != newVal) {
            m_undoManager->push({"Change Pan",
                [this, t, oldVal]{ m_strips[t].pan.setValue(oldVal);
                    m_engine->sendCommand(audio::SetTrackPanMsg{t, oldVal}); },
                [this, t, newVal]{ m_strips[t].pan.setValue(newVal);
                    m_engine->sendCommand(audio::SetTrackPanMsg{t, newVal}); },
                ""});
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
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    auto& s = m_strips[idx];
    float pad = ::yawn::ui::Theme::kSlotPadding;
    float ix = sx + pad, iw = stripW - pad * 2;
    const auto& ch = m_engine->mixer().trackChannel(idx);
    const auto& track = m_project->track(idx);
    Color col = ::yawn::ui::Theme::trackColors[track.colorIndex % ::yawn::ui::Theme::kNumTrackColors];

    r.drawRect(ix, stripY, iw, stripH, ::yawn::ui::Theme::background);
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

    // Track name (formerly s.nameLabel).
    {
        char nameBuf[32];
        const auto& trackName = m_project->track(idx).name;
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", trackName.c_str());
        const float nameSize = theme().metrics.fontSizeSmall;
        const float lh = tm.lineHeight(nameSize);
        tm.drawText(r, nameBuf, ix + 4,
                    stripY + 5 + (14.0f - lh) * 0.5f,
                    nameSize, ::yawn::ui::Theme::textPrimary);
    }

    float curY = stripY + 24;
    float btnW = std::min((iw - 16) / 3.0f, kButtonWidth);

    // Stop / Mute / Solo row — all v2 widgets.
    // Stop: FwButton with a small stop-icon square overlaid on top.
    {
        s.stopBtn.measure(Constraints::tight(btnW, kButtonHeight), ctx);
        s.stopBtn.layout(Rect{ix + 4, curY, btnW, kButtonHeight}, ctx);
        s.stopBtn.render(ctx);
        auto& sb = s.stopBtn.bounds();
        float iconSize = 6.0f;
        float iconX = sb.x + (sb.w - iconSize) * 0.5f;
        float iconY = sb.y + (sb.h - iconSize) * 0.5f;
        r.drawRect(iconX, iconY, iconSize, iconSize, ::yawn::ui::Theme::textSecondary);
    }

    // Mute: FwToggle. setState drives the red accent fill. Passes
    // Automation source so the per-frame engine-state sync doesn't
    // fire onChange — otherwise the engine's async command queue
    // makes the button ping-pong (user click → sendCommand → next
    // frame still reports old state → setState fires onChange →
    // reverts the click at audio thread).
    s.muteBtn.setState(ch.muted, ValueChangeSource::Automation);
    s.muteBtn.measure(Constraints::tight(btnW, kButtonHeight), ctx);
    s.muteBtn.layout(Rect{ix + 4 + btnW + 2, curY, btnW, kButtonHeight}, ctx);
    s.muteBtn.render(ctx);

    // Solo — same async-safe pattern.
    s.soloBtn.setState(ch.soloed, ValueChangeSource::Automation);
    s.soloBtn.measure(Constraints::tight(btnW, kButtonHeight), ctx);
    s.soloBtn.layout(Rect{ix + 4 + (btnW + 2) * 2, curY, btnW, kButtonHeight}, ctx);
    s.soloBtn.render(ctx);

    // Arm + Monitor row
    curY += kButtonHeight + 2;
    bool armed = track.armed;
    s.armBtn.setLabel("R");
    s.armBtn.setState(armed, ValueChangeSource::Automation);
    s.armBtn.measure(Constraints::tight(btnW, kButtonHeight), ctx);
    s.armBtn.layout(Rect{ix + 4, curY, btnW, kButtonHeight}, ctx);
    s.armBtn.render(ctx);

    // Monitor — v2 FwButton, cycles Off/In/Auto. Accent color reflects
    // current state and setHighlighted(true) shows the fill. Auto is
    // the "no highlight" default (matches v1's clipSlotEmpty look).
    auto mode = track.monitorMode;
    const char* monLabel = "Auto";
    Color monAccent = Color{40, 80, 40, 255};
    bool monHL = false;
    if (mode == Track::MonitorMode::In) {
        monLabel = "In";
        monAccent = Color{40, 80, 40, 255};
        monHL = true;
    } else if (mode == Track::MonitorMode::Off) {
        monLabel = "Off";
        monAccent = Color{80, 40, 40, 255};
        monHL = true;
    }
    s.monBtn.setLabel(monLabel);
    s.monBtn.setAccentColor(monAccent);
    s.monBtn.setHighlighted(monHL);
    float monX = ix + 4 + btnW + 2;
    float monW = iw - 8 - btnW - 2;          // fill remaining row width
    s.monBtn.measure(Constraints::tight(monW, kButtonHeight), ctx);
    s.monBtn.layout(Rect{monX, curY, monW, kButtonHeight}, ctx);
    s.monBtn.render(ctx);

    curY += kButtonHeight + 2;

    // Automation mode — v2 FwButton, 4-state cycle Off/Read/Touch/Latch.
    // Same pattern as monBtn: colored accent + highlight only when on.
    {
        auto am = track.autoMode;
        const char* autoLabel = "Off";
        Color autoAccent = ::yawn::ui::Theme::clipSlotEmpty;
        bool autoHL = false;
        if (am == automation::AutoMode::Read) {
            autoLabel = "Read";
            autoAccent = Color{40, 80, 40, 255};
            autoHL = true;
        } else if (am == automation::AutoMode::Touch) {
            autoLabel = "Touch";
            autoAccent = Color{100, 70, 20, 255};
            autoHL = true;
        } else if (am == automation::AutoMode::Latch) {
            autoLabel = "Latch";
            autoAccent = Color{100, 30, 30, 255};
            autoHL = true;
        }
        s.autoBtn.setLabel(autoLabel);
        s.autoBtn.setAccentColor(autoAccent);
        s.autoBtn.setHighlighted(autoHL);
        float autoW = iw - 8;
        s.autoBtn.measure(Constraints::tight(autoW, kButtonHeight), ctx);
        s.autoBtn.layout(Rect{ix + 4, curY, autoW, kButtonHeight}, ctx);
        s.autoBtn.render(ctx);
    }

    curY += kButtonHeight + 4;

    // Pan — v2 FwPan, skip sync during drag to avoid engine rubber-banding.
    // Automation source silences onChange so the per-frame engine-echo
    // doesn't fire a second command (ping-pong).
    if (!s.pan.isDragging())
        s.pan.setValue(ch.pan, ValueChangeSource::Automation);
    s.pan.setThumbColor(col);
    s.pan.measure(Constraints::tight(iw - 8, 16), ctx);
    s.pan.layout(Rect{ix + 4, curY, iw - 8, 16}, ctx);
    s.pan.render(ctx);

    // CC label for pan mapping
    if (m_learnManager) {
        auto panTarget = automation::AutomationTarget::mixer(idx, automation::MixerParam::Pan);
        auto* panMap = m_learnManager->findByTarget(panTarget);
        if (panMap) {
            auto lbl = panMap->label();
            const float ccSize = 8.0f;
            tm.drawText(r, lbl,
                ix + iw - 4 - tm.textWidth(lbl, ccSize),
                curY + 2, ccSize, Color{100, 180, 255, 255});
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
                knob.setValue(send.level, ValueChangeSource::Automation);

            // Color feedback: off → green → yellow → red
            float v = knob.value();
            Color arcCol;
            if (v < 0.01f) {
                arcCol = Color{50, 50, 55, 255};  // off
            } else if (v < 0.5f) {
                // green to yellow
                float tt = v / 0.5f;
                arcCol = Color{
                    static_cast<uint8_t>(40 + tt * 215),
                    static_cast<uint8_t>(180 + tt * 40),
                    static_cast<uint8_t>(40 * (1.0f - tt)),
                    255};
            } else {
                // yellow to red
                float tt = (v - 0.5f) / 0.5f;
                arcCol = Color{255,
                    static_cast<uint8_t>(220 * (1.0f - tt)),
                    0, 255};
            }
            knob.setAccentColor(arcCol);

            knob.measure(Constraints::tight(knobW, knobH), ctx);
            knob.layout(Rect{kx, ky, knobW, knobH}, ctx);
            knob.render(ctx);
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
        s.fader.setValue(ch.volume, ValueChangeSource::Automation);
    s.fader.setTrackColor(col);
    s.fader.measure(Constraints::tight(kFaderWidth, faderH), ctx);
    s.fader.layout(Rect{ix + 4, curY, kFaderWidth, faderH}, ctx);
    s.fader.render(ctx);

    float meterX = ix + 4 + kFaderWidth + 3;
    s.meter.setPeak(m_trackMeters[idx].peakL, m_trackMeters[idx].peakR);
    s.meter.measure(Constraints::tight(kMeterWidth * 2, faderH), ctx);
    s.meter.layout(Rect{meterX, curY, kMeterWidth * 2, faderH}, ctx);
    s.meter.render(ctx);

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

    // dB readout (formerly s.dbLabel). textSecondary gives better
    // contrast than textDim while keeping it visually subdued against
    // the active fader knob.
    {
        float db = ch.volume > 0.001f ? 20.0f * std::log10(ch.volume) : -60.0f;
        char dbText[16];
        if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
        else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
        const float dbSize = theme().metrics.fontSizeSmall;
        const float lh = tm.lineHeight(dbSize);
        tm.drawText(r, dbText, ix + 4,
                    stripY + stripH - 18 + (14.0f - lh) * 0.5f,
                    dbSize, ::yawn::ui::Theme::textSecondary);
    }

    // CC label for volume mapping
    if (m_learnManager) {
        auto volTarget = automation::AutomationTarget::mixer(idx, automation::MixerParam::Volume);
        auto* volMap = m_learnManager->findByTarget(volTarget);
        if (volMap) {
            auto lbl = volMap->label();
            const float ccSize = 8.0f;
            tm.drawText(r, lbl,
                ix + 4, stripY + stripH - 30, ccSize,
                Color{100, 180, 255, 255});
        }
    }

    r.drawRect(sx + stripW - 1, stripY, 1, stripH, ::yawn::ui::Theme::clipSlotBorder);
}

void MixerPanel::paintAudioIO(UIContext& ctx, TrackStrip& s, const Track& track,
                   int idx, float ioX, float ioW, float ioY, float ioH) {
    (void)ioH;
    float dropH = kIOHeight;
    float curY = ioY;

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
    s.audioInputDrop.setSelectedIndex(sel, ValueChangeSource::Automation);
    s.audioInputDrop.measure(Constraints::tight(ioW, dropH), ctx);
    s.audioInputDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.audioInputDrop.render(ctx);

    // Mono toggle — v2 FwToggle. setState drives the green accent fill.
    curY += dropH + 2;
    s.monoBtn.setLabel(track.mono ? "Mono" : "Stereo");
    s.monoBtn.setState(track.mono, ValueChangeSource::Automation);
    s.monoBtn.measure(Constraints::tight(ioW, dropH), ctx);
    s.monoBtn.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.monoBtn.render(ctx);
}

void MixerPanel::paintMidiIO(UIContext& ctx, TrackStrip& s, const Track& track,
                  int idx, float ioX, float ioW, float ioY, float ioH) {
    (void)ioH;
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    float dropH = kIOHeight;
    float labelH = 12.0f;
    // Theme-small for the RX/TX/SC row labels so they scale with the
    // font-scale setting and stay legible.
    const float labelSize = theme().metrics.fontSizeSmall;
    float curY = ioY;

    // "RX" label (formerly s.midiRxLabel).
    tm.drawText(r, "RX", ioX, curY, labelSize, ::yawn::ui::Theme::textSecondary);
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
    s.midiInDrop.setSelectedIndex(
        std::clamp(inPortSel, 0, static_cast<int>(inPortItems.size()) - 1),
        ValueChangeSource::Automation);
    s.midiInDrop.measure(Constraints::tight(ioW, dropH), ctx);
    s.midiInDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiInDrop.render(ctx);
    curY += dropH + 2;

    // MIDI Input channel
    std::vector<std::string> chItems = {"All"};
    for (int c = 1; c <= 16; ++c) chItems.push_back(std::to_string(c));
    s.midiInChDrop.setItems(chItems);
    int inChSel = (track.midiInputChannel < 0) ? 0 : track.midiInputChannel + 1;
    s.midiInChDrop.setSelectedIndex(std::clamp(inChSel, 0, 16),
        ValueChangeSource::Automation);
    s.midiInChDrop.measure(Constraints::tight(ioW, dropH), ctx);
    s.midiInChDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiInChDrop.render(ctx);
    curY += dropH + 4;

    // "TX" label (formerly s.midiTxLabel).
    tm.drawText(r, "TX", ioX, curY, labelSize, ::yawn::ui::Theme::textSecondary);
    curY += labelH;

    // MIDI Output port
    std::vector<std::string> outPortItems = {"None"};
    if (m_midiEngine) {
        for (int i = 0; i < m_midiEngine->availableOutputCount(); ++i)
            outPortItems.push_back(m_midiEngine->availableOutputName(i));
    }
    s.midiOutDrop.setItems(outPortItems);
    int outPortSel = (track.midiOutputPort < 0) ? 0 : track.midiOutputPort + 1;
    s.midiOutDrop.setSelectedIndex(
        std::clamp(outPortSel, 0, static_cast<int>(outPortItems.size()) - 1),
        ValueChangeSource::Automation);
    s.midiOutDrop.measure(Constraints::tight(ioW, dropH), ctx);
    s.midiOutDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiOutDrop.render(ctx);
    curY += dropH + 2;

    // MIDI Output channel
    s.midiOutChDrop.setItems(chItems);
    int outChSel = (track.midiOutputChannel < 0) ? 0 : track.midiOutputChannel + 1;
    s.midiOutChDrop.setSelectedIndex(std::clamp(outChSel, 0, 16),
        ValueChangeSource::Automation);
    s.midiOutChDrop.measure(Constraints::tight(ioW, dropH), ctx);
    s.midiOutChDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
    s.midiOutChDrop.render(ctx);
    curY += dropH + 4;

    // Sidechain source dropdown (only if instrument supports it)
    bool showSidechain = false;
    if (m_engine) {
        auto* inst = m_engine->instrument(idx);
        if (inst && inst->supportsSidechain()) showSidechain = true;
    }
    if (showSidechain && m_project) {
        // "SC" label (formerly s.sidechainLabel).
        tm.drawText(r, "SC", ioX, curY, labelSize, ::yawn::ui::Theme::textSecondary);
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
        s.sidechainDrop.setSelectedIndex(scSel, ValueChangeSource::Automation);
        s.sidechainDrop.measure(Constraints::tight(ioW, dropH), ctx);
        s.sidechainDrop.layout(Rect{ioX, curY, ioW, dropH}, ctx);
        s.sidechainDrop.render(ctx);
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
