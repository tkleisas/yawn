// App_WidgetTree.cpp — fw2 widget-tree construction: menu bar, panel
// tree build + layout, font loading, and detail-panel content sync.
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

bool App::loadFont() {
    // Bundled font first (same on all platforms), then system fallbacks
    const char* fontPaths[] = {
        "assets/fonts/JetBrainsMono-Regular.ttf",
#ifdef _WIN32
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\consola.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
    };

    for (const char* path : fontPaths) {
        if (m_font.load(path, 48.0f)) return true;
    }

    LOG_WARN("UI", "Could not find a system font. Text will not render.");
    return false;
}

void App::setupMenuBar() {
    using ::yawn::ui::fw2::MenuEntry;
    namespace M = ::yawn::ui::fw2::Menu;

    // File menu
    m_menuBar.addMenu("File", {
        M::item("New Project",  [this]() { newProject(); },   "Ctrl+N"),
        M::item("Open Project", [this]() { openProject(); },  "Ctrl+O"),
        M::item("Save Project", [this]() { saveProject(); },  "Ctrl+S"),
        M::item("Save As...",   [this]() { saveProjectAs(); },"Ctrl+Shift+S"),
        M::separator(),
        M::item("Export Audio", [this]() { openExportDialog(); }),
        M::item("Export Video (mp4)…", [this]() { openVideoExportDialog(); }),
        M::separator(),
        M::item("Quit",         [this]() { m_running = false; }, "Ctrl+Q"),
    });

    // Edit menu
    m_menuBar.addMenu("Edit", {
        M::item("Undo", [this]() { if (m_undoManager.canUndo()) { m_undoManager.undo(); markDirty(); } }, "Ctrl+Z"),
        M::item("Redo", [this]() { if (m_undoManager.canRedo()) { m_undoManager.redo(); markDirty(); } }, "Ctrl+Y"),
        // Wipe every recorded breakpoint envelope (track lanes +
        // every clip's lanes) across the project. Doesn't touch LFO
        // links, macro mappings, or MIDI Learn — those are device
        // routing, not recorded automation. Confirms before firing
        // because the operation is destructive (undoable, but the
        // confirm dialog matches the v0.61 design discussion).
        M::item("Clear All Automation…", [this]() {
            ui::fw2::ConfirmDialog::prompt(
                "Clear ALL recorded automation across every track + clip?\n"
                "(LFO links and macro mappings are not affected.)",
                [this]() {
                    // Snapshot all track lanes + all slot lanes for undo.
                    struct ClearAllSnap {
                        std::vector<std::vector<automation::AutomationLane>> trackLanes;
                        std::vector<std::vector<std::vector<automation::AutomationLane>>> slotLanes;
                    };
                    auto snap = std::make_shared<ClearAllSnap>();
                    snap->trackLanes.resize(m_project.numTracks());
                    snap->slotLanes.resize(m_project.numTracks());
                    for (int t = 0; t < m_project.numTracks(); ++t) {
                        snap->trackLanes[t] = m_project.track(t).automationLanes;
                        snap->slotLanes[t].resize(m_project.numScenes());
                        for (int s = 0; s < m_project.numScenes(); ++s) {
                            auto* slot = m_project.getSlot(t, s);
                            if (slot) snap->slotLanes[t][s] = slot->clipAutomation->lanes;
                        }
                    }
                    m_project.clearAllAutomation();
                    for (int t = 0; t < m_project.numTracks(); ++t)
                        for (int s = 0; s < m_project.numScenes(); ++s)
                            publishClipAutomation(t, s);
                    m_undoManager.push({"Clear All Automation",
                        [this, snap]{
                            for (int t = 0; t < (int)snap->trackLanes.size() &&
                                            t < m_project.numTracks(); ++t) {
                                m_project.track(t).automationLanes = snap->trackLanes[t];
                                for (int s = 0; s < (int)snap->slotLanes[t].size() &&
                                                s < m_project.numScenes(); ++s) {
                                    auto* slot = m_project.getSlot(t, s);
                                    if (slot) {
                                        m_project.replaceSlotAutomation(*slot, snap->slotLanes[t][s]);
                                        publishClipAutomation(t, s);
                                    }
                                }
                            }
                            markDirty();
                        },
                        [this]{
                            m_project.clearAllAutomation();
                            for (int t = 0; t < m_project.numTracks(); ++t)
                                for (int s = 0; s < m_project.numScenes(); ++s)
                                    publishClipAutomation(t, s);
                            markDirty();
                        }, ""});
                    markDirty();
                });
        }),
        M::item("Preferences", [this]() {
            ui::fw2::FwPreferencesDialog::State state;
            state.selectedOutputDevice = m_audioEngine.config().outputDevice;
            state.selectedInputDevice = m_audioEngine.config().inputDevice;
            state.sampleRate = m_audioEngine.config().sampleRate;
            state.bufferSize = static_cast<int>(m_audioEngine.config().framesPerBuffer);
            state.defaultLaunchQuantize = static_cast<audio::QuantizeMode>(m_settings.defaultLaunchQuantize);
            state.defaultRecordQuantize = static_cast<audio::QuantizeMode>(m_settings.defaultRecordQuantize);
            m_midiEngine.refreshPorts();
            state.enabledMidiInputs.clear();
            for (int i = 0; i < m_midiEngine.openInputPortCount(); ++i) {
                if (i < m_midiEngine.availableInputCount())
                    state.enabledMidiInputs.push_back(i);
            }
            if (state.enabledMidiInputs.empty()) {
                for (int i = 0; i < m_midiEngine.availableInputCount(); ++i)
                    state.enabledMidiInputs.push_back(i);
            }
            state.enabledMidiOutputs = m_settings.enabledMidiOutputs;
            state.metronomeVolume = m_settings.metronomeVolume;
            state.metronomeMode = m_settings.metronomeMode;
            state.countInBars = m_settings.countInBars;
            state.metronomeVisualStyle = m_settings.metronomeVisualStyle;
            state.fontScale = m_settings.fontScale;
            state.latencyCompensation = m_audioEngine.mixer().pdcEnabled();
            state.masterOversample = m_audioEngine.mixer().masterOversample();
            state.linkEnabled = m_audioEngine.linkManager().enabled();
            state.linkStartStopSync = m_audioEngine.linkManager().startStopSyncEnabled();
            m_preferencesDialog.open(state, &m_audioEngine, &m_midiEngine);
        }),
    });

    // View menu
    m_menuBar.addMenu("View", {
        M::item("Session View",     [this]() { switchToView(ViewMode::Session); },     "Tab"),
        M::item("Arrangement View", [this]() { switchToView(ViewMode::Arrangement); }, "Tab"),
        M::item("Toggle Mixer",     [this]() { m_showMixer = !m_showMixer; }, "M"),
        M::item("Detail Panel",     [this]() {
            m_showDetailPanel = !m_showDetailPanel;
            if (m_showDetailPanel) {
                m_detailPanel->setOpen(true);
                // Populate the device chain for the current target —
                // without this, toggling the panel on without first
                // clicking a track shows an empty body. The track-
                // click / bus-click handlers gate their detail updates
                // on m_showDetailPanel, so when D is OFF they don't
                // populate the chain. We re-populate here on toggle on.
                switch (m_detailTarget) {
                case DetailTarget::Track:     updateDetailForSelectedTrack(); break;
                case DetailTarget::ReturnBus: updateDetailForReturnBus(m_detailReturnBus); break;
                case DetailTarget::Master:    updateDetailForMaster(); break;
                }
            }
        }, "D"),
        M::item("Reload Controller Scripts", [this]() {
            m_controllerManager.reloadScripts("");
        }),
        M::item("Visual Output Window", [this]() {
            m_visualEngine.setOutputVisible(!m_visualEngine.isOutputVisible());
        }),
        M::item("Visual Output Fullscreen", [this]() {
            // If output is hidden, show it first — fullscreen on a hidden
            // window is a no-op and confusing. Toggle on a visible window.
            if (!m_visualEngine.isOutputVisible())
                m_visualEngine.setOutputVisible(true);
            m_visualEngine.setFullscreen(!m_visualEngine.isFullscreen());
        }, "F11"),
        M::item("Post FX: Add Bloom", [this]() {
            m_visualEngine.addPostFX("assets/shaders/post/bloom.frag");
            markDirty();
            updateDetailForSelectedTrack();
        }),
        M::item("Post FX: Add Pixelate", [this]() {
            m_visualEngine.addPostFX("assets/shaders/post/pixelate.frag");
            markDirty();
            updateDetailForSelectedTrack();
        }),
        M::item("Post FX: Add Kaleidoscope", [this]() {
            m_visualEngine.addPostFX("assets/shaders/post/kaleidoscope.frag");
            markDirty();
            updateDetailForSelectedTrack();
        }),
        M::item("Post FX: Add Chromatic Split", [this]() {
            m_visualEngine.addPostFX("assets/shaders/post/chroma.frag");
            markDirty();
            updateDetailForSelectedTrack();
        }),
        M::item("Post FX: Add Vignette", [this]() {
            m_visualEngine.addPostFX("assets/shaders/post/vignette.frag");
            markDirty();
            updateDetailForSelectedTrack();
        }),
        M::item("Post FX: Add Invert", [this]() {
            m_visualEngine.addPostFX("assets/shaders/post/invert.frag");
            markDirty();
            updateDetailForSelectedTrack();
        }),
        M::item("Post FX: Remove Last", [this]() {
            int n = m_visualEngine.numPostFX();
            if (n > 0) {
                m_visualEngine.removePostFX(n - 1);
                markDirty();
                updateDetailForSelectedTrack();
            }
        }),
        M::item("Post FX: Clear All", [this]() {
            while (m_visualEngine.numPostFX() > 0)
                m_visualEngine.removePostFX(0);
            markDirty();
            updateDetailForSelectedTrack();
        }),
    });

    // Track menu
    {
        m_menuBar.addMenu("Track", {
            M::item("Add Audio Track", [this]() {
                int idx = m_project.numTracks();
                m_project.addTrack("Audio " + std::to_string(idx + 1), Track::Type::Audio);
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{idx, 0});
                m_audioEngine.sendCommand(audio::SetTrackAudioInputChMsg{idx, m_project.track(idx).audioInputCh});
                markDirty();
                m_undoManager.push({"Add Audio Track",
                    [this]{ m_project.removeLastTrack(); markDirty(); },
                    [this]{
                        int i = m_project.numTracks();
                        m_project.addTrack("Audio " + std::to_string(i + 1), Track::Type::Audio);
                        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{i, 0});
                        m_audioEngine.sendCommand(audio::SetTrackAudioInputChMsg{i, m_project.track(i).audioInputCh});
                        markDirty();
                    }, ""});
                LOG_INFO("Audio", "Added Audio track %d", m_project.numTracks());
            }),
            M::item("Add MIDI Track", [this]() {
                int idx = m_project.numTracks();
                m_project.addTrack("MIDI " + std::to_string(idx + 1), Track::Type::Midi);
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{idx, 1});
                m_audioEngine.setInstrument(idx, std::make_unique<instruments::SubtractiveSynth>());
                markDirty();
                m_undoManager.push({"Add MIDI Track",
                    [this, idx]{
                        m_audioEngine.setInstrument(idx, nullptr);
                        m_project.removeLastTrack(); markDirty();
                    },
                    [this]{
                        int i = m_project.numTracks();
                        m_project.addTrack("MIDI " + std::to_string(i + 1), Track::Type::Midi);
                        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{i, 1});
                        m_audioEngine.setInstrument(i, std::make_unique<instruments::SubtractiveSynth>());
                        markDirty();
                    }, ""});
                LOG_INFO("MIDI", "Added MIDI track %d (with SubSynth)", m_project.numTracks());
            }),
            M::item("Add Visual Track", [this]() {
                int idx = m_project.numTracks();
                m_project.addTrack("Visual " + std::to_string(idx + 1), Track::Type::Visual);
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{idx, 2});
                markDirty();
                m_undoManager.push({"Add Visual Track",
                    [this]{ m_project.removeLastTrack(); markDirty(); },
                    [this]{
                        int i = m_project.numTracks();
                        m_project.addTrack("Visual " + std::to_string(i + 1), Track::Type::Visual);
                        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{i, 2});
                        markDirty();
                    }, ""});
                LOG_INFO("Visual", "Added Visual track %d", m_project.numTracks());
            }),
            M::separator(),
            M::item("Rename Track", [this]() {
                // SessionPanel is fw2 — check visibility directly.
                if (m_sessionPanel->isVisible()) {
                    m_sessionPanel->startTrackRename(m_selectedTrack);
                } else {
                    m_arrangementPanel->startTrackRename(m_selectedTrack);
                }
                SDL_StartTextInput(m_mainWindow.getHandle());
            }),
        });
    }

    // Scene menu — mirrors the scene-label right-click actions but
    // keyboard-accessible. Right-click on a scene label offers the
    // full Insert/Duplicate/Delete/Rename set.
    m_menuBar.addMenu("Scene", {
        M::item("Insert Scene\tIns", [this]() {
            insertSceneAtSelection();
        }),
    });

    // (MIDI device + Link sync config live in Edit → Preferences; the
    // virtual-keyboard note velocity is set from the transport bar.)

    // Tools menu — procedural preset generation. Runs on a worker
    // thread; results land in the global preset library and show up in
    // the Browser → Presets tab (where they can be auditioned/loaded).
    m_menuBar.addMenu("Tools", {
        M::item("Generate Preset Library (balanced names)", [this]() {
            startPresetGeneration(0.5f, /*selectedDeviceOnly*/false);
        }),
        M::item("Generate Preset Library (alien names)", [this]() {
            startPresetGeneration(0.85f, false);
        }),
        M::item("Generate Preset Library (descriptive names)", [this]() {
            startPresetGeneration(0.15f, false);
        }),
        M::separator(),
        M::item("Generate Presets for Selected Track's Device", [this]() {
            startPresetGeneration(0.5f, /*selectedDeviceOnly*/true);
        }),
        // (Session→arrangement capture moved to the transport ▸ARR button.)
    });

    // Help menu
    m_menuBar.addMenu("Help", {
        M::item("About Y.A.W.N", [this]() {
            ui::fw2::DialogSpec spec;
            spec.title = "Y.A.W.N";
            spec.message =
                "Yetanother Audio Workstation New\n"
                "Version " YAWN_VERSION_STRING "\n"
                "\n"
                "Made with AI-Sloptronic(TM) technology\n"
                "\n"
                "PM: Tasos Kleisas\n"
                "Chief Engineer: Claude (Anthropic)\n"
                "Where \"it compiles\" is the new \"it works\"";
            // Show the YAWN logo above the title (loaded at startup,
            // see m_iconTexture init).
            if (m_iconTexture) {
                spec.iconTextureId = static_cast<unsigned int>(m_iconTexture);
                spec.iconSize      = 128.0f;
            }
            ui::fw2::DialogButton ok;
            ok.label   = "OK";
            ok.primary = true;
            ok.cancel  = true;
            spec.buttons.push_back(std::move(ok));
            ui::fw2::Dialog::show(std::move(spec));
        }),
        M::item("Keyboard Shortcuts", []() {
            ui::fw2::DialogSpec spec;
            spec.title = "Keyboard Shortcuts";
            // Two columns separated by spaces. Using a proportional
            // font means alignment is approximate, but key combos
            // are short and consistent so it reads cleanly enough.
            // Group with blank lines + section headers.
            spec.message =
                "FILE\n"
                "  Ctrl+N           New Project\n"
                "  Ctrl+O           Open Project\n"
                "  Ctrl+S           Save Project\n"
                "  Ctrl+Shift+S     Save As...\n"
                "  Ctrl+Q           Quit\n"
                "\n"
                "EDIT\n"
                "  Ctrl+Z           Undo\n"
                "  Ctrl+Y           Redo\n"
                "\n"
                "VIEW\n"
                "  Tab              Switch Session / Arrangement\n"
                "  M                Toggle Mixer\n"
                "  D                Toggle Detail Panel\n"
                "  F11              Toggle Visual Output Fullscreen\n"
                "\n"
                "TRANSPORT\n"
                "  Space            Play / Stop (launches default clips)\n"
                "  Home             Return to Zero\n"
                "  + / =            Tempo +1 BPM\n"
                "  - / _            Tempo -1 BPM\n"
                "\n"
                "SESSION VIEW\n"
                "  Arrows           Move clip selection\n"
                "  Shift+Arrows     Move controller grid region\n"
                "  Enter            Launch / Stop selected clip\n"
                "  Delete / Bksp    Clear selected clip\n"
                "  Ctrl+C / X / V   Copy / Cut / Paste clip\n"
                "  Ctrl+D           Duplicate clip to next empty slot\n"
                "  G                Toggle controller grid overlay\n"
                "  Ins              Insert Scene below selection\n"
                "\n"
                "ARRANGEMENT VIEW\n"
                "  L                Toggle Loop\n"
                "  F                Toggle Follow Playhead\n"
                "  [                Set Loop Start at playhead\n"
                "  ]                Set Loop End at playhead\n"
                "  Ctrl+D           Duplicate selection\n"
                "  Delete / Bksp    Delete selection\n"
                "\n"
                "OTHER\n"
                "  Esc              Close menu / exit fullscreen / quit";
            ui::fw2::DialogButton ok;
            ok.label   = "OK";
            ok.primary = true;
            ok.cancel  = true;
            spec.buttons.push_back(std::move(ok));
            ui::fw2::Dialog::show(std::move(spec));
        }),
    });
}

