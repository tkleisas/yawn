// App_Events.cpp — SDL event pump: keyboard handling + the main
// processEvents() dispatch (mouse/keyboard/drag-drop/window events),
// plus the SDL→fw2 key/button/modifier translation helpers.
// Split out of App.cpp.
#include "app/App.h"
#include "Version.h"
#include "visual/LiveInputEnum.h"
#include "visual/VisualModBus.h"
#include "transcribe/AudioToMidi.h"
#include "ui/framework/v2/Fw2Painters.h"
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/Dialog.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Theme.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "instruments/KarplusStrong.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "instruments/Multisampler.h"
#include "instruments/InstrumentRack.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Limiter.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Phaser.h"
#include "effects/Wah.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"
#include "effects/Tuner.h"
#include "effects/BeatRepeat.h"
#include "effects/BufferRepeat.h"
#include "effects/Resampler.h"
#include "effects/ClockDrift.h"
#include "effects/CDError.h"
#include "effects/AutoPanner.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include "midi/LFO.h"
#include "util/ProjectSerializer.h"
#include "util/Logger.h"
#include "util/FileIO.h"
#include "util/MidiFileIO.h"
#include "audio/OfflineRenderer.h"
#include "presets/PresetGenerator.h"
#include "presets/MidiLoopManager.h"
#include "presets/DrumPatterns.h"
#include "presets/MelodicPatterns.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cinttypes>
#include <cstring>
#include <thread>

