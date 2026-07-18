// App_TrackClipMenus.cpp — track-header and clip-slot right-click
// context menus + the numeric-entry prompt helpers they use.
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

void App::showTrackContextMenu(int trackIndex, float mx, float my) {
    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
    auto& track = m_project.track(trackIndex);

    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    // item() defaults to enabled=true; several entries here grey-out the
    // active/unavailable choice, so wrap with an explicit enabled flag.
    auto itemEn = [](std::string label, std::function<void()> action, bool enabled) {
        MenuEntry e = item(std::move(label), std::move(action));
        e.enabled = enabled;
        return e;
    };
    std::vector<MenuEntry> items;

    // Track type selection (with confirmation dialog)
    items.push_back(itemEn("Set as Audio Track", [this, trackIndex]() {
        auto& trk = m_project.track(trackIndex);
        if (trk.type == Track::Type::Audio) return;
        ui::fw2::ConfirmDialog::prompt(
            "Change track type? All devices will be removed.",
            [this, trackIndex]() {
                m_audioEngine.midiEffectChain(trackIndex).clear();
                m_audioEngine.mixer().trackEffects(trackIndex).clear();
                m_audioEngine.setInstrument(trackIndex, nullptr);
                m_project.track(trackIndex).type = Track::Type::Audio;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 0});
                m_detailPanel->clear();
                markDirty();
            });
    }, track.type != Track::Type::Audio));

    items.push_back(itemEn("Set as MIDI Track", [this, trackIndex]() {
        auto& trk = m_project.track(trackIndex);
        if (trk.type == Track::Type::Midi) return;
        ui::fw2::ConfirmDialog::prompt(
            "Change track type? All devices will be removed.",
            [this, trackIndex]() {
                m_audioEngine.midiEffectChain(trackIndex).clear();
                m_audioEngine.mixer().trackEffects(trackIndex).clear();
                m_project.track(trackIndex).type = Track::Type::Midi;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
                m_audioEngine.setInstrument(trackIndex,
                    std::make_unique<instruments::SubtractiveSynth>());
                m_detailPanel->clear();
                markDirty();
            });
    }, track.type != Track::Type::Midi));

    items.push_back(itemEn("Set as Visual Track", [this, trackIndex]() {
        auto& trk = m_project.track(trackIndex);
        if (trk.type == Track::Type::Visual) return;
        ui::fw2::ConfirmDialog::prompt(
            "Change track type to Visual? All devices will be removed.",
            [this, trackIndex]() {
                m_audioEngine.midiEffectChain(trackIndex).clear();
                m_audioEngine.mixer().trackEffects(trackIndex).clear();
                m_audioEngine.setInstrument(trackIndex, nullptr);
                m_project.track(trackIndex).type = Track::Type::Visual;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 2});
                m_detailPanel->clear();
                markDirty();
            });
    }, track.type != Track::Type::Visual));

    // Blend Mode submenu — visible on Visual tracks only.
    if (track.type == Track::Type::Visual) {
        std::vector<MenuEntry> bmItems;
        const auto cur = track.visualBlendMode;
        auto addMode = [&](const char* label, Track::VisualBlendMode m,
                            visual::VisualEngine::BlendMode vm) {
            bmItems.push_back(itemEn(label, [this, trackIndex, m, vm]{
                m_project.track(trackIndex).visualBlendMode = m;
                m_visualEngine.setLayerBlendMode(trackIndex, vm);
                markDirty();
            }, cur != m));
        };
        addMode("Normal",   Track::VisualBlendMode::Normal,
                visual::VisualEngine::BlendMode::Normal);
        addMode("Add",      Track::VisualBlendMode::Add,
                visual::VisualEngine::BlendMode::Add);
        addMode("Multiply", Track::VisualBlendMode::Multiply,
                visual::VisualEngine::BlendMode::Multiply);
        addMode("Screen",   Track::VisualBlendMode::Screen,
                visual::VisualEngine::BlendMode::Screen);
        items.push_back(submenu("Blend Mode", std::move(bmItems)));

        // "Add Knob Lane" submenu — creates a track-level automation
        // lane targeting A..H. Lanes show up in the expanded track
        // row and can be edited in the arrangement view.
        std::vector<MenuEntry> laneItems;
        auto& trackLanes = m_project.track(trackIndex).automationLanes;
        for (int k = 0; k < 8; ++k) {
            char label[16];
            std::snprintf(label, sizeof(label), "Knob %c",
                           static_cast<char>('A' + k));
            // Grey out if a lane already exists for this knob.
            bool exists = false;
            for (const auto& lane : trackLanes) {
                if (lane.target.type == automation::TargetType::VisualKnob &&
                    lane.target.trackIndex == trackIndex &&
                    lane.target.paramIndex == k) {
                    exists = true; break;
                }
            }
            laneItems.push_back(itemEn(label,
                [this, trackIndex, k]{
                    auto& tr = m_project.track(trackIndex);
                    automation::AutomationLane lane;
                    lane.target = automation::AutomationTarget::visualKnob(trackIndex, k);
                    tr.automationLanes.push_back(std::move(lane));
                    // Playback needs the track in Read mode — Off
                    // would silently suppress the new lane.
                    if (tr.autoMode == automation::AutoMode::Off)
                        tr.autoMode = automation::AutoMode::Read;
                    markDirty();
                },
                !exists));
        }
        items.push_back(submenu("Add Knob Lane", std::move(laneItems)));
    }

    // Separator + Instruments submenu
    std::vector<MenuEntry> instrItems;
    auto addInstrItem = [&](const char* label, auto factory) {
        instrItems.push_back(item(label, [this, trackIndex, label, factory]() {
            auto oldType = m_project.track(trackIndex).type;
            std::string oldInstr;
            auto* inst = m_audioEngine.instrument(trackIndex);
            if (inst) oldInstr = inst->name();
            uint8_t oldTypeVal = (oldType == Track::Type::Audio)  ? 0
                                : (oldType == Track::Type::Midi)   ? 1
                                                                    : 2;
            m_project.track(trackIndex).type = Track::Type::Midi;
            m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
            m_audioEngine.setInstrument(trackIndex, factory());
            markDirty();
            std::string newInstr = label;
            m_undoManager.push({"Set Instrument: " + newInstr,
                [this, trackIndex, oldType, oldTypeVal, oldInstr]{
                    m_project.track(trackIndex).type = oldType;
                    m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, oldTypeVal});
                    m_audioEngine.setInstrument(trackIndex, createInstrumentByName(oldInstr));
                    markDirty();
                },
                [this, trackIndex, factory]{
                    m_project.track(trackIndex).type = Track::Type::Midi;
                    m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
                    m_audioEngine.setInstrument(trackIndex, factory());
                    markDirty();
                }, ""});
        }));
    };
    // One descriptor table drives all device menus (util/Factory.h);
    // Drawbar Organ and Electric Piano get custom entries below
    // (auto-insert Rotary / Phaser respectively).
    for (const auto& d : instrumentDescriptors()) {
        if (d.id == std::string("drawbarorgan") || d.id == std::string("electricpiano"))
            continue;
        addInstrItem(d.displayName, d.make);
    }

    // Drawbar Organ: same as addInstrItem, but also auto-inserts a
    // Rotary effect into the track's chain (unless one is already
    // present — re-picking the organ shouldn't stack Rotaries). The
    // rotary is the de-facto Hammond pairing, but kept as a
    // separate effect so it can be applied to anything else too.
    instrItems.push_back(item("Drawbar Organ", [this, trackIndex]() {
        auto oldType = m_project.track(trackIndex).type;
        std::string oldInstr;
        if (auto* inst = m_audioEngine.instrument(trackIndex))
            oldInstr = inst->name();
        const uint8_t oldTypeVal = (oldType == Track::Type::Audio) ? 0
                                  : (oldType == Track::Type::Midi)  ? 1 : 2;
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex,
            std::make_unique<instruments::DrawbarOrgan>());

        // Auto-insert Rotary if no rotary is already in the chain.
        auto& chain = m_audioEngine.mixer().trackEffects(trackIndex);
        bool hasRotary = false;
        for (int i = 0; i < chain.count(); ++i) {
            auto* e = chain.effectAt(i);
            if (e && std::string(e->id()) == "rotary") { hasRotary = true; break; }
        }
        if (!hasRotary) chain.append(std::make_unique<effects::Rotary>());

        markDirty();
        m_undoManager.push({"Set Instrument: Drawbar Organ",
            [this, trackIndex, oldType, oldTypeVal, oldInstr]{
                m_project.track(trackIndex).type = oldType;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, oldTypeVal});
                m_audioEngine.setInstrument(trackIndex, createInstrumentByName(oldInstr));
                markDirty();
            },
            [this, trackIndex]{
                m_project.track(trackIndex).type = Track::Type::Midi;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
                m_audioEngine.setInstrument(trackIndex,
                    std::make_unique<instruments::DrawbarOrgan>());
                markDirty();
            }, ""});
    }));

    // Electric Piano — same auto-add-FX pattern as Drawbar Organ. Pairs
    // a Suitcase-default EP with a Phaser, the iconic Mark V / Steely
    // Dan rig setup. Phaser is added once; re-picking the EP later
    // doesn't stack a second one. The user can remove or replace the
    // phaser freely — it's just a sensible default starting point.
    instrItems.push_back(item("Electric Piano", [this, trackIndex]() {
        auto oldType = m_project.track(trackIndex).type;
        std::string oldInstr;
        if (auto* inst = m_audioEngine.instrument(trackIndex))
            oldInstr = inst->name();
        const uint8_t oldTypeVal = (oldType == Track::Type::Audio) ? 0
                                  : (oldType == Track::Type::Midi)  ? 1 : 2;
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex,
            std::make_unique<instruments::ElectricPiano>());

        auto& chain = m_audioEngine.mixer().trackEffects(trackIndex);
        bool hasPhaser = false;
        for (int i = 0; i < chain.count(); ++i) {
            auto* e = chain.effectAt(i);
            if (e && std::string(e->id()) == "phaser") { hasPhaser = true; break; }
        }
        if (!hasPhaser) chain.append(std::make_unique<effects::Phaser>());

        markDirty();
        m_undoManager.push({"Set Instrument: Electric Piano",
            [this, trackIndex, oldType, oldTypeVal, oldInstr]{
                m_project.track(trackIndex).type = oldType;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, oldTypeVal});
                m_audioEngine.setInstrument(trackIndex, createInstrumentByName(oldInstr));
                markDirty();
            },
            [this, trackIndex]{
                m_project.track(trackIndex).type = Track::Type::Midi;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
                m_audioEngine.setInstrument(trackIndex,
                    std::make_unique<instruments::ElectricPiano>());
                markDirty();
            }, ""});
    }));