void App::buildWidgetTree() {
    using namespace ui::fw2;

    m_rootLayout = std::make_unique<FlexBox>(Direction::Column);
    m_rootLayout->setAlign(Align::Stretch);

    m_transportPanelOwner    = std::make_unique<TransportPanel>();
    m_sessionPanelOwner      = std::make_unique<SessionPanel>();
    m_arrangementPanelOwner  = std::make_unique<ArrangementPanel>();
    m_mixerPanelOwner        = std::make_unique<MixerPanel>();
    m_browserPanelOwner      = std::make_unique<BrowserPanel>();
    m_returnMasterPanelOwner = std::make_unique<ReturnMasterPanel>();
    m_contentGridOwner       = std::make_unique<ContentGrid>();
    m_detailPanelOwner       = std::make_unique<DetailPanelWidget>();
    m_visualParamsPanelOwner = std::make_unique<VisualParamsPanel>();
    m_pianoRollOwner         = std::make_unique<PianoRollPanel>();
    // v1 AboutDialog retired — fw2::Dialog drives the Help → About
    // prompt inline from the menu handler.
    // v1 ConfirmDialogWidget retired — fw2::ConfirmDialog handles
    // confirm prompts on the Modal layer (LayerStack).
    // v2 TextInputDialog, PreferencesDialog, and ExportDialog are
    // value-typed members of App — nothing to allocate here.

    // ContentGrid fills remaining space in the rootLayout column.
    m_contentGridOwner->setSizePolicy(SizePolicy::flexMin(1.0f, 200.0f));

    // Convenience raw pointers — main code uses these.
    m_transportPanel    = m_transportPanelOwner.get();
    m_sessionPanel      = m_sessionPanelOwner.get();
    m_arrangementPanel  = m_arrangementPanelOwner.get();
    m_mixerPanel        = m_mixerPanelOwner.get();
    m_browserPanel      = m_browserPanelOwner.get();
    m_returnMasterPanel = m_returnMasterPanelOwner.get();
    m_contentGrid       = m_contentGridOwner.get();
    m_detailPanel       = m_detailPanelOwner.get();
    m_visualParamsPanel = m_visualParamsPanelOwner.get();
    m_pianoRoll         = m_pianoRollOwner.get();

    m_preferencesDialog.setOnResult([this](ui::fw2::PreferencesResult result) {
        if (result == ui::fw2::PreferencesResult::OK) {
            auto& s = m_preferencesDialog.state();
            const auto& oldCfg = m_audioEngine.config();
            bool audioChanged = (s.sampleRate != oldCfg.sampleRate ||
                                 s.bufferSize != oldCfg.framesPerBuffer ||
                                 s.selectedOutputDevice != oldCfg.outputDevice ||
                                 s.selectedInputDevice != oldCfg.inputDevice);
            // Also restart when the engine is currently disconnected
            // (device was unplugged) — user might be re-picking the
            // same config to recover, so no change-detection hit.
            if (!m_audioEngine.hasStream()) audioChanged = true;
            if (audioChanged) {
                audio::AudioEngineConfig newCfg = oldCfg;
                newCfg.sampleRate = s.sampleRate;
                newCfg.framesPerBuffer = s.bufferSize;
                newCfg.outputDevice = s.selectedOutputDevice;
                newCfg.inputDevice = s.selectedInputDevice;
                m_audioEngine.stop();
                m_audioEngine.shutdown();
                m_audioEngine.init(newCfg);
                m_audioEngine.start();
            }

            m_midiEngine.shutdown();
            m_midiEngine.refreshPorts();
            for (int i : s.enabledMidiInputs)
                m_midiEngine.openInputPort(i);
            for (int i : s.enabledMidiOutputs)
                m_midiEngine.openOutputPort(i);

            m_settings.outputDevice = s.selectedOutputDevice;
            m_settings.inputDevice = s.selectedInputDevice;
            m_settings.sampleRate = s.sampleRate;
            m_settings.bufferSize = s.bufferSize;
            m_settings.defaultLaunchQuantize = static_cast<int>(s.defaultLaunchQuantize);
            m_settings.defaultRecordQuantize = static_cast<int>(s.defaultRecordQuantize);
            m_settings.enabledMidiInputs = s.enabledMidiInputs;
            m_settings.enabledMidiOutputs = s.enabledMidiOutputs;
            m_settings.metronomeVolume = s.metronomeVolume;
            m_settings.metronomeMode = s.metronomeMode;
            m_settings.countInBars = s.countInBars;
            m_settings.metronomeVisualStyle = s.metronomeVisualStyle;

            // Plugin Delay Compensation toggle. Persisted in
            // settings JSON; applied to the live mixer immediately.
            if (s.latencyCompensation != m_settings.latencyCompensation) {
                LOG_INFO("User", "preferences: latency compensation → %s",
                         s.latencyCompensation ? "enabled" : "disabled");
            }
            m_settings.latencyCompensation = s.latencyCompensation;
            m_audioEngine.mixer().setPdcEnabled(s.latencyCompensation);

            // Master soft-clip oversampling toggle.
            m_settings.masterOversample = s.masterOversample;
            m_audioEngine.mixer().setMasterOversample(s.masterOversample);

            // Ableton Link toggle. Apply to the live engine and
            // persist for next launch.
            if (s.linkEnabled != m_settings.linkEnabled) {
                LOG_INFO("User", "preferences: ableton link → %s",
                         s.linkEnabled ? "enabled" : "disabled");
            }
            m_settings.linkEnabled = s.linkEnabled;
            m_audioEngine.linkManager().enable(s.linkEnabled);

            if (s.linkStartStopSync != m_settings.linkStartStopSync) {
                LOG_INFO("User", "preferences: link start/stop sync → %s",
                         s.linkStartStopSync ? "enabled" : "disabled");
            }
            m_settings.linkStartStopSync = s.linkStartStopSync;
            m_audioEngine.linkManager().enableStartStopSync(s.linkStartStopSync);

            // Apply the UI font scale if it changed. setTheme() bumps
            // the fw2 UIContext epoch which invalidates every widget's
            // measure cache, so the new sizes propagate on the next
            // layout pass.
            if (s.fontScale != m_settings.fontScale) {
                m_settings.fontScale = s.fontScale;
                ui::fw2::Theme t;
                const float sc = std::max(0.5f, std::min(3.0f, s.fontScale));
                t.metrics.fontSize      *= sc;
                t.metrics.fontSizeSmall *= sc;
                t.metrics.fontSizeLarge *= sc;
                ui::fw2::setTheme(std::move(t));
            }

            // Apply metronome settings to audio engine
            m_audioEngine.sendCommand(audio::MetronomeSetVolumeMsg{s.metronomeVolume});
            m_audioEngine.sendCommand(audio::MetronomeSetModeMsg{s.metronomeMode});
            m_audioEngine.sendCommand(audio::TransportSetCountInMsg{s.countInBars});
            m_transportPanel->setCountInBars(s.countInBars);
            m_transportPanel->setMetronomeVisualStyle(s.metronomeVisualStyle);
            m_transportPanel->setLinkAllowed(s.linkEnabled);

            util::AppSettings::save(m_settings);
        }
    });

    // Init arrangement panel
    m_arrangementPanel->init(&m_project, &m_audioEngine, &m_undoManager);
    m_arrangementPanel->setOnTrackClick([this](int t) {
        LOG_INFO("User", "selectTrack %d (via arrangement)", t);
        m_selectedTrack = t;
        m_detailTarget = DetailTarget::Track;
        m_sessionPanel->setSelectedTrack(t);
        if (m_showDetailPanel) updateDetailForSelectedTrack();
    });
    m_arrangementPanel->setOnPlayheadClick([this](double beat) {
        double sr = m_audioEngine.sampleRate();
        double bpm = m_audioEngine.transport().bpm();
        int64_t samples = static_cast<int64_t>(beat * 60.0 / bpm * sr);
        m_audioEngine.sendCommand(audio::TransportSetPositionMsg{samples});
    });
    m_arrangementPanel->setOnClipChange([this](int trackIdx) {
        syncArrangementClipsToEngine(trackIdx);
    });
    m_arrangementPanel->setOnTrackArrToggle([this](int trackIdx, bool active) {
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{trackIdx, active});
        if (active) syncArrangementClipsToEngine(trackIdx);
    });
    m_arrangementPanel->setOnLoopChange([this](bool enabled, double start, double end) {
        m_audioEngine.sendCommand(audio::TransportSetLoopEnabledMsg{enabled});
        m_audioEngine.sendCommand(audio::TransportSetLoopRangeMsg{start, end});
    });
    m_arrangementPanel->setOnClipContextMenu(
        [this](int track, int clipIdx, float mx, float my) {
            showArrangementClipContextMenu(track, clipIdx, mx, my);
        });
    m_arrangementPanel->setOnTrackContextMenu(
        [this](int track, float mx, float my) {
            // Same track menu (add effect/instrument, etc.) the Session
            // view exposes on its track headers — now also reachable from
            // the Arrangement-view track headers.
            showTrackContextMenu(track, mx, my);
        });
    m_arrangementPanel->setOnClipDoubleClick(
        [this](int track, int clipIdx) {
            auto& clips = m_project.track(track).arrangementClips;
            if (clipIdx < 0 || clipIdx >= static_cast<int>(clips.size())) return;
            auto& ac = clips[clipIdx];
            if (ac.type == ArrangementClip::Type::Midi && ac.midiClip) {
                m_pianoRoll->setClip(ac.midiClip.get(), track,
                    ui::fw2::PianoRollPanel::Source::Arrangement);
                {
                    // DrumRoll mode: tracks hosting a DrumSynth get
                    // the 8-row drum view instead of the chromatic
                    // piano roll. Cheap to check at open time and
                    // doesn't touch the clip data — only the display
                    // layer.
                    auto* di = m_audioEngine.instrument(track);
                    m_pianoRoll->setDrumMode(
                        di && std::string(di->id()) == "drumsynth");
                }
                // Sync arrangement-clip duration from the MIDI clip
                // whenever it's edited (x2 / /2 / Clr / loop-drag /
                // autoExtend on note-draw). Without this the grid
                // representation would drift away from the audible
                // length of the clip.
                m_pianoRoll->setOnLengthChanged([this, track, clipIdx]() {
                    auto& cs = m_project.track(track).arrangementClips;
                    if (clipIdx < 0 || clipIdx >= static_cast<int>(cs.size())) return;
                    auto& a = cs[clipIdx];
                    if (a.type != ArrangementClip::Type::Midi || !a.midiClip) return;
                    a.lengthBeats = a.midiClip->lengthBeats();
                    m_project.track(track).sortArrangementClips();
                    m_project.updateArrangementLength();
                    syncArrangementClipsToEngine(track);
                    markDirty();
                });
                m_pianoRoll->setOpen(true);
            }
        });
    m_arrangementPanel->setVisible(false); // start in session view

    // Wire the 4-quadrant layout. ContentGrid and every panel including
    // ArrangementPanel are fw2 now — panels plug in directly and the
    // topLeft slot swaps between session and arrangement in setViewMode.
    m_contentGrid->setChildren(m_sessionPanel, m_browserPanel,
                               m_mixerPanel, m_returnMasterPanel);
    m_rootLayout->addChild(&m_menuBar);
    m_rootLayout->addChild(m_transportPanel);
    m_rootLayout->addChild(m_contentGrid);
    m_rootLayout->addChild(m_detailPanel);
    m_rootLayout->addChild(m_visualParamsPanel);
    m_visualParamsPanel->setVisible(false);
    m_visualParamsPanel->setDetailPanel(m_detailPanel);
    m_rootLayout->addChild(m_pianoRoll);

    // When a custom-named knob on the panel is turned, update both the
    // live VisualEngine layer and the clip's persistent store.
    auto persistParamValue = [this](const std::string& name, float v) {
        int track = m_selectedTrack;
        auto* slot = m_project.getSlot(track, m_project.track(track).defaultScene);
        if (!slot || !slot->visualClip) return;
        bool found = false;
        for (auto& kv : slot->visualClip->firstPassParamValues()) {
            if (kv.first == name) { kv.second = v; found = true; break; }
        }
        if (!found)
            slot->visualClip->firstPassParamValues().emplace_back(name, v);
        markDirty();
    };

    m_visualParamsPanel->setOnChanged(
        [this, persistParamValue](const std::string& name, float v) {
            m_visualEngine.setLayerParam(m_selectedTrack, name, v);
            persistParamValue(name, v);
        });

    // A..H knob row writes the per-track macro device. The per-frame
    // pump in update() then propagates the value into the visual
    // engine's knob[idx] and walks the macro's mapping list. The
    // panel's A..H knobs are visually the macros 0..7.
    m_visualParamsPanel->setOnKnobChanged(
        [this](int idx, float v) {
            if (m_selectedTrack < 0 ||
                m_selectedTrack >= m_project.numTracks()) return;
            if (idx < 0 || idx >= MacroDevice::kNumMacros) return;
            m_project.track(m_selectedTrack).macros.values[idx] = v;
            // Push to the engine right away so the visual output
            // reacts on the same frame as the click.
            m_visualEngine.setLayerKnob(m_selectedTrack, idx, v);
            markDirty();
        });

    // Visual A-H knob touch → AutoParamTouchMsg with TargetType::
    // VisualKnob. Lets the AutomationEngine record breakpoints when
    // GlobalAutoRecord + the visual track's AutoMode are armed.
    // The knob itself fires (touching=true with startV) then
    // (touching=false with endV) at drag-release; AutomationEngine
    // records both end values + whatever is current at each
    // process() block in between.
    m_visualParamsPanel->setOnKnobTouch(
        [this](int idx, float v, bool touching) {
            if (m_selectedTrack < 0 ||
                m_selectedTrack >= m_project.numTracks()) return;
            // VisualKnob target: chainIndex unused, paramIndex = idx
            // (the A..H knob index, 0..7). Mirrors how PadFx etc.
            // route through AutoParamTouchMsg.
            m_audioEngine.sendCommand(audio::AutoParamTouchMsg{
                m_selectedTrack,
                static_cast<uint8_t>(automation::TargetType::VisualKnob),
                /*chainIndex*/0, idx, v, touching});
        });

    // Right-click on an A..H knob → LFO configuration context menu.
    m_visualParamsPanel->setOnKnobRightClick(
        [this](int idx, float mx, float my) {
            showVisualKnobLFOMenu(idx, mx, my);
        });

    // Post-FX knob change → update engine, mark project dirty.
    m_visualParamsPanel->setOnPostFXChanged(
        [this](int fxIdx, const std::string& name, float v) {
            m_visualEngine.setPostFXParam(fxIdx, name, v);
            markDirty();
        });
    // Post-FX remove (× button) — drops it from the chain and refreshes UI.
    m_visualParamsPanel->setOnPostFXRemove(
        [this](int fxIdx) {
            m_visualEngine.removePostFX(fxIdx);
            markDirty();
            // Rebuild panel right away so the removed card disappears.
            updateDetailForSelectedTrack();
        });

    // Shader chain (per-clip) callbacks. Mutate the VisualClip in-place
    // and re-launch the layer so the live engine state matches the new
    // chain. Targets the track's active scene, falling back to the
    // selected scene when no clip is launched.
    auto chainTargetSlot = [this]() -> ClipSlot* {
        const int sc = (m_project.track(m_selectedTrack).defaultScene >= 0)
            ? m_project.track(m_selectedTrack).defaultScene
            : m_selectedScene;
        return m_project.getSlot(m_selectedTrack, sc);
    };
    auto reloadVisualLayer = [this](ClipSlot* slot) {
        if (!slot || !slot->visualClip) return;
        launchVisualClipData(m_selectedTrack, *slot->visualClip,
                              slot->visualClip->firstShaderPath());
    };

    m_visualParamsPanel->setOnChainPassChanged(
        [this](int passIdx, const std::string& name, float v) {
            // Effect chain lives on the *track* now — every clip on
            // this track shares the same chain (matches audio FX UX).
            if (m_selectedTrack < 0 ||
                m_selectedTrack >= m_project.numTracks()) return;
            auto& chain = m_project.track(m_selectedTrack).visualEffectChain;
            if (passIdx < 0 || passIdx >= (int)chain.size()) return;
            // Persist into the project model so save / clip-relaunch
            // see the new value.
            bool found = false;
            for (auto& kv : chain[passIdx].paramValues) {
                if (kv.first == name) { kv.second = v; found = true; break; }
            }
            if (!found) chain[passIdx].paramValues.emplace_back(name, v);
            // Push the change into the live engine via the cheap path
            // — just updates the cached uniform value the next render
            // reads. Crucially does NOT recompile any shaders, so a
            // continuous drag on a chain knob doesn't murder the
            // frame budget the way a full relaunch would (one
            // recompile per drag delta == ~30 program builds/sec).
            m_visualEngine.setLayerChainPassParam(m_selectedTrack,
                                                    passIdx, name, v);
            markDirty();
        });

    m_visualParamsPanel->setOnChainPassBypassToggle(
        [this](int passIdx, bool bypassed) {
            if (m_selectedTrack < 0 ||
                m_selectedTrack >= m_project.numTracks()) return;
            auto& chain = m_project.track(m_selectedTrack).visualEffectChain;
            if (passIdx < 0 || passIdx >= (int)chain.size()) return;
            chain[passIdx].bypassed = bypassed;
            // Cheap engine-side flip — no recompile, no relink. The
            // render loop just starts/stops including this pass.
            m_visualEngine.setLayerChainPassBypass(m_selectedTrack,
                                                     passIdx, bypassed);
            markDirty();
        });

    m_visualParamsPanel->setOnChainPassReorder(
        [this, chainTargetSlot, reloadVisualLayer](int from, int to) {
            if (m_selectedTrack < 0 ||
                m_selectedTrack >= m_project.numTracks()) return;
            auto& chain = m_project.track(m_selectedTrack).visualEffectChain;
            const int n = static_cast<int>(chain.size());
            if (from < 0 || from >= n) return;
            // `to` is an insertion slot in [0, n] — when `to > from`
            // the slot index shifts down by one after we erase the
            // source, which the panel guarantees by skipping
            // no-op cases (target == from / target == from + 1).
            int dst = (to > from) ? to - 1 : to;
            if (dst < 0) dst = 0;
            if (dst >= n) dst = n - 1;
            if (dst == from) return;
            visual::ShaderPass p = std::move(chain[from]);
            chain.erase(chain.begin() + from);
            chain.insert(chain.begin() + dst, std::move(p));
            // Re-launch to push the new chain order into the engine.
            // Reorders are infrequent (user gesture) so the recompile
            // cost is acceptable.
            auto* slot = chainTargetSlot();
            if (slot) reloadVisualLayer(slot);
            markDirty();
            updateDetailForSelectedTrack();
        });

    m_visualParamsPanel->setOnChainPassRemove(
        [this, chainTargetSlot, reloadVisualLayer](int passIdx) {
            if (m_selectedTrack < 0 ||
                m_selectedTrack >= m_project.numTracks()) return;
            auto& chain = m_project.track(m_selectedTrack).visualEffectChain;
            if (passIdx < 0 || passIdx >= (int)chain.size()) return;
            chain.erase(chain.begin() + passIdx);
            auto* slot = chainTargetSlot();
            if (slot) reloadVisualLayer(slot);
            markDirty();
            updateDetailForSelectedTrack();
        });

    m_visualParamsPanel->setOnChainAdd(
        [this](float mx, float my) {
            showShaderLibraryMenu(mx, my);
        });

    // Right-click on a source-shader (clip-custom) knob → open the
    // "Map to Macro N" context menu rooted at the click position.
    m_visualParamsPanel->setOnCustomKnobRightClick(
        [this](const std::string& paramName, float mx, float my) {
            MacroTarget t;
            t.kind      = MacroTarget::Kind::VisualSourceParam;
            t.paramName = paramName;
            showMacroMappingMenu(t, mx, my);
        });

    // Right-click on a chain-pass knob → same menu, with the chain
    // slot index baked into the target so the mapping survives clip
    // switches and chain reorders.
    m_visualParamsPanel->setOnChainPassKnobRightClick(
        [this](int passIdx, const std::string& paramName,
               float mx, float my) {
            MacroTarget t;
            t.kind      = MacroTarget::Kind::VisualChainParam;
            t.index     = passIdx;
            t.paramName = paramName;
            showMacroMappingMenu(t, mx, my);
        });

    // Synchronized horizontal scrolling between session clips and mixer strips
    m_sessionPanel->setOnScrollChanged([this](float sx) {
        m_mixerPanel->setScrollX(sx);
    });
    m_mixerPanel->setOnScrollChanged([this](float sx) {
        m_sessionPanel->setScrollX(sx);
    });

    // Track rename callbacks (shared undo handler)
    auto renameHandler = [this](int track, const std::string& oldName, const std::string& newName) {
        markDirty();
        m_undoManager.push({"Rename Track",
            [this, track, oldName]{ m_project.track(track).name = oldName; markDirty(); },
            [this, track, newName]{ m_project.track(track).name = newName; markDirty(); },
            ""});
    };
    m_sessionPanel->setOnTrackRenamed(renameHandler);
    m_arrangementPanel->setOnTrackRenamed(renameHandler);

    // Status-pip feed for live clips. Returns the engine's live-source
    // state (0..3) for the currently-launched scene on a track, or -1
    // for any other slot so the grid paints a neutral pip.
    m_sessionPanel->setOnQueryLiveState(
        [this](int track, int scene) -> int {
            if (m_project.track(track).defaultScene != scene) return -1;
            return static_cast<int>(m_visualEngine.getLayerLiveState(track));
        });

    // Launching a Visual clip loads its shader into that track's layer and
    // sets the layer's audio source for iAudioLevel / bands / kick. Each
    // visual track owns one compositor layer; track volume acts as opacity.
    m_sessionPanel->setOnStopVisualClip(
        [this](int track) {
            if (track < 0 || track >= kMaxTracks) return;
            m_visualEngine.clearLayer(track);
            m_activeArrVisualClip[track]  = -1;
            m_visualLaunchBeat[track]     = kNoVisualLaunch;
            m_visualLaunchScene[track]    = -1;
        });

    m_sessionPanel->setOnLaunchVisualClip(
        [this](int track, int scene, const std::string& /*shaderPath*/) {
            // Defer to the slot's launchQuantize so the video starts in sync
            // with the quantized audio/MIDI clips in the same scene rather
            // than immediately. A lone visual launch has nothing to sync to
            // while stopped, so only defer when already playing. (shaderPath
            // is re-derived from the slot.)
            launchVisualClipQuantized(track, scene,
                                      m_audioEngine.transport().isPlaying());
        });

    m_sessionPanel->setOnStopAllClips([this] { stopAllClips(); });

    // ─── Wire v2 framework ──────────────────────────────────────────
    // FontAdapter bridges v1 Font → fw2 TextMetrics. Register it + the
    // renderer in fw2::UIContext, publish that as the global context
    // for v2 widgets, then populate the painter registry so that
    // Widget::render() can dispatch paint calls by typeid.
    m_fw2FontAdapter = std::make_unique<ui::fw2::FontAdapter>(&m_font);
    m_fw2Context.renderer    = &m_renderer;
    m_fw2Context.textMetrics = m_fw2FontAdapter.get();
    m_fw2Context.layerStack  = &m_fw2LayerStack;
    ui::fw2::UIContext::setGlobal(&m_fw2Context);
    ui::fw2::registerAllFw2Painters();
}