namespace yawn {


// ─── SDL → fw2 event translation ────────────────────────────────────
// Used by the SDL event pump to feed events into the fw2 LayerStack
// before v1 dispatch sees them. Only the keys / buttons / mods that
// fw2 widgets currently consume are mapped; unmapped keys become
// Key::None which the LayerStack dispatch treats as "not handled".

static yawn::ui::fw2::Key sdlKeyToFw2(SDL_Keycode k) {
    using yawn::ui::fw2::Key;
    switch (k) {
        case SDLK_ESCAPE:     return Key::Escape;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:   return Key::Enter;
        case SDLK_SPACE:      return Key::Space;
        case SDLK_UP:         return Key::Up;
        case SDLK_DOWN:       return Key::Down;
        case SDLK_LEFT:       return Key::Left;
        case SDLK_RIGHT:      return Key::Right;
        case SDLK_TAB:        return Key::Tab;
        case SDLK_HOME:       return Key::Home;
        case SDLK_END:        return Key::End;
        case SDLK_PAGEUP:     return Key::PageUp;
        case SDLK_PAGEDOWN:   return Key::PageDown;
        case SDLK_BACKSPACE:  return Key::Backspace;
        case SDLK_DELETE:     return Key::Delete;
        case SDLK_INSERT:     return Key::Insert;
        default:              return Key::None;
    }
}

static uint16_t sdlModsToFw2(SDL_Keymod m) {
    using namespace yawn::ui::fw2::ModifierKey;
    uint16_t out = None;
    if (m & SDL_KMOD_SHIFT) out |= Shift;
    if (m & SDL_KMOD_CTRL)  out |= Ctrl;
    if (m & SDL_KMOD_ALT)   out |= Alt;
    if (m & SDL_KMOD_GUI)   out |= Super;
    return out;
}

static yawn::ui::fw2::MouseButton sdlBtnToFw2(int btn) {
    using yawn::ui::fw2::MouseButton;
    switch (btn) {
        case SDL_BUTTON_RIGHT:  return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        default:                return MouseButton::Left;
    }
}

void App::handleKeyEvent(const SDL_Event& event) {
    if (event.key.repeat) return;
    bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
    bool ctrl  = (event.key.mod & SDL_KMOD_CTRL)  != 0;

    // fw2 LayerStack first — open overlays (dropdowns,
    // dialogs, context menus) get first crack at keys.
    {
        ui::fw2::KeyEvent ke{};
        ke.key       = sdlKeyToFw2(event.key.key);
        ke.modifiers = sdlModsToFw2(SDL_GetModState());
        ke.isRepeat  = event.key.repeat;
        if (ke.key != ui::fw2::Key::None &&
            m_fw2LayerStack.dispatchKey(ke)) return;
    }

    // v1 confirm dialog retired — fw2::Dialog on the
    // Modal layer handles Escape/Enter through LayerStack.

    // v1 TextInputDialog retired — fw2 FwTextInputDialog
    // runs on LayerStack::Modal, so keys reach it via the
    // LayerStack dispatchKey path above.
    // v1 About dialog retired — fw2::Dialog handles
    // Escape/Enter through LayerStack.

    // v1 Preferences dialog retired — fw2 FwPreferencesDialog
    // runs on LayerStack::Modal and consumes keys via the
    // LayerStack::dispatchKey path above.

    // v1 Export dialog retired — FwExportDialog runs on
    // LayerStack::Modal and consumes keys via the
    // LayerStack dispatchKey path above.

    // v2 dropdowns in the BrowserPanel consume keyboard
    // events through LayerStack::dispatchKey (handled
    // above), so the panel-side hasOpenDropdown() early
    // route is no longer needed.

    // Transport editing (BPM / time signature) takes priority
    if (m_transportPanel->isEditing()) {
        int kc = 0;
        if (event.key.key == SDLK_RETURN) kc = 13;
        else if (event.key.key == SDLK_ESCAPE) kc = 27;
        else if (event.key.key == SDLK_BACKSPACE) kc = 8;
        else if (event.key.key == SDLK_TAB) kc = 9;
        if (kc) {
            m_transportPanel->handleKeyDown(kc);
            if (!m_transportPanel->isEditing())
                SDL_StopTextInput(m_mainWindow.getHandle());
        }
        return;
    }

    // Keyboard shortcuts for menus (Ctrl combos always take priority)
    if (ctrl) {
        switch (event.key.key) {
            case SDLK_Q: m_running = false; break;
            case SDLK_N: newProject(); break;
            case SDLK_O: openProject(); break;
            case SDLK_S:
                if (shift) saveProjectAs();
                else       saveProject();
                break;
            case SDLK_Z: // Undo
                if (m_undoManager.canUndo()) { m_undoManager.undo(); markDirty(); }
                break;
            case SDLK_Y: // Redo
                if (m_undoManager.canRedo()) { m_undoManager.redo(); markDirty(); }
                break;
            case SDLK_C: { // Copy clip
                // Arrangement view: copy the selected arrangement clip
                if (m_project.viewMode() == ViewMode::Arrangement) {
                    int ct = m_arrangementPanel->selectedClipTrack();
                    int ci = m_arrangementPanel->selectedClipIndex();
                    if (ct >= 0 && ci >= 0) {
                        auto& cs = m_project.track(ct).arrangementClips;
                        if (ci < static_cast<int>(cs.size())) {
                            m_arrangementClipboard = cs[ci];
                            m_arrangementClipboardValid = true;
                        }
                    }
                    break;
                }
                auto* slot = m_project.getSlot(m_selectedTrack, m_selectedScene);
                if (slot && slot->audioClip) {
                    m_clipboard.clear();
                    m_clipboard.type = ClipboardData::Type::Audio;
                    m_clipboard.audioClip = slot->audioClip->clone();
                } else if (slot && slot->midiClip) {
                    m_clipboard.clear();
                    m_clipboard.type = ClipboardData::Type::Midi;
                    m_clipboard.midiClip = slot->midiClip->clone();
                }
                break;
            }
            case SDLK_X: { // Cut clip
                // Arrangement view: cut = copy + delete
                if (m_project.viewMode() == ViewMode::Arrangement) {
                    int ct = m_arrangementPanel->selectedClipTrack();
                    int ci = m_arrangementPanel->selectedClipIndex();
                    if (ct >= 0 && ci >= 0) {
                        auto& cs = m_project.track(ct).arrangementClips;
                        if (ci < static_cast<int>(cs.size())) {
                            m_arrangementClipboard = cs[ci];
                            m_arrangementClipboardValid = true;
                            m_arrangementPanel->handleAppKey(
                                SDLK_DELETE, /*ctrl=*/false);
                        }
                    }
                    break;
                }
                int ct = m_selectedTrack, cs = m_selectedScene;
                auto* slot = m_project.getSlot(ct, cs);
                if (slot && slot->audioClip) {
                    auto backup = slot->audioClip->clone();
                    m_clipboard.clear();
                    m_clipboard.type = ClipboardData::Type::Audio;
                    m_clipboard.audioClip = slot->audioClip->clone();
                    m_audioEngine.sendCommand(audio::StopClipMsg{ct});
                    m_project.graveyardSlotClips(*slot);
                    markDirty();
                    m_undoManager.push({"Cut Audio Clip",
                        [this, ct, cs, b = std::shared_ptr<audio::Clip>(std::move(backup))]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { s->audioClip = b->clone(); markDirty(); }
                        },
                        [this, ct, cs]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { m_audioEngine.sendCommand(audio::StopClipMsg{ct});
                                      m_project.graveyardSlotClips(*s); markDirty(); }
                        }, ""});
                } else if (slot && slot->midiClip) {
                    auto backup = slot->midiClip->clone();
                    m_clipboard.clear();
                    m_clipboard.type = ClipboardData::Type::Midi;
                    m_clipboard.midiClip = slot->midiClip->clone();
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{ct});
                    m_project.graveyardSlotClips(*slot);
                    markDirty();
                    m_undoManager.push({"Cut MIDI Clip",
                        [this, ct, cs, b = std::shared_ptr<midi::MidiClip>(std::move(backup))]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { s->midiClip = b->clone(); markDirty(); }
                        },
                        [this, ct, cs]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { m_audioEngine.sendCommand(audio::StopMidiClipMsg{ct});
                                      m_project.graveyardSlotClips(*s); markDirty(); }
                        }, ""});
                }
                break;
            }
            case SDLK_V: { // Paste clip
                // Arrangement view: paste at current transport position
                // on the source track, from the arrangement clipboard.
                if (m_project.viewMode() == ViewMode::Arrangement) {
                    if (!m_arrangementClipboardValid) break;
                    // Pick destination track: selected track if it
                    // matches the clipboard's clip type, otherwise
                    // fall back to whatever track the clip came from
                    // when it was copied (we don't preserve that
                    // explicitly, so default to m_selectedTrack and
                    // let the user move it if needed).
                    int pt = m_arrangementPanel->selectedTrack();
                    if (pt < 0 || pt >= m_project.numTracks()) break;
                    // Type compatibility: audio→Audio track,
                    // midi→Midi, visual→Visual. If mismatched,
                    // reject silently — pasting a MIDI clip on an
                    // audio track wouldn't play.
                    auto trkType = m_project.track(pt).type;
                    auto clipType = m_arrangementClipboard.type;
                    bool typeOk =
                        (trkType == Track::Type::Audio  && clipType == ArrangementClip::Type::Audio)  ||
                        (trkType == Track::Type::Midi   && clipType == ArrangementClip::Type::Midi)   ||
                        (trkType == Track::Type::Visual && clipType == ArrangementClip::Type::Visual);
                    if (!typeOk) break;
                    ArrangementClip ac = m_arrangementClipboard;  // deep copy (visualClip cloned)
                    ac.startBeat = m_audioEngine.transport().positionInBeats();
                    m_project.track(pt).arrangementClips.push_back(std::move(ac));
                    m_project.track(pt).sortArrangementClips();
                    m_project.updateArrangementLength();
                    syncArrangementClipsToEngine(pt);
                    if (!m_project.track(pt).arrangementActive) {
                        m_project.track(pt).arrangementActive = true;
                        m_audioEngine.sendCommand(
                            audio::SetTrackArrActiveMsg{pt, true});
                    }
                    markDirty();
                    break;
                }
                int pt = m_selectedTrack, ps = m_selectedScene;
                if (m_clipboard.type == ClipboardData::Type::Audio && m_clipboard.audioClip) {
                    auto* slot = m_project.getSlot(pt, ps);
                    if (slot) {
                        // Capture old slot contents for undo
                        std::shared_ptr<audio::Clip> oldAudio;
                        std::shared_ptr<midi::MidiClip> oldMidi;
                        if (slot->audioClip) oldAudio.reset(slot->audioClip->clone().release());
                        if (slot->midiClip) oldMidi.reset(slot->midiClip->clone().release());
                        m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                        m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                        m_project.graveyardSlotClips(*slot);
                        slot->audioClip = m_clipboard.audioClip->clone();
                        markDirty();
                        auto pastedClone = m_clipboard.audioClip->clone();
                        m_undoManager.push({"Paste Audio Clip",
                            [this, pt, ps, oldAudio, oldMidi]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                m_project.graveyardSlotClips(*s);
                                if (oldAudio) s->audioClip = oldAudio->clone();
                                if (oldMidi) s->midiClip = oldMidi->clone();
                                markDirty();
                            },
                            [this, pt, ps, pc = std::shared_ptr<audio::Clip>(std::move(pastedClone))]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                m_project.graveyardSlotClips(*s);
                                s->audioClip = pc->clone();
                                markDirty();
                            }, ""});
                    }
                } else if (m_clipboard.type == ClipboardData::Type::Midi && m_clipboard.midiClip) {
                    auto* slot = m_project.getSlot(pt, ps);
                    if (slot) {
                        std::shared_ptr<audio::Clip> oldAudio;
                        std::shared_ptr<midi::MidiClip> oldMidi;
                        if (slot->audioClip) oldAudio.reset(slot->audioClip->clone().release());
                        if (slot->midiClip) oldMidi.reset(slot->midiClip->clone().release());
                        m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                        m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                        m_project.graveyardSlotClips(*slot);
                        slot->midiClip = m_clipboard.midiClip->clone();
                        markDirty();
                        auto pastedClone = m_clipboard.midiClip->clone();
                        m_undoManager.push({"Paste MIDI Clip",
                            [this, pt, ps, oldAudio, oldMidi]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                m_project.graveyardSlotClips(*s);
                                if (oldAudio) s->audioClip = oldAudio->clone();
                                if (oldMidi) s->midiClip = oldMidi->clone();
                                markDirty();
                            },
                            [this, pt, ps, pc = std::shared_ptr<midi::MidiClip>(std::move(pastedClone))]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                m_project.graveyardSlotClips(*s);
                                s->midiClip = pc->clone();
                                markDirty();
                            }, ""});
                    }
                }
                break;
            }
            case SDLK_D: { // Duplicate clip to next empty slot below
                int dt = m_selectedTrack, ds = m_selectedScene;
                auto* srcSlot = m_project.getSlot(dt, ds);
                if (srcSlot && !srcSlot->empty()) {
                    for (int s = ds + 1; s < m_project.numScenes(); ++s) {
                        auto* dst = m_project.getSlot(dt, s);
                        if (dst && dst->empty()) {
                            if (srcSlot->audioClip)
                                dst->audioClip = srcSlot->audioClip->clone();
                            else if (srcSlot->midiClip)
                                dst->midiClip = srcSlot->midiClip->clone();
                            int destScene = s;
                            m_selectedScene = s;
                            m_sessionPanel->setSelectedScene(m_selectedScene);
                            markDirty();
                            m_undoManager.push({"Duplicate Clip",
                                [this, dt, destScene]{
                                    auto* s2 = m_project.getSlot(dt, destScene);
                                    if (s2) { s2->clear(); markDirty(); }
                                },
                                [this, dt, ds, destScene]{
                                    auto* src2 = m_project.getSlot(dt, ds);
                                    auto* dst2 = m_project.getSlot(dt, destScene);
                                    if (src2 && dst2) {
                                        if (src2->audioClip) dst2->audioClip = src2->audioClip->clone();
                                        else if (src2->midiClip) dst2->midiClip = src2->midiClip->clone();
                                        markDirty();
                                    }
                                }, ""});
                            break;
                        }
                    }
                }
                break;
            }
            default: break;
        }
        return;
    }

    // InputState keyboard forwarding (for focused widgets)
    if (m_inputState.focused()) {
        if (m_inputState.onKeyDown(static_cast<int>(event.key.key), ctrl, shift))
            return;
    }

    // Track rename keyboard handling
    if (m_sessionPanel->isRenamingTrack()) {
        bool wasRenaming = true;
        m_sessionPanel->handleRenameKeyDown(static_cast<int>(event.key.key));
        if (!m_sessionPanel->isRenamingTrack())
            SDL_StopTextInput(m_mainWindow.getHandle());
        if (wasRenaming) return;
    }
    if (m_arrangementPanel->isRenamingTrack()) {
        bool wasRenaming = true;
        m_arrangementPanel->handleRenameKeyDown(static_cast<int>(event.key.key));
        if (!m_arrangementPanel->isRenamingTrack())
            SDL_StopTextInput(m_mainWindow.getHandle());
        if (wasRenaming) return;
    }

    // Detail panel knob text-edit mode
    if (m_showDetailPanel && m_detailPanel->hasEditingKnob()) {
        if (m_detailPanel->forwardKeyDown(static_cast<int>(event.key.key))) {
            if (!m_detailPanel->hasEditingKnob())
                SDL_StopTextInput(m_mainWindow.getHandle());
            return;
        }
    }
    // Visual params panel knob text-edit mode (Enter/Esc/Backspace
    // for any knob inside that panel — A..H, source, chain, post-fx).
    if (m_visualParamsPanel->isVisible() &&
        m_visualParamsPanel->hasEditingKnob()) {
        if (m_visualParamsPanel->forwardKeyDown(static_cast<int>(event.key.key))) {
            if (!m_visualParamsPanel->hasEditingKnob())
                SDL_StopTextInput(m_mainWindow.getHandle());
            return;
        }
    }
    // Browser panel knob text-edit mode
    if (m_browserPanel->hasEditingKnob()) {
        if (m_browserPanel->forwardKeyDown(static_cast<int>(event.key.key))) {
            if (!m_browserPanel->hasEditingKnob())
                SDL_StopTextInput(m_mainWindow.getHandle());
            return;
        }
    }

    // Piano roll keyboard shortcuts
    if (m_pianoRoll->isOpen()) {
        bool prCtrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
        if (m_pianoRoll->handleKeyDown(static_cast<int>(event.key.key), prCtrl)) {
            if (!m_pianoRoll->isOpen()) {
                // Piano roll was closed (Escape)
            }
            return;
        }
    }

    // Virtual keyboard (intercepts musical keys before shortcuts).
    // Skip when ANY text-input gesture is active so the number-row
    // keys (2/3/5/6/7/9/0 — all bound to black-key notes here) reach
    // the editing target instead of triggering note-on. Without
    // these guards, double-clicking a knob and typing a value
    // silently plays MIDI notes and the typed digits get eaten.
    {
        const bool textInputActive =
            m_sessionPanel->isRenamingTrack() ||
            m_arrangementPanel->isRenamingTrack() ||
            (m_showDetailPanel && m_detailPanel->hasEditingKnob()) ||
            (m_visualParamsPanel->isVisible() &&
             m_visualParamsPanel->hasEditingKnob()) ||
            m_browserPanel->hasEditingKnob() ||
            m_transportPanel->isEditing() ||
            m_textInputDialog.isOpen();
        if (!textInputActive) {
            if (m_virtualKeyboard.onKeyDown(event.key.key))
                return;
        }
    }

    // Detail panel arrow key navigation
    if (m_showDetailPanel && m_detailPanel->isFocused()) {
        if (event.key.key == SDLK_LEFT) { m_detailPanel->scrollLeft(); return; }
        if (event.key.key == SDLK_RIGHT) { m_detailPanel->scrollRight(); return; }
    }

    switch (event.key.key) {
        case SDLK_ESCAPE:
            // ESC cancels an in-flight stem-separation job first (it's a
            // multi-minute background task with no other cancel affordance).
            if (m_pendingStem && m_pendingStem->active.load() &&
                !m_pendingStem->done.load()) {
                m_pendingStem->cancel.store(true);
                m_toastManager.show("Cancelling stem separation…", 2.0f,
                                    ui::ToastManager::Severity::Info);
                break;
            }
            // ESC exits fullscreen on the visual output if
            // that's what's currently going on; otherwise it
            // keeps its menu/quit behaviour.
            if (m_visualEngine.isFullscreen()) {
                m_visualEngine.setFullscreen(false);
            } else if (m_menuBar.isOpen()) {
                m_menuBar.close();
            } else {
                m_running = false;
            }
            break;

        case SDLK_F11:
            if (!m_visualEngine.isOutputVisible())
                m_visualEngine.setOutputVisible(true);
            m_visualEngine.setFullscreen(!m_visualEngine.isFullscreen());
            break;

        case SDLK_SPACE:
            LOG_INFO("User", "Space → %s", m_displayPlaying ? "stop" : "play");
            if (m_displayPlaying) {
                m_audioEngine.sendCommand(audio::TransportStopMsg{});
            } else {
                // Re-launch default clips before starting transport.
                // Pressing the global Play means "start from the top, now",
                // so launch immediately (QuantizeMode::None) in phase with
                // the transport. Per-clip launchQuantize is for launching a
                // clip *live* during playback (come in on the next bar) — it
                // must not defer the audio behind the playhead on the Play
                // gesture (that was the "drums come in half a bar late" bug).
                for (int t = 0; t < m_project.numTracks(); ++t) {
                    int ds = m_project.track(t).defaultScene;
                    if (ds < 0 || ds >= m_project.numScenes()) continue;
                    auto* slot = m_project.getSlot(t, ds);
                    if (!slot) continue;
                    if (slot->audioClip)
                        m_audioEngine.sendCommand(audio::LaunchClipMsg{t, ds, slot->audioClip.get(),
                            audio::QuantizeMode::None, &slot->clipAutomation, slot->followAction});
                    else if (slot->midiClip)
                        m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{t, ds, slot->midiClip.get(),
                            audio::QuantizeMode::None, &slot->clipAutomation, slot->followAction});
                    else if (slot->visualClip) {
                        launchVisualClipData(t, *slot->visualClip,
                                              slot->visualClip->firstShaderPath());
                        stampVisualLaunch(t, ds);
                        m_sessionPanel->updateClipState(t, /*playing*/true,
                                                          /*playPos*/0, ds);
                    }
                }
                m_audioEngine.sendCommand(audio::TransportPlayMsg{});
            }
            break;

        case SDLK_KP_PLUS:
        case SDLK_EQUALS: {
            double newBpm = m_audioEngine.transport().bpm() + 1.0;
            m_audioEngine.sendCommand(audio::TransportSetBPMMsg{std::min(newBpm, 999.0)});
            break;
        }
        case SDLK_KP_MINUS:
        case SDLK_MINUS: {
            double newBpm = m_audioEngine.transport().bpm() - 1.0;
            m_audioEngine.sendCommand(audio::TransportSetBPMMsg{std::max(newBpm, 20.0)});
            break;
        }
        case SDLK_HOME:
            m_audioEngine.sendCommand(audio::TransportSetPositionMsg{0});
            break;

        case SDLK_TAB:
            if (!shift) {
                auto next = (m_project.viewMode() == ViewMode::Session)
                    ? ViewMode::Arrangement
                    : ViewMode::Session;
                switchToView(next);
            }
            break;

        case SDLK_INSERT:
            // Insert a new scene below the selection (session view).
            // Mirrors the Scene → Insert Scene menu item.
            insertSceneAtSelection();
            break;

        case SDLK_M:
            if (!shift) m_showMixer = !m_showMixer;
            break;

        case SDLK_D:
            if (ctrl && !shift && m_project.viewMode() == ViewMode::Arrangement) {
                m_arrangementPanel->handleAppKey(SDLK_D, /*ctrl=*/true);
            } else if (!shift && !ctrl) {
                m_showDetailPanel = !m_showDetailPanel;
                if (m_showDetailPanel) {
                    m_detailPanel->setOpen(true);
                    switch (m_detailTarget) {
                        case DetailTarget::Track:     updateDetailForSelectedTrack(); break;
                        case DetailTarget::ReturnBus: updateDetailForReturnBus(m_detailReturnBus); break;
                        case DetailTarget::Master:    updateDetailForMaster(); break;
                    }
                }
            }
            break;

        case SDLK_DELETE:
        case SDLK_BACKSPACE: {
            if (m_project.viewMode() == ViewMode::Arrangement) {
                m_arrangementPanel->handleAppKey(
                    static_cast<int>(event.key.key), /*ctrl=*/ctrl);
            } else {
                auto* slot = m_project.getSlot(m_selectedTrack, m_selectedScene);
                if (slot && !slot->empty()) {
                    // Immediate (unquantized) stop — the audio
                    // thread sets state.clip = nullptr right away,
                    // so by the time the graveyard TTL expires the
                    // pointer is no longer in use. With the default
                    // NextBar quantize, a slow tempo could keep
                    // state.clip live longer than the graveyard
                    // keepalive window → UAF.
                    m_audioEngine.sendCommand(audio::StopClipMsg{
                        m_selectedTrack, audio::QuantizeMode::None});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{m_selectedTrack});
                    m_project.clearSlot(m_selectedTrack, m_selectedScene);
                    markDirty();
                }
            }
            break;
        }

        // Arrow key clip navigation (session view only)
        // Shift+Arrow moves the controller grid region
        case SDLK_UP:
            if (m_project.viewMode() == ViewMode::Session) {
                if (shift) {
                    m_sessionPanel->moveGridRegion(0, -1);
                } else {
                    m_selectedScene = std::max(0, m_selectedScene - 1);
                    m_sessionPanel->setSelectedScene(m_selectedScene);
                    m_sessionPanel->ensureSelectionVisible();
                    updateDetailForSelectedTrack();
                }
            }
            break;
        case SDLK_DOWN:
            if (m_project.viewMode() == ViewMode::Session) {
                if (shift) {
                    m_sessionPanel->moveGridRegion(0, 1);
                } else {
                    m_selectedScene = std::min(m_project.numScenes() - 1, m_selectedScene + 1);
                    m_sessionPanel->setSelectedScene(m_selectedScene);
                    m_sessionPanel->ensureSelectionVisible();
                    updateDetailForSelectedTrack();
                }
            }
            break;
        case SDLK_LEFT:
            if (m_project.viewMode() == ViewMode::Session) {
                if (shift) {
                    m_sessionPanel->moveGridRegion(-1, 0);
                } else {
                    m_selectedTrack = std::max(0, m_selectedTrack - 1);
                    m_sessionPanel->setSelectedTrack(m_selectedTrack);
                    m_sessionPanel->ensureSelectionVisible();
                    updateDetailForSelectedTrack();
                }
            }
            break;
        case SDLK_RIGHT:
            if (m_project.viewMode() == ViewMode::Session) {
                if (shift) {
                    m_sessionPanel->moveGridRegion(1, 0);
                } else {
                    m_selectedTrack = std::min(m_project.numTracks() - 1, m_selectedTrack + 1);
                    m_sessionPanel->setSelectedTrack(m_selectedTrack);
                    m_sessionPanel->ensureSelectionVisible();
                    updateDetailForSelectedTrack();
                }
            }
            break;

        // Enter launches/stops selected clip
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (m_project.viewMode() == ViewMode::Session)
                m_sessionPanel->launchOrStopSlot(m_selectedTrack, m_selectedScene);
            break;

        // G toggles controller grid region overlay
        case SDLK_G:
            if (m_project.viewMode() == ViewMode::Session)
                m_sessionPanel->toggleGridRegion();
            break;

        // Arrangement-mode key forwarding (L=loop, F=follow)
        case SDLK_L:
        case SDLK_F:
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET:
            if (m_project.viewMode() == ViewMode::Arrangement) {
                m_arrangementPanel->handleAppKey(
                    static_cast<int>(event.key.key), /*ctrl=*/ctrl);
            }
            break;

        default:
            break;
    }
    return;
}