#ifdef YAWN_HAS_VST3
    // VST3 instruments — flat list with separator
    if (m_vst3Scanner && !m_vst3Scanner->instruments().empty()) {
        instrItems.push_back(separator());
        instrItems.push_back(header("── VST3 ──"));
        for (auto& info : m_vst3Scanner->instruments()) {
            std::string label = info.name;
            if (!info.vendor.empty()) label += " (" + info.vendor + ")";
            std::string modulePath = info.modulePath;
            std::string classID = info.classIDString;
            instrItems.push_back(item(label, [this, trackIndex, modulePath, classID]() {
                m_project.track(trackIndex).type = Track::Type::Midi;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
                m_audioEngine.setInstrument(trackIndex,
                    std::make_unique<vst3::VST3Instrument>(modulePath, classID));
                markDirty();
            }));
        }
    }
#endif

    items.push_back(separator());
    items.push_back(submenu("Add Instrument", std::move(instrItems)));

    // Audio effects submenu
    std::vector<MenuEntry> fxItems;
    auto addFxItem = [&](const char* label, auto factory) {
        fxItems.push_back(item(label, [this, trackIndex, label, factory]() {
            LOG_INFO("User", "addFx '%s' to track %d", label, trackIndex);
            auto& chain = m_audioEngine.mixer().trackEffects(trackIndex);
            chain.append(factory());
            int slot = chain.count() - 1;
            markDirty();
            std::string fxName = label;
            m_undoManager.push({"Add Effect: " + fxName,
                [this, trackIndex, slot]{
                    m_audioEngine.mixer().trackEffects(trackIndex).removeRetired(slot);
                    markDirty();
                },
                [this, trackIndex, factory]{
                    m_audioEngine.mixer().trackEffects(trackIndex).append(factory());
                    markDirty();
                }, ""});
        }));
    };
    // Built-in audio effects — one descriptor table (util/Factory.h)
    // drives this and every other Add-Effect menu.
    for (const auto& d : audioEffectDescriptors())
        addFxItem(d.displayName, d.make);

#ifdef YAWN_HAS_VST3
    // VST3 effects — flat list with separator
    if (m_vst3Scanner && !m_vst3Scanner->effects().empty()) {
        fxItems.push_back(separator());
        fxItems.push_back(header("── VST3 ──"));
        for (auto& info : m_vst3Scanner->effects()) {
            std::string label = info.name;
            if (!info.vendor.empty()) label += " (" + info.vendor + ")";
            std::string modulePath = info.modulePath;
            std::string classID = info.classIDString;
            fxItems.push_back(item(label, [this, trackIndex, modulePath, classID, label]() {
                LOG_INFO("User", "addFx VST3 '%s' to track %d (path=%s)",
                         label.c_str(), trackIndex, modulePath.c_str());
                auto& chain = m_audioEngine.mixer().trackEffects(trackIndex);
                chain.append(std::make_unique<vst3::VST3Effect>(modulePath, classID));
                markDirty();
            }));
        }
    }
#endif

    items.push_back(submenu("Add Audio Effect", std::move(fxItems)));

    // MIDI effects submenu
    std::vector<MenuEntry> midiItems;
    auto addMidiItem = [&](const char* label, auto factory) {
        midiItems.push_back(item(label, [this, trackIndex, label, factory]() {
            auto& chain = m_audioEngine.midiEffectChain(trackIndex);
            chain.addEffect(factory());
            int slot = chain.count() - 1;
            markDirty();
            std::string fxName = label;
            m_undoManager.push({"Add MIDI Effect: " + fxName,
                [this, trackIndex, slot]{
                    m_audioEngine.midiEffectChain(trackIndex).removeEffectRetired(slot);
                    markDirty();
                },
                [this, trackIndex, factory]{
                    m_audioEngine.midiEffectChain(trackIndex).addEffect(factory());
                    markDirty();
                }, ""});
        }));
    };
    // Built-in MIDI effects — from the descriptor table (util/Factory.h).
    for (const auto& d : midiEffectDescriptors())
        addMidiItem(d.displayName, d.make);
    items.push_back(submenu("Add MIDI Effect", std::move(midiItems)));

    // Record quantize submenu
    auto curRQ = track.recordQuantize;
    std::vector<MenuEntry> rqItems;
    rqItems.push_back(itemEn("None", [this, trackIndex, curRQ]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::None;
        m_undoManager.push({"Change Record Quantize",
            [this, trackIndex, curRQ]{ m_project.track(trackIndex).recordQuantize = curRQ; },
            [this, trackIndex]{ m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::None; },
            ""});
        markDirty();
    }, curRQ != audio::QuantizeMode::None));
    rqItems.push_back(itemEn("Beat", [this, trackIndex, curRQ]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBeat;
        m_undoManager.push({"Change Record Quantize",
            [this, trackIndex, curRQ]{ m_project.track(trackIndex).recordQuantize = curRQ; },
            [this, trackIndex]{ m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBeat; },
            ""});
        markDirty();
    }, curRQ != audio::QuantizeMode::NextBeat));
    rqItems.push_back(itemEn("Bar", [this, trackIndex, curRQ]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBar;
        m_undoManager.push({"Change Record Quantize",
            [this, trackIndex, curRQ]{ m_project.track(trackIndex).recordQuantize = curRQ; },
            [this, trackIndex]{ m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBar; },
            ""});
        markDirty();
    }, curRQ != audio::QuantizeMode::NextBar));
    items.push_back(submenu("Record Quantize", std::move(rqItems)));

#ifdef YAWN_HAS_VST3
    // Open VST3 Editor for instrument
    {
        auto* inst = m_audioEngine.instrument(trackIndex);
        auto* vsti = dynamic_cast<vst3::VST3Instrument*>(inst);
        if (vsti && vsti->instance()) {
            std::string edTitle = std::string(vsti->name()) + " - Track " + std::to_string(trackIndex + 1);
            std::string modPath = vsti->modulePath();
            std::string clsID = vsti->classIDString();
            items.push_back(item("Open VST3 Instrument Editor", [this, vsti, edTitle, modPath, clsID]() {
                openVST3Editor(vsti->instance(), modPath, clsID, edTitle);
            }));
        }
    }
    // Open VST3 Editor for effects in chain
    {
        auto& chain = m_audioEngine.mixer().trackEffects(trackIndex);
        for (int i = 0; i < chain.count(); ++i) {
            auto* fx = dynamic_cast<vst3::VST3Effect*>(chain.effectAt(i));
            if (fx && fx->instance()) {
                std::string fxName = fx->name();
                std::string edTitle = fxName + " - Track " + std::to_string(trackIndex + 1);
                std::string modPath = fx->modulePath();
                std::string clsID = fx->classIDString();
                items.push_back(item("Open Editor: " + fxName, [this, fx, edTitle, modPath, clsID]() {
                    openVST3Editor(fx->instance(), modPath, clsID, edTitle);
                }));
            }
        }
    }