void App::computeLayout() {
    using namespace ui::fw2;

    int w = m_mainWindow.getWidth();
    int h = m_mainWindow.getHeight();

    // Update panel visibility — single source of truth now (fw2 panel
    // directly), since the v1 wrappers are gone.
    m_detailPanel->setWindowHeight(static_cast<float>(h));
    const bool selectedIsVisual =
        m_selectedTrack >= 0 && m_selectedTrack < m_project.numTracks() &&
        m_project.track(m_selectedTrack).type == Track::Type::Visual;
    // Detail panel + Visual-params panel share a slot: never both at once.
    const bool showDetail = m_showDetailPanel && !selectedIsVisual;
    const bool showVisual = m_showDetailPanel && selectedIsVisual;
    m_detailPanel->setVisible(showDetail);
    m_visualParamsPanel->setVisible(showVisual);
    m_pianoRoll->setVisible(m_pianoRoll->isOpen());

    // Per-track latency readout. The Mixer-side accessor is cheap
    // (8 effects × bypass-check + virtual call); pushing it every
    // frame keeps the readout responsive when the user drags the
    // Lookahead knob on a NoiseGate / Limiter mid-playback.
    if (showDetail && m_selectedTrack >= 0 &&
        m_selectedTrack < m_project.numTracks()) {
        const int latency = m_audioEngine.mixer().trackLatencySamples(m_selectedTrack);
        m_detailPanel->setTrackLatencySamples(
            latency, m_audioEngine.sampleRate());
    } else {
        m_detailPanel->setTrackLatencySamples(0, 48000.0);
    }

    // ContentGrid manages session + mixer + browser + returns visibility.
    m_mixerPanel->setVisible(m_showMixer);
    m_returnMasterPanel->setVisible(m_showMixer);
    m_returnMasterPanel->setShowReturns(m_showReturns);

    Constraints c = Constraints::tight(static_cast<float>(w), static_cast<float>(h));
    m_rootLayout->measure(c, m_fw2Context);
    m_rootLayout->layout(Rect{0, 0, static_cast<float>(w), static_cast<float>(h)}, m_fw2Context);
}