void App::processEvents() {
    computeLayout();  // ensure widget bounds are current for hit-testing

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;

            case SDL_EVENT_AUDIO_DEVICE_REMOVED:
                // Device lost — fully tear down the PA stream so the
                // user can pick a new device in Preferences (stop +
                // close + null m_stream). The callback already guards
                // against null output, so any in-flight callbacks
                // return silence until the stream actually closes.
                m_audioEngine.handleDeviceLost();
                break;

            case SDL_EVENT_AUDIO_DEVICE_ADDED:
                // New device appeared — don't auto-resume (surprises
                // the user mid-session). Leave it to the toggle / the
                // Preferences dialog.
                LOG_INFO("Audio", "Device added (id=%u)",
                         static_cast<unsigned>(event.adevice.which));
                break;

             case SDL_EVENT_KEY_DOWN:
                handleKeyEvent(event);
                break;

            case SDL_EVENT_KEY_UP: {
                // Virtual keyboard note-off
                m_virtualKeyboard.onKeyUp(event.key.key);
                break;
            }

            case SDL_EVENT_TEXT_INPUT: {
                // Text input dialog (modal) — takeTextInput pushes
                // the text into the embedded FwTextInput.
                if (m_textInputDialog.isOpen()) {
                    m_textInputDialog.takeTextInput(event.text.text);
                    break;
                }
                // Auto-Sample dialog (modal) — capture-name field.
                if (m_autoSampleDialog.isOpen()) {
                    m_autoSampleDialog.takeTextInput(event.text.text);
                    break;
                }
                // Track rename text input
                if (m_sessionPanel->isRenamingTrack()) {
                    m_sessionPanel->handleRenameTextInput(event.text.text);
                    break;
                }
                if (m_arrangementPanel->isRenamingTrack()) {
                    m_arrangementPanel->handleRenameTextInput(event.text.text);
                    break;
                }
                // Transport editing (BPM / time sig)
                if (m_transportPanel->isEditing()) {
                    m_transportPanel->handleTextInput(event.text.text);
                    break;
                }
                // Detail panel knob text-edit mode
                if (m_showDetailPanel && m_detailPanel->hasEditingKnob()) {
                    m_detailPanel->forwardTextInput(event.text.text);
                    break;
                }
                // Visual-params panel knob text-edit mode (A..H,
                // source-shader knobs, chain pass knobs, post-FX
                // knobs all funnel through the same editingKnob()
                // search). Without this the user could enter edit
                // mode but typed digits would be ignored.
                if (m_visualParamsPanel->isVisible() &&
                    m_visualParamsPanel->hasEditingKnob()) {
                    m_visualParamsPanel->forwardTextInput(event.text.text);
                    break;
                }
                // Browser panel knob text-edit mode
                if (m_browserPanel->hasEditingKnob()) {
                    m_browserPanel->forwardTextInput(event.text.text);
                    break;
                }
                if (m_inputState.focused()) {
                    m_inputState.onTextInput(event.text.text);
                }
                break;
            }

            case SDL_EVENT_MOUSE_MOTION: {
                float mx = event.motion.x;
                float my = event.motion.y;
                m_lastMouseX = mx;
                m_lastMouseY = my;

                // Tooltip hover tracking — runs first so a tooltip
                // cancels as soon as the pointer leaves the target
                // widget, regardless of whether the LayerStack (or
                // v1 tree) ends up consuming the move for something
                // else. Cheap (hash lookup + rect tests).
                ui::fw2::TooltipManager::instance().onPointerMoved(mx, my);

                // Drag ghost follows the cursor regardless of which
                // panel ends up consuming the motion event. Ctrl
                // state is also pushed in so the ghost can render a
                // "+" badge for clone semantics.
                {
                    auto& dm = ui::fw2::DragManager::instance();
                    // Promote an armed MIDI-loop drag (mouse-down on a
                    // Loops-tab row) into an active global drag once the
                    // cursor moves past the threshold. This keeps a plain
                    // click-to-select from flashing a drag ghost.
                    if (m_loopDragArmed && !dm.active()) {
                        const float dx = mx - m_loopDragStartX;
                        const float dy = my - m_loopDragStartY;
                        constexpr float kThresh = 5.0f;
                        if (dx * dx + dy * dy > kThresh * kThresh) {
                            ui::fw2::DragPayload pl;
                            pl.kind = ui::fw2::DragPayload::Kind::MidiLoop;
                            pl.midiLoopPath = m_loopDragArmedPath;
                            pl.label = std::filesystem::path(m_loopDragArmedPath)
                                           .stem().string();
                            dm.start(std::move(pl), mx, my);
                        }
                    }
                    dm.updatePos(mx, my);
                    dm.setCtrlHeld((SDL_GetModState() & SDL_KMOD_CTRL) != 0);
                }

                // fw2 LayerStack — overlays track hover before v1 sees it.
                {
                    ui::fw2::MouseMoveEvent me{};
                    me.x = mx; me.y = my;
                    me.dx = event.motion.xrel; me.dy = event.motion.yrel;
                    me.modifiers = sdlModsToFw2(SDL_GetModState());
                    me.timestampMs = SDL_GetTicks();
                    if (m_fw2LayerStack.dispatchMouseMove(me)) break;
                }

                // Detail panel context menu hover
                if (m_showDetailPanel)
                    m_detailPanel->handleDeviceContextMenuMouseMove(mx, my);

                // Menu bar hover — now routed through the v1 widget tree
                // via MenuBarWrapper::onMouseMove, so no explicit call here.

                // InputState hover + drag (computes dx/dy internally)
                m_inputState.onMouseMove(mx, my);

                // Forward to widget tree. fw2::FlexBox's onMouseMove
                // handles two cases:
                //   • a descendant currently holds capture (knob drag,
                //     fader drag, etc.) → forward to it directly,
                //   • otherwise → propagate hover to the child under
                //     the pointer so panels can update their hover
                //     state (e.g. ContentGrid's divider hover, used
                //     just below to pick a cursor shape).
                {
                    ui::fw2::MouseMoveEvent me{};
                    me.x = mx; me.y = my;
                    me.dx = event.motion.xrel; me.dy = event.motion.yrel;
                    me.modifiers = sdlModsToFw2(SDL_GetModState());
                    me.timestampMs = SDL_GetTicks();
                    m_rootLayout->dispatchMouseMove(me);
                }

                // Bottom-docked panel resize handles (piano roll, detail
                // panel): hover is computed HERE from the live pointer
                // position — fw2 move dispatch only reaches the child
                // under the pointer, so panel-local tracking would go
                // stale the moment the pointer leaves the strip.
                const bool prHandle = m_pianoRoll &&
                    (m_pianoRoll->isOverResizeHandle(mx, my) ||
                     m_pianoRoll->isHandleDragging());
                const bool dpHandle = m_showDetailPanel && m_detailPanel &&
                    (m_detailPanel->isOverResizeHandle(mx, my) ||
                     m_detailPanel->isHandleDragging());
                if (m_pianoRoll)   m_pianoRoll->setHandleHover(prHandle);
                if (m_detailPanel) m_detailPanel->setHandleHover(dpHandle);

                // Update cursor shape based on content grid divider hover or panel resize
                bool wantNS = m_contentGrid->wantsVerticalResize()
                           || (m_arrangementPanel && m_arrangementPanel->wantsVerticalResize())
                           || prHandle || dpHandle;
                bool gridEW = m_contentGrid->wantsHorizontalResize();
                // Hovering / dragging a clip's left or right trim edge.
                bool clipEW = m_arrangementPanel &&
                              m_arrangementPanel->wantsHorizontalResize();
                if (gridEW && wantNS) {
                    SDL_SetCursor(m_cursorMove);
                } else if (gridEW || clipEW) {
                    SDL_SetCursor(m_cursorEWResize);
                } else if (wantNS) {
                    SDL_SetCursor(m_cursorNSResize);
                } else {
                    SDL_SetCursor(m_cursorDefault);
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                float mx = event.button.x;
                float my = event.button.y;
                int btn = event.button.button;

                // fw2 LayerStack — overlays (modal dialogs, dropdowns,
                // tooltips, toasts) get first crack. Outside-click
                // dismiss happens here; non-modal overlays fall
                // through so v1 clicks keep working.
                {
                    ui::fw2::MouseEvent me{};
                    me.x = mx; me.y = my;
                    me.button = sdlBtnToFw2(btn);
                    me.modifiers = sdlModsToFw2(SDL_GetModState());
                    me.timestampMs = SDL_GetTicks();
                    if (m_fw2LayerStack.dispatchMouseDown(me)) break;
                }

                // v1 confirm dialog retired — fw2::Dialog on the
                // Modal layer handles mouse dispatch through
                // LayerStack.

                // v1 TextInput mouse dispatch retired — FwTextInputDialog
                // receives clicks through LayerStack::dispatchMouseDown
                // above.

                // v1 About dialog retired — fw2::Dialog dispatches
                // mouse events through LayerStack.

                // v1 Preferences mouse dispatch retired — FwPreferencesDialog
                // receives clicks through LayerStack::dispatchMouseDown
                // above.

                // v1 Export mouse dispatch retired — FwExportDialog
                // receives clicks through LayerStack::dispatchMouseDown
                // above.

                // v1 context menu: retired — fw2::ContextMenu (handled
                // by the LayerStack dispatch above) owns this slot now.

                // InputState widgets
                if (m_inputState.onMouseDown(mx, my, btn))
                    break;

                bool rightClick = (btn == SDL_BUTTON_RIGHT);

                // DetailPanel right-click — special: pops a knob-aware
                // MIDI-Learn / reset menu, separate from the normal
                // mouseDown path. Take this BEFORE the rootLayout
                // dispatch so the panel's own onMouseDown doesn't
                // intercept.
                if (rightClick && m_showDetailPanel && m_detailPanel->isVisible()) {
                    if (m_detailPanel->handleRightClick(mx, my)) {
                        m_detailPanel->setFocused(true);
                        break;
                    }
                }

                // PianoRoll right-click — its own handler builds a
                // ruler/note context menu inline.
                if (rightClick && m_pianoRoll->isOpen()) {
                    if (m_pianoRoll->handleRightClick(mx, my)) break;
                }

                // Snapshot per-panel editing state so we can toggle
                // SDL text input around the dispatch (needed because
                // FwKnob's beginEdit fires from gesture onDoubleClick
                // which lives inside the panel's dispatch).
                const bool hadDetailKnob   = m_showDetailPanel && m_detailPanel->hasEditingKnob();
                const bool hadVisualKnob   = m_visualParamsPanel->isVisible() &&
                                              m_visualParamsPanel->hasEditingKnob();
                const bool hadBrowserKnob  = m_browserPanel->hasEditingKnob();
                const bool wasTransportEd  = m_transportPanel->isEditing();

                // Double-click on a MIDI clip slot → open piano roll
                // (this lives outside the SessionPanel because it
                // crosses panel boundaries). Transport panel handles
                // its own BPM/timesig double-click via the rootLayout
                // dispatch below.
                if (event.button.clicks >= 2 && btn == SDL_BUTTON_LEFT) {
                    int dblTrack = -1, dblScene = -1;
                    if (m_sessionPanel->getSlotAt(mx, my, dblTrack, dblScene)) {
                        auto* slot = m_project.getSlot(dblTrack, dblScene);
                        if (slot && slot->midiClip) {
                            m_pianoRoll->setClip(slot->midiClip.get(), dblTrack);
                            m_pianoRoll->setOnLengthChanged(nullptr);
                            {
                                auto* di = m_audioEngine.instrument(dblTrack);
                                m_pianoRoll->setDrumMode(
                                    di && std::string(di->id()) == "drumsynth");
                            }
                            m_pianoRoll->setOpen(true);
                            m_selectedTrack = dblTrack;
                            break;
                        }
                        if (m_project.track(dblTrack).type == Track::Type::Midi &&
                            (!slot || slot->empty())) {
                            auto newClip = std::make_unique<midi::MidiClip>(
                                m_audioEngine.transport().numerator() * 4.0);
                            newClip->setName("MIDI Clip");
                            newClip->setLoop(true);
                            auto* clipPtr = newClip.get();
                            int ct = dblTrack, cs = dblScene;
                            m_project.setMidiClip(ct, cs, std::move(newClip));
                            m_pianoRoll->setClip(clipPtr, ct);
                            m_pianoRoll->setOnLengthChanged(nullptr);
                            {
                                auto* di = m_audioEngine.instrument(ct);
                                m_pianoRoll->setDrumMode(
                                    di && std::string(di->id()) == "drumsynth");
                            }
                            m_pianoRoll->setOpen(true);
                            m_selectedTrack = ct;
                            markDirty();
                            m_undoManager.push({"Create MIDI Clip",
                                [this, ct, cs]{
                                    auto* s = m_project.getSlot(ct, cs);
                                    if (s) { m_project.graveyardSlotClips(*s); markDirty(); }
                                },
                                [this, ct, cs]{
                                    auto nc = std::make_unique<midi::MidiClip>(
                                        m_audioEngine.transport().numerator() * 4.0);
                                    nc->setName("MIDI Clip");
                                    nc->setLoop(true);
                                    m_project.setMidiClip(ct, cs, std::move(nc));
                                    markDirty();
                                }, ""});
                            break;
                        }
                    }
                }

                // Right-click on session-view track headers — opens
                // context menu (session panel doesn't surface track-
                // header right-clicks itself).
                if (rightClick && m_project.viewMode() == ViewMode::Session) {
                    auto sb = m_sessionPanel->bounds();
                    float headerY = sb.y;
                    float headerEnd = headerY + ui::Theme::kTrackHeaderHeight;
                    float gridX = ui::Theme::kSceneLabelWidth;
                    if (my >= headerY && my < headerEnd && mx >= gridX) {
                        float contentMX = mx + m_sessionPanel->scrollX();
                        int trackIdx = static_cast<int>((contentMX - gridX) / ui::Theme::kTrackWidth);
                        if (trackIdx >= 0 && trackIdx < m_project.numTracks()) {
                            m_selectedTrack = trackIdx;
                            m_virtualKeyboard.setTargetTrack(trackIdx);
                            m_sessionPanel->setSelectedTrack(trackIdx);
                            m_mixerPanel->setSelectedTrack(trackIdx);
                            showTrackContextMenu(trackIdx, mx, my);
                            break;
                        }
                    }
                }

                // Main dispatch: rootLayout walks its fw2 children
                // (menu bar, transport, content grid, detail/visual,
                // piano roll), routing the click to whichever child
                // contains the point. Each panel handles its own
                // gesture state machine + capture.
                m_sessionPanel->clearLastClickTrack();
                m_sessionPanel->clearRightClick();
                {
                    ui::fw2::MouseEvent me{};
                    me.x = mx; me.y = my;
                    me.lx = mx; me.ly = my;
                    me.button = sdlBtnToFw2(btn);
                    me.modifiers = sdlModsToFw2(SDL_GetModState());
                    me.clickCount = event.button.clicks;
                    me.timestampMs = SDL_GetTicks();
                    m_rootLayout->dispatchMouseDown(me);
                }

                // Post-dispatch: react to state changes the panels
                // signalled (rename gestures, click selections, knob
                // edit transitions, etc.).
                if (m_sessionPanel->isRenamingTrack() ||
                    m_arrangementPanel->isRenamingTrack() ||
                    m_transportPanel->isEditing())
                {
                    SDL_StartTextInput(m_mainWindow.getHandle());
                }
                int selTrack = m_sessionPanel->lastClickTrack();
                if (selTrack >= 0) {
                    m_selectedTrack = selTrack;
                    m_detailTarget = DetailTarget::Track;
                    m_virtualKeyboard.setTargetTrack(selTrack);
                    m_mixerPanel->setSelectedTrack(selTrack);
                }
                int selScene = m_sessionPanel->lastClickScene();
                if (selScene >= 0) m_selectedScene = selScene;
                if (selTrack >= 0 || selScene >= 0) {
                    updateDetailForSelectedTrack();
                }
                int rcTrack = m_sessionPanel->lastRightClickTrack();
                int rcScene = m_sessionPanel->lastRightClickScene();
                if (rcTrack >= 0 && rcScene >= 0) {
                    m_selectedTrack = rcTrack;
                    m_selectedScene = rcScene;
                    showClipContextMenu(rcTrack, rcScene, mx, my);
                }
                int rcSceneLabel = m_sessionPanel->rightClickSceneLabel();
                if (rcSceneLabel >= 0) {
                    m_selectedScene = rcSceneLabel;
                    showSceneContextMenu(rcSceneLabel, mx, my);
                }

                // Knob edit-mode SDL text-input toggling. Each panel
                // hosts knobs that open inline edit on double-click;
                // we mirror that into SDL_StartTextInput so digit
                // keys reach the knob's takeTextInput.
                auto syncTextInput = [&](bool was, bool now) {
                    if (!was && now) SDL_StartTextInput(m_mainWindow.getHandle());
                    else if (was && !now) SDL_StopTextInput(m_mainWindow.getHandle());
                };
                syncTextInput(hadDetailKnob,  m_showDetailPanel && m_detailPanel->hasEditingKnob());
                syncTextInput(hadVisualKnob,  m_visualParamsPanel->isVisible() && m_visualParamsPanel->hasEditingKnob());
                syncTextInput(hadBrowserKnob, m_browserPanel->hasEditingKnob());

                // Detail-panel focus rules: clicking outside the panel
                // cancels its editing knob and clears focus.
                if (hadDetailKnob && !m_detailPanel->hasEditingKnob()) {
                    m_detailPanel->cancelEditingKnobs();
                }
                if (hadBrowserKnob && !m_browserPanel->hasEditingKnob()) {
                    m_browserPanel->cancelEditingKnobs();
                }
                // Set / clear detail-panel focus ring based on whether
                // the click landed inside the panel.
                if (m_detailPanel->isVisible()) {
                    auto& db = m_detailPanel->bounds();
                    bool inDetail = mx >= db.x && mx < db.x + db.w &&
                                    my >= db.y && my < db.y + db.h;
                    m_detailPanel->setFocused(inDetail);
                }
                if (wasTransportEd && !m_transportPanel->isEditing()) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                }
                if (m_pianoRoll->isOpen() && !rightClick) markDirty();
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                float mx = event.button.x;
                float my = event.button.y;
                int btn = event.button.button;

                // MIDI-loop drag-to-slot: resolve before anything else
                // consumes the release. Drop onto the session cell under
                // the cursor; otherwise the drag just tears down. The
                // arm flag is always cleared (covers a plain click that
                // armed but never crossed the drag threshold).
                {
                    auto& dm = ui::fw2::DragManager::instance();
                    if (dm.isDraggingMidiLoop()) {
                        int dt = -1, ds = -1;
                        if (m_sessionPanel->cellAtScreen(mx, my, dt, ds))
                            loadMidiLoopIntoSlot(dm.payload().midiLoopPath, dt, ds);
                        dm.finish();
                    }
                    m_loopDragArmed = false;
                    m_loopDragArmedPath.clear();
                }

                // fw2 LayerStack — mirror mouse-down routing.
                {
                    ui::fw2::MouseEvent me{};
                    me.x = mx; me.y = my;
                    me.button = sdlBtnToFw2(btn);
                    me.modifiers = sdlModsToFw2(SDL_GetModState());
                    me.timestampMs = SDL_GetTicks();
                    if (m_fw2LayerStack.dispatchMouseUp(me)) break;
                }

                m_inputState.onMouseUp(mx, my, btn);
                // Always dispatch mouseUp through the widget tree.
                //
                // Two flows need it:
                //   1. A descendant captured on mouseDown (knob drag,
                //      fader drag, etc.) — fw2's gesture SM ends the
                //      drag inside dispatchMouseUp + releases capture.
                //   2. A widget that runs its OWN drag state machine
                //      WITHOUT taking fw2 capture — e.g. ContentGrid's
                //      divider drag, which sets m_dragH on mouseDown
                //      and clears it on mouseUp. Without an unconditional
                //      dispatch, the divider keeps tracking the cursor
                //      forever after the user releases.
                {
                    // FwKnob's beginEdit fires from gesture onDoubleClick
                    // which lives inside dispatchMouseUp — snapshot the
                    // before/after state so we can mirror it into SDL
                    // text input (digits arriving from the keyboard
                    // need to reach the knob's takeTextInput).
                    const bool browserEditingBefore =
                        m_browserPanel->hasEditingKnob();
                    const bool detailEditingBefore =
                        m_showDetailPanel && m_detailPanel->hasEditingKnob();
                    const bool visualEditingBefore =
                        m_visualParamsPanel->isVisible() &&
                        m_visualParamsPanel->hasEditingKnob();

                    ui::fw2::MouseEvent me{};
                    me.x = mx; me.y = my;
                    me.lx = mx; me.ly = my;
                    me.button = sdlBtnToFw2(btn);
                    me.modifiers = sdlModsToFw2(SDL_GetModState());
                    me.timestampMs = SDL_GetTicks();
                    m_rootLayout->dispatchMouseUp(me);

                    auto sync = [&](bool before, bool after) {
                        if (!before && after) SDL_StartTextInput(m_mainWindow.getHandle());
                        else if (before && !after) SDL_StopTextInput(m_mainWindow.getHandle());
                    };
                    sync(browserEditingBefore, m_browserPanel->hasEditingKnob());
                    sync(detailEditingBefore,  m_showDetailPanel && m_detailPanel->hasEditingKnob());
                    sync(visualEditingBefore,  m_visualParamsPanel->isVisible() && m_visualParamsPanel->hasEditingKnob());
                }
                // Mirror the mouse-DOWN selection-sync block here.
                // SessionPanel now defers clip-cell selection to
                // mouse-up (so a click that turns into a drag
                // doesn't yank the detail panel off its target);
                // App still has to apply that deferred state, and
                // the mouse-down read at the top is too early. Read
                // again here so Delete / clip context reflect the
                // freshly-selected cell.
                {
                    int selTrackUp = m_sessionPanel->lastClickTrack();
                    if (selTrackUp >= 0) {
                        m_selectedTrack = selTrackUp;
                        m_detailTarget = DetailTarget::Track;
                        m_virtualKeyboard.setTargetTrack(selTrackUp);
                        m_mixerPanel->setSelectedTrack(selTrackUp);
                    }
                    int selSceneUp = m_sessionPanel->lastClickScene();
                    if (selSceneUp >= 0) m_selectedScene = selSceneUp;
                    if (selTrackUp >= 0 || selSceneUp >= 0) {
                        updateDetailForSelectedTrack();
                    }
                }

                // Handle completed clip drag-and-drop
                if (m_sessionPanel->clipDragCompleted()) {
                    int srcT = m_sessionPanel->dragSourceTrack();
                    int srcS = m_sessionPanel->dragSourceScene();
                    int dstT = m_sessionPanel->dragTargetTrack();
                    int dstS = m_sessionPanel->dragTargetScene();
                    bool isCopy = m_sessionPanel->dragIsCopy();
                    m_sessionPanel->clearDragResult();
                    performClipDragDrop(srcT, srcS, dstT, dstS, isCopy);
                    m_selectedTrack = dstT;
                    m_selectedScene = dstS;
                    m_detailTarget = DetailTarget::Track;
                    updateDetailForSelectedTrack();
                }
                // Cross-panel "global" drag drop dispatch + fallback.
                // If an audio-clip drag is active and the cursor
                // released inside the detail panel, first give the
                // panel a chance to route to a precise sub-target
                // (DrumRack pad under cursor, etc.). If the panel
                // doesn't consume, fall through to the bulk
                // sample-receiver loaders for the selected track.
                {
                    auto& dm = ui::fw2::DragManager::instance();
                    if (dm.isDraggingAudioClip() && m_showDetailPanel) {
                        const auto db = m_detailPanel->bounds();
                        const bool inside =
                            (mx >= db.x && mx < db.x + db.w &&
                             my >= db.y && my < db.y + db.h);
                        if (inside) {
                            const auto& pl = dm.payload();
                            // 1) Sub-target dispatch (per-pad / per-zone).
                            bool consumed = false;
                            if (pl.audioBuffer) {
                                consumed = m_detailPanel->tryConsumeAudioDropAt(
                                    mx, my, *pl.audioBuffer, pl.label);
                            }
                            // 2) Bulk loaders for the selected
                            // track's instrument (Sampler / Granular
                            // / DrumSlop / Vocoder). First one that
                            // matches wins.
                            if (!consumed) {
                                const int t = m_selectedTrack;
                                consumed =
                                    loadBufferToSampler(pl.audioBuffer, pl.label, t)
                                    || loadBufferToDrumSlop(pl.audioBuffer, pl.label, t)
                                    || loadBufferToGranular(pl.audioBuffer, pl.label, t)
                                    || loadBufferToVocoder (pl.audioBuffer, pl.label, t);
                            }
                            if (consumed) dm.finish();
                        }
                    }
                    // Tear down any unconsumed drag — covers releases
                    // over empty space, over a non-receiving panel, or
                    // over a receiving panel where every load* returned
                    // false (e.g. wrong instrument on the selected
                    // track).
                    if (dm.active()) dm.cancel();
                }
                break;
            }

            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                if (event.window.windowID == SDL_GetWindowID(m_mainWindow.getHandle())) {
                    float s = SDL_GetWindowDisplayScale(m_mainWindow.getHandle());
                    ui::Theme::scaleFactor = s;
                    m_fw2Context.setDpiScale(s);
                }
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(m_mainWindow.getHandle())) {
                    m_running = false;
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL: {
                float dx = event.wheel.x;
                float dy = event.wheel.y;
                // SDL3's wheel event carries the mouse position at the
                // moment of the scroll — use that directly so overlays
                // dispatch against the live cursor location, not the
                // last MOUSE_MOTION-tracked value (which can be stale
                // when the pointer has moved between motion events).
                float wheelX = event.wheel.mouse_x;
                float wheelY = event.wheel.mouse_y;

                // fw2 LayerStack — overlay lists consume wheel first.
                {
                    ui::fw2::ScrollEvent se{};
                    se.x = wheelX;
                    se.y = wheelY;
                    se.dx = dx; se.dy = dy;
                    se.modifiers = sdlModsToFw2(SDL_GetModState());
                    if (m_fw2LayerStack.dispatchScroll(se)) break;
                }

                auto sb = m_sessionPanel->bounds();
                // ContentGrid is fw2; all child panel bounds() return fw2::Rect
                // (same x/y/w/h fields as v1 — comparison math is unchanged).
                auto mb = m_mixerPanel->bounds();
                auto db = m_detailPanel->bounds();
                auto pb = m_pianoRoll->bounds();
                auto bb = m_browserPanel->bounds();

                // Browser panel first — its dropdown popups live
                // inside this region and need wheel events to scroll
                // through lists that overflow the 8-item cap. Dispatched
                // directly to the fw2::BrowserPanel.
                if (m_lastMouseX >= bb.x && m_lastMouseX < bb.x + bb.w &&
                    m_lastMouseY >= bb.y && m_lastMouseY < bb.y + bb.h) {
                    ui::fw2::ScrollEvent se{};
                    se.x = m_lastMouseX; se.y = m_lastMouseY;
                    se.dx = dx; se.dy = dy;
                    if (m_browserPanel->dispatchScroll(se)) break;
                }

                if (m_pianoRoll->isOpen() && m_lastMouseY >= pb.y) {
                    auto mod = SDL_GetModState();
                    bool ctrl  = (mod & SDL_KMOD_CTRL) != 0;
                    bool shift = (mod & SDL_KMOD_SHIFT) != 0;
                    m_pianoRoll->handleScroll(dx, dy, ctrl, shift, m_lastMouseX, m_lastMouseY);
                } else if (m_showDetailPanel && m_lastMouseY >= db.y) {
                    auto mod = SDL_GetModState();
                    bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
                    m_detailPanel->setLastMousePos(m_lastMouseX, m_lastMouseY);
                    m_detailPanel->handleScroll(dx, dy, ctrl);
                } else if (m_showMixer && m_lastMouseY >= mb.y && m_lastMouseY < mb.y + mb.h) {
                    // Dispatch directly to fw2::MixerPanel.
                    ui::fw2::ScrollEvent se{};
                    se.x = m_lastMouseX; se.y = m_lastMouseY;
                    se.dx = dx; se.dy = dy;
                    m_mixerPanel->dispatchScroll(se);
                } else if (m_lastMouseY >= sb.y && m_lastMouseY < sb.y + sb.h) {
                    if (m_project.viewMode() == ViewMode::Arrangement) {
                        auto ab = m_arrangementPanel->bounds();
                        if (m_lastMouseX >= ab.x && m_lastMouseX < ab.x + ab.w) {
                            auto mod = SDL_GetModState();
                            uint16_t mods = 0;
                            if (mod & SDL_KMOD_CTRL)  mods |= ui::fw2::ModifierKey::Ctrl;
                            if (mod & SDL_KMOD_SHIFT) mods |= ui::fw2::ModifierKey::Shift;
                            ui::fw2::ScrollEvent se;
                            se.x = m_lastMouseX; se.y = m_lastMouseY;
                            se.dx = dx; se.dy = dy;
                            se.modifiers = mods;
                            m_arrangementPanel->dispatchScroll(se);
                        }
                    } else {
                        m_sessionPanel->handleScroll(dx, dy);
                    }
                }
                break;
            }

            case SDL_EVENT_DROP_BEGIN:
                // A new drag-drop batch is starting. Clear the multi-file
                // video cascade anchor so the first video in this batch
                // re-anchors on the slot under the cursor, and drop any
                // stale cursor position from a previous drag.
                m_videoDropTrack = -1;
                m_videoDropScene = -1;
                m_haveDropPos    = false;
                break;

            case SDL_EVENT_DROP_POSITION:
                // Live cursor position as the drag moves over the window.
                // This is the reliable source of the drop location — the
                // DROP_FILE event itself reports (0,0) on some platforms.
                if (event.drop.x != 0.0f || event.drop.y != 0.0f) {
                    m_lastDropX   = event.drop.x;
                    m_lastDropY   = event.drop.y;
                    m_haveDropPos = true;
                }
                break;

            case SDL_EVENT_DROP_FILE: {
                const char* file = event.drop.data;
                if (file) {
                    // Resolve the drop position. SDL_EVENT_DROP_FILE reports
                    // (0,0) on some platforms (notably Linux), which would
                    // make the grid hit-test fail and drop the file into the
                    // selected track's default slot instead of under the
                    // cursor. Fall back to the live position tracked from
                    // DROP_POSITION during the drag, then to the OS mouse
                    // state.
                    float dropX = event.drop.x;
                    float dropY = event.drop.y;
                    if (dropX == 0.0f && dropY == 0.0f) {
                        if (m_haveDropPos) {
                            dropX = m_lastDropX;
                            dropY = m_lastDropY;
                        } else {
                            SDL_GetMouseState(&dropX, &dropY);
                        }
                    }
                    LOG_INFO("Drop",
                             "file drop raw=(%.0f,%.0f) eff=(%.0f,%.0f) dropPos=%d",
                             event.drop.x, event.drop.y, dropX, dropY,
                             m_haveDropPos ? 1 : 0);

                    // Check if drop is over the detail panel (for sample-based instruments)
                    auto db = m_detailPanel->bounds();
                    if (m_detailPanel->isOpen() && dropY >= db.y && dropY < db.y + db.h
                        && dropX >= db.x && dropX < db.x + db.w) {
                        if (!loadSampleToSampler(file, m_selectedTrack))
                            if (!loadLoopToDrumSlop(file, m_selectedTrack))
                                if (!loadSampleToDrumRack(file, m_selectedTrack))
                                    if (!loadSampleToGranular(file, m_selectedTrack))
                                        loadModulatorToVocoder(file, m_selectedTrack);
                        break;
                    }

                    // Arrangement view: drop audio file onto timeline
                    if (m_project.viewMode() == ViewMode::Arrangement) {
                        auto ab = m_arrangementPanel->bounds();
                        float arrGridX = ab.x + ui::fw2::ArrangementPanel::kTrackHeaderW;
                        float arrGridY = ab.y + ui::fw2::ArrangementPanel::kRulerH;
                        if (dropX >= arrGridX && dropY >= arrGridY) {
                            float relX = (dropX - arrGridX) + m_arrangementPanel->scrollX();
                            float relY = (dropY - arrGridY) + m_arrangementPanel->scrollY();
                            int trackIdx = m_arrangementPanel->trackAtY(relY);
                            double beatPos = static_cast<double>(relX) / m_arrangementPanel->zoom();
                            beatPos = m_arrangementPanel->snapBeat(beatPos);
                            if (trackIdx >= 0 && trackIdx < m_project.numTracks()) {
                                auto trackType = m_project.track(trackIdx).type;
                                if (trackType == Track::Type::Audio) {
                                    loadClipToArrangement(file, trackIdx, beatPos);
                                } else if (trackType == Track::Type::Midi) {
                                    // For MIDI tracks, try loading as sample to instrument
                                    if (!loadSampleToSampler(file, trackIdx))
                                        if (!loadSampleToDrumRack(file, trackIdx))
                                            if (!loadSampleToGranular(file, trackIdx))
                                                LOG_WARN("Drop", "Cannot drop audio on MIDI arrangement track");
                                } else {
                                    // Visual or other: ignore audio drop.
                                    LOG_WARN("Drop", "Cannot drop audio file on visual track");
                                }
                            }
                        }
                        break;
                    }

                    // Session view: determine target track/scene from drop position
                    auto sb = m_sessionPanel->bounds();
                    float headerY = sb.y + ui::Theme::kTrackHeaderHeight;
                    float gridX = sb.x + ui::Theme::kSceneLabelWidth;
                    int targetTrack = m_selectedTrack;
                    int targetScene = m_nextDropScene;

                    if (dropY >= headerY && dropX >= gridX) {
                        float contentMX = (dropX - gridX) + m_sessionPanel->scrollX();
                        float contentMY = (dropY - headerY) + m_sessionPanel->scrollY();
                        int t = static_cast<int>(contentMX / ui::Theme::kTrackWidth);
                        int s = static_cast<int>(contentMY / ui::Theme::kClipSlotHeight);
                        if (t >= 0 && t < m_project.numTracks()) targetTrack = t;
                        if (s >= 0 && s < m_project.numScenes()) targetScene = s;
                    }

                    // Video drop onto a Visual track → kick off an import.
                    // Detection is purely extension-based; ffmpeg handles
                    // whatever the actual format turns out to be.
                    auto isVideoExt = [](const std::string& path) {
                        static const char* exts[] = {
                            ".mp4",".mov",".mkv",".webm",".avi",".m4v"};
                        std::string p = path;
                        for (auto& c : p) c = static_cast<char>(std::tolower(c));
                        for (const char* e : exts) {
                            size_t n = std::strlen(e);
                            if (p.size() >= n && p.compare(p.size() - n, n, e) == 0)
                                return true;
                        }
                        return false;
                    };
                    if (targetTrack >= 0 && targetTrack < m_project.numTracks() &&
                        m_project.track(targetTrack).type == Track::Type::Visual &&
                        isVideoExt(file)) {
                        if (m_projectPath.empty()) {
                            ui::fw2::ConfirmDialog::prompt(
                                "Videos are imported into <project>/media/.\n"
                                "Save the project first, then drag the video again.",
                                [this]() { saveProjectAs(); });
                        } else {
                            // First video of the batch anchors on the slot
                            // under the cursor; later files cascade down
                            // successive scenes so dropping several videos
                            // fills several slots. Clamp to the last existing
                            // scene rather than growing the grid: addScene()
                            // reallocates the per-track clip-slot vectors,
                            // which would dangle the clip-automation pointers
                            // the audio thread reads every buffer (→ crash).
                            // Extra videos past the last scene stack on the
                            // final slot.
                            if (m_videoDropTrack < 0) {
                                m_videoDropTrack = targetTrack;
                                m_videoDropScene = targetScene;
                            }
                            int vs = std::min(m_videoDropScene,
                                              m_project.numScenes() - 1);
                            startVideoImport(m_videoDropTrack, vs, file);
                            ++m_videoDropScene;
                        }
                        break;
                    }

                    // If the target track has a sample-based instrument, load sample
                    if (loadSampleToSampler(file, targetTrack)) {
                        // Sample loaded into Sampler — done
                    } else if (loadLoopToDrumSlop(file, targetTrack)) {
                        // Loop loaded into DrumSlop — done
                    } else if (loadSampleToGranular(file, targetTrack)) {
                        // Sample loaded into Granular Synth — done
                    } else if (loadModulatorToVocoder(file, targetTrack)) {
                        // Modulator loaded into Vocoder — done
                    } else {
                        loadClipToSlot(file, targetTrack, targetScene);
                        m_selectedTrack = targetTrack;
                        m_selectedScene = targetScene;
                        m_sessionPanel->setSelectedTrack(m_selectedTrack);
                        m_sessionPanel->setSelectedScene(m_selectedScene);
                        updateDetailForSelectedTrack();
                        // Advance scene for next drop on same track
                        m_nextDropScene = targetScene + 1;
                        if (m_nextDropScene >= m_project.numScenes()) m_nextDropScene = 0;
                    }
                }
                break;
            }

            default:
                break;
        }
    }
}


} // namespace yawn