#endif

    // Delete track (with confirmation) — migrated to v2 ConfirmDialog:
    // rides OverlayLayer::Modal so the scrim + event blocking are
    // handled uniformly by LayerStack instead of the v1 paintOverlay
    // + hand-rolled escape/click dispatch.
    bool canDelete = m_project.numTracks() > 1;
    items.push_back(separator());
    items.push_back(itemEn("Delete Track", [this, trackIndex]() {
        ui::fw2::ConfirmDialog::promptCustom(
            "Delete Track",
            "This will stop playback and delete the track. Continue?",
            "Delete",
            "Cancel",
            [this, trackIndex]() {
                // Stop transport and all clips (visual layers clear
                // via the stop-counter poll in update()).
                m_audioEngine.sendCommand(audio::TransportStopMsg{});
                for (int t = 0; t < m_project.numTracks(); ++t) {
                    m_audioEngine.sendCommand(audio::StopClipMsg{t});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{t});
                }

                int numActive = m_project.numTracks();

                // Shift engine arrays
                m_audioEngine.removeTrackSlot(trackIndex, numActive);

                // Remove track from project
                m_project.deleteTrack(trackIndex);

                // Remove MIDI mappings that target this track, shift higher indices
                m_midiLearnManager.removeTrackMappings(trackIndex);

                // Fix selected track
                if (m_selectedTrack >= m_project.numTracks())
                    m_selectedTrack = m_project.numTracks() - 1;
                m_virtualKeyboard.setTargetTrack(m_selectedTrack);
                m_mixerPanel->setSelectedTrack(m_selectedTrack);

                m_detailPanel->clear();
                markDirty();
            });   // promptCustom
    }, canDelete));

    // Clear-automation entry on the track header. Wipes the
    // arrangement-level lanes AND every per-clip envelope on this
    // track (matches the v0.61 design). Conditionally surfaced so
    // tracks with no recorded automation don't get a useless
    // grey-out in the menu.
    {
        const auto& trk = m_project.track(trackIndex);
        bool hasAny = !trk.automationLanes.empty();
        if (!hasAny) {
            for (int s = 0; s < m_project.numScenes(); ++s) {
                const auto* slot = m_project.getSlot(trackIndex, s);
                if (slot && !slot->clipAutomation->lanes.empty()) {
                    hasAny = true;
                    break;
                }
            }
        }
        // MIDI Overdub toggle — only meaningful for MIDI tracks.
        // Replace = recording over a slot that has a MIDI clip wipes
        // the old clip and records a fresh one. Overdub = layers
        // incoming notes/CCs into the existing clip, extending its
        // length if the take runs past the current end. Undoable.
        if (trk.type == Track::Type::Midi) {
            const bool curOverdub = trk.midiOverdub;
            std::string label = "MIDI Overdub: ";
            label += curOverdub ? "On" : "Off";
            items.push_back(item(label,
                [this, trackIndex, curOverdub]() {
                    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
                    m_project.track(trackIndex).midiOverdub = !curOverdub;
                    m_undoManager.push({"Toggle MIDI Overdub",
                        [this, trackIndex, curOverdub]{
                            if (trackIndex >= 0 && trackIndex < m_project.numTracks())
                                m_project.track(trackIndex).midiOverdub = curOverdub;
                            markDirty();
                        },
                        [this, trackIndex, curOverdub]{
                            if (trackIndex >= 0 && trackIndex < m_project.numTracks())
                                m_project.track(trackIndex).midiOverdub = !curOverdub;
                            markDirty();
                        }, ""});
                    markDirty();
                }));
        }

        if (hasAny) {
            items.push_back(item("Clear Track Automation",
                [this, trackIndex]() {
                    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
                    // Snapshot for undo: track lanes + every slot's
                    // clip lanes on this track.
                    auto savedTrack = m_project.track(trackIndex).automationLanes;
                    std::vector<std::vector<automation::AutomationLane>> savedSlots(
                        m_project.numScenes());
                    for (int s = 0; s < m_project.numScenes(); ++s) {
                        auto* slot = m_project.getSlot(trackIndex, s);
                        if (slot) savedSlots[s] = slot->clipAutomation->lanes;
                    }
                    m_project.clearTrackAutomation(trackIndex);
                    for (int s = 0; s < m_project.numScenes(); ++s)
                        publishClipAutomation(trackIndex, s);
                    m_undoManager.push({"Clear Track Automation",
                        [this, trackIndex, savedTrack, savedSlots]{
                            if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
                            m_project.track(trackIndex).automationLanes = savedTrack;
                            for (int s = 0; s < (int)savedSlots.size(); ++s) {
                                auto* slot = m_project.getSlot(trackIndex, s);
                                if (slot) {
                                    m_project.replaceSlotAutomation(*slot, savedSlots[s]);
                                    publishClipAutomation(trackIndex, s);
                                }
                            }
                            markDirty();
                        },
                        [this, trackIndex]{
                            m_project.clearTrackAutomation(trackIndex);
                            for (int s = 0; s < m_project.numScenes(); ++s)
                                publishClipAutomation(trackIndex, s);
                            markDirty();
                        }, ""});
                    markDirty();
                }));
        }
    }

    // ── Clear Arrangement (convenience) ──
    // Empty this track's arrangement lane, or every track's, returning
    // them to session control. The all-tracks variant confirms first.
    {
        items.push_back(separator());
        const bool trackHasArr = !track.arrangementClips.empty() ||
                                  track.arrangementActive;
        items.push_back(itemEn("Clear Arrangement (Track)",
            [this, trackIndex]() { clearTrackArrangement(trackIndex); },
            trackHasArr));
        items.push_back(item("Clear Arrangement (All Tracks)…",
            [this]() {
                ui::fw2::ConfirmDialog::prompt(
                    "Clear the arrangement on ALL tracks?",
                    [this]() { clearAllArrangements(); });
            }));
    }

    ui::fw2::ContextMenu::show(std::move(items), ui::fw2::Point{mx, my});
}


void App::promptNumber(const char* title, const std::string& def,
                       std::function<void(const std::string&)> onText) {
    SDL_StartTextInput(m_mainWindow.getHandle());
    m_textInputDialog.prompt(title, def,
        [this, onText = std::move(onText)](const std::string& text) {
            SDL_StopTextInput(m_mainWindow.getHandle());
            onText(text);
        });
}

void App::promptCustomFloat(const char* title, float current, float lo, float hi,
                            float promptScale, std::function<void(float)> apply) {
    if (promptScale == 0.0f) promptScale = 1.0f;
    char def[32];
    std::snprintf(def, sizeof def, "%g", current / promptScale);
    promptNumber(title, def,
        [lo, hi, promptScale, apply = std::move(apply)](const std::string& text) {
            float v;
            try { v = std::stof(text); } catch (...) { return; }
            v = std::clamp(v * promptScale, lo, hi);
            apply(v);
        });
}

void App::promptCustomInt(const char* title, int current, int lo, int hi,
                          std::function<void(int)> apply) {
    promptNumber(title, std::to_string(current),
        [lo, hi, apply = std::move(apply)](const std::string& text) {
            int v;
            try { v = std::stoi(text); } catch (...) { return; }
            v = std::clamp(v, lo, hi);
            apply(v);
        });
}