void App::updateDetailForSelectedTrack() {
    if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) {
        m_detailPanel->clear();
        m_browserPanel->setFollowAction(nullptr);
        return;
    }

    // Visual track: rebuild the params panel (computeLayout handles show/hide).
    if (m_project.track(m_selectedTrack).type == Track::Type::Visual) {
        auto params = m_visualEngine.getLayerParams(m_selectedTrack);
        std::string shaderLabel;
        int scene = m_project.track(m_selectedTrack).defaultScene;
        // If the layer hasn't been launched yet, parse the selected
        // clip's shader directly so the user can see and edit
        // modelSpinY / speed / etc. before hitting play.
        if (scene < 0) scene = m_selectedScene;
        if (params.empty() && scene >= 0) {
            auto* slot = m_project.getSlot(m_selectedTrack, scene);
            if (slot && slot->visualClip) {
                std::string shaderPath = slot->visualClip->firstShaderPath();
                // Same passthrough fallbacks the launch path uses, so
                // the pre-launch preview shows the same knob set the
                // clip will actually get.
                if (shaderPath.empty() && !slot->visualClip->modelPath.empty())
                    shaderPath = "assets/shaders/model_passthrough.frag";
                else if (shaderPath.empty() &&
                          (!slot->visualClip->videoPath.empty() ||
                           slot->visualClip->liveInput))
                    shaderPath = "assets/shaders/video_passthrough.frag";
                if (!shaderPath.empty()) {
                    params = visual::VisualEngine::parseShaderFileParams(
                        resolveShaderPath(shaderPath));
                    // Overlay any persisted paramValues so the knobs
                    // show the user's last saved positions, not the
                    // bare @range defaults.
                    for (auto& info : params) {
                        for (auto& kv : slot->visualClip->firstPassParamValues()) {
                            if (kv.first == info.name) {
                                info.value = kv.second; break;
                            }
                        }
                    }
                }
            }
        }
        if (scene >= 0) {
            auto* slot = m_project.getSlot(m_selectedTrack, scene);
            if (slot && slot->visualClip) shaderLabel = slot->visualClip->name;
        }
        m_visualParamsPanel->rebuildCustom(params, shaderLabel);
        // A..H values come from the track-level macro device — that's
        // the persistent source of truth in Phase 4.1. The engine's
        // per-layer knobValues mirror this each frame; reading from
        // the project here keeps the panel correct even when no clip
        // has been launched (no engine layer exists yet).
        {
            const auto& macros = m_project.track(m_selectedTrack).macros;
            float knobs[8];
            for (int i = 0; i < MacroDevice::kNumMacros; ++i)
                knobs[i] = macros.values[i];
            m_visualParamsPanel->setKnobValues(knobs);
        }

        // Build the post-fx chain snapshot: (displayName, params) per effect.
        std::vector<std::pair<std::string,
            std::vector<visual::VisualEngine::LayerParamInfo>>> fxChain;
        for (int i = 0; i < m_visualEngine.numPostFX(); ++i) {
            std::filesystem::path p(m_visualEngine.postFXPath(i));
            fxChain.emplace_back(p.stem().string(),
                                  m_visualEngine.getPostFXParams(i));
        }
        m_visualParamsPanel->rebuildPostFX(fxChain);

        // Build the effect-chain snapshot for the panel — one entry
        // per effect on the selected *track* (chain-on-track, not on
        // clip). Each entry carries name + bypass state + the
        // shader's @range uniforms overlaid with the saved per-pass
        // values, so the panel can render the audio-FX-style card
        // (bypass pill + name + × + knobs) without sidecar lookups.
        std::vector<ui::fw2::VisualParamsPanel::ChainPassSnap> shaderChainSnap;
        {
            const auto& trkChain =
                m_project.track(m_selectedTrack).visualEffectChain;
            for (const auto& pass : trkChain) {
                if (pass.shaderPath.empty()) continue;
                std::filesystem::path pp(pass.shaderPath);
                auto pParams = visual::VisualEngine::parseShaderFileParams(
                    resolveShaderPath(pass.shaderPath));
                for (auto& info : pParams) {
                    for (const auto& kv : pass.paramValues) {
                        if (kv.first == info.name) {
                            info.value = kv.second; break;
                        }
                    }
                }
                ui::fw2::VisualParamsPanel::ChainPassSnap snap;
                snap.name     = pp.stem().string();
                snap.bypassed = pass.bypassed;
                snap.params   = std::move(pParams);
                shaderChainSnap.push_back(std::move(snap));
            }
        }
        m_visualParamsPanel->rebuildShaderChain(shaderChainSnap);

        // Wire the selected visual-clip slot's follow-action block
        // into the browser-panel editor (the browser panel is shown
        // for visual tracks too, via the Clip tab). Without this the
        // editor stays in its "Select a clip slot" placeholder.
        auto* vslot = m_project.getSlot(m_selectedTrack, m_selectedScene);
        const bool vhasClip = vslot && vslot->visualClip;
        m_browserPanel->setFollowAction(vhasClip ? &vslot->followAction
                                                    : nullptr);
        // Also expose the clip's automation lanes so users can draw
        // per-knob envelopes under the Follow Actions in the Clip
        // tab. The shader's @range uniforms join A..H in the
        // dropdown so any of them can be automated per clip.
        if (vhasClip) {
            std::vector<std::string> paramNames;
            paramNames.reserve(params.size());
            for (const auto& p : params) paramNames.push_back(p.name);
            m_browserPanel->setVisualClipAutomation(
                &vslot->clipAutomation->lanes,
                m_selectedTrack,
                vslot->visualClip->lengthBeats > 0.25
                    ? vslot->visualClip->lengthBeats : 4.0,
                std::move(paramNames));
        } else {
            m_browserPanel->setVisualClipAutomation(nullptr, -1, 4.0, {});
        }
        return;
    }

    // Non-visual tracks: clear the visual-clip envelope editor so
    // that its leftover state doesn't re-appear when switching back
    // to a visual track.
    m_browserPanel->setVisualClipAutomation(nullptr, -1, 4.0, {});

    m_detailPanel->setTrackIndex(m_selectedTrack);

    // Update browser panel follow action for any clip slot
    auto* slot = m_project.getSlot(m_selectedTrack, m_selectedScene);
    bool hasClip = slot && (slot->audioClip || slot->midiClip);
    m_browserPanel->setFollowAction(hasClip ? &slot->followAction : nullptr);

    // Check if the selected slot has an audio clip — show clip detail view
    auto* audioClip = slot ? slot->audioClip.get() : nullptr;
    if (audioClip) {
        auto* fxChain = &m_audioEngine.mixer().trackEffects(m_selectedTrack);
        m_detailPanel->setAudioClip(audioClip, fxChain,
                                     static_cast<int>(m_audioEngine.sampleRate()));
        m_detailPanel->setClipAutomation(&slot->clipAutomation->lanes, m_selectedTrack);
        return;
    }

    auto* midiChain = &m_audioEngine.midiEffectChain(m_selectedTrack);
    auto* inst = m_audioEngine.instrument(m_selectedTrack);
    auto* fxChain = &m_audioEngine.mixer().trackEffects(m_selectedTrack);

    m_detailPanel->setDeviceChain(midiChain, inst, fxChain);

    // Wire clip automation if a MIDI clip is selected
    if (slot && slot->midiClip) {
        m_detailPanel->setClipAutomation(&slot->clipAutomation->lanes, m_selectedTrack);
    }
}

void App::updateDetailForReturnBus(int bus) {
    if (bus < 0 || bus >= kMaxReturnBuses) { m_detailPanel->clear(); return; }
    m_detailPanel->setTrackIndex(-1);  // no track association
    auto* fxChain = &m_audioEngine.mixer().returnEffects(bus);
    m_detailPanel->setDeviceChain(nullptr, nullptr, fxChain);
}

void App::updateDetailForMaster() {
    m_detailPanel->setTrackIndex(-1);
    auto* fxChain = &m_audioEngine.mixer().masterEffects();
    m_detailPanel->setDeviceChain(nullptr, nullptr, fxChain);
}


} // namespace yawn