void App::showClipContextMenu(int trackIndex, int sceneIndex, float mx, float my) {
    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    auto itemEn = [](std::string label, std::function<void()> action, bool enabled) {
        MenuEntry e = item(std::move(label), std::move(action));
        e.enabled = enabled;
        return e;
    };
    std::vector<MenuEntry> items;
    auto* slot = m_project.getSlot(trackIndex, sceneIndex);
    bool hasClip = slot && !slot->empty();
    bool hasClipboard = m_clipboard.type != ClipboardData::Type::None;
    const bool isVisualTrack =
        m_project.track(trackIndex).type == Track::Type::Visual;

    // Visual track: offer a shader picker at the top. Label flips between
    // "Load Shader…" (empty slot) and "Replace Shader…" (clip present).
    if (isVisualTrack) {
        const bool hasVisualClip = slot && slot->visualClip;
        const char* label = hasVisualClip ? "Replace Shader…" : "Load Shader…";
        items.push_back(item(label, [this, trackIndex, sceneIndex]() {
            m_pendingShaderTrack = trackIndex;
            m_pendingShaderScene = sceneIndex;
            static SDL_DialogFileFilter filter{"Fragment shaders", "frag;glsl;fs"};
            SDL_ShowOpenFileDialog(
                [](void* ud, const char* const* filelist, int) {
                    auto* self = static_cast<App*>(ud);
                    if (!filelist || !filelist[0]) return;
                    int ti = self->m_pendingShaderTrack;
                    int si = self->m_pendingShaderScene;
                    self->m_pendingShaderTrack = -1;
                    self->m_pendingShaderScene = -1;
                    if (ti < 0 || si < 0) return;
                    // Remember the folder so the next shader load starts here.
                    self->m_settings.lastShaderDir =
                        std::filesystem::path(filelist[0]).parent_path().string();
                    self->m_settingsDirty = true;
                    self->m_settingsDirtyAge = 0;
                    auto vc = std::make_unique<visual::VisualClip>();
                    // Copy the source into <project>/shaders/<stem>.frag
                    // and store the project-relative path. Bundled
                    // shaders and pre-save loads pass through untouched.
                    vc->ensurePass0().shaderPath = self->localizeShader(filelist[0]);
                    std::filesystem::path p(filelist[0]);
                    vc->name       = p.stem().string();
                    vc->colorIndex = self->m_project.track(ti).colorIndex;
                    self->m_project.setVisualClip(ti, si, std::move(vc));
                    self->markDirty();
                },
                this, m_mainWindow.getHandle(),
                &filter, 1,
                m_settings.lastShaderDir.empty()
                    ? nullptr : m_settings.lastShaderDir.c_str(),
                /*allow_many*/ false);
        }));

        // "New Shader…" — prompts for a name, writes a minimal
        // Shadertoy-compatible template to <project>/shaders/<name>.frag,
        // and assigns it to this clip. Requires a saved project so the
        // file has somewhere to land.
        items.push_back(item("New Shader…",
            [this, trackIndex, sceneIndex]() {
                if (m_projectPath.empty()) {
                    LOG_WARN("Shader",
                        "Save the project first — shaders live under "
                        "<project>/shaders/.");
                    return;
                }
                SDL_StartTextInput(m_mainWindow.getHandle());
                m_textInputDialog.prompt("New Shader Name", "untitled",
                    [this, trackIndex, sceneIndex](const std::string& raw) {
                        SDL_StopTextInput(m_mainWindow.getHandle());
                        if (raw.empty()) return;
                        namespace fs = std::filesystem;
                        // Sanitize: strip any extension, keep basename only.
                        fs::path p(raw);
                        std::string stem = p.stem().string();
                        if (stem.empty()) stem = raw;
                        fs::path dir = m_projectPath / "shaders";
                        std::error_code ec;
                        fs::create_directories(dir, ec);
                        // Dedup — if <stem>.frag exists, pick <stem>_N.frag.
                        fs::path target = dir / (stem + ".frag");
                        for (int n = 2; fs::exists(target); ++n) {
                            target = dir / (stem + "_" + std::to_string(n) + ".frag");
                        }
                        const char* kTemplate =
                            "// Shadertoy-style fragment shader.\n"
                            "// Available uniforms: iResolution, iTime, iBeat,\n"
                            "// iAudioLevel/Low/Mid/High, iKick, knobA..knobH.\n"
                            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
                            "    vec2 uv = fragCoord / iResolution.xy;\n"
                            "    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0.0, 2.0, 4.0));\n"
                            "    fragColor = vec4(col, 1.0);\n"
                            "}\n";
                        FILE* f = std::fopen(target.string().c_str(), "wb");
                        if (!f) {
                            LOG_ERROR("Shader", "Failed to create %s",
                                      target.string().c_str());
                            return;
                        }
                        std::fwrite(kTemplate, 1, std::strlen(kTemplate), f);
                        std::fclose(f);
                        LOG_INFO("Shader", "Created new shader %s",
                                 target.string().c_str());

                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s) return;
                        if (!s->visualClip)
                            s->visualClip = std::make_unique<visual::VisualClip>();
                        s->visualClip->ensurePass0().shaderPath =
                            "shaders/" + target.filename().string();
                        s->visualClip->name = target.stem().string();
                        s->visualClip->colorIndex =
                            m_project.track(trackIndex).colorIndex;
                        // Reload live layer if this is the launched clip.
                        if (m_project.track(trackIndex).defaultScene == sceneIndex) {
                            m_visualEngine.loadLayer(trackIndex,
                                resolveShaderPath(s->visualClip->firstShaderPath()),
                                s->visualClip->audioSource);
                        }
                        markDirty();
                    });
            }));

        // "Fork Shader" — duplicate the current shader file to a new
        // <stem>_fork_N.frag so the user can edit it without touching
        // shaders shared by other clips.
        if (hasVisualClip && !slot->visualClip->firstShaderPath().empty()) {
            items.push_back(item("Fork Shader",
                [this, trackIndex, sceneIndex]() {
                    if (m_projectPath.empty()) {
                        LOG_WARN("Shader",
                            "Save the project first — shaders live under "
                            "<project>/shaders/.");
                        return;
                    }
                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s || !s->visualClip) return;
                    namespace fs = std::filesystem;
                    fs::path source = resolveShaderPath(s->visualClip->firstShaderPath());
                    if (!fs::exists(source)) {
                        LOG_ERROR("Shader", "Cannot fork — source missing: %s",
                                  source.string().c_str());
                        return;
                    }
                    fs::path dir = m_projectPath / "shaders";
                    std::error_code ec;
                    fs::create_directories(dir, ec);
                    std::string baseStem = source.stem().string();
                    fs::path target;
                    for (int n = 1; ; ++n) {
                        target = dir / (baseStem + "_fork_" +
                                        std::to_string(n) + ".frag");
                        if (!fs::exists(target)) break;
                    }
                    fs::copy_file(source, target,
                                  fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        LOG_ERROR("Shader", "Fork copy failed: %s",
                                  ec.message().c_str());
                        return;
                    }
                    LOG_INFO("Shader", "Forked %s → %s",
                             source.string().c_str(),
                             target.string().c_str());
                    s->visualClip->ensurePass0().shaderPath =
                        "shaders/" + target.filename().string();
                    s->visualClip->name = target.stem().string();
                    if (m_project.track(trackIndex).defaultScene == sceneIndex) {
                        m_visualEngine.loadLayer(trackIndex,
                            resolveShaderPath(s->visualClip->firstShaderPath()),
                            s->visualClip->audioSource);
                    }
                    markDirty();
                }));
        }

        // "Localize Shader" — copy an external/bundled shader into
        // <project>/shaders/ so the project is self-contained. Only
        // offered when the current path isn't already project-relative.
        if (hasVisualClip && !slot->visualClip->firstShaderPath().empty()) {
            const std::string& sp = slot->visualClip->firstShaderPath();
            const bool alreadyLocal =
                sp.compare(0, 8, "shaders/") == 0;
            if (!alreadyLocal) {
                items.push_back(item("Localize Shader",
                    [this, trackIndex, sceneIndex]() {
                        if (m_projectPath.empty()) {
                            LOG_WARN("Shader",
                                "Save the project first to localize shaders.");
                            return;
                        }
                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s || !s->visualClip) return;
                        std::string resolved =
                            resolveShaderPath(s->visualClip->firstShaderPath());
                        std::string newPath = localizeShader(resolved);
                        if (newPath == resolved) return; // nothing changed
                        s->visualClip->ensurePass0().shaderPath = newPath;
                        if (m_project.track(trackIndex).defaultScene == sceneIndex) {
                            m_visualEngine.loadLayer(trackIndex,
                                resolveShaderPath(newPath),
                                s->visualClip->audioSource);
                        }
                        markDirty();
                    }));
            }
        }

        // "Clear Shader" — drop the clip's source shader. Mirrors the
        // Clear Live Input / Clear All Models options. If this is the
        // launched clip, refresh the live layer: re-launch (falling back
        // to the video/model/live passthrough) when another source
        // remains, otherwise tear the layer down.
        if (hasVisualClip && !slot->visualClip->firstShaderPath().empty()) {
            items.push_back(item("Clear Shader",
                [this, trackIndex, sceneIndex]() {
                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s || !s->visualClip) return;
                    s->visualClip->ensurePass0().shaderPath.clear();
                    if (m_project.track(trackIndex).defaultScene == sceneIndex) {
                        const auto& vc = *s->visualClip;
                        const bool hasOtherSource =
                            !vc.videoPath.empty() ||
                            (vc.liveInput && !vc.liveUrl.empty()) ||
                            !vc.modelPath.empty();
                        if (hasOtherSource)
                            launchVisualClipData(trackIndex, vc,
                                                  vc.firstShaderPath());
                        else
                            m_visualEngine.clearLayer(trackIndex);
                    }
                    markDirty();
                }));
        }

        // "Set Video…" — opens a file picker, kicks off the ffmpeg
        // transcode, and drops the imported .mp4 path into this clip.
        items.push_back(item("Set Video…",
            [this, trackIndex, sceneIndex]() {
                if (m_projectPath.empty()) {
                    // Videos transcode into <project>/media/, so the
                    // project must have a home on disk. Surface this
                    // as a user-visible prompt instead of a silent
                    // log — the menu item feels broken otherwise.
                    ui::fw2::ConfirmDialog::prompt(
                        "Videos are imported into <project>/media/.\n"
                        "Save the project first, then try again.",
                        [this]() { saveProjectAs(); });
                    return;
                }
                m_pendingVideoTrack = trackIndex;
                m_pendingVideoScene = sceneIndex;
                static SDL_DialogFileFilter filter{
                    "Video files", "mp4;mov;mkv;webm;avi;m4v"};
                SDL_ShowOpenFileDialog(
                    [](void* ud, const char* const* filelist, int) {
                        auto* self = static_cast<App*>(ud);
                        if (!filelist || !filelist[0]) return;
                        int ti = self->m_pendingVideoTrack;
                        int si = self->m_pendingVideoScene;
                        self->m_pendingVideoTrack = -1;
                        self->m_pendingVideoScene = -1;
                        if (ti < 0 || si < 0) return;
                        self->startVideoImport(ti, si, filelist[0]);
                    },
                    this, m_mainWindow.getHandle(),
                    &filter, 1, nullptr, false);
            }));

        // "Live Input ▸" — submenu listing discovered devices + a
        // Custom URL… fallback + Clear when the clip is already live.
        // Device enumeration is platform-best-effort; Linux returns
        // /dev/video* with sysfs-derived names, others return empty.
        {
            // Shared action: assign `url` as the clip's live source and
            // wire it into the engine if this is the launched scene.
            auto applyLiveUrl = [this, trackIndex, sceneIndex](const std::string& url) {
                if (url.empty()) return;
                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                if (!s) return;
                if (!s->visualClip)
                    s->visualClip = std::make_unique<visual::VisualClip>();
                s->visualClip->liveInput = true;
                s->visualClip->liveUrl   = url;
                // Live and file video are mutually exclusive — clear
                // the file fields so the clip data stays consistent.
                s->visualClip->videoPath.clear();
                s->visualClip->thumbnailPath.clear();
                if (s->visualClip->name.empty())
                    s->visualClip->name = "Live";
                if (s->visualClip->colorIndex == 0)
                    s->visualClip->colorIndex =
                        m_project.track(trackIndex).colorIndex;
                if (m_project.track(trackIndex).defaultScene == sceneIndex) {
                    m_visualEngine.setLayerLiveInput(trackIndex, url);
                }
                markDirty();
            };

            std::vector<MenuEntry> liveItems;

            auto devices = visual::enumerateLiveInputDevices();
            for (auto& d : devices) {
                std::string url = d.url;
                liveItems.push_back(item(d.label,
                    [applyLiveUrl, url]{ applyLiveUrl(url); }));
            }
            if (!devices.empty()) {
                liveItems.push_back(separator());
            }
            liveItems.push_back(item("Custom URL…",
                [this, trackIndex, sceneIndex, applyLiveUrl]() {
                    auto* s0 = m_project.getSlot(trackIndex, sceneIndex);
                    std::string initial;
                    if (s0 && s0->visualClip) initial = s0->visualClip->liveUrl;
                    if (initial.empty()) initial = "v4l2:///dev/video0";
                    SDL_StartTextInput(m_mainWindow.getHandle());
                    m_textInputDialog.prompt("Live Input URL", initial,
                        [this, applyLiveUrl](const std::string& url) {
                            SDL_StopTextInput(m_mainWindow.getHandle());
                            applyLiveUrl(url);
                        });
                }));

            // Clear action is only meaningful when the clip is already
            // configured for live input.
            if (hasVisualClip && slot->visualClip->liveInput) {
                liveItems.push_back(item("Clear Live Input",
                    [this, trackIndex, sceneIndex]() {
                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s || !s->visualClip) return;
                        s->visualClip->liveInput = false;
                        s->visualClip->liveUrl.clear();
                        if (m_project.track(trackIndex).defaultScene == sceneIndex)
                            m_visualEngine.setLayerLiveInput(trackIndex, "");
                        markDirty();
                    }));
            }

            items.push_back(submenu("Live Input", std::move(liveItems)));
        }

        // Model list. A clip can carry several models; a scene script
        // picks among them by index (0 = primary). With none set the menu
        // shows a single "Add Model…"; with one or more it's a
        // "Models (N)…" submenu (add / per-model remove / clear all).
        {
            const auto modelPaths = hasVisualClip
                ? slot->visualClip->modelList() : std::vector<std::string>{};
            const int  nModels  = static_cast<int>(modelPaths.size());
            const bool hasModel = nModels > 0;

            // Shared dialog launcher → addModelToClip(). The first model
            // picked becomes the primary; later ones append as extras.
            auto addModelDialog = [this, trackIndex, sceneIndex]() {
                m_pendingModelTrack = trackIndex;
                m_pendingModelScene = sceneIndex;
                static SDL_DialogFileFilter filter{
                    "3D models (.glb .gltf)", "glb;gltf"};
                SDL_ShowOpenFileDialog(
                    [](void* ud, const char* const* filelist, int) {
                        auto* self = static_cast<App*>(ud);
                        if (!filelist || !filelist[0]) return;
                        int ti = self->m_pendingModelTrack;
                        int si = self->m_pendingModelScene;
                        self->m_pendingModelTrack = -1;
                        self->m_pendingModelScene = -1;
                        if (ti < 0 || si < 0) return;
                        self->addModelToClip(ti, si, filelist[0]);
                    },
                    this, m_mainWindow.getHandle(), &filter, 1, nullptr, false);
            };

            if (nModels == 0) {
                items.push_back(item("Add Model…", addModelDialog));
            } else {
                std::vector<MenuEntry> mItems;
                mItems.push_back(item("Add Model…", addModelDialog));
                mItems.push_back(separator());
                for (int i = 0; i < nModels; ++i) {
                    std::string stem =
                        std::filesystem::path(modelPaths[i]).stem().string();
                    if (stem.empty()) stem = "(model)";
                    std::string rowLabel = std::to_string(i) + ":  " + stem +
                                           (i == 0 ? "   (primary)" : "");
                    std::vector<MenuEntry> row;
                    row.push_back(item("Remove",
                        [this, trackIndex, sceneIndex, i]() {
                            removeModelFromClip(trackIndex, sceneIndex, i);
                        }));
                    mItems.push_back(submenu(rowLabel, std::move(row)));
                }
                mItems.push_back(separator());
                mItems.push_back(item("Clear All Models",
                    [this, trackIndex, sceneIndex]() {
                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s || !s->visualClip) return;
                        s->visualClip->modelPath.clear();
                        s->visualClip->modelSourcePath.clear();
                        s->visualClip->extraModelPaths.clear();
                        s->visualClip->extraModelSourcePaths.clear();
                        // A scene script needs a model — drop it too.
                        s->visualClip->scenePath.clear();
                        if (m_project.track(trackIndex).defaultScene == sceneIndex) {
                            m_visualEngine.setLayerModel(trackIndex, "");
                            m_visualEngine.setLayerSceneScript(trackIndex, "");
                        }
                        markDirty();
                    }));
                items.push_back(submenu(
                    "Models (" + std::to_string(nModels) + ")…",
                    std::move(mItems)));
            }

            // "Set Scene Script…" — only offered when the clip has a
            // model (the script drives instances of that model). File
            // picker filters to .lua; localizes to <project>/scripts/.
            if (hasModel) {
                const bool hasScript = !slot->visualClip->scenePath.empty();
                const char* sLabel = hasScript ? "Change Scene Script…"
                                                 : "Set Scene Script…";
                items.push_back(item(sLabel,
                    [this, trackIndex, sceneIndex]() {
                        m_pendingSceneTrack = trackIndex;
                        m_pendingSceneScene = sceneIndex;
                        static SDL_DialogFileFilter filter{
                            "Lua scene scripts (.lua)", "lua"};
                        SDL_ShowOpenFileDialog(
                            [](void* ud, const char* const* filelist, int) {
                                auto* self = static_cast<App*>(ud);
                                if (!filelist || !filelist[0]) return;
                                int ti = self->m_pendingSceneTrack;
                                int si = self->m_pendingSceneScene;
                                self->m_pendingSceneTrack = -1;
                                self->m_pendingSceneScene = -1;
                                if (ti < 0 || si < 0) return;
                                auto* s = self->m_project.getSlot(ti, si);
                                if (!s || !s->visualClip) return;
                                std::string stored =
                                    self->localizeScene(filelist[0]);
                                s->visualClip->scenePath = stored;
                                if (self->m_project.track(ti).defaultScene == si) {
                                    self->m_visualEngine.setLayerSceneScript(ti,
                                        self->resolveScenePath(stored));
                                }
                                self->markDirty();
                            },
                            this, m_mainWindow.getHandle(),
                            &filter, 1, nullptr, false);
                    }));

                if (hasScript) {
                    items.push_back(item("Clear Scene Script",
                        [this, trackIndex, sceneIndex]() {
                            auto* s = m_project.getSlot(trackIndex, sceneIndex);
                            if (!s || !s->visualClip) return;
                            s->visualClip->scenePath.clear();
                            if (m_project.track(trackIndex).defaultScene == sceneIndex)
                                m_visualEngine.setLayerSceneScript(trackIndex, "");
                            markDirty();
                        }));
                }
            }

            // "Animation" submenu — clip + speed for a rigged model. Only
            // for the live clip (we read the loaded layer's animation list)
            // and only when the model actually has animation clips.
            if (hasModel &&
                m_project.track(trackIndex).defaultScene == sceneIndex) {
                const int nAnim = m_visualEngine.layerAnimationCount(trackIndex);
                if (nAnim > 0) {
                    std::vector<MenuEntry> aItems;
                    const int curClip = slot->visualClip->animClip;
                    for (int i = 0; i < nAnim; ++i) {
                        std::string nm = m_visualEngine.layerAnimationName(trackIndex, i);
                        if (nm.empty()) nm = "Clip " + std::to_string(i);
                        aItems.push_back(radio("anim", nm, i == curClip,
                            [this, trackIndex, sceneIndex, i]() {
                                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                                if (!s || !s->visualClip) return;
                                s->visualClip->animClip = i;
                                m_visualEngine.setLayerAnimation(trackIndex, i,
                                    s->visualClip->animSpeed);
                                markDirty();
                            }));
                    }
                    aItems.push_back(separator());
                    const float curSpeed = slot->visualClip->animSpeed;
                    const float speeds[]  = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                    const char* slabels[] = { "0.25x", "0.5x", "1x", "2x", "4x" };
                    std::vector<MenuEntry> sItems;
                    for (int k = 0; k < 5; ++k) {
                        const float sp = speeds[k];
                        sItems.push_back(radio("animspd", slabels[k],
                            std::fabs(curSpeed - sp) < 1e-3f,
                            [this, trackIndex, sceneIndex, sp]() {
                                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                                if (!s || !s->visualClip) return;
                                s->visualClip->animSpeed = sp;
                                m_visualEngine.setLayerAnimation(trackIndex,
                                    s->visualClip->animClip, sp);
                                markDirty();
                            }));
                    }
                    sItems.push_back(separator());
                    sItems.push_back(item("Custom…",
                        [this, trackIndex, sceneIndex, curSpeed]() {
                            promptCustomFloat("Animation Speed (×)", curSpeed,
                                0.05f, 16.0f, 1.0f,
                                [this, trackIndex, sceneIndex](float sp) {
                                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                                    if (!s || !s->visualClip) return;
                                    s->visualClip->animSpeed = sp;
                                    m_visualEngine.setLayerAnimation(trackIndex,
                                        s->visualClip->animClip, sp);
                                    markDirty();
                                });
                        }));
                    aItems.push_back(submenu("Speed", std::move(sItems)));
                    items.push_back(submenu("Animation", std::move(aItems)));
                }
            }
        }

        // "Tempo Sync" toggle — when on, the clip follows the project
        // tempo (a video stretches to play over its Clip Length; a
        // shader/scene's animation clock becomes beat-driven). Off =
        // free-running on wall-clock. Auto-enabled for imported videos.
        if (hasVisualClip) {
            const bool synced = slot->visualClip->tempoSync;
            items.push_back(item(
                synced ? "Tempo Sync: On" : "Tempo Sync: Off",
                [this, trackIndex, sceneIndex]() {
                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s || !s->visualClip) return;
                    const bool on = !s->visualClip->tempoSync;
                    s->visualClip->tempoSync = on;
                    if (m_project.track(trackIndex).defaultScene == sceneIndex)
                        m_visualEngine.setLayerTempoSync(trackIndex, on,
                            s->visualClip->lengthBeats);
                    markDirty();
                }));
        }

        // "Clip Length" submenu — drives the envelope loop / follow-
        // action bar counter / session-clip "duration" abstraction, and
        // (when Tempo Sync is on) the tempo-locked playback length.
        if (hasVisualClip) {
            const double curLen = slot->visualClip->lengthBeats;
            const int    curBars = static_cast<int>(std::round(curLen / 4.0));
            auto setLen = [this, trackIndex, sceneIndex](int bars) {
                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                if (!s || !s->visualClip) return;
                s->visualClip->lengthBeats = bars * 4.0;
                // Live-update a currently-playing synced layer so the
                // loop length / beat clock changes take effect at once.
                if (s->visualClip->tempoSync &&
                    m_project.track(trackIndex).defaultScene == sceneIndex)
                    m_visualEngine.setLayerTempoSync(trackIndex, true,
                        s->visualClip->lengthBeats);
                markDirty();
            };
            std::vector<MenuEntry> lenItems;
            auto addLen = [&](const char* label, int bars) {
                lenItems.push_back(itemEn(label, [setLen, bars]{ setLen(bars); },
                                       curBars != bars));
            };
            addLen("1 bar",   1);
            addLen("2 bars",  2);
            addLen("4 bars",  4);
            addLen("8 bars",  8);
            addLen("16 bars", 16);
            addLen("32 bars", 32);
            lenItems.push_back(separator());
            lenItems.push_back(item("Custom…", [this, setLen, curBars]() {
                promptCustomInt("Clip Length (bars)", curBars, 1, 4096, setLen);
            }));
            items.push_back(submenu("Clip Length", std::move(lenItems)));
        }

        // "Send to Arrangement" — clone this visual clip onto the
        // arrangement timeline at the current transport beat, so the
        // user can lay out a long-form show without having to trigger
        // clips live. Uses the session clip's lengthBeats as the
        // duration; they can resize afterwards on the arrangement.
        if (hasVisualClip) {
            items.push_back(item("Send to Arrangement",
                [this, trackIndex, sceneIndex]() {
                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s || !s->visualClip) return;
                    ArrangementClip ac;
                    ac.type         = ArrangementClip::Type::Visual;
                    ac.startBeat    = m_audioEngine.transport().positionInBeats();
                    ac.lengthBeats  = s->visualClip->lengthBeats > 0
                                       ? s->visualClip->lengthBeats : 4.0;
                    ac.name         = s->visualClip->name.empty()
                                       ? "visual"
                                       : s->visualClip->name;
                    ac.colorIndex   = s->visualClip->colorIndex;
                    // A tempo-synced session clip was already following the
                    // tempo, so it stretches to fill its slot; otherwise it
                    // loops on extend (the default).
                    ac.stretch      = s->visualClip->tempoSync;
                    ac.visualClip   = s->visualClip->clone();
                    auto& clips = m_project.track(trackIndex).arrangementClips;
                    clips.push_back(std::move(ac));
                    m_project.track(trackIndex).sortArrangementClips();
                    m_project.track(trackIndex).arrangementActive = true;
                    markDirty();
                }));
        }

        // "Re-import Video" — delete cached transcode and rerun ffmpeg.
        // Visible only when we know the original source path (imported
        // under the current code path).
        if (slot && slot->visualClip &&
            !slot->visualClip->videoSourcePath.empty()) {
            std::string src = slot->visualClip->videoSourcePath;
            items.push_back(item("Re-import Video", [this, trackIndex, sceneIndex, src]() {
                if (m_projectPath.empty()) return;
                // Delete the cached files so the importer actually re-runs.
                std::filesystem::path mediaDir = m_projectPath / "media";
                std::string id = visual::VideoImporter::shortHash(src);
                std::error_code ec;
                for (const char* ext : { ".mp4", ".wav", "_thumb.jpg" }) {
                    std::filesystem::remove(mediaDir / (id + ext), ec);
                }
                startVideoImport(trackIndex, sceneIndex, src);
            }));
        }

        // Video-only: loop length + playback rate submenus. Only useful
        // if a video is assigned to the clip; shown always on visual
        // clips so the user can queue settings before import.
        if (slot && slot->visualClip && !slot->visualClip->videoPath.empty()) {
            const int curBars = slot->visualClip->videoLoopBars;
            const float curRate = slot->visualClip->videoRate;

            auto applyTiming = [this, trackIndex, sceneIndex](int bars, float rate) {
                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                if (!s || !s->visualClip) return;
                s->visualClip->videoLoopBars = bars;
                s->visualClip->videoRate     = rate;
                if (m_project.track(trackIndex).defaultScene == sceneIndex)
                    m_visualEngine.setLayerVideoTiming(trackIndex, bars, rate);
                markDirty();
            };

            // Loop Length submenu.
            std::vector<MenuEntry> loopItems;
            auto addLoop = [&](const char* label, int bars) {
                loopItems.push_back(itemEn(label,
                    [applyTiming, curRate, bars]{ applyTiming(bars, curRate); },
                    curBars != bars));
            };
            addLoop("Free (native rate)", 0);
            addLoop("1/2 bar", 0); // placeholder — overwritten below
            loopItems.pop_back();
            addLoop("1 bar",  1);
            addLoop("2 bars", 2);
            addLoop("4 bars", 4);
            addLoop("8 bars", 8);
            addLoop("16 bars",16);
            loopItems.push_back(separator());
            loopItems.push_back(item("Custom…", [this, applyTiming, curRate, curBars]() {
                promptCustomInt("Video Loop (bars, 0 = free)", curBars, 0, 4096,
                    [applyTiming, curRate](int b){ applyTiming(b, curRate); });
            }));
            items.push_back(submenu("Video Loop", std::move(loopItems)));

            // Playback Rate submenu (F.3).
            std::vector<MenuEntry> rateItems;
            auto addRate = [&](const char* label, float rate) {
                rateItems.push_back(itemEn(label,
                    [applyTiming, curBars, rate]{ applyTiming(curBars, rate); },
                    std::abs(curRate - rate) > 0.001f));
            };
            addRate("0.25×", 0.25f);
            addRate("0.5×",  0.5f);
            addRate("1× (normal)", 1.0f);
            addRate("2×",    2.0f);
            addRate("4×",    4.0f);
            rateItems.push_back(separator());
            rateItems.push_back(item("Custom…", [this, applyTiming, curBars, curRate]() {
                promptCustomFloat("Video Rate (×)", curRate, 0.05f, 16.0f, 1.0f,
                    [applyTiming, curBars](float r){ applyTiming(curBars, r); });
            }));
            items.push_back(submenu("Video Rate", std::move(rateItems)));

            // In/Out trim submenu — picks a sub-range of the source.
            const float curIn  = slot->visualClip->videoIn;
            const float curOut = slot->visualClip->videoOut;
            std::vector<MenuEntry> trimItems;
            auto addTrim = [&](const char* label, float inF, float outF) {
                trimItems.push_back(itemEn(label,
                    [this, trackIndex, sceneIndex, inF, outF]() {
                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s || !s->visualClip) return;
                        s->visualClip->videoIn  = inF;
                        s->visualClip->videoOut = outF;
                        if (m_project.track(trackIndex).defaultScene == sceneIndex)
                            m_visualEngine.setLayerVideoTrim(trackIndex, inF, outF);
                        markDirty();
                    },
                    !(std::abs(curIn - inF) < 0.005f &&
                      std::abs(curOut - outF) < 0.005f)));
            };
            addTrim("Full (0–100%)",         0.00f, 1.00f);
            addTrim("First half (0–50%)",    0.00f, 0.50f);
            addTrim("Last half (50–100%)",   0.50f, 1.00f);
            addTrim("Middle (25–75%)",       0.25f, 0.75f);
            addTrim("First quarter (0–25%)", 0.00f, 0.25f);
            addTrim("Last quarter (75–100%)",0.75f, 1.00f);
            trimItems.push_back(separator());
            trimItems.push_back(item("Custom… (in out %)",
                [this, trackIndex, sceneIndex, curIn, curOut]() {
                    char def[48];
                    std::snprintf(def, sizeof def, "%g %g",
                                  curIn * 100.0f, curOut * 100.0f);
                    promptNumber("Video Trim — in out, percent (e.g. 25 75)", def,
                        [this, trackIndex, sceneIndex](const std::string& t) {
                            float inP = 0.0f, outP = 0.0f;
                            if (std::sscanf(t.c_str(), "%f %f", &inP, &outP) != 2)
                                return;
                            float inF  = std::clamp(inP  * 0.01f, 0.0f, 1.0f);
                            float outF = std::clamp(outP * 0.01f, 0.0f, 1.0f);
                            if (outF <= inF) return;   // ignore invalid range
                            auto* s = m_project.getSlot(trackIndex, sceneIndex);
                            if (!s || !s->visualClip) return;
                            s->visualClip->videoIn  = inF;
                            s->visualClip->videoOut = outF;
                            if (m_project.track(trackIndex).defaultScene == sceneIndex)
                                m_visualEngine.setLayerVideoTrim(trackIndex, inF, outF);
                            markDirty();
                        });
                }));
            items.push_back(submenu("Video Trim", std::move(trimItems)));

            items.push_back(separator());
        }

        // "Set Text…" — edits the string that gets rasterised to iChannel1.
        if (slot && slot->visualClip) {
            items.push_back(item("Set Text…",
                [this, trackIndex, sceneIndex]() {
                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s || !s->visualClip) return;
                    SDL_StartTextInput(m_mainWindow.getHandle());
                    m_textInputDialog.prompt("Text (for iChannel1)",
                        s->visualClip->text,
                        [this, trackIndex, sceneIndex](const std::string& txt) {
                            SDL_StopTextInput(m_mainWindow.getHandle());
                            auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                            if (!s2 || !s2->visualClip) return;
                            s2->visualClip->text = txt;
                            // Only push to the live layer if this clip is the
                            // currently-launched one on its track.
                            if (m_project.track(trackIndex).defaultScene == sceneIndex)
                                m_visualEngine.setLayerText(trackIndex, txt);
                            markDirty();
                        });
                }));
        }

        // Audio source submenu — what track's level drives iAudioLevel.
        if (slot && slot->visualClip) {
            std::vector<MenuEntry> srcItems;
            const int curSource = slot->visualClip->audioSource;
            auto reassign = [this, trackIndex, sceneIndex](int newSrc) {
                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                if (!s || !s->visualClip) return;
                s->visualClip->audioSource = newSrc;
                // If this clip is the currently-launched one on its track,
                // update the visual engine layer's live source too.
                if (m_project.track(trackIndex).defaultScene == sceneIndex)
                    m_visualEngine.setLayerAudioSource(trackIndex, newSrc);
                markDirty();
            };
            srcItems.push_back(itemEn("Master", [reassign]{ reassign(-1); },
                                curSource != -1));
            srcItems.push_back(separator());
            for (int t = 0; t < m_project.numTracks(); ++t) {
                if (m_project.track(t).type == Track::Type::Visual) continue;
                std::string label = "Track " + std::to_string(t + 1) + ": "
                                     + m_project.track(t).name;
                srcItems.push_back(itemEn(label, [reassign, t]{ reassign(t); },
                                     curSource != t));
            }
            items.push_back(submenu("Audio Source", std::move(srcItems)));
        }
        items.push_back(separator());
    }

    // "Send to Arrangement" for audio/MIDI session clips — mirrors the
    // visual-clip path above. Audio shares the underlying AudioBuffer
    // with the session slot; MIDI is cloned (unique_ptr source → shared
    // on the arrangement side so the same clip can recur on the
    // timeline without cross-editing the session version).
    const bool canSendAudio = slot && slot->audioClip && slot->audioClip->buffer;
    const bool canSendMidi  = slot && slot->midiClip;
    if (canSendAudio || canSendMidi) {
        items.push_back(item("Send to Arrangement",
            [this, trackIndex, sceneIndex]() {
                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                if (!s) return;
                ArrangementClip ac;
                ac.startBeat  = m_audioEngine.transport().positionInBeats();
                ac.colorIndex = m_project.track(trackIndex).colorIndex;
                if (s->audioClip && s->audioClip->buffer) {
                    const double sr  = m_audioEngine.sampleRate();
                    const double bpm = m_audioEngine.transport().bpm();
                    const double durSec =
                        static_cast<double>(s->audioClip->buffer->numFrames()) / sr;
                    ac.type        = ArrangementClip::Type::Audio;
                    ac.audioBuffer = s->audioClip->buffer;
                    ac.lengthBeats = durSec * bpm / 60.0;
                    ac.loop        = s->audioClip->looping;  // one-shots don't loop
                    ac.name        = s->audioClip->name.empty()
                                     ? "audio" : s->audioClip->name;
                } else if (s->midiClip) {
                    ac.type        = ArrangementClip::Type::Midi;
                    ac.midiClip    = std::shared_ptr<midi::MidiClip>(
                                         s->midiClip->clone().release());
                    ac.lengthBeats = s->midiClip->lengthBeats() > 0
                                     ? s->midiClip->lengthBeats() : 4.0;
                    ac.name        = s->midiClip->name().empty()
                                     ? "midi" : s->midiClip->name();
                } else {
                    return;
                }
                m_project.track(trackIndex).arrangementClips.push_back(std::move(ac));
                m_project.track(trackIndex).sortArrangementClips();
                m_project.updateArrangementLength();
                syncArrangementClipsToEngine(trackIndex);
                if (!m_project.track(trackIndex).arrangementActive) {
                    m_project.track(trackIndex).arrangementActive = true;
                    m_audioEngine.sendCommand(
                        audio::SetTrackArrActiveMsg{trackIndex, true});
                }
                markDirty();
            }));
        items.push_back(separator());
    }

    // "Convert to MIDI" — Basic Pitch audio-to-MIDI. Transcribes this
    // audio clip into a new MIDI track in the same scene. Only shown when
    // the feature is compiled in (YAWN_HAS_BASIC_PITCH). Runs
    // synchronously; fast for typical clip-length audio (threading is a
    // future refinement for long takes).
    if (canSendAudio && transcribe::available()) {
        items.push_back(item("Convert to MIDI (Basic Pitch)",
            [this, trackIndex, sceneIndex]() {
                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                if (!s || !s->audioClip || !s->audioClip->buffer) return;
                const double sr  = m_audioEngine.sampleRate();
                const double bpm = m_audioEngine.transport().bpm();
                auto clip = transcribe::audioToMidi(*s->audioClip->buffer, sr, bpm);
                if (!clip) {
                    m_toastManager.show("Audio→MIDI: no notes detected",
                                        2.5f, ui::ToastManager::Severity::Warn);
                    return;
                }
                const std::string srcName = s->audioClip->name.empty()
                                             ? "Audio" : s->audioClip->name;
                clip->setName(srcName + " MIDI");
                // New MIDI track (with a default synth) in the same scene.
                const int idx = m_project.numTracks();
                m_project.addTrack(srcName + " MIDI", Track::Type::Midi);
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{idx, 1});
                m_audioEngine.setInstrument(idx,
                    std::make_unique<instruments::SubtractiveSynth>());
                m_project.setMidiClip(idx, sceneIndex, std::move(clip));
                markDirty();
                m_toastManager.show(
                    "Audio→MIDI: transcribed to track " + std::to_string(idx + 1),
                    3.0f, ui::ToastManager::Severity::Info);
            }));
        items.push_back(separator());
    }

    // "Separate Stems" — Demucs v4 four-stem separation onto 4 new audio
    // tracks. Only shown when the feature is compiled in. Downloads the
    // ~170 MB model on first use; runs on a worker thread (Esc to cancel).
    if (canSendAudio && transcribe::stemSeparationAvailable()) {
        items.push_back(item("Separate Stems (Demucs)",
            [this, trackIndex, sceneIndex]() {
                startStemSeparation(trackIndex, sceneIndex);
            }));
        items.push_back(separator());
    }

    items.push_back(itemEn("Copy", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s && s->audioClip) {
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Audio;
            m_clipboard.audioClip = s->audioClip->clone();
        } else if (s && s->midiClip) {
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Midi;
            m_clipboard.midiClip = s->midiClip->clone();
        }
    }, hasClip));

    items.push_back(itemEn("Cut", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s && s->audioClip) {
            auto backup = s->audioClip->clone();
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Audio;
            m_clipboard.audioClip = s->audioClip->clone();
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_project.graveyardSlotClips(*s);
            markDirty();
            m_undoManager.push({"Cut Audio Clip",
                [this, trackIndex, sceneIndex, b = std::shared_ptr<audio::Clip>(std::move(backup))]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { s2->audioClip = b->clone(); markDirty(); }
                },
                [this, trackIndex, sceneIndex]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                              m_project.graveyardSlotClips(*s2); markDirty(); }
                }, ""});
        } else if (s && s->midiClip) {
            auto backup = s->midiClip->clone();
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Midi;
            m_clipboard.midiClip = s->midiClip->clone();
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            m_project.graveyardSlotClips(*s);
            markDirty();
            m_undoManager.push({"Cut MIDI Clip",
                [this, trackIndex, sceneIndex, b = std::shared_ptr<midi::MidiClip>(std::move(backup))]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { s2->midiClip = b->clone(); markDirty(); }
                },
                [this, trackIndex, sceneIndex]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
                              m_project.graveyardSlotClips(*s2); markDirty(); }
                }, ""});
        }
    }, hasClip));

    items.push_back(itemEn("Paste", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (!s) return;
        std::shared_ptr<audio::Clip> oldAudio;
        std::shared_ptr<midi::MidiClip> oldMidi;
        if (s->audioClip) oldAudio.reset(s->audioClip->clone().release());
        if (s->midiClip) oldMidi.reset(s->midiClip->clone().release());
        if (m_clipboard.type == ClipboardData::Type::Audio && m_clipboard.audioClip) {
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            m_project.graveyardSlotClips(*s);
            s->audioClip = m_clipboard.audioClip->clone();
            markDirty();
            auto pc = m_clipboard.audioClip->clone();
            m_undoManager.push({"Paste Audio Clip",
                [this, trackIndex, sceneIndex, oldAudio, oldMidi]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2) return;
                    m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
                    s2->clear();
                    if (oldAudio) s2->audioClip = oldAudio->clone();
                    if (oldMidi) s2->midiClip = oldMidi->clone();
                    markDirty();
                },
                [this, trackIndex, sceneIndex, p = std::shared_ptr<audio::Clip>(std::move(pc))]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2) return;
                    m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
                    s2->clear();
                    s2->audioClip = p->clone();
                    markDirty();
                }, ""});
        } else if (m_clipboard.type == ClipboardData::Type::Midi && m_clipboard.midiClip) {
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            m_project.graveyardSlotClips(*s);
            s->midiClip = m_clipboard.midiClip->clone();
            markDirty();
            auto pc = m_clipboard.midiClip->clone();
            m_undoManager.push({"Paste MIDI Clip",
                [this, trackIndex, sceneIndex, oldAudio, oldMidi]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2) return;
                    m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
                    s2->clear();
                    if (oldAudio) s2->audioClip = oldAudio->clone();
                    if (oldMidi) s2->midiClip = oldMidi->clone();
                    markDirty();
                },
                [this, trackIndex, sceneIndex, p = std::shared_ptr<midi::MidiClip>(std::move(pc))]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2) return;
                    m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
                    s2->clear();
                    s2->midiClip = p->clone();
                    markDirty();
                }, ""});
        }
    }, hasClipboard));

    items.push_back(itemEn("Duplicate", [this, trackIndex, sceneIndex]() {
        auto* src = m_project.getSlot(trackIndex, sceneIndex);
        if (!src || src->empty()) return;
        for (int s = sceneIndex + 1; s < m_project.numScenes(); ++s) {
            auto* dst = m_project.getSlot(trackIndex, s);
            if (dst && dst->empty()) {
                if (src->audioClip)       dst->audioClip  = src->audioClip->clone();
                else if (src->midiClip)   dst->midiClip   = src->midiClip->clone();
                else if (src->visualClip) dst->visualClip = src->visualClip->clone();
                int destScene = s;
                m_selectedScene = s;
                m_sessionPanel->setSelectedScene(s);
                markDirty();
                m_undoManager.push({"Duplicate Clip",
                    [this, trackIndex, destScene]{
                        auto* s2 = m_project.getSlot(trackIndex, destScene);
                        if (s2) { s2->clear(); markDirty(); }
                    },
                    [this, trackIndex, sceneIndex, destScene]{
                        auto* src2 = m_project.getSlot(trackIndex, sceneIndex);
                        auto* dst2 = m_project.getSlot(trackIndex, destScene);
                        if (src2 && dst2) {
                            if (src2->audioClip)       dst2->audioClip  = src2->audioClip->clone();
                            else if (src2->midiClip)   dst2->midiClip   = src2->midiClip->clone();
                            else if (src2->visualClip) dst2->visualClip = src2->visualClip->clone();
                            markDirty();
                        }
                    }, ""});
                break;
            }
        }
    }, hasClip));

    items.push_back(separator());

    // Launch quantize submenu
    auto* slotForQ = m_project.getSlot(trackIndex, sceneIndex);
    auto curLQ = slotForQ ? slotForQ->launchQuantize : audio::QuantizeMode::NextBar;
    std::vector<MenuEntry> lqItems;
    lqItems.push_back(itemEn("None", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s) s->launchQuantize = audio::QuantizeMode::None;
    }, curLQ != audio::QuantizeMode::None));
    lqItems.push_back(itemEn("Beat", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s) s->launchQuantize = audio::QuantizeMode::NextBeat;
    }, curLQ != audio::QuantizeMode::NextBeat));
    lqItems.push_back(itemEn("Bar", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s) s->launchQuantize = audio::QuantizeMode::NextBar;
    }, curLQ != audio::QuantizeMode::NextBar));
    items.push_back(submenu("Launch Quantize", std::move(lqItems)));

    items.push_back(separator());

    items.push_back(itemEn("Delete", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s && !s->empty()) {
            std::shared_ptr<audio::Clip> oldAudio;
            std::shared_ptr<midi::MidiClip> oldMidi;
            std::shared_ptr<visual::VisualClip> oldVisual;
            if (s->audioClip)  oldAudio.reset(s->audioClip->clone().release());
            if (s->midiClip)   oldMidi.reset(s->midiClip->clone().release());
            if (s->visualClip) oldVisual.reset(s->visualClip->clone().release());
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            m_project.graveyardSlotClips(*s);
            markDirty();
            m_undoManager.push({"Delete Clip",
                [this, trackIndex, sceneIndex, oldAudio, oldMidi, oldVisual]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2) return;
                    if (oldAudio)  s2->audioClip  = oldAudio->clone();
                    if (oldMidi)   s2->midiClip   = oldMidi->clone();
                    if (oldVisual) s2->visualClip = oldVisual->clone();
                    markDirty();
                },
                [this, trackIndex, sceneIndex]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2) return;
                    m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
                    s2->clear();
                    markDirty();
                }, ""});
        }
    }, hasClip));

    items.push_back(itemEn("Rename", [this, trackIndex, sceneIndex]() {
        // TODO: open rename dialog
        (void)trackIndex; (void)sceneIndex;
    }, hasClip));

    // Save a MIDI clip into the loop library so it shows up in the
    // Browser's Loops tab. Prompts for a name (default = clip name),
    // auto-categorizes, writes the .mid, and indexes the result inline
    // so the tab updates without waiting for a rescan.
    if (slot && slot->midiClip) {
        items.push_back(item("Save to Loop Library…", [this, trackIndex, sceneIndex]() {
            auto* s = m_project.getSlot(trackIndex, sceneIndex);
            if (!s || !s->midiClip) return;
            std::string def = s->midiClip->name();
            if (def.empty()) def = "loop";
            SDL_StartTextInput(m_mainWindow.getHandle());
            m_textInputDialog.prompt("Loop Name", def,
                [this, trackIndex, sceneIndex](const std::string& raw) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                    if (raw.empty()) return;
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2 || !s2->midiClip) return;
                    const midi::MidiClip& clip = *s2->midiClip;

                    const std::string cat = MidiLoopManager::autoCategory(clip);
                    const double bpm   = m_audioEngine.transport().bpm();
                    const int    tsNum = m_audioEngine.transport().numerator();
                    const int    tsDen = m_audioEngine.transport().denominator();

                    auto path = MidiLoopManager::saveMidiLoop(raw, cat, clip, bpm, tsNum, tsDen);
                    if (path.empty()) {
                        m_toastManager.show("Couldn't save MIDI loop", 2.5f,
                                            ui::ToastManager::Severity::Error);
                        return;
                    }

                    // Index it immediately so it appears in the Loops tab.
                    // Mirrors LibraryScanner::doScanMidiLoops record build
                    // (libraryPathId 0 = YAWN-managed root).
                    library::MidiLoopRecord r;
                    r.path        = path.string();
                    r.name        = path.stem().string();
                    r.category    = cat;
                    r.lengthBeats = clip.lengthBeats();
                    r.tempoBPM    = bpm;
                    r.timeSigNum  = tsNum;
                    r.timeSigDen  = tsDen;
                    r.noteCount   = clip.noteCount();
                    int lo = 127, hi = 0;
                    for (int i = 0; i < clip.noteCount(); ++i) {
                        int p = clip.note(i).pitch;
                        if (p < lo) lo = p;
                        if (p > hi) hi = p;
                    }
                    if (clip.noteCount() == 0) { lo = 0; hi = 0; }
                    r.lowestPitch   = lo;
                    r.highestPitch  = hi;
                    r.libraryPathId = 0;
                    std::error_code ec;
                    auto ftime = std::filesystem::last_write_time(path, ec);
                    r.lastModified = std::chrono::duration_cast<std::chrono::seconds>(
                        ftime.time_since_epoch()).count();
                    m_libraryDb.insertOrUpdateMidiLoop(r);
                    m_browserPanel->loopsTab().refreshList();
                    m_toastManager.show("Saved \"" + r.name + "\" to loops", 2.0f,
                                        ui::ToastManager::Severity::Info);
                });
        }));
    }

    items.push_back(separator());

    // Record length setting — per-slot. Clicking this slot to start a
    // recording uses this length (0 = unlimited). Does nothing once
    // the slot is occupied by a clip.
    {
        auto* rlSlot = m_project.getSlot(trackIndex, sceneIndex);
        int curRL = rlSlot ? rlSlot->recordLengthBars : 0;
        std::string rlLabel = "Record Length: ";
        rlLabel += (curRL == 0) ? "Unlimited" : (std::to_string(curRL) + (curRL == 1 ? " Bar" : " Bars"));
        std::vector<MenuEntry> rlItems;
        auto setRL = [this, trackIndex, sceneIndex](int bars) {
            auto* s = m_project.getSlot(trackIndex, sceneIndex);
            if (s) s->recordLengthBars = bars;
        };
        auto addRLItem = [&](const char* label, int bars) {
            rlItems.push_back(itemEn(label, [this, setRL, curRL, bars]() {
                setRL(bars);
                m_undoManager.push({"Change Record Length",
                    [setRL, curRL]{ setRL(curRL); },
                    [setRL, bars]{ setRL(bars); },
                    ""});
                markDirty();
            }, curRL != bars));
        };
        addRLItem("Unlimited", 0);
        addRLItem("1 Bar", 1);
        addRLItem("2 Bars", 2);
        addRLItem("4 Bars", 4);
        addRLItem("8 Bars", 8);
        addRLItem("16 Bars", 16);
        rlItems.push_back(separator());
        rlItems.push_back(item("Custom...", [this, setRL, curRL]() {
            std::string def = (curRL > 0) ? std::to_string(curRL) : "4";
            SDL_StartTextInput(m_mainWindow.getHandle());
            m_textInputDialog.prompt("Record Length (bars)", def,
                [this, setRL, curRL](const std::string& text) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                    int bars = 0;
                    try { bars = std::stoi(text); } catch (...) { return; }
                    if (bars < 0) bars = 0;
                    setRL(bars);
                    m_undoManager.push({"Change Record Length",
                        [setRL, curRL]{ setRL(curRL); },
                        [setRL, bars]{ setRL(bars); },
                        ""});
                    markDirty();
                });
        }));
        items.push_back(submenu(rlLabel, std::move(rlItems)));

        // Loop-on-playback toggle for future recordings into this slot.
        const bool curLoop = rlSlot ? rlSlot->recordLoop : true;
        std::string loopLabel = "Record Loop: ";
        loopLabel += curLoop ? "On" : "Off";
        items.push_back(item(loopLabel, [this, trackIndex, sceneIndex, curLoop]() {
            auto* s = m_project.getSlot(trackIndex, sceneIndex);
            if (!s) return;
            s->recordLoop = !curLoop;
            m_undoManager.push({"Toggle Record Loop",
                [this, trackIndex, sceneIndex, curLoop]{
                    auto* ss = m_project.getSlot(trackIndex, sceneIndex);
                    if (ss) ss->recordLoop = curLoop;
                },
                [this, trackIndex, sceneIndex, curLoop]{
                    auto* ss = m_project.getSlot(trackIndex, sceneIndex);
                    if (ss) ss->recordLoop = !curLoop;
                }, ""});
            markDirty();
        }));
    }

    // Per-clip automation-record toggle — disables recording into
    // this clip's lanes regardless of global / track arming. Lets
    // the user freeze a take's automation without disabling the
    // track-wide arm. Always shown on slots that have a clip (audio
    // or MIDI; visual clips don't go through the audio thread's
    // automation engine, so the toggle is meaningless there).
    {
        auto* slotForLock = m_project.getSlot(trackIndex, sceneIndex);
        const bool hasAudioOrMidi = slotForLock &&
                                       (slotForLock->audioClip || slotForLock->midiClip);
        if (hasAudioOrMidi) {
            const bool curDisabled = slotForLock->autoRecordDisabled;
            std::string label = "Auto-Rec: ";
            label += curDisabled ? "Disabled (this clip)" : "Enabled";
            items.push_back(item(label,
                [this, trackIndex, sceneIndex, curDisabled]() {
                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s) return;
                    s->autoRecordDisabled = !curDisabled;
                    // Push the change to the engine so it takes
                    // effect immediately even if the clip is mid-
                    // playback. The next launch will also carry the
                    // value via LaunchClipMsg.autoRecordDisabled,
                    // so this msg is just for the live case.
                    m_audioEngine.sendCommand(audio::SetClipAutoRecordDisabledMsg{
                        trackIndex, sceneIndex, !curDisabled});
                    m_undoManager.push({"Toggle Per-Clip Auto-Rec",
                        [this, trackIndex, sceneIndex, curDisabled]{
                            auto* ss = m_project.getSlot(trackIndex, sceneIndex);
                            if (ss) ss->autoRecordDisabled = curDisabled;
                            m_audioEngine.sendCommand(audio::SetClipAutoRecordDisabledMsg{
                                trackIndex, sceneIndex, curDisabled});
                            markDirty();
                        },
                        [this, trackIndex, sceneIndex, curDisabled]{
                            auto* ss = m_project.getSlot(trackIndex, sceneIndex);
                            if (ss) ss->autoRecordDisabled = !curDisabled;
                            m_audioEngine.sendCommand(audio::SetClipAutoRecordDisabledMsg{
                                trackIndex, sceneIndex, !curDisabled});
                            markDirty();
                        }, ""});
                    markDirty();
                }));
        }
    }

    // Clear-automation entry — only meaningful when the slot has any
    // recorded clip-level automation lanes. Surface it conditionally
    // so it doesn't clutter the menu on empty / never-automated slots.
    {
        auto* slotForAuto = m_project.getSlot(trackIndex, sceneIndex);
        const bool hasClipAuto = slotForAuto &&
                                   !slotForAuto->clipAutomation->lanes.empty();
        if (hasClipAuto) {
            items.push_back(item("Clear Clip Automation",
                [this, trackIndex, sceneIndex]() {
                    auto* s = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s) return;
                    auto saved = s->clipAutomation->lanes;   // copy for undo
                    m_project.replaceSlotAutomation(*s, {});
                    publishClipAutomation(trackIndex, sceneIndex);
                    m_undoManager.push({"Clear Clip Automation",
                        [this, trackIndex, sceneIndex, saved]{
                            auto* ss = m_project.getSlot(trackIndex, sceneIndex);
                            if (ss) {
                                m_project.replaceSlotAutomation(*ss, saved);
                                publishClipAutomation(trackIndex, sceneIndex);
                            }
                            markDirty();
                        },
                        [this, trackIndex, sceneIndex]{
                            auto* ss = m_project.getSlot(trackIndex, sceneIndex);
                            if (ss) {
                                m_project.replaceSlotAutomation(*ss, {});
                                publishClipAutomation(trackIndex, sceneIndex);
                            }
                            markDirty();
                        }, ""});
                    markDirty();
                }));
        }
    }

    ui::fw2::ContextMenu::show(std::move(items),
                                 ui::fw::Point{mx, my});
}


} // namespace yawn
