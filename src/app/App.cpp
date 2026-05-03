#include "app/App.h"
#include "Version.h"
#include "visual/LiveInputEnum.h"
#include "ui/framework/v2/Fw2Painters.h"
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/Dialog.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/V1MenuBridge.h"
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
#include "audio/OfflineRenderer.h"
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

// Helper: load audio file, resample to engine rate, convert to interleaved
static bool loadInterleaved(audio::AudioEngine& engine, const std::string& path,
                            std::vector<float>& outInterleaved, int& outFrames, int& outChannels) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(engine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, engine.sampleRate());
        if (!buffer) return false;
    }

    outFrames = buffer->numFrames();
    outChannels = buffer->numChannels();
    outInterleaved.resize(static_cast<size_t>(outFrames) * outChannels);
    for (int ch = 0; ch < outChannels; ++ch) {
        const float* src = buffer->channelData(ch);
        for (int i = 0; i < outFrames; ++i)
            outInterleaved[static_cast<size_t>(i) * outChannels + ch] = src[i];
    }
    return true;
}

// Helper: extract filename without extension from a path
static std::string fileNameFromPath(const std::string& path) {
    // Find last separator (handle both / and \ for cross-platform paths).
    auto sep = path.find_last_of("/\\");
    std::string name = (sep == std::string::npos) ? path : path.substr(sep + 1);
    // Strip extension.
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name.erase(dot);
    return name;
}

App::~App() {
    shutdown();
}

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
        M::separator(),
        M::item("Quit",         [this]() { m_running = false; }, "Ctrl+Q"),
    });

    // Edit menu
    m_menuBar.addMenu("Edit", {
        M::item("Undo", [this]() { if (m_undoManager.canUndo()) { m_undoManager.undo(); markDirty(); } }, "Ctrl+Z"),
        M::item("Redo", [this]() { if (m_undoManager.canRedo()) { m_undoManager.redo(); markDirty(); } }, "Ctrl+Y"),
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
        auto makeDeleteTrack = []() -> MenuEntry {
            // v1 had "Delete Track" as a disabled placeholder (nullptr
            // action + enabled=false). v2 Menu::item can't set enabled
            // directly; build the entry manually.
            MenuEntry e;
            e.label = "Delete Track";
            e.enabled = false;
            return e;
        };
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
            makeDeleteTrack(),
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

    // MIDI menu — all three items are currently no-op placeholders.
    {
        auto disabled = [](std::string label) -> MenuEntry {
            MenuEntry e;
            e.label = std::move(label);
            e.enabled = false;
            return e;
        };
        m_menuBar.addMenu("MIDI", {
            M::item("MIDI Devices",  nullptr),
            M::item("MIDI Sync",     nullptr),
            M::separator(),
            M::item("Key Velocity: Soft (25%)",  [this]() { m_virtualKeyboard.setVelocity(32); }),
            M::item("Key Velocity: Medium (50%)", [this]() { m_virtualKeyboard.setVelocity(64); }),
            M::item("Key Velocity: Normal (75%)", [this]() { m_virtualKeyboard.setVelocity(96); }),
            M::item("Key Velocity: Hard (100%)",  [this]() { m_virtualKeyboard.setVelocity(127); }),
            M::separator(),
            disabled("Link Settings"),
        });
    }

    // Help menu
    m_menuBar.addMenu("Help", {
        M::item("About Y.A.W.N", []() {
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
            ui::fw2::DialogButton ok;
            ok.label   = "OK";
            ok.primary = true;
            ok.cancel  = true;
            spec.buttons.push_back(std::move(ok));
            ui::fw2::Dialog::show(std::move(spec));
        }),
        M::item("Keyboard Shortcuts", nullptr),
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
        [this](int track, int scene, const std::string& shaderPath) {
            auto* slot = m_project.getSlot(track, scene);
            if (!slot || !slot->visualClip) return;
            launchVisualClipData(track, *slot->visualClip, shaderPath);
            // Session launches use wall-clock time — iTime starts at
            // 0 on launch and advances with real time, regardless of
            // where the transport sits. Explicitly set to override any
            // previous arrangement mode on the same track.
            m_visualEngine.setLayerWallClock(track);
            // Stamp the launch beat so the follow-action poller knows
            // when barCount bars have elapsed on this clip.
            if (track >= 0 && track < kMaxTracks) {
                m_visualLaunchBeat[track]  =
                    m_audioEngine.transport().positionInBeats();
                m_visualLaunchScene[track] = scene;
            }
        });

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

void App::showTrackContextMenu(int trackIndex, float mx, float my) {
    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
    auto& track = m_project.track(trackIndex);

    std::vector<ui::ContextMenu::Item> items;

    // Track type selection (with confirmation dialog)
    items.push_back({"Set as Audio Track", [this, trackIndex]() {
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
    }, false, track.type != Track::Type::Audio});

    items.push_back({"Set as MIDI Track", [this, trackIndex]() {
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
    }, false, track.type != Track::Type::Midi});

    items.push_back({"Set as Visual Track", [this, trackIndex]() {
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
    }, false, track.type != Track::Type::Visual});

    // Blend Mode submenu — visible on Visual tracks only.
    if (track.type == Track::Type::Visual) {
        std::vector<ui::ContextMenu::Item> bmItems;
        const auto cur = track.visualBlendMode;
        auto addMode = [&](const char* label, Track::VisualBlendMode m,
                            visual::VisualEngine::BlendMode vm) {
            bmItems.push_back({label, [this, trackIndex, m, vm]{
                m_project.track(trackIndex).visualBlendMode = m;
                m_visualEngine.setLayerBlendMode(trackIndex, vm);
                markDirty();
            }, false, cur != m});
        };
        addMode("Normal",   Track::VisualBlendMode::Normal,
                visual::VisualEngine::BlendMode::Normal);
        addMode("Add",      Track::VisualBlendMode::Add,
                visual::VisualEngine::BlendMode::Add);
        addMode("Multiply", Track::VisualBlendMode::Multiply,
                visual::VisualEngine::BlendMode::Multiply);
        addMode("Screen",   Track::VisualBlendMode::Screen,
                visual::VisualEngine::BlendMode::Screen);
        items.push_back({"Blend Mode", nullptr, false, true, std::move(bmItems)});

        // "Add Knob Lane" submenu — creates a track-level automation
        // lane targeting A..H. Lanes show up in the expanded track
        // row and can be edited in the arrangement view.
        std::vector<ui::ContextMenu::Item> laneItems;
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
            laneItems.push_back({label,
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
                false, !exists});
        }
        items.push_back({"Add Knob Lane", nullptr, false, true,
                           std::move(laneItems)});
    }

    // Separator + Instruments submenu
    std::vector<ui::ContextMenu::Item> instrItems;
    auto addInstrItem = [&](const char* label, auto factory) {
        instrItems.push_back({label, [this, trackIndex, label, factory]() {
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
        }});
    };
    addInstrItem("SubSynth", [](){ return std::make_unique<instruments::SubtractiveSynth>(); });
    addInstrItem("FM Synth", [](){ return std::make_unique<instruments::FMSynth>(); });
    addInstrItem("Sampler", [](){ return std::make_unique<instruments::Sampler>(); });
    addInstrItem("Drum Rack", [](){ return std::make_unique<instruments::DrumRack>(); });
    addInstrItem("Drum Synth", [](){ return std::make_unique<instruments::DrumSynth>(); });
    addInstrItem("DrumSlop", [](){ return std::make_unique<instruments::DrumSlop>(); });
    addInstrItem("Karplus-Strong", [](){ return std::make_unique<instruments::KarplusStrong>(); });
    addInstrItem("Wavetable Synth", [](){ return std::make_unique<instruments::WavetableSynth>(); });
    addInstrItem("Granular Synth", [](){ return std::make_unique<instruments::GranularSynth>(); });
    addInstrItem("Vocoder", [](){ return std::make_unique<instruments::Vocoder>(); });
    addInstrItem("Multisampler", [](){ return std::make_unique<instruments::Multisampler>(); });
    addInstrItem("Instrument Rack", [](){ return std::make_unique<instruments::InstrumentRack>(); });

#ifdef YAWN_HAS_VST3
    // VST3 instruments — flat list with separator
    if (m_vst3Scanner && !m_vst3Scanner->instruments().empty()) {
        instrItems.push_back({"── VST3 ──", nullptr, true});
        for (auto& info : m_vst3Scanner->instruments()) {
            std::string label = info.name;
            if (!info.vendor.empty()) label += " (" + info.vendor + ")";
            std::string modulePath = info.modulePath;
            std::string classID = info.classIDString;
            instrItems.push_back({label, [this, trackIndex, modulePath, classID]() {
                m_project.track(trackIndex).type = Track::Type::Midi;
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
                m_audioEngine.setInstrument(trackIndex,
                    std::make_unique<vst3::VST3Instrument>(modulePath, classID));
                markDirty();
            }});
        }
    }
#endif

    items.push_back({"Add Instrument", nullptr, true, true, std::move(instrItems)});

    // Audio effects submenu
    std::vector<ui::ContextMenu::Item> fxItems;
    auto addFxItem = [&](const char* label, auto factory) {
        fxItems.push_back({label, [this, trackIndex, label, factory]() {
            LOG_INFO("User", "addFx '%s' to track %d", label, trackIndex);
            auto& chain = m_audioEngine.mixer().trackEffects(trackIndex);
            chain.append(factory());
            int slot = chain.count() - 1;
            markDirty();
            std::string fxName = label;
            m_undoManager.push({"Add Effect: " + fxName,
                [this, trackIndex, slot]{
                    m_audioEngine.mixer().trackEffects(trackIndex).remove(slot);
                    markDirty();
                },
                [this, trackIndex, factory]{
                    m_audioEngine.mixer().trackEffects(trackIndex).append(factory());
                    markDirty();
                }, ""});
        }});
    };
    addFxItem("Reverb",      [](){ return std::make_unique<effects::Reverb>(); });
    addFxItem("Delay",       [](){ return std::make_unique<effects::Delay>(); });
    addFxItem("EQ",          [](){ return std::make_unique<effects::EQ>(); });
    addFxItem("Compressor",  [](){ return std::make_unique<effects::Compressor>(); });
    addFxItem("Limiter",     [](){ return std::make_unique<effects::Limiter>(); });
    addFxItem("Filter",      [](){ return std::make_unique<effects::Filter>(); });
    addFxItem("Chorus",      [](){ return std::make_unique<effects::Chorus>(); });
    addFxItem("Phaser",      [](){ return std::make_unique<effects::Phaser>(); });
    addFxItem("Wah",         [](){ return std::make_unique<effects::Wah>(); });
    addFxItem("Distortion",  [](){ return std::make_unique<effects::Distortion>(); });
    addFxItem("Bitcrusher",  [](){ return std::make_unique<effects::Bitcrusher>(); });
    addFxItem("Noise Gate",  [](){ return std::make_unique<effects::NoiseGate>(); });
    addFxItem("Ping-Pong Delay", [](){ return std::make_unique<effects::PingPongDelay>(); });
    addFxItem("Envelope Follower", [](){ return std::make_unique<effects::EnvelopeFollower>(); });
    addFxItem("Spline EQ",   [](){ return std::make_unique<effects::SplineEQ>(); });
    addFxItem("Neural Amp",  [](){ return std::make_unique<effects::NeuralAmp>(); });
    addFxItem("Conv Reverb", [](){ return std::make_unique<effects::ConvolutionReverb>(); });
    addFxItem("Tape Emulation", [](){ return std::make_unique<effects::TapeEmulation>(); });
    addFxItem("Amp Simulator",  [](){ return std::make_unique<effects::AmpSimulator>(); });
    addFxItem("Oscilloscope",   [](){ return std::make_unique<effects::Oscilloscope>(); });
    addFxItem("Spectrum",       [](){ return std::make_unique<effects::SpectrumAnalyzer>(); });
    addFxItem("Tuner",          [](){ return std::make_unique<effects::Tuner>(); });

#ifdef YAWN_HAS_VST3
    // VST3 effects — flat list with separator
    if (m_vst3Scanner && !m_vst3Scanner->effects().empty()) {
        fxItems.push_back({"── VST3 ──", nullptr, true});
        for (auto& info : m_vst3Scanner->effects()) {
            std::string label = info.name;
            if (!info.vendor.empty()) label += " (" + info.vendor + ")";
            std::string modulePath = info.modulePath;
            std::string classID = info.classIDString;
            fxItems.push_back({label, [this, trackIndex, modulePath, classID, label]() {
                LOG_INFO("User", "addFx VST3 '%s' to track %d (path=%s)",
                         label.c_str(), trackIndex, modulePath.c_str());
                auto& chain = m_audioEngine.mixer().trackEffects(trackIndex);
                chain.append(std::make_unique<vst3::VST3Effect>(modulePath, classID));
                markDirty();
            }});
        }
    }
#endif

    items.push_back({"Add Audio Effect", nullptr, false, true, std::move(fxItems)});

    // MIDI effects submenu
    std::vector<ui::ContextMenu::Item> midiItems;
    auto addMidiItem = [&](const char* label, auto factory) {
        midiItems.push_back({label, [this, trackIndex, label, factory]() {
            auto& chain = m_audioEngine.midiEffectChain(trackIndex);
            chain.addEffect(factory());
            int slot = chain.count() - 1;
            markDirty();
            std::string fxName = label;
            m_undoManager.push({"Add MIDI Effect: " + fxName,
                [this, trackIndex, slot]{
                    m_audioEngine.midiEffectChain(trackIndex).removeEffect(slot);
                    markDirty();
                },
                [this, trackIndex, factory]{
                    m_audioEngine.midiEffectChain(trackIndex).addEffect(factory());
                    markDirty();
                }, ""});
        }});
    };
    addMidiItem("Arpeggiator", [](){ return std::make_unique<midi::Arpeggiator>(); });
    addMidiItem("Chord",       [](){ return std::make_unique<midi::Chord>(); });
    addMidiItem("Scale",       [](){ return std::make_unique<midi::Scale>(); });
    addMidiItem("Note Length", [](){ return std::make_unique<midi::NoteLength>(); });
    addMidiItem("Velocity",    [](){ return std::make_unique<midi::VelocityEffect>(); });
    addMidiItem("Random",      [](){ return std::make_unique<midi::MidiRandom>(); });
    addMidiItem("Pitch",       [](){ return std::make_unique<midi::MidiPitch>(); });
    addMidiItem("LFO",         [](){ return std::make_unique<midi::LFO>(); });
    items.push_back({"Add MIDI Effect", nullptr, false, true, std::move(midiItems)});

    // Record quantize submenu
    auto curRQ = track.recordQuantize;
    std::vector<ui::ContextMenu::Item> rqItems;
    rqItems.push_back({"None", [this, trackIndex, curRQ]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::None;
        m_undoManager.push({"Change Record Quantize",
            [this, trackIndex, curRQ]{ m_project.track(trackIndex).recordQuantize = curRQ; },
            [this, trackIndex]{ m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::None; },
            ""});
        markDirty();
    }, false, curRQ != audio::QuantizeMode::None});
    rqItems.push_back({"Beat", [this, trackIndex, curRQ]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBeat;
        m_undoManager.push({"Change Record Quantize",
            [this, trackIndex, curRQ]{ m_project.track(trackIndex).recordQuantize = curRQ; },
            [this, trackIndex]{ m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBeat; },
            ""});
        markDirty();
    }, false, curRQ != audio::QuantizeMode::NextBeat});
    rqItems.push_back({"Bar", [this, trackIndex, curRQ]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBar;
        m_undoManager.push({"Change Record Quantize",
            [this, trackIndex, curRQ]{ m_project.track(trackIndex).recordQuantize = curRQ; },
            [this, trackIndex]{ m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBar; },
            ""});
        markDirty();
    }, false, curRQ != audio::QuantizeMode::NextBar});
    items.push_back({"Record Quantize", nullptr, false, true, std::move(rqItems)});

#ifdef YAWN_HAS_VST3
    // Open VST3 Editor for instrument
    {
        auto* inst = m_audioEngine.instrument(trackIndex);
        auto* vsti = dynamic_cast<vst3::VST3Instrument*>(inst);
        if (vsti && vsti->instance()) {
            std::string edTitle = std::string(vsti->name()) + " - Track " + std::to_string(trackIndex + 1);
            std::string modPath = vsti->modulePath();
            std::string clsID = vsti->classIDString();
            items.push_back({"Open VST3 Instrument Editor", [this, vsti, edTitle, modPath, clsID]() {
                openVST3Editor(vsti->instance(), modPath, clsID, edTitle);
            }});
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
                items.push_back({"Open Editor: " + fxName, [this, fx, edTitle, modPath, clsID]() {
                    openVST3Editor(fx->instance(), modPath, clsID, edTitle);
                }});
            }
        }
    }
#endif

    // Delete track (with confirmation) — migrated to v2 ConfirmDialog:
    // rides OverlayLayer::Modal so the scrim + event blocking are
    // handled uniformly by LayerStack instead of the v1 paintOverlay
    // + hand-rolled escape/click dispatch.
    bool canDelete = m_project.numTracks() > 1;
    items.push_back({"Delete Track", [this, trackIndex]() {
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
    }, true, canDelete});

    ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
}

void App::launchVisualClipData(int track,
                                const visual::VisualClip& vc,
                                const std::string& shaderPath) {
    const int audioSource = vc.audioSource;

    // Clip without a custom shader → fall back to the bundled
    // passthrough so whatever's feeding iChannel2 (file video, live
    // input, or 3D model render) shows up full-frame.
    std::string effectiveShader = shaderPath;
    const bool hasFileVideo = !vc.videoPath.empty();
    const bool hasLiveVideo = vc.liveInput && !vc.liveUrl.empty();
    const bool hasModel     = !vc.modelPath.empty();
    if (effectiveShader.empty() && hasModel) {
        effectiveShader = "assets/shaders/model_passthrough.frag";
    } else if (effectiveShader.empty() &&
                (hasFileVideo || hasLiveVideo)) {
        effectiveShader = "assets/shaders/video_passthrough.frag";
    }
    if (effectiveShader.empty()) return;

    std::string resolved = resolveShaderPath(effectiveShader);
    m_visualEngine.loadLayer(track, resolved, audioSource);
    m_visualEngine.setLayerBlendMode(track,
        static_cast<visual::VisualEngine::BlendMode>(
            m_project.track(track).visualBlendMode));

    m_visualEngine.applyLayerParamValues(track, vc.firstPassParamValues());

    // Effect chain — additional passes after the source. The chain
    // lives on the *track* (audio-FX-style) so it persists across
    // clip switches; we just re-push it here whenever the layer is
    // re-launched. Empty chain clears any prior extras. Bypassed
    // passes are still pushed (and compiled) so toggling them on is
    // instant; the engine skips them at render time.
    {
        std::vector<visual::VisualEngine::ChainPassSpec> extras;
        if (track >= 0 && track < m_project.numTracks()) {
            const auto& chain = m_project.track(track).visualEffectChain;
            extras.reserve(chain.size());
            for (const auto& p : chain) {
                visual::VisualEngine::ChainPassSpec spec;
                spec.shaderPath  = resolveShaderPath(p.shaderPath);
                spec.paramValues = p.paramValues;
                spec.bypassed    = p.bypassed;
                extras.push_back(std::move(spec));
            }
        }
        m_visualEngine.setLayerAdditionalPasses(track, extras);
    }

    // Macro values + LFO config flow from the *track*, not the clip.
    // Phase 4.1 — track.macros owns the 8 knob values and modulators;
    // launching a clip just re-pushes the current track-level state
    // into the freshly loaded layer so visuals start in sync.
    if (track >= 0 && track < m_project.numTracks()) {
        const auto& macros = m_project.track(track).macros;
        for (int i = 0; i < MacroDevice::kNumMacros; ++i) {
            m_visualEngine.setLayerKnob(track, i, macros.values[i]);
            const auto& s = macros.lfos[i];
            visual::VisualLFO lfo;
            lfo.enabled = s.enabled;
            lfo.shape   = static_cast<visual::VisualLFO::Shape>(s.shape);
            lfo.rate    = s.rate;
            lfo.depth   = s.depth;
            lfo.sync    = s.sync;
            m_visualEngine.setLayerKnobLFO(track, i, lfo);
        }
    }
    m_visualEngine.setLayerText(track, vc.text);

    // iChannel2 source selection — mutually exclusive per layer.
    // Priority: model > live > file. Engine enforces the teardown
    // either way; we skip the non-winning setters so we don't churn
    // decoder threads / uploads unnecessarily.
    if (!vc.modelPath.empty()) {
        m_visualEngine.setLayerModel(track,
            resolveModelPath(vc.modelPath));
        m_visualEngine.setLayerSceneScript(track,
            resolveScenePath(vc.scenePath));
    } else if (vc.liveInput && !vc.liveUrl.empty()) {
        m_visualEngine.setLayerLiveInput(track, vc.liveUrl);
    } else {
        m_visualEngine.setLayerVideo(track, vc.videoPath);
        m_visualEngine.setLayerVideoTiming(track,
            vc.videoLoopBars, vc.videoRate);
        m_visualEngine.setLayerVideoTrim(track,
            vc.videoIn, vc.videoOut);
    }
}

void App::pollArrangementVisualPlayback() {
    // Initialize the per-track "last active" state on first call so
    // we don't mis-detect a transition on startup.
    if (!m_activeArrInit) {
        for (int i = 0; i < kMaxTracks; ++i) m_activeArrVisualClip[i] = -1;
        m_activeArrInit = true;
    }

    // Poll regardless of play state so that dragging the playhead
    // while paused previews whatever clip sits under it — that's
    // what makes scrubbing feel right. The underlying launch/clear
    // are idempotent on the same activeIdx.
    const double beat = m_audioEngine.transport().positionInBeats();
    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        auto& track = m_project.track(t);
        if (track.type != Track::Type::Visual) continue;
        if (!track.arrangementActive) continue;

        int activeIdx = -1;
        for (int ci = 0; ci < static_cast<int>(track.arrangementClips.size()); ++ci) {
            const auto& c = track.arrangementClips[ci];
            if (c.type != ArrangementClip::Type::Visual) continue;
            if (beat >= c.startBeat && beat < c.endBeat()) {
                activeIdx = ci;   // last-match-wins for overlapping clips
            }
        }

        if (activeIdx == m_activeArrVisualClip[t]) continue;

        // Transition detected.
        if (activeIdx < 0) {
            // Leaving a clip into a gap → clear the layer so the
            // arrangement goes quiet on this track.
            m_visualEngine.clearLayer(t);
        } else {
            const auto& nc = track.arrangementClips[activeIdx];
            if (nc.visualClip) {
                launchVisualClipData(t, *nc.visualClip,
                                      resolveShaderPath(nc.visualClip->firstShaderPath()));
                // Arrangement clips use transport-driven clock so
                // scrubbing the playhead seeks the visuals. Origin =
                // the clip's start beat on the arrangement timeline.
                m_visualEngine.setLayerTransportClock(t, nc.startBeat);
            }
        }
        m_activeArrVisualClip[t] = activeIdx;
    }
}

int App::resolveFollowActionScene(int track, int currentScene,
                                    FollowActionType action) const {
    const int numScenes = m_project.numScenes();
    if (track < 0 || track >= m_project.numTracks() || numScenes <= 0)
        return -1;

    // Build the set of occupied scenes on this track (same convention
    // as the audio follow-action handler — visual and audio/MIDI slots
    // all count as "occupied" for navigation purposes).
    std::vector<int> occupied;
    for (int s = 0; s < numScenes; ++s) {
        auto* slot = m_project.getSlot(track, s);
        if (slot && !slot->empty()) occupied.push_back(s);
    }
    if (occupied.empty()) return -1;

    switch (action) {
        case FollowActionType::Next: {
            for (int s : occupied) if (s > currentScene) return s;
            return occupied.front();  // wrap
        }
        case FollowActionType::Previous: {
            for (int i = static_cast<int>(occupied.size()) - 1; i >= 0; --i)
                if (occupied[i] < currentScene) return occupied[i];
            return occupied.back();   // wrap
        }
        case FollowActionType::First:
            return occupied.front();
        case FollowActionType::Last:
            return occupied.back();
        case FollowActionType::Random:
            return occupied[std::rand() % occupied.size()];
        case FollowActionType::Any: {
            std::vector<int> others;
            for (int s : occupied) if (s != currentScene) others.push_back(s);
            if (!others.empty()) return others[std::rand() % others.size()];
            return currentScene;
        }
        case FollowActionType::PlayAgain:
            return currentScene;
        default:
            return -1;
    }
}

void App::pollVisualFollowActions() {
    // One-shot init: mark all tracks as "not launched" on first call.
    if (!m_visualLaunchInit) {
        for (int i = 0; i < kMaxTracks; ++i) {
            m_visualLaunchBeat[i]  = kNoVisualLaunch;
            m_visualLaunchScene[i] = -1;
        }
        m_visualLaunchInit = true;
    }

    // Follow actions only fire while transport is actively advancing
    // bars — matches the audio ClipEngine behavior. Stop-then-play
    // resets each clip's bar counter (the set-launch-beat path).
    if (!m_audioEngine.transport().isPlaying()) return;

    const double beat = m_audioEngine.transport().positionInBeats();
    const int beatsPerBar = std::max(1, m_audioEngine.transport().numerator());

    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        if (m_project.track(t).type != Track::Type::Visual) continue;
        if (m_visualLaunchBeat[t] == kNoVisualLaunch) continue;

        const int scene = m_visualLaunchScene[t];
        if (scene < 0) continue;
        auto* slot = m_project.getSlot(t, scene);
        if (!slot || !slot->visualClip) continue;
        const auto& fa = slot->followAction;
        if (!fa.enabled || fa.barCount <= 0) continue;

        const double elapsedBeats = beat - m_visualLaunchBeat[t];
        if (elapsedBeats < fa.barCount * beatsPerBar) continue;

        // Roll A/B probability, pick action, resolve target scene.
        const int roll = std::rand() % 100;
        FollowActionType action = (roll < fa.chanceA) ? fa.actionA : fa.actionB;
        if (action == FollowActionType::None) {
            // Restart the bar timer so we roll again N bars later —
            // None means "loop", not "disarm".
            m_visualLaunchBeat[t] += fa.barCount * beatsPerBar;
            continue;
        }
        if (action == FollowActionType::Stop) {
            m_visualEngine.clearLayer(t);
            m_visualLaunchBeat[t]  = kNoVisualLaunch;
            m_visualLaunchScene[t] = -1;
            m_sessionPanel->updateClipState(t, false, 0, -1);
            continue;
        }

        const int target = resolveFollowActionScene(t, scene, action);
        if (target < 0) {
            // No valid target — stop to avoid infinite no-op retries.
            m_visualEngine.clearLayer(t);
            m_visualLaunchBeat[t]  = kNoVisualLaunch;
            m_visualLaunchScene[t] = -1;
            m_sessionPanel->updateClipState(t, false, 0, -1);
            continue;
        }
        auto* tslot = m_project.getSlot(t, target);
        if (!tslot || !tslot->visualClip) continue;

        launchVisualClipData(t, *tslot->visualClip,
                              resolveShaderPath(tslot->visualClip->firstShaderPath()));
        m_visualEngine.setLayerWallClock(t);
        m_project.track(t).defaultScene = target;
        m_visualLaunchBeat[t]  = beat;
        m_visualLaunchScene[t] = target;
        // Mirror the scene change into the session grid so the play
        // indicator follows the active clip instead of staying stuck
        // on the originally-launched row.
        m_sessionPanel->updateClipState(t, /*playing*/true,
                                          /*playPos*/0,
                                          /*playingScene*/target);
    }
}

void App::stopAllVisualLayers() {
    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        if (m_project.track(t).type != Track::Type::Visual) continue;
        m_visualEngine.clearLayer(t);
        m_activeArrVisualClip[t]  = -1;
        m_visualLaunchBeat[t]     = kNoVisualLaunch;
        m_visualLaunchScene[t]    = -1;
        m_sessionPanel->updateClipState(t, false, 0, -1);
    }
}

// Linear scan for a parameter by name on any device that exposes
// the parameterCount() + parameterInfo(idx).name pair (instruments,
// audio effects, midi effects all qualify). Returns -1 when the
// name doesn't resolve. Per-frame call site, but the param counts
// are tiny so a hash isn't worth the upkeep.
namespace {
template <typename Device>
int findParamByName(const Device& dev, const std::string& name) {
    const int n = dev.parameterCount();
    for (int i = 0; i < n; ++i) {
        const char* pn = dev.parameterInfo(i).name;
        if (pn && name == pn) return i;
    }
    return -1;
}
} // namespace

void App::applyMacroMappings() {
    // Walk every track's MacroDevice once per frame; each mapping
    // reads the live (LFO-modulated) macro value, lerps it through
    // [rangeMin, rangeMax] (a 0..1 sub-range), then unnormalises into
    // the target parameter's *natural* range and pushes it. Inverted
    // sub-ranges (rangeMin > rangeMax) drop straight out of the lerp
    // arithmetic — no special case.
    //
    // Reading the engine's *display* value (rather than the raw
    // track.macros.values entry) keeps mapped targets in lock-step
    // with what the same macro does to the shader's knob[idx]
    // uniform — so an LFO breathing the macro pulses every mapped
    // target in unison.
    const int nTracks = m_project.numTracks();
    for (int t = 0; t < nTracks; ++t) {
        auto& track = m_project.track(t);
        if (track.macros.mappings.empty()) continue;

        for (const auto& m : track.macros.mappings) {
            if (m.macroIdx < 0 ||
                m.macroIdx >= MacroDevice::kNumMacros) continue;

            // Visual macros pull modulated value from the engine
            // (which has the LFO state). For non-visual tracks we
            // fall back to the raw macro value — audio/MIDI tracks
            // don't yet have layer-style LFO eval (Phase 4.6).
            const float macroV = (track.type == Track::Type::Visual)
                ? m_visualEngine.getLayerKnobDisplayValue(t, m.macroIdx)
                : track.macros.values[m.macroIdx];
            const float normV = m.rangeMin
                + (m.rangeMax - m.rangeMin) * macroV;

            switch (m.target.kind) {
                case MacroTarget::Kind::VisualSourceParam: {
                    if (track.type != Track::Type::Visual) break;
                    float pmin = 0.0f, pmax = 1.0f;
                    if (!m_visualEngine.getLayerParamRange(
                            t, m.target.paramName, &pmin, &pmax))
                        break;
                    const float v = pmin + normV * (pmax - pmin);
                    m_visualEngine.setLayerParam(t, m.target.paramName, v);
                    break;
                }
                case MacroTarget::Kind::VisualChainParam: {
                    if (track.type != Track::Type::Visual) break;
                    float pmin = 0.0f, pmax = 1.0f;
                    if (!m_visualEngine.getLayerChainPassParamRange(
                            t, m.target.index, m.target.paramName,
                            &pmin, &pmax))
                        break;
                    const float v = pmin + normV * (pmax - pmin);
                    m_visualEngine.setLayerChainPassParam(
                        t, m.target.index, m.target.paramName, v);
                    break;
                }
                case MacroTarget::Kind::AudioInstrumentParam: {
                    auto* inst = m_audioEngine.instrument(t);
                    if (!inst) break;
                    int pi = findParamByName(*inst, m.target.paramName);
                    if (pi < 0) break;
                    const auto& info = inst->parameterInfo(pi);
                    const float v =
                        info.minValue + normV * (info.maxValue - info.minValue);
                    inst->setParameter(pi, v);
                    break;
                }
                case MacroTarget::Kind::AudioEffectParam: {
                    auto& chain = m_audioEngine.mixer().trackEffects(t);
                    auto* fx = chain.effectAt(m.target.index);
                    if (!fx) break;
                    int pi = findParamByName(*fx, m.target.paramName);
                    if (pi < 0) break;
                    const auto& info = fx->parameterInfo(pi);
                    const float v =
                        info.minValue + normV * (info.maxValue - info.minValue);
                    fx->setParameter(pi, v);
                    break;
                }
                case MacroTarget::Kind::MidiEffectParam: {
                    auto& chain = m_audioEngine.midiEffectChain(t);
                    auto* fx = chain.effect(m.target.index);
                    if (!fx) break;
                    int pi = findParamByName(*fx, m.target.paramName);
                    if (pi < 0) break;
                    const auto& info = fx->parameterInfo(pi);
                    const float v =
                        info.minValue + normV * (info.maxValue - info.minValue);
                    fx->setParameter(pi, v);
                    break;
                }
                case MacroTarget::Kind::TrackVolume: {
                    // Linear gain 0..2 (matches the mixer fader).
                    m_audioEngine.mixer().setTrackVolume(t, normV * 2.0f);
                    break;
                }
                case MacroTarget::Kind::TrackPan: {
                    // -1 left … +1 right.
                    m_audioEngine.mixer().setTrackPan(t, normV * 2.0f - 1.0f);
                    break;
                }
                case MacroTarget::Kind::None:
                    break;
            }
        }
    }
}

void App::pollVisualKnobAutomation() {
    // Evaluate visual-knob automation on the main thread. We reuse
    // the existing AutomationLane data model (TargetType::VisualKnob
    // with paramIndex = 0..7 for A..H) but bypass the audio-thread
    // AutomationEngine — visual knobs don't need audio-thread jitter
    // guarantees, and the project → audio-engine lane sync isn't
    // wired for drawn envelopes anyway.
    //
    // Precedence (per the H.3 design):
    //   1. Per-clip envelope (on the active arrangement clip)  — lower
    //   2. Per-track arrangement lane                          — higher
    // Applied in that order → the track lane's write wins when both
    // target the same knob. LFOs still compose on top via the
    // existing knobDisplayValues pipeline.
    const double transportBeat = m_audioEngine.transport().positionInBeats();
    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        auto& track = m_project.track(t);
        if (track.type != Track::Type::Visual) continue;

        // Pass 1a: session-grid clip envelopes. Clip-local beat is
        // (transportBeat - launchBeat) wrapped mod lengthBeats, so
        // envelopes loop with the clip. Session launches populate
        // m_visualLaunchBeat[t] via the launch callback.
        if (m_visualLaunchBeat[t] != kNoVisualLaunch &&
            m_visualLaunchScene[t] >= 0) {
            auto* slot = m_project.getSlot(t, m_visualLaunchScene[t]);
            if (slot && slot->visualClip && !slot->clipAutomation.empty()) {
                const double launchBeat = m_visualLaunchBeat[t];
                const double lenBeats   = std::max(0.25,
                                                    slot->visualClip->lengthBeats);
                double clipBeat = std::fmod(transportBeat - launchBeat, lenBeats);
                if (clipBeat < 0.0) clipBeat += lenBeats;
                for (const auto& lane : slot->clipAutomation) {
                    if (lane.envelope.empty()) continue;
                    const float v = lane.envelope.valueAt(clipBeat);
                    if (lane.target.type == automation::TargetType::VisualKnob) {
                        const int knob = lane.target.paramIndex;
                        if (knob >= 0 && knob < 8)
                            m_visualEngine.setLayerKnob(t, knob, v);
                    } else if (lane.target.type == automation::TargetType::VisualParam) {
                        if (!lane.target.paramName.empty())
                            m_visualEngine.setLayerParam(t, lane.target.paramName, v);
                    }
                }
            }
        }

        // Pass 1b: arrangement-clip envelopes — deferred; ArrangementClip
        // doesn't store clipAutomation yet. Placeholder for that phase.

        // Pass 2: per-track arrangement lanes (absolute beat time).
        // Only applied when autoMode isn't Off. Track lanes run AFTER
        // clip envelopes so the arrangement layer always wins per the
        // H.3 precedence design.
        if (track.autoMode == automation::AutoMode::Off) continue;
        for (const auto& lane : track.automationLanes) {
            if (lane.target.trackIndex != t) continue;
            if (lane.envelope.empty()) continue;
            const float v = lane.envelope.valueAt(transportBeat);
            if (lane.target.type == automation::TargetType::VisualKnob) {
                const int knob = lane.target.paramIndex;
                if (knob >= 0 && knob < 8)
                    m_visualEngine.setLayerKnob(t, knob, v);
            } else if (lane.target.type == automation::TargetType::VisualParam) {
                if (!lane.target.paramName.empty())
                    m_visualEngine.setLayerParam(t, lane.target.paramName, v);
            }
        }
    }
}

void App::showClipContextMenu(int trackIndex, int sceneIndex, float mx, float my) {
    std::vector<ui::ContextMenu::Item> items;
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
        items.push_back({label, [this, trackIndex, sceneIndex]() {
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
                /*default_location*/ nullptr,
                /*allow_many*/ false);
        }, false, true});

        // "New Shader…" — prompts for a name, writes a minimal
        // Shadertoy-compatible template to <project>/shaders/<name>.frag,
        // and assigns it to this clip. Requires a saved project so the
        // file has somewhere to land.
        items.push_back({"New Shader…",
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
            }, false, true});

        // "Fork Shader" — duplicate the current shader file to a new
        // <stem>_fork_N.frag so the user can edit it without touching
        // shaders shared by other clips.
        if (hasVisualClip && !slot->visualClip->firstShaderPath().empty()) {
            items.push_back({"Fork Shader",
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
                }, false, true});
        }

        // "Localize Shader" — copy an external/bundled shader into
        // <project>/shaders/ so the project is self-contained. Only
        // offered when the current path isn't already project-relative.
        if (hasVisualClip && !slot->visualClip->firstShaderPath().empty()) {
            const std::string& sp = slot->visualClip->firstShaderPath();
            const bool alreadyLocal =
                sp.compare(0, 8, "shaders/") == 0;
            if (!alreadyLocal) {
                items.push_back({"Localize Shader",
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
                    }, false, true});
            }
        }

        // "Set Video…" — opens a file picker, kicks off the ffmpeg
        // transcode, and drops the imported .mp4 path into this clip.
        items.push_back({"Set Video…",
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
            }, false, true});

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

            std::vector<ui::ContextMenu::Item> liveItems;

            auto devices = visual::enumerateLiveInputDevices();
            for (auto& d : devices) {
                std::string url = d.url;
                liveItems.push_back({d.label,
                    [applyLiveUrl, url]{ applyLiveUrl(url); },
                    false, true});
            }
            if (!devices.empty()) {
                liveItems.push_back({"", nullptr, true, false}); // separator
            }
            liveItems.push_back({"Custom URL…",
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
                }, false, true});

            // Clear action is only meaningful when the clip is already
            // configured for live input.
            if (hasVisualClip && slot->visualClip->liveInput) {
                liveItems.push_back({"Clear Live Input",
                    [this, trackIndex, sceneIndex]() {
                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s || !s->visualClip) return;
                        s->visualClip->liveInput = false;
                        s->visualClip->liveUrl.clear();
                        if (m_project.track(trackIndex).defaultScene == sceneIndex)
                            m_visualEngine.setLayerLiveInput(trackIndex, "");
                        markDirty();
                    }, false, true});
            }

            ui::ContextMenu::Item liveEntry;
            liveEntry.label   = "Live Input";
            liveEntry.enabled = true;
            liveEntry.submenu = std::move(liveItems);
            items.push_back(std::move(liveEntry));
        }

        // "Set Model…" — pick a .glb / .gltf. On success we localize
        // the file into <project>/models/ (only .glb is self-contained
        // enough to safely copy; .gltf stays at its absolute path) and
        // wire it into the engine if this is the launched clip.
        {
            const bool hasModel = hasVisualClip &&
                                   !slot->visualClip->modelPath.empty();
            const char* label = hasModel ? "Change Model…" : "Set Model…";
            items.push_back({label,
                [this, trackIndex, sceneIndex]() {
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
                            auto* s = self->m_project.getSlot(ti, si);
                            if (!s) return;
                            if (!s->visualClip)
                                s->visualClip = std::make_unique<visual::VisualClip>();
                            std::string stored =
                                self->localizeModel(filelist[0]);
                            s->visualClip->modelPath       = stored;
                            s->visualClip->modelSourcePath = filelist[0];
                            // Mutual exclusion — clear the other
                            // iChannel2 sources at the data level too.
                            s->visualClip->videoPath.clear();
                            s->visualClip->thumbnailPath.clear();
                            s->visualClip->liveInput = false;
                            s->visualClip->liveUrl.clear();
                            if (s->visualClip->name.empty()) {
                                std::filesystem::path p(filelist[0]);
                                s->visualClip->name = p.stem().string();
                            }
                            if (s->visualClip->colorIndex == 0) {
                                s->visualClip->colorIndex =
                                    self->m_project.track(ti).colorIndex;
                            }
                            if (self->m_project.track(ti).defaultScene == si) {
                                self->m_visualEngine.setLayerModel(ti,
                                    self->resolveModelPath(stored));
                            }
                            self->markDirty();
                        },
                        this, m_mainWindow.getHandle(),
                        &filter, 1, nullptr, false);
                }, false, true});

            if (hasModel) {
                items.push_back({"Clear Model",
                    [this, trackIndex, sceneIndex]() {
                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s || !s->visualClip) return;
                        s->visualClip->modelPath.clear();
                        s->visualClip->modelSourcePath.clear();
                        // Scene script is only meaningful with a model
                        // present — drop it too to keep state coherent.
                        s->visualClip->scenePath.clear();
                        if (m_project.track(trackIndex).defaultScene == sceneIndex) {
                            m_visualEngine.setLayerModel(trackIndex, "");
                            m_visualEngine.setLayerSceneScript(trackIndex, "");
                        }
                        markDirty();
                    }, false, true});
            }

            // "Set Scene Script…" — only offered when the clip has a
            // model (the script drives instances of that model). File
            // picker filters to .lua; localizes to <project>/scripts/.
            if (hasModel) {
                const bool hasScript = !slot->visualClip->scenePath.empty();
                const char* sLabel = hasScript ? "Change Scene Script…"
                                                 : "Set Scene Script…";
                items.push_back({sLabel,
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
                    }, false, true});

                if (hasScript) {
                    items.push_back({"Clear Scene Script",
                        [this, trackIndex, sceneIndex]() {
                            auto* s = m_project.getSlot(trackIndex, sceneIndex);
                            if (!s || !s->visualClip) return;
                            s->visualClip->scenePath.clear();
                            if (m_project.track(trackIndex).defaultScene == sceneIndex)
                                m_visualEngine.setLayerSceneScript(trackIndex, "");
                            markDirty();
                        }, false, true});
                }
            }
        }

        // "Clip Length" submenu — drives the envelope loop / follow-
        // action bar counter / session-clip "duration" abstraction.
        if (hasVisualClip) {
            const double curLen = slot->visualClip->lengthBeats;
            const int    curBars = static_cast<int>(std::round(curLen / 4.0));
            auto setLen = [this, trackIndex, sceneIndex](int bars) {
                auto* s = m_project.getSlot(trackIndex, sceneIndex);
                if (!s || !s->visualClip) return;
                s->visualClip->lengthBeats = bars * 4.0;
                markDirty();
            };
            std::vector<ui::ContextMenu::Item> lenItems;
            auto addLen = [&](const char* label, int bars) {
                lenItems.push_back({label, [setLen, bars]{ setLen(bars); },
                                       false, curBars != bars});
            };
            addLen("1 bar",   1);
            addLen("2 bars",  2);
            addLen("4 bars",  4);
            addLen("8 bars",  8);
            addLen("16 bars", 16);
            addLen("32 bars", 32);
            items.push_back({"Clip Length", nullptr, false, true,
                              std::move(lenItems)});
        }

        // "Send to Arrangement" — clone this visual clip onto the
        // arrangement timeline at the current transport beat, so the
        // user can lay out a long-form show without having to trigger
        // clips live. Uses the session clip's lengthBeats as the
        // duration; they can resize afterwards on the arrangement.
        if (hasVisualClip) {
            items.push_back({"Send to Arrangement",
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
                    ac.visualClip   = s->visualClip->clone();
                    auto& clips = m_project.track(trackIndex).arrangementClips;
                    clips.push_back(std::move(ac));
                    m_project.track(trackIndex).sortArrangementClips();
                    m_project.track(trackIndex).arrangementActive = true;
                    markDirty();
                }, false, true});
        }

        // "Re-import Video" — delete cached transcode and rerun ffmpeg.
        // Visible only when we know the original source path (imported
        // under the current code path).
        if (slot && slot->visualClip &&
            !slot->visualClip->videoSourcePath.empty()) {
            std::string src = slot->visualClip->videoSourcePath;
            items.push_back({"Re-import Video", [this, trackIndex, sceneIndex, src]() {
                if (m_projectPath.empty()) return;
                // Delete the cached files so the importer actually re-runs.
                std::filesystem::path mediaDir = m_projectPath / "media";
                std::string id = visual::VideoImporter::shortHash(src);
                std::error_code ec;
                for (const char* ext : { ".mp4", ".wav", "_thumb.jpg" }) {
                    std::filesystem::remove(mediaDir / (id + ext), ec);
                }
                startVideoImport(trackIndex, sceneIndex, src);
            }, false, true});
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
            std::vector<ui::ContextMenu::Item> loopItems;
            auto addLoop = [&](const char* label, int bars) {
                loopItems.push_back({label,
                    [applyTiming, curRate, bars]{ applyTiming(bars, curRate); },
                    false, curBars != bars});
            };
            addLoop("Free (native rate)", 0);
            addLoop("1/2 bar", 0); // placeholder — overwritten below
            loopItems.pop_back();
            addLoop("1 bar",  1);
            addLoop("2 bars", 2);
            addLoop("4 bars", 4);
            addLoop("8 bars", 8);
            addLoop("16 bars",16);
            items.push_back({"Video Loop", nullptr, false, true, std::move(loopItems)});

            // Playback Rate submenu (F.3).
            std::vector<ui::ContextMenu::Item> rateItems;
            auto addRate = [&](const char* label, float rate) {
                rateItems.push_back({label,
                    [applyTiming, curBars, rate]{ applyTiming(curBars, rate); },
                    false, std::abs(curRate - rate) > 0.001f});
            };
            addRate("0.25×", 0.25f);
            addRate("0.5×",  0.5f);
            addRate("1× (normal)", 1.0f);
            addRate("2×",    2.0f);
            addRate("4×",    4.0f);
            items.push_back({"Video Rate", nullptr, false, true, std::move(rateItems)});

            // In/Out trim submenu — picks a sub-range of the source.
            const float curIn  = slot->visualClip->videoIn;
            const float curOut = slot->visualClip->videoOut;
            std::vector<ui::ContextMenu::Item> trimItems;
            auto addTrim = [&](const char* label, float inF, float outF) {
                trimItems.push_back({label,
                    [this, trackIndex, sceneIndex, inF, outF]() {
                        auto* s = m_project.getSlot(trackIndex, sceneIndex);
                        if (!s || !s->visualClip) return;
                        s->visualClip->videoIn  = inF;
                        s->visualClip->videoOut = outF;
                        if (m_project.track(trackIndex).defaultScene == sceneIndex)
                            m_visualEngine.setLayerVideoTrim(trackIndex, inF, outF);
                        markDirty();
                    },
                    false,
                    !(std::abs(curIn - inF) < 0.005f &&
                      std::abs(curOut - outF) < 0.005f)});
            };
            addTrim("Full (0–100%)",         0.00f, 1.00f);
            addTrim("First half (0–50%)",    0.00f, 0.50f);
            addTrim("Last half (50–100%)",   0.50f, 1.00f);
            addTrim("Middle (25–75%)",       0.25f, 0.75f);
            addTrim("First quarter (0–25%)", 0.00f, 0.25f);
            addTrim("Last quarter (75–100%)",0.75f, 1.00f);
            items.push_back({"Video Trim", nullptr, false, true, std::move(trimItems)});

            items.push_back({"", nullptr, true}); // separator
        }

        // "Set Text…" — edits the string that gets rasterised to iChannel1.
        if (slot && slot->visualClip) {
            items.push_back({"Set Text…",
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
                }, false, true});
        }

        // Audio source submenu — what track's level drives iAudioLevel.
        if (slot && slot->visualClip) {
            std::vector<ui::ContextMenu::Item> srcItems;
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
            srcItems.push_back({"Master", [reassign]{ reassign(-1); },
                                false, curSource != -1});
            srcItems.push_back({"", nullptr, true}); // separator
            for (int t = 0; t < m_project.numTracks(); ++t) {
                if (m_project.track(t).type == Track::Type::Visual) continue;
                std::string label = "Track " + std::to_string(t + 1) + ": "
                                     + m_project.track(t).name;
                srcItems.push_back({label, [reassign, t]{ reassign(t); },
                                     false, curSource != t});
            }
            items.push_back({"Audio Source", nullptr, false, true, std::move(srcItems)});
        }
        items.push_back({"", nullptr, true}); // separator
    }

    // "Send to Arrangement" for audio/MIDI session clips — mirrors the
    // visual-clip path above. Audio shares the underlying AudioBuffer
    // with the session slot; MIDI is cloned (unique_ptr source → shared
    // on the arrangement side so the same clip can recur on the
    // timeline without cross-editing the session version).
    const bool canSendAudio = slot && slot->audioClip && slot->audioClip->buffer;
    const bool canSendMidi  = slot && slot->midiClip;
    if (canSendAudio || canSendMidi) {
        items.push_back({"Send to Arrangement",
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
            }, false, true});
        items.push_back({"", nullptr, true}); // separator
    }

    items.push_back({"Copy", [this, trackIndex, sceneIndex]() {
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
    }, false, hasClip});

    items.push_back({"Cut", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s && s->audioClip) {
            auto backup = s->audioClip->clone();
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Audio;
            m_clipboard.audioClip = s->audioClip->clone();
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            s->audioClip.reset();
            markDirty();
            m_undoManager.push({"Cut Audio Clip",
                [this, trackIndex, sceneIndex, b = std::shared_ptr<audio::Clip>(std::move(backup))]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { s2->audioClip = b->clone(); markDirty(); }
                },
                [this, trackIndex, sceneIndex]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                              s2->audioClip.reset(); markDirty(); }
                }, ""});
        } else if (s && s->midiClip) {
            auto backup = s->midiClip->clone();
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Midi;
            m_clipboard.midiClip = s->midiClip->clone();
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            s->midiClip.reset();
            markDirty();
            m_undoManager.push({"Cut MIDI Clip",
                [this, trackIndex, sceneIndex, b = std::shared_ptr<midi::MidiClip>(std::move(backup))]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { s2->midiClip = b->clone(); markDirty(); }
                },
                [this, trackIndex, sceneIndex]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (s2) { m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
                              s2->midiClip.reset(); markDirty(); }
                }, ""});
        }
    }, false, hasClip});

    items.push_back({"Paste", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (!s) return;
        std::shared_ptr<audio::Clip> oldAudio;
        std::shared_ptr<midi::MidiClip> oldMidi;
        if (s->audioClip) oldAudio.reset(s->audioClip->clone().release());
        if (s->midiClip) oldMidi.reset(s->midiClip->clone().release());
        if (m_clipboard.type == ClipboardData::Type::Audio && m_clipboard.audioClip) {
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            s->clear();
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
            s->clear();
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
    }, false, hasClipboard});

    items.push_back({"Duplicate", [this, trackIndex, sceneIndex]() {
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
    }, false, hasClip});

    items.push_back({"", nullptr, true}); // separator

    // Launch quantize submenu
    auto* slotForQ = m_project.getSlot(trackIndex, sceneIndex);
    auto curLQ = slotForQ ? slotForQ->launchQuantize : audio::QuantizeMode::NextBar;
    std::vector<ui::ContextMenu::Item> lqItems;
    lqItems.push_back({"None", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s) s->launchQuantize = audio::QuantizeMode::None;
    }, false, curLQ != audio::QuantizeMode::None});
    lqItems.push_back({"Beat", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s) s->launchQuantize = audio::QuantizeMode::NextBeat;
    }, false, curLQ != audio::QuantizeMode::NextBeat});
    lqItems.push_back({"Bar", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (s) s->launchQuantize = audio::QuantizeMode::NextBar;
    }, false, curLQ != audio::QuantizeMode::NextBar});
    items.push_back({"Launch Quantize", nullptr, false, true, std::move(lqItems)});

    items.push_back({"", nullptr, true}); // separator

    items.push_back({"Delete", [this, trackIndex, sceneIndex]() {
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
            s->clear();
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
    }, false, hasClip});

    items.push_back({"Rename", [this, trackIndex, sceneIndex]() {
        // TODO: open rename dialog
        (void)trackIndex; (void)sceneIndex;
    }, false, hasClip});

    items.push_back({"", nullptr, true}); // separator

    // Record length setting — per-slot. Clicking this slot to start a
    // recording uses this length (0 = unlimited). Does nothing once
    // the slot is occupied by a clip.
    {
        auto* rlSlot = m_project.getSlot(trackIndex, sceneIndex);
        int curRL = rlSlot ? rlSlot->recordLengthBars : 0;
        std::string rlLabel = "Record Length: ";
        rlLabel += (curRL == 0) ? "Unlimited" : (std::to_string(curRL) + (curRL == 1 ? " Bar" : " Bars"));
        std::vector<ui::ContextMenu::Item> rlItems;
        auto setRL = [this, trackIndex, sceneIndex](int bars) {
            auto* s = m_project.getSlot(trackIndex, sceneIndex);
            if (s) s->recordLengthBars = bars;
        };
        auto addRLItem = [&](const char* label, int bars) {
            rlItems.push_back({label, [this, setRL, curRL, bars]() {
                setRL(bars);
                m_undoManager.push({"Change Record Length",
                    [setRL, curRL]{ setRL(curRL); },
                    [setRL, bars]{ setRL(bars); },
                    ""});
                markDirty();
            }, false, curRL != bars});
        };
        addRLItem("Unlimited", 0);
        addRLItem("1 Bar", 1);
        addRLItem("2 Bars", 2);
        addRLItem("4 Bars", 4);
        addRLItem("8 Bars", 8);
        addRLItem("16 Bars", 16);
        rlItems.push_back({"", nullptr, true}); // separator
        rlItems.push_back({"Custom...", [this, setRL, curRL]() {
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
        }});
        items.push_back({rlLabel.c_str(), nullptr, false, true, std::move(rlItems)});

        // Loop-on-playback toggle for future recordings into this slot.
        const bool curLoop = rlSlot ? rlSlot->recordLoop : true;
        std::string loopLabel = "Record Loop: ";
        loopLabel += curLoop ? "On" : "Off";
        items.push_back({loopLabel.c_str(), [this, trackIndex, sceneIndex, curLoop]() {
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
        }});
    }

    ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
}

std::string App::resolveShaderPath(const std::string& stored) const {
    if (stored.empty()) return stored;
    std::filesystem::path p(stored);
    if (p.is_absolute()) return stored;
    // Paths from bundled YAWN resources (e.g. "assets/shaders/...") live
    // alongside the binary in the app's working directory, so pass them
    // through untouched. Only genuinely project-relative entries (the
    // ones we start writing as "shaders/<stem>.frag") get prefixed.
    if (!m_projectPath.empty() && stored.compare(0, 8, "shaders/") == 0) {
        return (m_projectPath / stored).string();
    }
    return stored;
}

std::string App::resolveModelPath(const std::string& stored) const {
    if (stored.empty()) return stored;
    std::filesystem::path p(stored);
    if (p.is_absolute()) return stored;
    if (!m_projectPath.empty() && stored.compare(0, 7, "models/") == 0) {
        return (m_projectPath / stored).string();
    }
    return stored;
}

std::string App::localizeModel(const std::string& sourcePath) {
    if (sourcePath.empty()) return sourcePath;
    if (m_projectPath.empty()) {
        LOG_WARN("Model",
            "Project not saved — model stays at %s. Save the project to "
            "copy models into <project>/models/.", sourcePath.c_str());
        return sourcePath;
    }

    namespace fs = std::filesystem;
    fs::path src(sourcePath);
    std::string ext = src.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));

    // .gltf references external .bin / texture files by relative path.
    // Copying the .gltf without those files silently breaks the load,
    // so for MVP we refuse to localize .gltf and just store the
    // absolute path the user picked.
    if (ext != ".glb") {
        LOG_INFO("Model",
            "Not localizing non-.glb model %s — use .glb for portable projects.",
            sourcePath.c_str());
        return sourcePath;
    }

    fs::path modelsDir = m_projectPath / "models";
    std::error_code ec;
    fs::create_directories(modelsDir, ec);

    std::string stem = src.stem().string();
    fs::path target = modelsDir / (stem + ".glb");

    if (!fs::exists(target)) {
        fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_ERROR("Model", "Failed to copy %s → %s: %s",
                      src.string().c_str(), target.string().c_str(),
                      ec.message().c_str());
            return sourcePath;
        }
        LOG_INFO("Model", "Localized %s → %s",
                 src.string().c_str(), target.string().c_str());
    }
    return "models/" + stem + ".glb";
}

std::string App::resolveScenePath(const std::string& stored) const {
    if (stored.empty()) return stored;
    std::filesystem::path p(stored);
    if (p.is_absolute()) return stored;
    if (!m_projectPath.empty() && stored.compare(0, 8, "scripts/") == 0) {
        return (m_projectPath / stored).string();
    }
    return stored;
}

std::string App::localizeScene(const std::string& sourcePath) {
    if (sourcePath.empty()) return sourcePath;
    if (m_projectPath.empty()) {
        LOG_WARN("Scene",
            "Project not saved — scene script stays at %s. Save the "
            "project to copy scripts into <project>/scripts/.",
            sourcePath.c_str());
        return sourcePath;
    }
    namespace fs = std::filesystem;
    fs::path src(sourcePath);
    std::string stem = src.stem().string();
    std::string ext  = src.extension().string();
    if (ext.empty()) ext = ".lua";

    fs::path scriptsDir = m_projectPath / "scripts";
    std::error_code ec;
    fs::create_directories(scriptsDir, ec);
    fs::path target = scriptsDir / (stem + ext);

    if (!fs::exists(target)) {
        fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_ERROR("Scene", "Failed to copy %s → %s: %s",
                      src.string().c_str(), target.string().c_str(),
                      ec.message().c_str());
            return sourcePath;
        }
        LOG_INFO("Scene", "Localized %s → %s",
                 src.string().c_str(), target.string().c_str());
    }
    return "scripts/" + stem + ext;
}

std::string App::localizeShader(const std::string& sourcePath) {
    if (sourcePath.empty()) return sourcePath;
    if (m_projectPath.empty()) {
        LOG_WARN("Shader",
            "Project not saved — shader stays at %s. Save the project to "
            "copy shaders into <project>/shaders/.", sourcePath.c_str());
        return sourcePath;
    }

    namespace fs = std::filesystem;
    fs::path shadersDir = m_projectPath / "shaders";
    std::error_code ec;
    fs::create_directories(shadersDir, ec);

    fs::path src(sourcePath);
    std::string stem = src.stem().string();
    std::string ext  = src.extension().string();
    if (ext.empty()) ext = ".frag";

    fs::path target = shadersDir / (stem + ext);

    // If a shader with the same stem already exists in the project:
    //   - If `target` IS `src` (source IS project-local), nothing to do.
    //   - Otherwise share it (user can Fork for a separate copy).
    if (!fs::exists(target)) {
        fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_ERROR("Shader", "Failed to copy %s → %s: %s",
                      src.string().c_str(), target.string().c_str(),
                      ec.message().c_str());
            return sourcePath;
        }
        LOG_INFO("Shader", "Localized %s → %s",
                 src.string().c_str(), target.string().c_str());
    }
    return "shaders/" + stem + ext;
}

void App::startVideoImport(int track, int scene, const std::string& sourcePath) {
    if (m_projectPath.empty()) return;

    // Ensure the slot has a VisualClip — create one named after the file
    // if necessary. The videoPath gets filled when the import finishes.
    auto* slot = m_project.getSlot(track, scene);
    if (!slot) return;
    if (!slot->visualClip) {
        auto vc = std::make_unique<visual::VisualClip>();
        std::filesystem::path p(sourcePath);
        vc->name       = p.stem().string();
        vc->colorIndex = m_project.track(track).colorIndex;
        m_project.setVisualClip(track, scene, std::move(vc));
    }

    auto importer = std::make_unique<visual::VideoImporter>();
    std::filesystem::path mediaDir = m_projectPath / "media";
    if (!importer->start(sourcePath, mediaDir)) {
        LOG_ERROR("Video", "Import failed to start: %s", sourcePath.c_str());
        m_toastManager.show(
            std::string("Video import failed: ") + importer->error(),
            3.0f, ui::ToastManager::Severity::Error);
        return;
    }

    PendingVideoImport pi;
    pi.track      = track;
    pi.scene      = scene;
    pi.sourcePath = sourcePath;
    pi.importer   = std::move(importer);
    m_pendingImports.push_back(std::move(pi));

    m_sessionPanel->setSlotImporting(track, scene, true);
    LOG_INFO("Video", "Import started: %s", sourcePath.c_str());
}

void App::onVideoImportDone(PendingVideoImport& pi) {
    const auto& r = pi.importer->result();
    auto* slot = m_project.getSlot(pi.track, pi.scene);
    if (slot && slot->visualClip) {
        slot->visualClip->videoPath       = r.videoPath;
        slot->visualClip->thumbnailPath   = r.thumbnailPath;
        slot->visualClip->videoSourcePath = pi.sourcePath;
        markDirty();
    }

    // If audio was extracted, append a new audio track named
    // "<stem> audio" and load the WAV as an audio clip at the same scene
    // so the user can scene-launch both together.
    if (!r.audioPath.empty()) {
        std::filesystem::path srcP(pi.sourcePath);
        std::string newTrackName = srcP.stem().string() + " audio";
        m_project.addTrack(newTrackName, Track::Type::Audio);
        int newTrack = m_project.numTracks() - 1;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{newTrack, 0});
        if (loadClipToSlot(r.audioPath, newTrack, pi.scene)) {
            LOG_INFO("Video", "Audio track created for %s at scene %d",
                      srcP.stem().string().c_str(), pi.scene);
        }
        syncTracksToEngine();
        markDirty();
    }

    m_sessionPanel->setSlotImporting(pi.track, pi.scene, false);
    LOG_INFO("Video", "Import done: %s", pi.sourcePath.c_str());
    m_toastManager.show("Video imported",
                        1.5f, ui::ToastManager::Severity::Info);
}

void App::showShaderLibraryMenu(float mx, float my) {
    // Refresh on every open so newly imported shaders appear without
    // restarting the app. Project root drives <project>/shaders/.
    if (!m_projectPath.empty())
        m_shaderLibrary.setProjectShadersDir(m_projectPath / "shaders");
    else
        m_shaderLibrary.setProjectShadersDir({});
    m_shaderLibrary.refresh();

    const auto& entries = m_shaderLibrary.entries();
    std::vector<ui::ContextMenu::Item> items;

    // The "+ Add Pass" menu only adds *effects* — passes that sample
    // iPrev and process the previous output. "Sources" (standalone
    // generators that ignore iPrev) would blank the chain if appended,
    // so we filter them out here. Project shaders are kept (the user
    // wrote them; they presumably know the convention they used).
    auto isEffectish = [](const std::string& cat) {
        return cat == "Effects" || cat == "Project";
    };
    bool anyEffect = false;
    for (const auto& e : entries) if (isEffectish(e.category)) { anyEffect = true; break; }

    if (entries.empty() || !anyEffect) {
        items.push_back({"(no chainable effects found)", nullptr, false, false});
    } else {
        // Group by category — Project then Effects, in that order.
        // Insert separator + heading rows so the menu reads like a
        // categorised picker.
        std::string lastCat;
        for (const auto& e : entries) {
            if (!isEffectish(e.category)) continue;
            if (e.category != lastCat) {
                if (!lastCat.empty())
                    items.push_back({"", nullptr, true, false}); // separator
                items.push_back({"── " + e.category + " ──", nullptr, false, false});
                lastCat = e.category;
            }
            std::string path = e.absolutePath;
            items.push_back({e.label, [this, path]() {
                if (m_selectedTrack < 0 ||
                    m_selectedTrack >= m_project.numTracks()) return;
                auto& trk = m_project.track(m_selectedTrack);
                visual::ShaderPass pass;
                // Store project-relative path when possible so projects
                // remain portable; fall back to absolute otherwise.
                pass.shaderPath = path;
                if (!m_projectPath.empty()) {
                    std::error_code ec;
                    auto rel = std::filesystem::relative(path, m_projectPath, ec);
                    if (!ec && !rel.empty() && rel.string().find("..") == std::string::npos)
                        pass.shaderPath = rel.generic_string();
                }
                trk.visualEffectChain.push_back(std::move(pass));
                // Re-launch the active clip so the engine picks up
                // the new chain entry. If no clip is launched on this
                // track yet, the chain just sits ready and applies
                // the next time one launches.
                int sc = (trk.defaultScene >= 0) ? trk.defaultScene
                                                   : m_selectedScene;
                auto* slot = m_project.getSlot(m_selectedTrack, sc);
                if (slot && slot->visualClip) {
                    launchVisualClipData(m_selectedTrack, *slot->visualClip,
                                          slot->visualClip->firstShaderPath());
                }
                markDirty();
                updateDetailForSelectedTrack();
            }, false, true});
        }
    }

    ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
}

void App::showMacroMappingMenu(const MacroTarget& target, float mx, float my) {
    if (m_selectedTrack < 0 ||
        m_selectedTrack >= m_project.numTracks()) return;
    auto& macros = m_project.track(m_selectedTrack).macros;

    // Find current mapping (if any) for this exact target. Targets
    // match on (kind, index, paramName); a parameter can only be
    // mapped to one macro at a time — picking a new macro replaces.
    auto matches = [&](const MacroMapping& m) {
        return m.target.kind      == target.kind  &&
               m.target.index     == target.index &&
               m.target.paramName == target.paramName;
    };
    int   currentMacro = -1;
    float currentMin   = 0.0f;
    float currentMax   = 1.0f;
    for (size_t i = 0; i < macros.mappings.size(); ++i) {
        if (matches(macros.mappings[i])) {
            currentMacro = macros.mappings[i].macroIdx;
            currentMin   = macros.mappings[i].rangeMin;
            currentMax   = macros.mappings[i].rangeMax;
            break;
        }
    }

    // Helper: locate the live mapping in the chain (or nullptr) so
    // the range-edit lambdas don't re-search every fire.
    auto findMapping = [this, target]() -> MacroMapping* {
        if (m_selectedTrack < 0 ||
            m_selectedTrack >= m_project.numTracks()) return nullptr;
        auto& chain = m_project.track(m_selectedTrack).macros.mappings;
        for (auto& m : chain) {
            if (m.target.kind      == target.kind  &&
                m.target.index     == target.index &&
                m.target.paramName == target.paramName) {
                return &m;
            }
        }
        return nullptr;
    };

    auto setMapping = [this, target, currentMin, currentMax](int macroIdx) {
        if (m_selectedTrack < 0 ||
            m_selectedTrack >= m_project.numTracks()) return;
        auto& chain = m_project.track(m_selectedTrack).macros.mappings;
        // Replace any existing mapping for this exact target before
        // adding the new one — one macro per parameter. We carry the
        // existing range across so re-routing to a different macro
        // doesn't silently widen the user's carefully-set sub-range.
        chain.erase(std::remove_if(chain.begin(), chain.end(),
            [&](const MacroMapping& m) {
                return m.target.kind      == target.kind  &&
                       m.target.index     == target.index &&
                       m.target.paramName == target.paramName;
            }), chain.end());
        MacroMapping nm;
        nm.macroIdx = macroIdx;
        nm.target   = target;
        nm.rangeMin = currentMin;
        nm.rangeMax = currentMax;
        chain.push_back(std::move(nm));
        markDirty();
    };

    auto unmap = [this, target]() {
        if (m_selectedTrack < 0 ||
            m_selectedTrack >= m_project.numTracks()) return;
        auto& chain = m_project.track(m_selectedTrack).macros.mappings;
        chain.erase(std::remove_if(chain.begin(), chain.end(),
            [&](const MacroMapping& m) {
                return m.target.kind      == target.kind  &&
                       m.target.index     == target.index &&
                       m.target.paramName == target.paramName;
            }), chain.end());
        markDirty();
    };

    std::vector<ui::ContextMenu::Item> items;
    // Header row — disabled, just shows what we're mapping.
    {
        std::string title = "Map '" + target.paramName + "' to:";
        items.push_back({title, nullptr, false, false});
        items.push_back({"", nullptr, true});  // separator
    }

    // 8 macros — show "Macro N" + custom label if any. Currently
    // mapped macro carries a "✓" prefix (cheap visual hint pre-4.2
    // proper card UI).
    for (int i = 0; i < MacroDevice::kNumMacros; ++i) {
        std::string label = (i == currentMacro ? "✓ Macro " : "   Macro ")
                             + std::to_string(i + 1);
        if (!macros.labels[i].empty())
            label += ": " + macros.labels[i];
        items.push_back({label, [setMapping, i]() { setMapping(i); },
                          false, true});
    }

    if (currentMacro >= 0) {
        items.push_back({"", nullptr, true});  // separator

        // Sub-range section. Header shows the live range (lets the
        // user verify in one glance without typing). The five action
        // items below cover the common edits — typed bounds for
        // precision, presets for speed, invert as a one-click flip.
        {
            char rangeLabel[64];
            std::snprintf(rangeLabel, sizeof(rangeLabel),
                           "Range: %.2f .. %.2f", currentMin, currentMax);
            items.push_back({rangeLabel, nullptr, false, false});
        }

        items.push_back({"Set min…", [this, findMapping]() {
            auto* m = findMapping();
            if (!m) return;
            char def[16];
            std::snprintf(def, sizeof(def), "%.3f", m->rangeMin);
            m_textInputDialog.prompt("Range min", def,
                [this, findMapping](const std::string& text) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                    auto* mm = findMapping();
                    if (!mm) return;
                    try { mm->rangeMin = std::stof(text); }
                    catch (...) { return; }
                    markDirty();
                });
        }, false, true});

        items.push_back({"Set max…", [this, findMapping]() {
            auto* m = findMapping();
            if (!m) return;
            char def[16];
            std::snprintf(def, sizeof(def), "%.3f", m->rangeMax);
            m_textInputDialog.prompt("Range max", def,
                [this, findMapping](const std::string& text) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                    auto* mm = findMapping();
                    if (!mm) return;
                    try { mm->rangeMax = std::stof(text); }
                    catch (...) { return; }
                    markDirty();
                });
        }, false, true});

        items.push_back({"Invert range",
            [this, findMapping]() {
                auto* m = findMapping();
                if (!m) return;
                std::swap(m->rangeMin, m->rangeMax);
                markDirty();
            },
            false,
            // Only meaningful when min != max — disable on a
            // pinched mapping so a stray click can't no-op visibly.
            std::abs(currentMax - currentMin) > 1e-6f});

        items.push_back({"Reset range (0..1)",
            [this, findMapping]() {
                auto* m = findMapping();
                if (!m) return;
                m->rangeMin = 0.0f;
                m->rangeMax = 1.0f;
                markDirty();
            }, false,
            // Disabled when already at the default — same logic the
            // LFO menu uses for its preset checkmarks.
            std::abs(currentMin) > 1e-6f ||
            std::abs(currentMax - 1.0f) > 1e-6f});

        items.push_back({"", nullptr, true});  // separator
        items.push_back({"Unmap", [unmap]() { unmap(); }, false, true});
    }

    ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
}

void App::showVisualKnobLFOMenu(int knobIdx, float mx, float my) {
    if (knobIdx < 0 || knobIdx >= 8) return;
    if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
    if (m_project.track(m_selectedTrack).type != Track::Type::Visual) return;

    // Pull current LFO state — prefer the engine's live copy when a
    // layer exists (already kept in sync with the macro device on
    // every launch), otherwise fall back to the track's macros.lfos
    // directly. The fallback path is what makes the menu open even
    // when no clip has been launched on this track yet — useful for
    // configuring a tempo-synced LFO before pressing play.
    visual::VisualLFO snapshot;
    if (const visual::VisualLFO* live =
            m_visualEngine.getLayerKnobLFO(m_selectedTrack, knobIdx)) {
        snapshot = *live;
    } else {
        const auto& s =
            m_project.track(m_selectedTrack).macros.lfos[knobIdx];
        snapshot.enabled = s.enabled;
        snapshot.shape   = static_cast<visual::VisualLFO::Shape>(s.shape);
        snapshot.rate    = s.rate;
        snapshot.depth   = s.depth;
        snapshot.sync    = s.sync;
    }
    // `cur` is kept as a const ref to `snapshot` so the rest of this
    // function (which inspects `cur->...` to mark menu items checked)
    // stays unchanged whether we sourced from the engine or the
    // project model.
    const visual::VisualLFO* cur = &snapshot;

    auto apply = [this, knobIdx](visual::VisualLFO lfo) {
        m_visualEngine.setLayerKnobLFO(m_selectedTrack, knobIdx, lfo);
        // Mirror into the track-level macro device for persistence.
        // (Phase 4.1: LFO state moved off VisualClip — modulators
        // belong with the macro values they drive.)
        if (m_selectedTrack >= 0 &&
            m_selectedTrack < m_project.numTracks() &&
            knobIdx >= 0 && knobIdx < MacroDevice::kNumMacros) {
            auto& saved = m_project.track(m_selectedTrack).macros.lfos[knobIdx];
            saved.enabled = lfo.enabled;
            saved.shape   = static_cast<uint8_t>(lfo.shape);
            saved.rate    = lfo.rate;
            saved.depth   = lfo.depth;
            saved.sync    = lfo.sync;
            markDirty();
        }
    };

    std::vector<ui::ContextMenu::Item> items;
    char title[32];
    std::snprintf(title, sizeof(title), "Knob %c",
                   static_cast<char>('A' + knobIdx));
    items.push_back({title, nullptr, false, false});  // header (disabled)
    items.push_back({"", nullptr, true});  // separator

    // MIDI Learn / Remove Mapping — routes a MIDI CC to this knob.
    {
        auto tgt = automation::AutomationTarget::visualKnob(m_selectedTrack, knobIdx);
        bool mapped = false;
        std::string mappedLabel;
        for (const auto& mm : m_midiLearnManager.mappings()) {
            if (mm.target == tgt) { mapped = true; mappedLabel = mm.label(); break; }
        }
        const bool learning = m_midiLearnManager.isLearning() &&
                              m_midiLearnManager.learnTarget() == tgt;
        if (learning) {
            items.push_back({"Cancel MIDI Learn", [this]() {
                m_midiLearnManager.cancelLearn();
            }, false, true});
        } else if (mapped) {
            std::string label = "MIDI: " + mappedLabel + "  (click to re-learn)";
            items.push_back({label, [this, tgt]() {
                m_midiLearnManager.startLearn(tgt, 0.0f, 1.0f);
            }, false, true});
            items.push_back({"Remove MIDI Mapping", [this, tgt]() {
                m_midiLearnManager.removeByTarget(tgt);
            }, false, true});
        } else {
            items.push_back({"MIDI Learn…", [this, tgt]() {
                m_midiLearnManager.startLearn(tgt, 0.0f, 1.0f);
            }, false, true});
        }
        items.push_back({"", nullptr, true});
    }

    items.push_back({"-- LFO --", nullptr, false, false});

    // On/off toggle — shown checked when disabled so the active state stays.
    items.push_back({cur->enabled ? "Disable LFO" : "Enable LFO",
        [apply, snapshot]() mutable {
            snapshot.enabled = !snapshot.enabled;
            apply(snapshot);
        }, false, true});

    items.push_back({"", nullptr, true});

    // Shape submenu.
    std::vector<ui::ContextMenu::Item> shapeItems;
    const char* shapeNames[] = {"Sine", "Triangle", "Saw", "Square", "Sample & Hold"};
    for (int s = 0; s < 5; ++s) {
        shapeItems.push_back({shapeNames[s],
            [apply, snapshot, s]() mutable {
                snapshot.shape = static_cast<visual::VisualLFO::Shape>(s);
                snapshot.enabled = true;
                apply(snapshot);
            }, false, static_cast<int>(cur->shape) != s});
    }
    items.push_back({"Shape", nullptr, false, true, std::move(shapeItems)});

    // Rate submenu (beats per cycle).
    std::vector<ui::ContextMenu::Item> rateItems;
    struct RatePreset { const char* label; float beats; };
    static const RatePreset rates[] = {
        {"1/16",   0.25f}, {"1/8",   0.5f}, {"1/4",   1.0f}, {"1/2",   2.0f},
        {"1 bar",  4.0f},  {"2 bars",8.0f}, {"4 bars", 16.0f},
    };
    for (const auto& rp : rates) {
        rateItems.push_back({rp.label,
            [apply, snapshot, rp]() mutable {
                snapshot.rate = rp.beats;
                snapshot.sync = true;
                snapshot.enabled = true;
                apply(snapshot);
            }, false, std::abs(cur->rate - rp.beats) > 0.001f});
    }
    items.push_back({"Rate", nullptr, false, true, std::move(rateItems)});

    // Depth submenu.
    std::vector<ui::ContextMenu::Item> depthItems;
    static const float depths[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
    static const char* depthLabels[] = {"10%", "25%", "50%", "75%", "100%"};
    for (int d = 0; d < 5; ++d) {
        float dv = depths[d];
        depthItems.push_back({depthLabels[d],
            [apply, snapshot, dv]() mutable {
                snapshot.depth = dv;
                snapshot.enabled = true;
                apply(snapshot);
            }, false, std::abs(cur->depth - dv) > 0.001f});
    }
    items.push_back({"Depth", nullptr, false, true, std::move(depthItems)});

    ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
}

void App::showSceneContextMenu(int sceneIndex, float mx, float my) {
    std::vector<ui::ContextMenu::Item> items;
    bool canDelete = m_project.numScenes() > 1;

    items.push_back({"Insert Scene", [this, sceneIndex]() {
        m_project.insertScene(sceneIndex);
        markDirty();
        m_undoManager.push({"Insert Scene",
            [this, sceneIndex]{
                m_project.deleteScene(sceneIndex);
                markDirty();
            },
            [this, sceneIndex]{
                m_project.insertScene(sceneIndex);
                markDirty();
            }, ""});
    }});

    items.push_back({"Duplicate Scene", [this, sceneIndex]() {
        m_project.duplicateScene(sceneIndex);
        markDirty();
        int dst = sceneIndex + 1;
        m_undoManager.push({"Duplicate Scene",
            [this, dst]{
                m_project.deleteScene(dst);
                markDirty();
            },
            [this, sceneIndex]{
                m_project.duplicateScene(sceneIndex);
                markDirty();
            }, ""});
    }});

    items.push_back({"Delete Scene", [this, sceneIndex]() {
        // Capture all clip data for undo
        struct SlotBackup {
            std::unique_ptr<audio::Clip> audioClip;
            std::unique_ptr<midi::MidiClip> midiClip;
            audio::QuantizeMode launchQuantize;
            FollowAction followAction;
            std::vector<automation::AutomationLane> clipAutomation;
        };
        auto sceneName = m_project.scene(sceneIndex).name;
        std::vector<SlotBackup> backups;
        for (int t = 0; t < m_project.numTracks(); ++t) {
            auto* slot = m_project.getSlot(t, sceneIndex);
            SlotBackup b;
            if (slot) {
                if (slot->audioClip) b.audioClip = slot->audioClip->clone();
                if (slot->midiClip)  b.midiClip  = slot->midiClip->clone();
                b.launchQuantize = slot->launchQuantize;
                b.followAction   = slot->followAction;
                b.clipAutomation = slot->clipAutomation;
            }
            backups.push_back(std::move(b));
        }
        // Stop any playing clips in this scene
        for (int t = 0; t < m_project.numTracks(); ++t) {
            m_audioEngine.sendCommand(audio::StopClipMsg{t});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{t});
        }
        m_project.deleteScene(sceneIndex);
        markDirty();
        auto shared = std::make_shared<std::vector<SlotBackup>>(std::move(backups));
        m_undoManager.push({"Delete Scene",
            [this, sceneIndex, sceneName, shared]{
                m_project.insertScene(sceneIndex);
                m_project.scene(sceneIndex).name = sceneName;
                for (int t = 0; t < m_project.numTracks() && t < static_cast<int>(shared->size()); ++t) {
                    auto* slot = m_project.getSlot(t, sceneIndex);
                    auto& b = (*shared)[t];
                    if (slot) {
                        if (b.audioClip) slot->audioClip = b.audioClip->clone();
                        if (b.midiClip)  slot->midiClip  = b.midiClip->clone();
                        slot->launchQuantize = b.launchQuantize;
                        slot->followAction   = b.followAction;
                        slot->clipAutomation = b.clipAutomation;
                    }
                }
                markDirty();
            },
            [this, sceneIndex]{
                for (int t = 0; t < m_project.numTracks(); ++t) {
                    m_audioEngine.sendCommand(audio::StopClipMsg{t});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{t});
                }
                m_project.deleteScene(sceneIndex);
                markDirty();
            }, ""});
    }, false, canDelete});

    ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
}

void App::showArrangementClipContextMenu(int trackIndex, int clipIdx,
                                         float mx, float my) {
    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
    auto& clips = m_project.track(trackIndex).arrangementClips;
    if (clipIdx < 0 || clipIdx >= static_cast<int>(clips.size())) return;

    std::vector<ui::ContextMenu::Item> items;

    items.push_back({"Copy", [this, trackIndex, clipIdx]() {
        auto& cs = m_project.track(trackIndex).arrangementClips;
        if (clipIdx >= 0 && clipIdx < static_cast<int>(cs.size())) {
            m_arrangementClipboard = cs[clipIdx];
            m_arrangementClipboardValid = true;
        }
    }, false, true});

    items.push_back({"Cut", [this, trackIndex, clipIdx]() {
        auto& cs = m_project.track(trackIndex).arrangementClips;
        if (clipIdx < 0 || clipIdx >= static_cast<int>(cs.size())) return;
        m_arrangementClipboard      = cs[clipIdx];
        m_arrangementClipboardValid = true;
        // Reuse panel's delete path so selection + undo stay consistent.
        m_arrangementPanel->setSelectedClip(trackIndex, clipIdx);
        m_arrangementPanel->handleAppKey(SDLK_DELETE, /*ctrl=*/false);
    }, false, true});

    // Paste: enabled only if the clipboard holds something compatible
    // with this track's type (audio→Audio, midi→Midi, visual→Visual).
    const auto trkType  = m_project.track(trackIndex).type;
    const bool canPaste = m_arrangementClipboardValid && (
        (trkType == Track::Type::Audio  && m_arrangementClipboard.type == ArrangementClip::Type::Audio)  ||
        (trkType == Track::Type::Midi   && m_arrangementClipboard.type == ArrangementClip::Type::Midi)   ||
        (trkType == Track::Type::Visual && m_arrangementClipboard.type == ArrangementClip::Type::Visual));
    items.push_back({"Paste", [this, trackIndex]() {
        if (!m_arrangementClipboardValid) return;
        ArrangementClip ac = m_arrangementClipboard;
        ac.startBeat = m_audioEngine.transport().positionInBeats();
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
    }, false, canPaste});

    items.push_back({"", nullptr, true}); // separator

    items.push_back({"Duplicate", [this, trackIndex, clipIdx]() {
        m_arrangementPanel->setSelectedClip(trackIndex, clipIdx);
        m_arrangementPanel->handleAppKey(SDLK_D, /*ctrl=*/true);
    }, false, true});

    items.push_back({"Delete", [this, trackIndex, clipIdx]() {
        m_arrangementPanel->setSelectedClip(trackIndex, clipIdx);
        m_arrangementPanel->handleAppKey(SDLK_DELETE, /*ctrl=*/false);
    }, false, true});

    ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
}

void App::performClipDragDrop(int srcT, int srcS, int dstT, int dstS, bool isCopy) {
    auto* srcSlot = m_project.getSlot(srcT, srcS);
    auto* dstSlot = m_project.getSlot(dstT, dstS);
    if (!srcSlot || !dstSlot || srcSlot->empty()) return;

    // Capture state for undo
    std::unique_ptr<audio::Clip> oldDstAudio;
    std::unique_ptr<midi::MidiClip> oldDstMidi;
    FollowAction oldDstFA = dstSlot->followAction;
    auto oldDstAuto = dstSlot->clipAutomation;
    auto oldDstQuant = dstSlot->launchQuantize;
    if (dstSlot->audioClip) oldDstAudio = dstSlot->audioClip->clone();
    if (dstSlot->midiClip) oldDstMidi = dstSlot->midiClip->clone();

    std::unique_ptr<audio::Clip> oldSrcAudio;
    std::unique_ptr<midi::MidiClip> oldSrcMidi;
    FollowAction oldSrcFA = srcSlot->followAction;
    auto oldSrcAuto = srcSlot->clipAutomation;
    auto oldSrcQuant = srcSlot->launchQuantize;
    if (srcSlot->audioClip) oldSrcAudio = srcSlot->audioClip->clone();
    if (srcSlot->midiClip) oldSrcMidi = srcSlot->midiClip->clone();

    // Stop playback on source track if moving (not copying)
    if (!isCopy) {
        if (srcSlot->audioClip)
            m_audioEngine.sendCommand(audio::StopClipMsg{srcT});
        else if (srcSlot->midiClip)
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{srcT});
    }

    // Stop playback on destination track if occupied
    if (!dstSlot->empty()) {
        if (dstSlot->audioClip)
            m_audioEngine.sendCommand(audio::StopClipMsg{dstT});
        else if (dstSlot->midiClip)
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{dstT});
    }

    // Perform the operation
    if (isCopy)
        m_project.copySlot(srcT, srcS, dstT, dstS);
    else
        m_project.moveSlot(srcT, srcS, dstT, dstS);

    markDirty();

    std::string action = isCopy ? "Copy Clip" : "Move Clip";
    m_undoManager.push({action,
        [this, srcT, srcS, dstT, dstS,
         srcAudio = std::shared_ptr<audio::Clip>(std::move(oldSrcAudio)),
         srcMidi = std::shared_ptr<midi::MidiClip>(std::move(oldSrcMidi)),
         srcFA = oldSrcFA, srcAuto = oldSrcAuto, srcQuant = oldSrcQuant,
         dstAudio = std::shared_ptr<audio::Clip>(std::move(oldDstAudio)),
         dstMidi = std::shared_ptr<midi::MidiClip>(std::move(oldDstMidi)),
         dstFA = oldDstFA, dstAuto = oldDstAuto, dstQuant = oldDstQuant]() {
            // Undo: restore both slots to original state
            auto* src = m_project.getSlot(srcT, srcS);
            auto* dst = m_project.getSlot(dstT, dstS);
            if (!src || !dst) return;
            src->audioClip = srcAudio ? srcAudio->clone() : nullptr;
            src->midiClip  = srcMidi  ? srcMidi->clone()  : nullptr;
            src->followAction = srcFA;
            src->clipAutomation = srcAuto;
            src->launchQuantize = srcQuant;
            dst->audioClip = dstAudio ? dstAudio->clone() : nullptr;
            dst->midiClip  = dstMidi  ? dstMidi->clone()  : nullptr;
            dst->followAction = dstFA;
            dst->clipAutomation = dstAuto;
            dst->launchQuantize = dstQuant;
            markDirty();
        },
        [this, srcT, srcS, dstT, dstS, isCopy]() {
            // Redo
            if (isCopy) m_project.copySlot(srcT, srcS, dstT, dstS);
            else        m_project.moveSlot(srcT, srcS, dstT, dstS);
            markDirty();
        },
        ""
    });
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
                &vslot->clipAutomation,
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
        m_detailPanel->setClipAutomation(&slot->clipAutomation, m_selectedTrack);
        return;
    }

    auto* midiChain = &m_audioEngine.midiEffectChain(m_selectedTrack);
    auto* inst = m_audioEngine.instrument(m_selectedTrack);
    auto* fxChain = &m_audioEngine.mixer().trackEffects(m_selectedTrack);

    m_detailPanel->setDeviceChain(midiChain, inst, fxChain);

    // Wire clip automation if a MIDI clip is selected
    if (slot && slot->midiClip) {
        m_detailPanel->setClipAutomation(&slot->clipAutomation, m_selectedTrack);
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

bool App::init() {
    // App metadata must be set before SDL_Init so Wayland uses the right
    // app_id (must match the .desktop file basename to get the dock icon)
    // and X11 gets a proper WM_CLASS.
    SDL_SetAppMetadata("Y.A.W.N", YAWN_VERSION_STRING, "com.yawn.daw");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG_ERROR("App", "Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    ui::WindowConfig winConfig;
    winConfig.title = "Y.A.W.N — Yet Another Audio Workstation New";
    winConfig.width = 1280;
    winConfig.height = 800;
    winConfig.resizable = true;

    if (!m_mainWindow.create(winConfig)) return false;

    // Query display DPI scale for HiDPI support
    float displayScale = SDL_GetWindowDisplayScale(m_mainWindow.getHandle());
    if (displayScale > 0.0f) {
        ui::Theme::scaleFactor = displayScale;
        // fw2 context needs the same scale — its dispatchMouseMove
        // divides raw event deltas by dpiScale to produce the
        // "logical" deltas widgets base drag math on. Without this,
        // HiDPI knobs would feel stiffer than their v1 counterparts
        // (or too fast, depending on which units SDL emits).
        m_fw2Context.setDpiScale(displayScale);
    }

    if (!m_renderer.init()) {
        LOG_ERROR("UI", "Failed to initialize 2D renderer");
        return false;
    }

    // Visual engine: creates secondary output window (hidden until toggled).
    // Shares GL resources with the main context, so creation happens while
    // the main context is still current.
    if (!m_visualEngine.init()) {
        LOG_WARN("Visual", "VisualEngine init failed — visual output disabled");
    }
    m_visualEngine.setAudioEngine(&m_audioEngine);
    m_mainWindow.makeCurrent();  // init() left the main context current, but be explicit

    // Load application icon — set window icon and create GL texture for About dialog
    {
        auto icon = util::loadIcon("assets/icon.png");
        if (!icon.pixels.empty()) {
            // SDL3 window icon
            SDL_Surface* surface = SDL_CreateSurfaceFrom(
                icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
                icon.pixels.data(), icon.width * 4);
            if (surface) {
                SDL_SetWindowIcon(m_mainWindow.getHandle(), surface);
                SDL_DestroySurface(surface);
            }
            // GL texture for About dialog
            m_iconTexture = util::createGLTexture(icon);
        }
    }

    loadFont();

    m_project.init();  // uses kDefaultNumTracks / kDefaultNumScenes
    setupDefaultTracks();
    m_virtualKeyboard.init(&m_audioEngine);
    setupMenuBar();
    buildWidgetTree();
    // v1 m_aboutDialog retired — fw2 About dialog is text-only for now.
    m_pianoRoll->setTransport(&m_audioEngine.transport());
    m_sessionPanel->init(&m_project, &m_audioEngine, &m_undoManager);
    m_mixerPanel->init(&m_project, &m_audioEngine, &m_midiEngine, &m_undoManager);
    m_mixerPanel->setLearnManager(&m_midiLearnManager);
    m_detailPanel->setLearnManager(&m_midiLearnManager);
    m_transportPanel->init(&m_project, &m_audioEngine, &m_undoManager);
    m_transportPanel->setLearnManager(&m_midiLearnManager);
    m_returnMasterPanel->init(&m_project, &m_audioEngine, &m_undoManager);
    m_returnMasterPanel->setLearnManager(&m_midiLearnManager);
    m_mixerPanel->setOnReturnToggle([this](bool show) {
        m_showReturns = show;
    });
    m_mixerPanel->setOnTrackSelected([this](int t) {
        LOG_INFO("User", "selectTrack %d (via mixer)", t);
        m_selectedTrack = t;
        m_detailTarget = DetailTarget::Track;
        m_virtualKeyboard.setTargetTrack(t);
        m_sessionPanel->setSelectedTrack(t);
        m_arrangementPanel->setSelectedTrack(t);
        if (m_showDetailPanel) updateDetailForSelectedTrack();
    });

    // Wire return/master strip clicks → open detail panel
    m_returnMasterPanel->setOnReturnClick([this](int bus) {
        m_detailTarget = DetailTarget::ReturnBus;
        m_detailReturnBus = bus;
        m_showDetailPanel = true;
        m_detailPanel->setOpen(true);
        updateDetailForReturnBus(bus);
    });
    m_returnMasterPanel->setOnMasterClick([this]() {
        m_detailTarget = DetailTarget::Master;
        m_detailReturnBus = -1;
        m_showDetailPanel = true;
        m_detailPanel->setOpen(true);
        updateDetailForMaster();
    });

    // Right-click on return/master strip → show "Add Effect" context menu
    // resolveChain returns the correct EffectChain* at execution time
    auto buildFxMenu = [this](DetailTarget target, int bus, float mx, float my) {
        auto resolveChain = [this, target, bus]() -> effects::EffectChain* {
            if (target == DetailTarget::ReturnBus) return &m_audioEngine.mixer().returnEffects(bus);
            if (target == DetailTarget::Master)    return &m_audioEngine.mixer().masterEffects();
            return nullptr;
        };

        std::vector<ui::ContextMenu::Item> fxItems;
        auto addFx = [&](const char* label, auto factory) {
            fxItems.push_back({label, [this, label, factory, target, bus, resolveChain]() {
                auto* chain = resolveChain();
                if (!chain) return;
                chain->append(factory());
                int slot = chain->count() - 1;
                markDirty();
                std::string fxName = label;
                m_undoManager.push({"Add Effect: " + fxName,
                    [this, resolveChain, slot]{
                        if (auto* c = resolveChain()) c->remove(slot);
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, resolveChain, factory]{
                        if (auto* c = resolveChain()) c->append(factory());
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                // Open detail panel to show the new effect
                m_detailTarget = target;
                m_detailReturnBus = bus;
                m_showDetailPanel = true;
                m_detailPanel->setOpen(true);
                m_detailPanel->clear(); // force rebuild
            }});
        };
        addFx("Reverb",          [](){ return std::make_unique<effects::Reverb>(); });
        addFx("Delay",           [](){ return std::make_unique<effects::Delay>(); });
        addFx("EQ",              [](){ return std::make_unique<effects::EQ>(); });
        addFx("Compressor",      [](){ return std::make_unique<effects::Compressor>(); });
        addFx("Limiter",         [](){ return std::make_unique<effects::Limiter>(); });
        addFx("Filter",          [](){ return std::make_unique<effects::Filter>(); });
        addFx("Chorus",          [](){ return std::make_unique<effects::Chorus>(); });
        addFx("Phaser",          [](){ return std::make_unique<effects::Phaser>(); });
        addFx("Wah",             [](){ return std::make_unique<effects::Wah>(); });
        addFx("Distortion",      [](){ return std::make_unique<effects::Distortion>(); });
        addFx("Bitcrusher",      [](){ return std::make_unique<effects::Bitcrusher>(); });
        addFx("Noise Gate",      [](){ return std::make_unique<effects::NoiseGate>(); });
        addFx("Ping-Pong Delay", [](){ return std::make_unique<effects::PingPongDelay>(); });
        addFx("Envelope Follower", [](){ return std::make_unique<effects::EnvelopeFollower>(); });
        addFx("Spline EQ",       [](){ return std::make_unique<effects::SplineEQ>(); });
        addFx("Neural Amp",      [](){ return std::make_unique<effects::NeuralAmp>(); });
        addFx("Conv Reverb",     [](){ return std::make_unique<effects::ConvolutionReverb>(); });
        addFx("Tape Emulation",  [](){ return std::make_unique<effects::TapeEmulation>(); });
        addFx("Amp Simulator",   [](){ return std::make_unique<effects::AmpSimulator>(); });
        addFx("Oscilloscope",    [](){ return std::make_unique<effects::Oscilloscope>(); });
        addFx("Spectrum",        [](){ return std::make_unique<effects::SpectrumAnalyzer>(); });
        addFx("Tuner",           [](){ return std::make_unique<effects::Tuner>(); });

#ifdef YAWN_HAS_VST3
        if (m_vst3Scanner && !m_vst3Scanner->effects().empty()) {
            fxItems.push_back({"── VST3 ──", nullptr, true});
            for (auto& info : m_vst3Scanner->effects()) {
                std::string vlabel = info.name;
                if (!info.vendor.empty()) vlabel += " (" + info.vendor + ")";
                std::string modulePath = info.modulePath;
                std::string classID = info.classIDString;
                fxItems.push_back({vlabel, [this, modulePath, classID, target, bus, resolveChain]() {
                    auto* chain = resolveChain();
                    if (!chain) return;
                    chain->append(std::make_unique<vst3::VST3Effect>(modulePath, classID));
                    markDirty();
                    m_detailTarget = target;
                    m_detailReturnBus = bus;
                    m_showDetailPanel = true;
                    m_detailPanel->setOpen(true);
                    m_detailPanel->clear();
                }});
            }
        }
#endif

        ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(fxItems)),
                                     ui::fw::Point{mx, my});
    };

    m_returnMasterPanel->setOnReturnRightClick([this, buildFxMenu](int bus, float mx, float my) {
        buildFxMenu(DetailTarget::ReturnBus, bus, mx, my);
    });
    m_returnMasterPanel->setOnMasterRightClick([this, buildFxMenu](float mx, float my) {
        buildFxMenu(DetailTarget::Master, -1, mx, my);
    });

    m_settings = util::AppSettings::load();

    // Apply persisted UI font scale to the fw2 theme before any widget
    // measures itself — multiplying the baked-in font sizes makes every
    // v2 widget (menu bar, dropdowns, dialogs …) pick up the scale
    // through theme().metrics.
    {
        ui::fw2::Theme t;
        const float s = std::max(0.5f, std::min(3.0f, m_settings.fontScale));
        t.metrics.fontSize      *= s;
        t.metrics.fontSizeSmall *= s;
        t.metrics.fontSizeLarge *= s;
        ui::fw2::setTheme(std::move(t));
    }

    audio::AudioEngineConfig audioConfig;
    audioConfig.sampleRate = m_settings.sampleRate;
    audioConfig.framesPerBuffer = m_settings.bufferSize;
    audioConfig.outputChannels = 2;
    audioConfig.outputDevice = m_settings.outputDevice;
    audioConfig.inputDevice = m_settings.inputDevice;

    if (!m_audioEngine.init(audioConfig)) {
        LOG_WARN("Audio", "Audio engine failed to initialize");
    } else if (!m_audioEngine.start()) {
        LOG_WARN("Audio", "Audio engine failed to start");
    }

    // ── Controller scripting (must init BEFORE MidiEngine to claim ports) ──
    m_controllerManager.init(&m_audioEngine, &m_project);
    m_controllerManager.setSelectedTrackGetter([this]() { return m_selectedTrack; });
    m_controllerManager.setSelectedTrackSetter([this](int t) {
        if (t >= 0 && t < m_project.numTracks()) {
            LOG_INFO("User", "selectTrack %d (via controller)", t);
            m_selectedTrack = t;
            if (m_sessionPanel) m_sessionPanel->setSelectedTrack(t);
        }
    });
    m_controllerManager.setCommandSender([this](const audio::AudioCommand& cmd) {
        m_audioEngine.sendCommand(cmd);
    });
    m_controllerManager.setTapTempoHandler([this]() {
        if (m_transportPanel) m_transportPanel->tapTempo();
    });
    m_controllerManager.setClipStateGetter([this](int track, int scene)
        -> controllers::ControllerManager::ClipSlotState {
        controllers::ControllerManager::ClipSlotState st;
        if (track < 0 || track >= m_project.numTracks()) return st;
        if (scene < 0 || scene >= m_project.numScenes()) return st;
        auto* slot = m_project.getSlot(track, scene);
        if (slot) {
            if (slot->audioClip) st.type = 1;
            else if (slot->midiClip) st.type = 2;
        }
        st.armed = m_project.track(track).armed;
        if (m_sessionPanel) {
            auto& ts = m_sessionPanel->trackState(track);
            st.playing = ts.playing && ts.playingScene == scene;
            st.recording = ts.recording && ts.recordingScene == scene;
        }
        return st;
    });
    m_controllerManager.setClipLauncher([this](int track, int scene) {
        if (track < 0 || track >= m_project.numTracks()) return;
        if (scene < 0 || scene >= m_project.numScenes()) return;
        if (m_sessionPanel)
            m_sessionPanel->launchClipSlot(track, scene);
    });
    m_controllerManager.setSceneLauncher([this](int scene) {
        if (scene < 0 || scene >= m_project.numScenes()) return;
        if (m_sessionPanel)
            m_sessionPanel->launchScene(scene);
    });
    m_controllerManager.setToastHandler(
        [this](const std::string& msg, float dur, int sev) {
            m_toastManager.show(msg, dur,
                static_cast<ui::ToastManager::Severity>(sev));
        });
    m_controllerManager.scanScripts("scripts/controllers");
    m_controllerManager.autoConnect();

    // Wire MidiEngine: scan ports, open inputs, connect to AudioEngine
    // Skip ports claimed by controller scripts (exclusive access on Windows)
    auto claimedInputs = m_controllerManager.claimedInputPortNames();
    m_midiEngine.refreshPorts();
    LOG_INFO("MIDI", "Available inputs: %d, claimed by controller: %d",
             m_midiEngine.availableInputCount(), static_cast<int>(claimedInputs.size()));
    for (int i = 0; i < m_midiEngine.availableInputCount(); ++i)
        LOG_INFO("MIDI", "  Input %d: '%s'", i, m_midiEngine.availableInputName(i).c_str());
    for (auto& cn : claimedInputs)
        LOG_INFO("MIDI", "  Claimed: '%s'", cn.c_str());

    if (!m_settings.enabledMidiInputs.empty()) {
        LOG_INFO("MIDI", "Opening user-configured inputs (%d)", static_cast<int>(m_settings.enabledMidiInputs.size()));
        for (int i : m_settings.enabledMidiInputs) {
            bool claimed = false;
            if (i < m_midiEngine.availableInputCount()) {
                for (auto& cn : claimedInputs)
                    if (m_midiEngine.availableInputName(i) == cn)
                    { claimed = true; break; }
            }
            if (!claimed) {
                LOG_INFO("MIDI", "  Opening input %d: '%s'", i,
                         i < m_midiEngine.availableInputCount() ? m_midiEngine.availableInputName(i).c_str() : "?");
                m_midiEngine.openInputPort(i);
            } else {
                LOG_INFO("MIDI", "  Skipping input %d: '%s' (claimed)", i, m_midiEngine.availableInputName(i).c_str());
            }
        }
    } else {
        LOG_INFO("MIDI", "Opening all available inputs");
        for (int i = 0; i < m_midiEngine.availableInputCount(); ++i) {
            bool claimed = false;
            for (auto& cn : claimedInputs)
                if (m_midiEngine.availableInputName(i) == cn)
                { claimed = true; break; }
            if (!claimed) {
                LOG_INFO("MIDI", "  Opening input %d: '%s'", i, m_midiEngine.availableInputName(i).c_str());
                m_midiEngine.openInputPort(i);
            } else {
                LOG_INFO("MIDI", "  Skipping input %d: '%s' (claimed)", i, m_midiEngine.availableInputName(i).c_str());
            }
        }
    }
    for (int i : m_settings.enabledMidiOutputs)
        m_midiEngine.openOutputPort(i);
    m_audioEngine.setMidiEngine(&m_midiEngine);
    m_midiEngine.setMonitorBuffer(&m_midiMonitor);
    // Controller-claimed ports bypass MidiEngine — route them into the
    // same monitor buffer so the MIDI Monitor panel shows Push / Move /
    // etc. activity just like any other input. Port-index base 100
    // keeps controller entries visually separate from regular inputs.
    m_controllerManager.setMonitorBuffer(&m_midiMonitor, 100);

    // Wire MIDI Learn manager
    m_midiEngine.setLearnManager(&m_midiLearnManager);
    m_midiEngine.setCommandSender([this](const audio::AudioCommand& cmd) {
        m_audioEngine.sendCommand(cmd);
    });

    // Wire MIDI monitor to browser panel
    m_browserPanel->setMidiMonitor(&m_midiMonitor);
    m_browserPanel->setPortNameFn([this](int idx) -> std::string {
        if (idx >= 0 && idx < m_midiEngine.availableInputCount())
            return m_midiEngine.availableInputName(idx);
        // Controller-claimed ports are forwarded to the monitor with
        // portIdxBase >= 100 (set in setMonitorBuffer above). Surface
        // them with the connected script's name, so the panel column
        // reads e.g. "Ableton Move" instead of a bare "100".
        if (idx >= 100 && m_controllerManager.isConnected())
            return m_controllerManager.connectedName();
        return std::to_string(idx);
    });

    // ── Library database & scanner ──
    if (m_libraryDb.open()) {
        m_libraryScanner = std::make_unique<library::LibraryScanner>(m_libraryDb);
        m_browserPanel->setLibraryDatabase(&m_libraryDb);
        m_browserPanel->setLibraryScanner(m_libraryScanner.get());

        // Files tab: "Add Folder" button → open folder dialog
        m_browserPanel->filesTab().setOnAddFolder([this]() {
            SDL_ShowOpenFolderDialog([](void* ud, const char* const* filelist, int) {
                auto* self = static_cast<App*>(ud);
                if (filelist && filelist[0]) {
                    self->m_libraryDb.addLibraryPath(filelist[0]);
                    auto paths = self->m_libraryDb.getLibraryPaths();
                    if (!paths.empty()) {
                        auto& lp = paths.back();
                        self->m_libraryScanner->scanLibraryPath(lp.id, lp.path);
                    }
                    self->m_browserPanel->filesTab().refreshTree();
                }
            }, this, m_mainWindow.getHandle(), nullptr, false);
        });

        // Files tab: double-click → load into selected clip slot
        m_browserPanel->filesTab().setOnFileDoubleClick([this](const std::string& path) {
            loadClipToSlot(path, m_selectedTrack, m_selectedScene);
        });

        // Files tab: remove folder
        m_browserPanel->filesTab().setOnRemoveFolder([this](int64_t pathId) {
            m_libraryDb.removeLibraryPath(pathId);
            m_browserPanel->filesTab().refreshTree();
        });

        // Files tab: rescan folder
        m_browserPanel->filesTab().setOnRescanFolder([this](int64_t pathId, const std::string& path) {
            m_libraryScanner->scanLibraryPath(pathId, path);
        });

        // Presets tab: double-click → load preset on selected track
        m_browserPanel->presetsTab().setOnPresetDoubleClick(
            [this](const std::string& path, const std::string& deviceId) {
                if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
                PresetData data;
                if (!PresetManager::loadPreset(path, data)) return;

                // Try to match against instrument first, then effects
                auto* inst = m_audioEngine.instrument(m_selectedTrack);
                if (inst && inst->id() == deviceId) {
                    loadDevicePreset(path, *inst);
                    updateDetailForSelectedTrack();
                    return;
                }
                auto& fxChain = m_audioEngine.mixer().trackEffects(m_selectedTrack);
                for (int i = 0; i < fxChain.count(); ++i) {
                    auto* fx = fxChain.effectAt(i);
                    if (fx && fx->id() == deviceId) {
                        loadDevicePreset(path, *fx);
                        updateDetailForSelectedTrack();
                        return;
                    }
                }
            });

        // Initial data load
        m_browserPanel->filesTab().refreshTree();
        m_browserPanel->presetsTab().refreshList();

        // Start background scan
        m_libraryScanner->startFullScan();
    }

    // Apply metronome settings from saved preferences
    m_audioEngine.sendCommand(audio::MetronomeSetVolumeMsg{m_settings.metronomeVolume});
    m_audioEngine.sendCommand(audio::MetronomeSetModeMsg{m_settings.metronomeMode});
    m_audioEngine.sendCommand(audio::TransportSetCountInMsg{m_settings.countInBars});
    m_transportPanel->setCountInBars(m_settings.countInBars);
    m_transportPanel->setMetronomeVisualStyle(m_settings.metronomeVisualStyle);

    // Sync project track properties to the audio engine
    syncTracksToEngine();

    // Wire mixer arm button to MidiEngine
    m_mixerPanel->setOnTrackArmedChanged([this](int trackIndex, bool armed) {
        LOG_INFO("User", "armTrack %d %s", trackIndex, armed ? "armed" : "disarmed");
        m_midiEngine.setTrackArmed(trackIndex, armed);
        // Arming a MIDI track selects it for virtual keyboard input
        if (armed && trackIndex < m_project.numTracks() &&
            m_project.track(trackIndex).type == Track::Type::Midi) {
            m_selectedTrack = trackIndex;
            m_virtualKeyboard.setTargetTrack(trackIndex);
            m_sessionPanel->setSelectedTrack(trackIndex);
            m_mixerPanel->setSelectedTrack(trackIndex);
        }
    });

    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

    // Cache system cursors
    m_cursorDefault  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    m_cursorEWResize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    m_cursorNSResize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    m_cursorMove     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);

    m_running = true;

    // Wire up detail panel remove device callback
    m_detailPanel->setOnRemoveDevice([this](ui::fw2::DetailPanelWidget::DeviceType type, int chainIndex) {
        const char* typeName =
            (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx)     ? "midiFx" :
            (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) ? "instrument" :
            (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx)    ? "audioFx" :
                                                                            "?";
        const char* targetName =
            (m_detailTarget == DetailTarget::Track)     ? "track" :
            (m_detailTarget == DetailTarget::ReturnBus) ? "returnBus" :
                                                          "master";
        LOG_INFO("User", "removeDevice type=%s target=%s/%d chainIdx=%d",
                 typeName, targetName,
                 m_detailTarget == DetailTarget::Track     ? m_selectedTrack :
                 m_detailTarget == DetailTarget::ReturnBus ? m_detailReturnBus : 0,
                 chainIndex);
        // Resolve which effect chain to operate on based on detail target
        auto getAudioFxChain = [this]() -> effects::EffectChain* {
            switch (m_detailTarget) {
                case DetailTarget::Track:
                    if (m_selectedTrack >= 0 && m_selectedTrack < m_project.numTracks())
                        return &m_audioEngine.mixer().trackEffects(m_selectedTrack);
                    return nullptr;
                case DetailTarget::ReturnBus:
                    if (m_detailReturnBus >= 0 && m_detailReturnBus < kMaxReturnBuses)
                        return &m_audioEngine.mixer().returnEffects(m_detailReturnBus);
                    return nullptr;
                case DetailTarget::Master:
                    return &m_audioEngine.mixer().masterEffects();
            }
            return nullptr;
        };

        switch (type) {
            case ui::fw2::DetailPanelWidget::DeviceType::MidiFx: {
                // MIDI effects only apply to tracks
                if (m_detailTarget != DetailTarget::Track) break;
                int t = m_selectedTrack;
                if (t < 0 || t >= m_project.numTracks()) break;
                auto* fx = m_audioEngine.midiEffectChain(t).effect(chainIndex);
                std::string fxName = fx ? fx->name() : "";
                m_audioEngine.midiEffectChain(t).removeEffect(chainIndex);
                m_audioEngine.sendCommand(audio::SendMidiToTrackMsg{
                    t, (uint8_t)midi::MidiMessage::Type::ControlChange, 0, 0, 0, 0, 123});
                LOG_INFO("MIDI", "Removed MIDI effect %d from track %d", chainIndex, t + 1);
                m_undoManager.push({"Remove MIDI Effect",
                    [this, t, chainIndex, fxName]{
                        auto inst = createMidiEffectByName(fxName);
                        if (inst) {
                            auto& chain = m_audioEngine.midiEffectChain(t);
                            chain.addEffect(std::move(inst));
                            for (int i = chain.count() - 1; i > chainIndex; --i)
                                chain.moveEffect(i, i - 1);
                        }
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, t, chainIndex]{
                        m_audioEngine.midiEffectChain(t).removeEffect(chainIndex);
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            case ui::fw2::DetailPanelWidget::DeviceType::AudioFx: {
                auto* chain = getAudioFxChain();
                if (!chain) break;
                auto* fx = chain->effectAt(chainIndex);
                std::string fxName = fx ? fx->name() : "";
                chain->remove(chainIndex);

                // Capture target state for undo
                auto target = m_detailTarget;
                int bus = m_detailReturnBus;
                int t = m_selectedTrack;

                auto resolveChain = [this, target, bus, t]() -> effects::EffectChain* {
                    switch (target) {
                        case DetailTarget::Track:     return &m_audioEngine.mixer().trackEffects(t);
                        case DetailTarget::ReturnBus: return &m_audioEngine.mixer().returnEffects(bus);
                        case DetailTarget::Master:    return &m_audioEngine.mixer().masterEffects();
                    }
                    return nullptr;
                };

                std::string label = (target == DetailTarget::Track)
                    ? "Remove Audio Effect" : (target == DetailTarget::ReturnBus)
                    ? "Remove Return Effect" : "Remove Master Effect";

                m_undoManager.push({label,
                    [this, resolveChain, chainIndex, fxName]{
                        auto inst = createAudioEffectByName(fxName);
                        if (inst) {
                            if (auto* c = resolveChain()) c->insert(chainIndex, std::move(inst));
                        }
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, resolveChain, chainIndex]{
                        if (auto* c = resolveChain()) c->remove(chainIndex);
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            default:
                break;
        }
        m_detailPanel->clear();
        markDirty();
    });

    m_detailPanel->setOnMoveDevice([this](ui::fw2::DetailPanelWidget::DeviceType type, int fromIdx, int toIdx) {
        switch (type) {
            case ui::fw2::DetailPanelWidget::DeviceType::MidiFx: {
                if (m_detailTarget != DetailTarget::Track) break;
                int t = m_selectedTrack;
                if (t < 0 || t >= m_project.numTracks()) break;
                m_audioEngine.midiEffectChain(t).moveEffect(fromIdx, toIdx);
                m_audioEngine.sendCommand(audio::ResetMidiEffectChainMsg{t});
                LOG_INFO("MIDI", "Moved MIDI effect %d to %d on track %d", fromIdx, toIdx, t + 1);
                m_undoManager.push({"Move MIDI Effect",
                    [this, t, fromIdx, toIdx]{
                        m_audioEngine.midiEffectChain(t).moveEffect(toIdx, fromIdx);
                        m_audioEngine.sendCommand(audio::ResetMidiEffectChainMsg{t});
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, t, fromIdx, toIdx]{
                        m_audioEngine.midiEffectChain(t).moveEffect(fromIdx, toIdx);
                        m_audioEngine.sendCommand(audio::ResetMidiEffectChainMsg{t});
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            case ui::fw2::DetailPanelWidget::DeviceType::AudioFx: {
                // Resolve the target chain
                effects::EffectChain* chain = nullptr;
                auto target = m_detailTarget;
                int bus = m_detailReturnBus;
                int t = m_selectedTrack;
                switch (target) {
                    case DetailTarget::Track:
                        if (t >= 0 && t < m_project.numTracks())
                            chain = &m_audioEngine.mixer().trackEffects(t);
                        break;
                    case DetailTarget::ReturnBus:
                        if (bus >= 0 && bus < kMaxReturnBuses)
                            chain = &m_audioEngine.mixer().returnEffects(bus);
                        break;
                    case DetailTarget::Master:
                        chain = &m_audioEngine.mixer().masterEffects();
                        break;
                }
                if (!chain) break;
                chain->moveEffect(fromIdx, toIdx);

                auto resolveChain = [this, target, bus, t]() -> effects::EffectChain* {
                    switch (target) {
                        case DetailTarget::Track:     return &m_audioEngine.mixer().trackEffects(t);
                        case DetailTarget::ReturnBus: return &m_audioEngine.mixer().returnEffects(bus);
                        case DetailTarget::Master:    return &m_audioEngine.mixer().masterEffects();
                    }
                    return nullptr;
                };

                std::string label = (target == DetailTarget::Track)
                    ? "Move Audio Effect" : (target == DetailTarget::ReturnBus)
                    ? "Move Return Effect" : "Move Master Effect";

                m_undoManager.push({label,
                    [this, resolveChain, fromIdx, toIdx]{
                        if (auto* c = resolveChain()) c->moveEffect(toIdx, fromIdx);
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, resolveChain, fromIdx, toIdx]{
                        if (auto* c = resolveChain()) c->moveEffect(fromIdx, toIdx);
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            default:
                break;
        }
        m_detailPanel->clear();
        markDirty();
    });

    // Wire automation recording: knob touch → AutoParamTouchMsg
    m_detailPanel->setOnParamTouch([this](int track, uint8_t tt, int ci, int pi, float v, bool touching) {
        m_audioEngine.sendCommand(audio::AutoParamTouchMsg{track, tt, ci, pi, v, touching});
    });

    // Right-click on an instrument / audio FX / MIDI FX knob →
    // build the MacroTarget for it and open the macro-mapping
    // context menu rooted at the click. The chain index is only
    // meaningful for the audio/MIDI effect kinds; instrument
    // mappings ignore it (target.index defaulted to 0).
    m_detailPanel->setOnParamRightClick(
        [this](int trackIdx, ui::fw2::DetailPanelWidget::DeviceType type,
               int chainIndex, const std::string& paramName,
               int /*paramIdx*/, float mx, float my) {
            if (trackIdx < 0) return;
            // Selecting a different track via the macro menu would
            // show the wrong track's macros — sync the selection so
            // showMacroMappingMenu (which reads m_selectedTrack)
            // operates on the device the user actually clicked.
            m_selectedTrack = trackIdx;
            MacroTarget t;
            t.paramName = paramName;
            switch (type) {
                case ui::fw2::DetailPanelWidget::DeviceType::Instrument:
                    t.kind  = MacroTarget::Kind::AudioInstrumentParam;
                    t.index = 0;
                    break;
                case ui::fw2::DetailPanelWidget::DeviceType::AudioFx:
                    t.kind  = MacroTarget::Kind::AudioEffectParam;
                    t.index = chainIndex;
                    break;
                case ui::fw2::DetailPanelWidget::DeviceType::MidiFx:
                    t.kind  = MacroTarget::Kind::MidiEffectParam;
                    t.index = chainIndex;
                    break;
            }
            showMacroMappingMenu(t, mx, my);
        });

    // Wire preset click: open context menu with preset list + Save
    m_detailPanel->setOnPresetClick([this](ui::fw2::DetailPanelWidget::DeviceType type,
                                           int chainIndex, float mx, float my) {
        // Resolve device pointer and device ID
        std::string deviceId;
        std::string deviceName;

        // Helper to get audio effect from the right chain
        auto getAudioEffect = [this](int chainIndex) -> effects::AudioEffect* {
            switch (m_detailTarget) {
                case DetailTarget::Track:
                    return m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex);
                case DetailTarget::ReturnBus:
                    return m_audioEngine.mixer().returnEffects(m_detailReturnBus).effectAt(chainIndex);
                case DetailTarget::Master:
                    return m_audioEngine.mixer().masterEffects().effectAt(chainIndex);
            }
            return nullptr;
        };

        if (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) {
            if (m_detailTarget != DetailTarget::Track) return;
            if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
            auto* inst = m_audioEngine.instrument(m_selectedTrack);
            if (!inst) return;
            deviceId = inst->id();
            deviceName = inst->name();
        } else if (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx) {
            auto* fx = getAudioEffect(chainIndex);
            if (!fx) return;
            deviceId = fx->id();
            deviceName = fx->name();
        } else if (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx) {
            if (m_detailTarget != DetailTarget::Track) return;
            if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
            auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
            if (!fx) return;
            deviceId = fx->id();
            deviceName = fx->name();
        }
        auto presets = PresetManager::listPresetsForDevice(deviceId);
        std::vector<ui::ContextMenu::Item> items;

        // Add "Save Preset..." item
        auto target = m_detailTarget;
        int bus = m_detailReturnBus;
        items.push_back({"Save Preset...", [this, type, chainIndex, deviceId, deviceName, target, bus]() {
            SDL_StartTextInput(m_mainWindow.getHandle());
            m_textInputDialog.prompt("Save Preset", "My Preset",
                [this, type, chainIndex, deviceId, deviceName, target, bus](const std::string& name) {
                    std::filesystem::path saved;
                    if (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) {
                        auto* inst = m_audioEngine.instrument(m_selectedTrack);
                        if (inst) saved = saveDevicePreset(name, *inst);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx) {
                        effects::AudioEffect* fx = nullptr;
                        switch (target) {
                            case DetailTarget::Track:     fx = m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex); break;
                            case DetailTarget::ReturnBus: fx = m_audioEngine.mixer().returnEffects(bus).effectAt(chainIndex); break;
                            case DetailTarget::Master:    fx = m_audioEngine.mixer().masterEffects().effectAt(chainIndex); break;
                        }
                        if (fx) saved = saveDevicePreset(name, *fx);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx) {
                        auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
                        if (fx) saved = saveDevicePreset(name, *fx);
                    }
                    if (!saved.empty()) {
                        m_detailPanel->setDevicePresetName(type, chainIndex, name);
                        LOG_INFO("Preset", "Saved '%s' for %s", name.c_str(), deviceName.c_str());
                        // Re-scan the presets folder so the new file lands
                        // in the library DB → Browser's Presets tab picks
                        // it up on its next render. Without this, the file
                        // is on disk but the DB-backed list stays stale
                        // until the next full library scan / app restart.
                        // scanPresets() is async (worker thread) and does
                        // an UPSERT per file, so re-running it on every
                        // save is cheap (~ms for hundreds of files).
                        if (m_libraryScanner) m_libraryScanner->scanPresets();
                    }
                });
        }});

        // Separator if there are presets
        if (!presets.empty()) {
            items.push_back({"", nullptr, true}); // separator
        }

        // Add each preset as a loadable item
        for (const auto& preset : presets) {
            std::filesystem::path path = preset.filePath;
            std::string pName = preset.name;
            items.push_back({preset.name,
                [this, type, chainIndex, path, pName, target, bus]() {
                    bool ok = false;
                    if (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) {
                        auto* inst = m_audioEngine.instrument(m_selectedTrack);
                        if (inst) ok = loadDevicePreset(path, *inst);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx) {
                        effects::AudioEffect* fx = nullptr;
                        switch (target) {
                            case DetailTarget::Track:     fx = m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex); break;
                            case DetailTarget::ReturnBus: fx = m_audioEngine.mixer().returnEffects(bus).effectAt(chainIndex); break;
                            case DetailTarget::Master:    fx = m_audioEngine.mixer().masterEffects().effectAt(chainIndex); break;
                        }
                        if (fx) ok = loadDevicePreset(path, *fx);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx) {
                        auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
                        if (fx) ok = loadDevicePreset(path, *fx);
                    }
                    if (ok) {
                        m_detailPanel->setDevicePresetName(type, chainIndex, pName);
                        m_detailPanel->clear(); // force rebuild
                        LOG_INFO("Preset", "Loaded '%s'", pName.c_str());
                    }
                }
            });
        }

        ui::fw2::ContextMenu::show(ui::fw2::v1ItemsToFw2(std::move(items)),
                                 ui::fw::Point{mx, my});
    });

    // Wire Auto-Sample request from MultisamplerDisplayPanel.
    // Builds the dialog Context from current project + engine state and
    // opens the modal. Refreshes the detail panel on completion so newly-
    // populated zones show up in the zone list.
    m_detailPanel->setOnAutoSampleRequested([this](instruments::Multisampler* ms) {
        if (!ms) return;
        if (m_projectPath.empty()) {
            // Captured samples live under <project>/samples/, so we need
            // a saved project as the destination root. Surface this as a
            // visible modal rather than just a log line — silently
            // doing nothing is the worst kind of UX feedback.
            LOG_WARN("AutoSample",
                "Save the project first — captured samples live under "
                "<project>/samples/.");
            ui::fw2::DialogSpec spec;
            spec.title   = "Save Project First";
            spec.message =
                "Auto-Sample captures land in <project>/samples/<name>/, "
                "which needs a saved project to write to.\n\n"
                "Use File → Save As… to give this project a folder, "
                "then click Auto-Sample again.";
            spec.buttons = { {"OK", {}, /*primary*/true, /*cancel*/true} };
            ui::fw2::Dialog::show(std::move(spec));
            return;
        }
        // Build a default capture name from the current track's name,
        // so a track called "MIDI 1" gets "midi_1_capture" by default.
        std::string trackName;
        if (m_selectedTrack >= 0 && m_selectedTrack < m_project.numTracks()) {
            trackName = m_project.track(m_selectedTrack).name;
        }

        ui::fw2::FwAutoSampleDialog::Context ctx;
        ctx.engine             = &m_audioEngine;
        ctx.midi               = &m_midiEngine;
        ctx.target             = ms;
        ctx.samplesRoot        = m_projectPath / "samples";
        ctx.defaultCaptureName = audio::defaultCaptureName(trackName);

        m_autoSampleDialog.setOnResult(
            [this](ui::fw2::FwAutoSampleDialog::Result r) {
                // Always release SDL text-input mode on close so
                // subsequent focus changes don't keep the IME armed.
                SDL_StopTextInput(m_mainWindow.getHandle());
                if (r == ui::fw2::FwAutoSampleDialog::Result::Done) {
                    // Force the detail panel to rebuild so the new zones
                    // populated by the worker show up in the zone list.
                    m_detailPanel->clear();
                    markDirty();
                    LOG_INFO("AutoSample", "Capture finished — zones added");
                } else if (r == ui::fw2::FwAutoSampleDialog::Result::Cancelled) {
                    LOG_INFO("AutoSample", "Capture cancelled");
                } else {
                    LOG_ERROR("AutoSample", "Capture failed");
                }
            });
        // Capture-name field needs SDL text-input mode active to receive
        // keystrokes. We start it on open and stop it in the result
        // callback above; the in-dialog onKey path forwards Backspace /
        // Delete / arrows etc. directly to FwTextInput::onKeyDown.
        SDL_StartTextInput(m_mainWindow.getHandle());
        m_autoSampleDialog.open(ctx);
    });

    // ── Sidechain plumbing for sidechain-aware instrument panels ──
    // Vocoder's display panel hosts an inline source dropdown — these
    // three callbacks bridge it to the project + audio engine without
    // pulling Project into DetailPanelWidget directly. (Same surface
    // is reusable for ring-mod / gate / etc. when they grow sidechain
    // UIs.)
    m_detailPanel->setTrackNamesProvider([this]() {
        std::vector<std::string> names;
        const int n = m_project.numTracks();
        names.reserve(n);
        for (int i = 0; i < n; ++i) names.push_back(m_project.track(i).name);
        return names;
    });
    m_detailPanel->setSidechainSourceProvider([this](int trackIdx) {
        if (trackIdx < 0 || trackIdx >= m_project.numTracks()) return -1;
        return m_project.track(trackIdx).sidechainSource;
    });
    m_detailPanel->setOnSetSidechainSource([this](int trackIdx, int sourceIdx) {
        if (trackIdx < 0 || trackIdx >= m_project.numTracks()) return;
        // Sanity: refuse self-routing (the picker hides self anyway,
        // but defensive — Mixer I/O could in theory push junk).
        if (sourceIdx == trackIdx) sourceIdx = -1;
        // Cycle guard: if dst → src would close a longer feedback
        // loop (A→B→C→A …), reject the assignment outright. The
        // engine has its own check as defense-in-depth, but doing it
        // here keeps the project state consistent with what the
        // engine will actually run, and lets us surface a warning to
        // the user / log instead of silently dropping the request on
        // the audio thread.
        if (m_project.wouldCreateSidechainCycle(trackIdx, sourceIdx)) {
            LOG_WARN("Sidechain",
                     "Refused track %d <- track %d: would create feedback loop",
                     trackIdx, sourceIdx);
            // Re-sync UI to whatever the project actually has so the
            // dropdown snaps back to the previous selection on next
            // displayUpdater tick.
            return;
        }
        m_project.track(trackIdx).sidechainSource = sourceIdx;
        m_audioEngine.sendCommand(audio::SetSidechainSourceMsg{trackIdx, sourceIdx});
        markDirty();
    });

    // ConvolutionReverb IR loader — opens an SDL file dialog,
    // decodes via FileIO::loadAudioFile (libsndfile), sums to mono,
    // pushes into the effect's loadIRMono. The Conv Reverb display
    // panel triggers this via its "Load IR…" button. Stashes the
    // ConvolutionReverb pointer in m_pendingConvIRReverb because
    // SDL_ShowOpenFileDialog's callback signature is C-style with
    // a void* userdata, and lambda captures don't survive that.
    // Neural Amp .nam loader — mirrors the Conv Reverb IR loader
    // exactly. Stash the effect ptr in m_pendingNamEffect because
    // SDL_ShowOpenFileDialog's C-style callback only carries a
    // void* userdata. NAM's actual file read + Reset + prewarm
    // happens inside NeuralAmp::setModelPath; we just hand it the
    // path the user picked.
    m_detailPanel->setOnLoadNamModel([this](effects::NeuralAmp* na) {
        if (!na) return;
        m_pendingNamEffect = na;
        static SDL_DialogFileFilter filter{"Neural Amp Models", "nam"};
        // Open the dialog pre-pointed at the bundled NAM folder
        // so first-time users see the starter amp captures
        // immediately (no navigation required). Falls back to
        // null (system default) if the bundled folder doesn't
        // exist — release builds always include it; dev builds
        // before the assets-copy step might not.
        const std::filesystem::path bundled =
            std::filesystem::current_path() / "assets" / "nam";
        const std::string defaultLoc = std::filesystem::exists(bundled)
            ? bundled.string() : std::string{};
        SDL_ShowOpenFileDialog(
            [](void* ud, const char* const* filelist, int) {
                auto* self = static_cast<App*>(ud);
                auto* eff  = self->m_pendingNamEffect;
                self->m_pendingNamEffect = nullptr;
                if (!eff || !filelist || !filelist[0]) return;
                eff->setModelPath(filelist[0]);
            },
            this, m_mainWindow.getHandle(),
            &filter, 1,
            defaultLoc.empty() ? nullptr : defaultLoc.c_str(),
            /*allow_many*/ false);
    });

    m_detailPanel->setOnLoadConvIR([this](effects::ConvolutionReverb* cr) {
        if (!cr) return;
        m_pendingConvIRReverb = cr;
        static SDL_DialogFileFilter filter{"Audio files", "wav;flac;aif;aiff;ogg;mp3"};
        // Open the dialog pre-pointed at the bundled Voxengo IR
        // folder so first-time users see the bundled options
        // immediately (no navigation required). Falls back to
        // null (system default) if the bundled folder doesn't
        // exist — release builds always include it; dev builds
        // before the assets-copy step might not.
        const std::filesystem::path bundled =
            std::filesystem::current_path() / "assets" / "reverbs" / "voxengo";
        const std::string defaultLoc = std::filesystem::exists(bundled)
            ? bundled.string() : std::string{};
        SDL_ShowOpenFileDialog(
            [](void* ud, const char* const* filelist, int) {
                auto* self = static_cast<App*>(ud);
                auto* eff  = self->m_pendingConvIRReverb;
                self->m_pendingConvIRReverb = nullptr;
                if (!eff || !filelist || !filelist[0]) return;
                self->loadIRIntoConvReverb(eff, filelist[0]);
            },
            this, m_mainWindow.getHandle(),
            &filter, 1,
            defaultLoc.empty() ? nullptr : defaultLoc.c_str(),
            /*allow_many*/ false);
    });

#ifdef YAWN_HAS_VST3
    // Initialize VST3 plugin scanner
    m_vst3Scanner = std::make_unique<vst3::VST3Scanner>();
    std::string vst3CachePath = (util::AppSettings::settingsPath().parent_path()
                                  / "vst3_cache.json").string();
    if (!m_vst3Scanner->loadCache(vst3CachePath)) {
        LOG_INFO("VST3", "Scanning for VST3 plugins...");
        m_vst3Scanner->scan();
        m_vst3Scanner->saveCache(vst3CachePath);
    }
    LOG_INFO("VST3", "Found %d VST3 plugins (%d instruments, %d effects)",
             (int)m_vst3Scanner->plugins().size(),
             (int)m_vst3Scanner->instruments().size(),
             (int)m_vst3Scanner->effects().size());
#endif

    LOG_INFO("App", "Y.A.W.N initialized successfully");
    LOG_INFO("App", "  [Space] Play/Stop        [Up/Down] BPM +/-");
    LOG_INFO("App", "  [Home] Reset position    [M] Toggle mixer");
    LOG_INFO("App", "  [D] Toggle detail panel");
    LOG_INFO("App", "  Virtual keyboard: Q2W3ER5T6Y7UI9O0P  [Z/X] Octave down/up");
    LOG_INFO("App", "  Click clip slots to launch/stop");
    LOG_INFO("App", "  Drag & drop audio files to load clips");
    LOG_INFO("App", "  [Esc] Quit");

    // Surface FFmpeg-build gaps loudly — silently missing video support
    // makes Visual tracks look broken (no Set Video, empty Live Input
    // submenu) and the user has no way to know without reading source.
    // Log on stdout (so it lands in yawn.log) AND show a sticky toast.
#if !defined(YAWN_HAS_VIDEO)
    LOG_INFO("App", "[!] Video support DISABLED — this build has no FFmpeg.");
    LOG_INFO("App", "    Visual tracks can run shaders only; video files");
    LOG_INFO("App", "    and live capture (webcams) won't work.");
    LOG_INFO("App", "    Fix: download ffmpeg-master-latest-win64-gpl-shared");
    LOG_INFO("App", "    from BtbN/FFmpeg-Builds, then reconfigure CMake");
    LOG_INFO("App", "    with -DFFMPEG_ROOT=<extracted folder>.");
    m_toastManager.show("Video support disabled — set FFMPEG_ROOT and rebuild "
                        "to enable video files / webcams",
                        8.0f, ui::ToastManager::Severity::Warn);
#elif !defined(YAWN_HAS_AVDEVICE)
    LOG_INFO("App", "[!] Live capture DISABLED — libavdevice not linked.");
    LOG_INFO("App", "    Video files work, but webcams / dshow streams won't.");
    LOG_INFO("App", "    Fix: rebuild against an FFmpeg distro that ships");
    LOG_INFO("App", "    avdevice (BtbN's gpl-shared bundle does).");
    m_toastManager.show("Live capture disabled — libavdevice missing from "
                        "this FFmpeg build",
                        6.0f, ui::ToastManager::Severity::Warn);
#endif

    updateWindowTitle();
    return true;
}

void App::run() {
    while (m_running) {
        processEvents();
        update();
        render();
    }
}

void App::shutdown() {
    // Stop controller scripts
    m_controllerManager.shutdown();

    // Stop library scanner before anything else
    if (m_libraryScanner) m_libraryScanner->stop();
    m_libraryScanner.reset();
    m_libraryDb.close();

#ifdef YAWN_HAS_VST3
    m_vst3Editors.clear();
    m_vst3Scanner.reset();
#endif
    m_audioEngine.shutdown();
    // Tear down visual output window + GL resources while SDL is still up.
    m_visualEngine.shutdown();
    // Destroy cached cursors
    if (m_cursorDefault)  SDL_DestroyCursor(m_cursorDefault);
    if (m_cursorEWResize) SDL_DestroyCursor(m_cursorEWResize);
    if (m_cursorNSResize) SDL_DestroyCursor(m_cursorNSResize);
    if (m_cursorMove)     SDL_DestroyCursor(m_cursorMove);
    if (m_iconTexture) { glDeleteTextures(1, &m_iconTexture); m_iconTexture = 0; }
    m_renderer.shutdown();
    m_font.destroy();
    m_mainWindow.destroy();
    SDL_Quit();
    LOG_INFO("App", "Y.A.W.N shutdown complete");
}

bool App::loadClipToSlot(const std::string& path, int trackIndex, int sceneIndex) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        LOG_INFO("Audio", "Resampling from %d Hz to %.0f Hz...",
            info.sampleRate, m_audioEngine.sampleRate());
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    std::string name = fileNameFromPath(path);

    auto clip = std::make_unique<audio::Clip>();
    clip->name = name;
    clip->buffer = buffer;
    clip->looping = true;
    clip->gain = 0.8f;

    m_project.setClip(trackIndex, sceneIndex, std::move(clip));

    LOG_INFO("File", "Loaded '%s' -> Track %d, Scene %d",
        name.c_str(), trackIndex + 1, sceneIndex + 1);

    markDirty();
    return true;
}

bool App::loadClipToArrangement(const std::string& path, int trackIndex, double beatPos) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    std::string name = fileNameFromPath(path);

    // Calculate length in beats from audio duration
    double sampleRate = m_audioEngine.sampleRate();
    double bpm = m_audioEngine.transport().bpm();
    double durationSec = static_cast<double>(buffer->numFrames()) / sampleRate;
    double durationBeats = durationSec * bpm / 60.0;

    ArrangementClip clip;
    clip.type = ArrangementClip::Type::Audio;
    clip.startBeat = beatPos;
    clip.lengthBeats = durationBeats;
    clip.audioBuffer = buffer;
    clip.name = name;
    clip.colorIndex = m_project.track(trackIndex).colorIndex;

    m_project.track(trackIndex).arrangementClips.push_back(std::move(clip));
    m_project.track(trackIndex).sortArrangementClips();
    m_project.updateArrangementLength();
    syncArrangementClipsToEngine(trackIndex);

    // Activate arrangement playback for this track
    if (!m_project.track(trackIndex).arrangementActive) {
        m_project.track(trackIndex).arrangementActive = true;
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{trackIndex, true});
    }

    LOG_INFO("File", "Loaded '%s' -> Arrangement Track %d at beat %.1f",
        name.c_str(), trackIndex + 1, beatPos);

    markDirty();
    return true;
}

bool App::loadSampleToSampler(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* sampler = dynamic_cast<instruments::Sampler*>(inst);
    if (!sampler) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    sampler->loadSample(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded sample '%s' into Sampler on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Sample (Sampler)",
            [this, ti]{ // undo: clear sample
                auto* s = dynamic_cast<instruments::Sampler*>(m_audioEngine.instrument(ti));
                if (s) s->clearSample();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadSampleToSampler(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadLoopToDrumSlop(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* ds = dynamic_cast<instruments::DrumSlop*>(inst);
    if (!ds) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    ds->loadLoop(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded loop '%s' into DrumSlop on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Loop (DrumSlop)",
            [this, ti]{ // undo: clear loop
                auto* d = dynamic_cast<instruments::DrumSlop*>(m_audioEngine.instrument(ti));
                if (d) d->clearLoop();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadLoopToDrumSlop(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadSampleToDrumRack(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* rack = dynamic_cast<instruments::DrumRack*>(inst);
    if (!rack) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    rack->loadPad(rack->selectedPad(), interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    int padIdx = rack->selectedPad();
    LOG_INFO("File", "Loaded sample '%s' into Drum Rack pad %d on Track %d",
        name.c_str(), padIdx, trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Sample (DrumRack)",
            [this, ti, padIdx]{ // undo: clear pad
                auto* r = dynamic_cast<instruments::DrumRack*>(m_audioEngine.instrument(ti));
                if (r) r->clearPad(padIdx);
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadSampleToDrumRack(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadSampleToGranular(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* granular = dynamic_cast<instruments::GranularSynth*>(inst);
    if (!granular) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    granular->loadSample(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded sample '%s' into Granular Synth on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Sample (Granular)",
            [this, ti]{ // undo: clear sample
                auto* g = dynamic_cast<instruments::GranularSynth*>(m_audioEngine.instrument(ti));
                if (g) g->clearSample();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadSampleToGranular(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadModulatorToVocoder(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* vocoder = dynamic_cast<instruments::Vocoder*>(inst);
    if (!vocoder) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    vocoder->loadModulatorSample(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded modulator '%s' into Vocoder on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Modulator (Vocoder)",
            [this, ti]{ // undo: clear modulator
                auto* v = dynamic_cast<instruments::Vocoder*>(m_audioEngine.instrument(ti));
                if (v) v->clearModulatorSample();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadModulatorToVocoder(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
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
                    slot->audioClip.reset();
                    markDirty();
                    m_undoManager.push({"Cut Audio Clip",
                        [this, ct, cs, b = std::shared_ptr<audio::Clip>(std::move(backup))]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { s->audioClip = b->clone(); markDirty(); }
                        },
                        [this, ct, cs]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { m_audioEngine.sendCommand(audio::StopClipMsg{ct});
                                      s->audioClip.reset(); markDirty(); }
                        }, ""});
                } else if (slot && slot->midiClip) {
                    auto backup = slot->midiClip->clone();
                    m_clipboard.clear();
                    m_clipboard.type = ClipboardData::Type::Midi;
                    m_clipboard.midiClip = slot->midiClip->clone();
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{ct});
                    slot->midiClip.reset();
                    markDirty();
                    m_undoManager.push({"Cut MIDI Clip",
                        [this, ct, cs, b = std::shared_ptr<midi::MidiClip>(std::move(backup))]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { s->midiClip = b->clone(); markDirty(); }
                        },
                        [this, ct, cs]{
                            auto* s = m_project.getSlot(ct, cs);
                            if (s) { m_audioEngine.sendCommand(audio::StopMidiClipMsg{ct});
                                      s->midiClip.reset(); markDirty(); }
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
                        slot->clear();
                        slot->audioClip = m_clipboard.audioClip->clone();
                        markDirty();
                        auto pastedClone = m_clipboard.audioClip->clone();
                        m_undoManager.push({"Paste Audio Clip",
                            [this, pt, ps, oldAudio, oldMidi]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                s->clear();
                                if (oldAudio) s->audioClip = oldAudio->clone();
                                if (oldMidi) s->midiClip = oldMidi->clone();
                                markDirty();
                            },
                            [this, pt, ps, pc = std::shared_ptr<audio::Clip>(std::move(pastedClone))]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                s->clear();
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
                        slot->clear();
                        slot->midiClip = m_clipboard.midiClip->clone();
                        markDirty();
                        auto pastedClone = m_clipboard.midiClip->clone();
                        m_undoManager.push({"Paste MIDI Clip",
                            [this, pt, ps, oldAudio, oldMidi]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                s->clear();
                                if (oldAudio) s->audioClip = oldAudio->clone();
                                if (oldMidi) s->midiClip = oldMidi->clone();
                                markDirty();
                            },
                            [this, pt, ps, pc = std::shared_ptr<midi::MidiClip>(std::move(pastedClone))]{
                                auto* s = m_project.getSlot(pt, ps);
                                if (!s) return;
                                m_audioEngine.sendCommand(audio::StopClipMsg{pt});
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{pt});
                                s->clear();
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
                // Re-launch default clips before starting transport
                for (int t = 0; t < m_project.numTracks(); ++t) {
                    int ds = m_project.track(t).defaultScene;
                    if (ds < 0 || ds >= m_project.numScenes()) continue;
                    auto* slot = m_project.getSlot(t, ds);
                    if (!slot) continue;
                    if (slot->audioClip)
                        m_audioEngine.sendCommand(audio::LaunchClipMsg{t, ds, slot->audioClip.get(),
                            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
                    else if (slot->midiClip)
                        m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{t, ds, slot->midiClip.get(),
                            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
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
                    m_audioEngine.sendCommand(audio::StopClipMsg{m_selectedTrack});
                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{m_selectedTrack});
                    slot->clear();
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

                // Update cursor shape based on content grid divider hover or panel resize
                bool wantNS = m_contentGrid->wantsVerticalResize()
                           || (m_arrangementPanel && m_arrangementPanel->wantsVerticalResize());
                if (m_contentGrid->wantsHorizontalResize() && wantNS) {
                    SDL_SetCursor(m_cursorMove);
                } else if (m_contentGrid->wantsHorizontalResize()) {
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
                                    if (s) { s->midiClip.reset(); markDirty(); }
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

            case SDL_EVENT_DROP_FILE: {
                const char* file = event.drop.data;
                if (file) {
                    float dropX = event.drop.x;
                    float dropY = event.drop.y;

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
                            startVideoImport(targetTrack, targetScene, file);
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

void App::update() {
    // Update animation timer for session panel (recording pulse, playback pulse)
    uint64_t now = SDL_GetTicks();
    float dt = (m_lastFrameTicks > 0) ? (now - m_lastFrameTicks) / 1000.0f : 0.0f;
    m_lastFrameTicks = now;
    m_sessionPanel->updateAnimTimer(dt);

    // Detail panel open/close height animation. Lives outside fw2's
    // measure cache so a re-layout in the same frame doesn't read a
    // stale height; tick() advances the animated height and bumps the
    // panel's local version so the next measure() pass picks it up.
    m_detailPanel->tick();

    // Piano roll uses the same animated-height + measure-cache pattern
    // — tick() advances the open/close animation and invalidates so
    // the new height is visible on the next frame. Without this, the
    // panel opens to a stub that shows only the toolbar (cached at the
    // first sampled m_animatedHeight) and the resize handle does
    // nothing visible.
    m_pianoRoll->tick();

    // Auto-Sample dialog — runs the worker state machine each frame
    // when a capture is in progress, and resolves the test-note Note-Off
    // wall-clock timer regardless. Cheap when the dialog is idle.
    m_autoSampleDialog.tick();

    // Transport stop: whenever the stop-counter advances (even while
    // transport is already stopped), clear every visual layer so
    // session-launched shaders / models / videos go dark in lockstep
    // with the audio / MIDI scheduleStop path.
    {
        const uint64_t stopCount = m_audioEngine.transport().stopCounter();
        if (stopCount != m_lastSeenStopCounter) {
            stopAllVisualLayers();
            m_lastSeenStopCounter = stopCount;
        }
    }

    // Arrangement-driven visual clip playback: detect which clip is
    // under the transport head on each visual track and fire launch /
    // clear on transitions. Main-thread polling is precise enough for
    // visual cues (16 ms granularity at 60 Hz).
    pollArrangementVisualPlayback();

    // Per-track visual-knob automation lanes — evaluated here rather
    // than on the audio thread (visuals don't need that precision).
    pollVisualKnobAutomation();

    // Per-track macro mappings — same once-per-frame cadence as
    // automation. Pulls each macro's live LFO-modulated value from
    // the engine and pushes through to every mapped target so
    // shader / chain params follow the macro in lock-step.
    applyMacroMappings();

    // Session-view follow actions for visual clips. Fires Next /
    // Random / etc. once barCount bars have elapsed since launch.
    pollVisualFollowActions();

    // Video imports — advance each background transcode, apply results.
    for (auto it = m_pendingImports.begin(); it != m_pendingImports.end(); ) {
        it->importer->poll();
        auto st = it->importer->state();
        m_sessionPanel->setSlotImportProgress(it->track, it->scene,
                                                it->importer->progress());
        if (st == visual::VideoImporter::State::Done) {
            onVideoImportDone(*it);
            it = m_pendingImports.erase(it);
        } else if (st == visual::VideoImporter::State::Failed) {
            LOG_ERROR("Video", "Import failed: %s", it->importer->error().c_str());
            m_sessionPanel->setSlotImporting(it->track, it->scene, false);
            m_toastManager.show("Video import failed",
                                2.5f, ui::ToastManager::Severity::Error);
            it = m_pendingImports.erase(it);
        } else {
            ++it;
        }
    }

    // Pull any fresh MIDI CC values for visual-knob targets off the bus
    // and apply them to the layer + the track-level macro device.
    // Audio thread writes, UI thread reads once per frame. Hardware
    // controllers (Push / Move encoders 1..8) feed straight into the
    // track macros via this path.
    for (int t = 0; t < m_project.numTracks() && t < kMaxTracks; ++t) {
        if (m_project.track(t).type != Track::Type::Visual) continue;
        for (int k = 0; k < MacroDevice::kNumMacros; ++k) {
            float v;
            if (!visual::VisualKnobBus::instance().readIfChanged(
                    t, k, m_visualKnobBusVersions[t][k], &v)) continue;
            m_project.track(t).macros.values[k] = v;
            m_visualEngine.setLayerKnob(t, k, v);
            markDirty();
            // Reflect into the panel immediately if this is the
            // selected visual track.
            if (t == m_selectedTrack) {
                float knobs[8];
                for (int i = 0; i < MacroDevice::kNumMacros; ++i)
                    knobs[i] = m_project.track(t).macros.values[i];
                m_visualParamsPanel->setKnobValues(knobs);
            }
        }
    }

    // Push live modulated knob values into the visual-params panel so the
    // A..H arcs breathe with their LFOs. Only active while the panel is
    // visible — audio/midi tracks get cleared overrides.
    if (m_visualParamsPanel->isVisible() &&
        m_selectedTrack >= 0 && m_selectedTrack < m_project.numTracks() &&
        m_project.track(m_selectedTrack).type == Track::Type::Visual) {
        float disp[8];
        for (int i = 0; i < 8; ++i)
            disp[i] = m_visualEngine.getLayerKnobDisplayValue(m_selectedTrack, i);
        m_visualParamsPanel->setKnobDisplayValues(disp);
    } else {
        m_visualParamsPanel->setKnobDisplayValues(nullptr);
    }

    // Poll controller scripts (MIDI input → Lua callbacks)
    m_controllerManager.update();

    // Sync controller session focus rectangle to SessionPanel
    {
        const auto& focus = m_controllerManager.sessionFocus();
        m_sessionPanel->setControllerGridRegion(
            focus.trackOffset, focus.sceneOffset, focus.active);
    }

    // Process pending file dialog results (from SDL async callbacks)
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        if (!m_pendingOpenPath.empty()) {
            std::string path = std::move(m_pendingOpenPath);
            m_pendingOpenPath.clear();
            doOpenProject(path);
        }
        if (!m_pendingSavePath.empty()) {
            std::string path = std::move(m_pendingSavePath);
            m_pendingSavePath.clear();
            doSaveProject(path);
        }
        if (!m_pendingExportPath.empty()) {
            std::string path = std::move(m_pendingExportPath);
            m_pendingExportPath.clear();
            startExportRender(path);
        }
    }

#ifdef YAWN_HAS_VST3
    // Poll parameter changes from VST3 editor processes
    for (auto& ed : m_vst3Editors) {
        if (ed && ed->isOpen()) ed->pollParamChanges();
    }
    // Clean up closed VST3 editor windows
    m_vst3Editors.erase(
        std::remove_if(m_vst3Editors.begin(), m_vst3Editors.end(),
                        [](const auto& w) { return !w || !w->isOpen(); }),
        m_vst3Editors.end());
#endif

    // Check if render completed
    if (m_exportDialog.isRendering() && m_exportDialog.progress().done.load()) {
        m_exportDialog.setRendering(false);
        m_exportDialog.forceClose();
        if (m_exportDialog.progress().failed.load()) {
            LOG_ERROR("Export", "Render failed");
        } else if (m_exportDialog.progress().cancelled.load()) {
            LOG_INFO("Export", "Render cancelled by user");
        } else {
            LOG_INFO("Export", "Export completed successfully");
        }
    }

    audio::AudioEvent evt;
    while (m_audioEngine.pollEvent(evt)) {
        std::visit([this](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, audio::TransportPositionUpdate>) {
                m_displayBeats = msg.positionInBeats;
                m_displayPlaying = msg.isPlaying;
            }
            else if constexpr (std::is_same_v<T, audio::ClipStateUpdate>) {
                m_sessionPanel->updateClipState(msg.trackIndex, msg.playing, msg.playPosition,
                                                msg.playingScene, msg.isMidi, msg.clipLengthBeats);
                m_sessionPanel->setTrackRecording(msg.trackIndex, msg.recording, msg.recordingScene);
                // Sync defaultScene for follow actions (engine changed playing scene)
                if (msg.playing && msg.playingScene >= 0 &&
                    msg.trackIndex >= 0 && msg.trackIndex < m_project.numTracks()) {
                    m_project.track(msg.trackIndex).defaultScene = msg.playingScene;
                }
                // Forward playhead to detail panel waveform
                if (msg.trackIndex == m_selectedTrack &&
                    m_detailPanel->viewMode() == ui::fw2::DetailPanelWidget::ViewMode::AudioClip &&
                    !msg.isMidi) {
                    m_detailPanel->setClipPlayPosition(msg.playPosition);
                    m_detailPanel->setClipPlaying(msg.playing);
                    m_detailPanel->setTransportBPM(m_audioEngine.transport().bpm());
                }
                // Forward MIDI playhead to piano roll — only when the
                // clip currently playing on this track is the SAME
                // clip the piano roll is editing. Without this guard,
                // launching scene 1 while the piano roll showed the
                // scene-0 clip animated the playhead on scene 0's
                // notes (the message's trackIndex matches but the
                // clip identity doesn't). Arrangement-source piano
                // roll keeps the existing behaviour for now (its
                // playhead semantics depend on transport position +
                // clip start offset, not on session-clip launch).
                if (msg.isMidi && msg.trackIndex == m_pianoRoll->trackIndex() &&
                    m_pianoRoll->isOpen() &&
                    m_pianoRoll->source() == ui::fw2::PianoRollPanel::Source::Session) {
                    auto* playingClip = (msg.playingScene >= 0 && msg.playing)
                        ? m_project.getMidiClip(msg.trackIndex, msg.playingScene)
                        : nullptr;
                    if (playingClip == m_pianoRoll->clip()) {
                        double beats = static_cast<double>(msg.playPosition) / 1000000.0;
                        m_pianoRoll->setPlayBeat(beats, msg.playing);
                    } else {
                        // Different clip launched on this track (or
                        // nothing playing) — freeze the piano roll's
                        // playhead so it stops animating over notes
                        // that aren't currently playing.
                        m_pianoRoll->setPlayBeat(0.0, false);
                    }
                } else if (msg.isMidi && msg.trackIndex == m_pianoRoll->trackIndex() &&
                           m_pianoRoll->isOpen()) {
                    // Arrangement source — keep existing behaviour.
                    double beats = static_cast<double>(msg.playPosition) / 1000000.0;
                    m_pianoRoll->setPlayBeat(beats, msg.playing);
                }
            }
            else if constexpr (std::is_same_v<T, audio::MeterUpdate>) {
                if (msg.trackIndex >= 0)
                    m_mixerPanel->updateMeter(msg.trackIndex, msg.peakL, msg.peakR);
                else
                    m_returnMasterPanel->updateMeter(msg.trackIndex, msg.peakL, msg.peakR);
            }
            else if constexpr (std::is_same_v<T, audio::TransportRecordStateUpdate>) {
                m_transportPanel->setRecordState(msg.recording, msg.countingIn, msg.countInProgress, msg.countInBeats);
                m_sessionPanel->setGlobalRecordArmed(msg.recording);
            }
            else if constexpr (std::is_same_v<T, audio::MidiRecordCompleteEvent>) {
                int ti = msg.trackIndex;
                auto& data = m_audioEngine.recordedMidiData(ti);
                if (data.ready.load(std::memory_order_acquire)) {
                    int si = data.sceneIndex;
                    if (ti >= 0 && si >= 0 && (!data.notes.empty() || !data.ccs.empty())) {
                        auto* existingClip = m_project.getMidiClip(ti, si);
                        if (data.overdub && existingClip) {
                            for (auto& note : data.notes)
                                existingClip->addNote(note);
                            for (auto& cc : data.ccs)
                                existingClip->addCC(cc);
                            if (data.lengthBeats > existingClip->lengthBeats())
                                existingClip->setLengthBeats(data.lengthBeats);
                        } else {
                            auto* recSlot0 = m_project.getSlot(ti, si);
                            const bool shouldLoop = recSlot0 ? recSlot0->recordLoop : true;
                            auto newClip = std::make_unique<midi::MidiClip>(data.lengthBeats);
                            newClip->setName("Rec " + std::to_string(ti + 1));
                            newClip->setLoop(shouldLoop);
                            for (auto& note : data.notes)
                                newClip->addNote(note);
                            for (auto& cc : data.ccs)
                                newClip->addCC(cc);
                            m_project.setMidiClip(ti, si, std::move(newClip));
                        }
                        markDirty();

                        auto* clipPtr = m_project.getMidiClip(ti, si);
                        if (clipPtr) {
                            auto* slot = m_project.getSlot(ti, si);
                            // Launch immediately when auto-stopped (fixed-duration) to avoid
                            // an empty bar caused by the UI round-trip delay
                            auto lq = data.autoStopped ? audio::QuantizeMode::None
                                : (slot ? slot->launchQuantize : audio::QuantizeMode::NextBar);
                            m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{ti, si, clipPtr, lq,
                                slot ? &slot->clipAutomation : nullptr,
                                slot ? slot->followAction : FollowAction{}});
                        }
                    }
                    // Always clear recording state in UI
                    m_sessionPanel->setTrackRecording(ti, false, -1);
                    data.ready.store(false, std::memory_order_release);
                }
            }
            else if constexpr (std::is_same_v<T, audio::AudioRecordCompleteEvent>) {
                int ti = msg.trackIndex;
                auto& data = m_audioEngine.recordedAudioData(ti);
                if (data.ready.load(std::memory_order_acquire)) {
                    int si = data.sceneIndex;
                    if (ti >= 0 && si >= 0 && data.frameCount > 0) {
                        // Create AudioBuffer from recorded data
                        auto audioBuffer = std::make_shared<audio::AudioBuffer>(
                            data.channels, static_cast<int>(data.frameCount));
                        for (int ch = 0; ch < data.channels; ++ch) {
                            std::memcpy(
                                audioBuffer->channelData(ch),
                                data.buffer.data() + ch * data.frameCount,
                                data.frameCount * sizeof(float));
                        }

                        auto* existingSlot = m_project.getSlot(ti, si);
                        auto* existingClip = existingSlot ? existingSlot->audioClip.get() : nullptr;

                        if (data.overdub && existingClip && existingClip->buffer) {
                            // Overdub: mix new audio into existing clip buffer
                            auto& existBuf = existingClip->buffer;
                            int mixFrames = std::min(
                                static_cast<int>(data.frameCount), existBuf->numFrames());
                            int mixCh = std::min(data.channels, existBuf->numChannels());
                            for (int ch = 0; ch < mixCh; ++ch) {
                                float* dst = existBuf->channelData(ch);
                                const float* src = audioBuffer->channelData(ch);
                                for (int f = 0; f < mixFrames; ++f)
                                    dst[f] += src[f];
                            }
                            LOG_INFO("Audio", "Audio overdub: Track %d, Scene %d, %d frames mixed",
                                        ti + 1, si + 1, mixFrames);
                        } else {
                            // New recording: create clip. Loop flag
                            // comes from the slot's recordLoop (user
                            // decision per-slot) — defaults to true.
                            auto* recSlot0 = m_project.getSlot(ti, si);
                            const bool shouldLoop = recSlot0 ? recSlot0->recordLoop : true;
                            auto clip = std::make_unique<audio::Clip>();
                            clip->name = "Rec " + std::to_string(ti + 1) + "-" + std::to_string(si + 1);
                            clip->buffer = audioBuffer;
                            clip->looping = shouldLoop;
                            clip->gain = 1.0f;
                            clip->originalBPM = m_audioEngine.transport().bpm();
                            m_project.setClip(ti, si, std::move(clip));
                            LOG_INFO("Audio", "Audio recorded: Track %d, Scene %d, %" PRId64 " frames",
                                        ti + 1, si + 1, data.frameCount);
                        }
                        markDirty();

                        auto* clipPtr = m_project.getClip(ti, si);
                        if (clipPtr) {
                            auto* recSlot = m_project.getSlot(ti, si);
                            auto lq = data.autoStopped ? audio::QuantizeMode::None
                                : (recSlot ? recSlot->launchQuantize : audio::QuantizeMode::NextBar);
                            m_audioEngine.sendCommand(audio::LaunchClipMsg{ti, si, clipPtr, lq,
                                recSlot ? &recSlot->clipAutomation : nullptr,
                                recSlot ? recSlot->followAction : FollowAction{}});
                        }
                    }
                    // Clear recording state in UI
                    m_sessionPanel->setTrackRecording(ti, false, -1);
                    // Free the transfer buffer
                    data.buffer.clear();
                    data.buffer.shrink_to_fit();
                    data.ready.store(false, std::memory_order_release);
                }
            }
            else if constexpr (std::is_same_v<T, audio::RecordBufferFullEvent>) {
                LOG_WARN("Audio", "Recording buffer full on Track %d (%.0f sec limit). Auto-saved.",
                         msg.trackIndex + 1, 300.0);
            }
            else if constexpr (std::is_same_v<T, audio::FollowActionTriggeredEvent>) {
                // Resolve follow action target and launch the next clip
                int ti = msg.trackIndex;
                int si = msg.sceneIndex;
                int numScenes = m_project.numScenes();
                if (ti < 0 || ti >= m_project.numTracks() || numScenes <= 0) return;

                // Build list of occupied scenes for this track
                std::vector<int> occupied;
                for (int s = 0; s < numScenes; ++s) {
                    auto* slot = m_project.getSlot(ti, s);
                    if (slot && !slot->empty()) occupied.push_back(s);
                }
                if (occupied.empty()) return;

                int targetScene = -1;
                switch (msg.resolvedAction) {
                    case FollowActionType::Next: {
                        // Find next occupied scene after current
                        for (int s : occupied) {
                            if (s > si) { targetScene = s; break; }
                        }
                        if (targetScene < 0) targetScene = occupied.front(); // wrap
                        break;
                    }
                    case FollowActionType::Previous: {
                        for (int i = (int)occupied.size() - 1; i >= 0; --i) {
                            if (occupied[i] < si) { targetScene = occupied[i]; break; }
                        }
                        if (targetScene < 0) targetScene = occupied.back(); // wrap
                        break;
                    }
                    case FollowActionType::First:
                        targetScene = occupied.front();
                        break;
                    case FollowActionType::Last:
                        targetScene = occupied.back();
                        break;
                    case FollowActionType::Random:
                        targetScene = occupied[std::rand() % occupied.size()];
                        break;
                    case FollowActionType::Any: {
                        // Any other clip (exclude self)
                        std::vector<int> others;
                        for (int s : occupied) { if (s != si) others.push_back(s); }
                        if (!others.empty())
                            targetScene = others[std::rand() % others.size()];
                        else
                            targetScene = si; // only one clip, play again
                        break;
                    }
                    default: return;
                }

                if (targetScene < 0) return;
                auto* slot = m_project.getSlot(ti, targetScene);
                if (!slot || slot->empty()) return;

                auto lq = slot->launchQuantize;
                if (slot->audioClip) {
                    m_audioEngine.sendCommand(audio::LaunchClipMsg{
                        ti, targetScene, slot->audioClip.get(), lq,
                        &slot->clipAutomation, slot->followAction});
                } else if (slot->midiClip) {
                    m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{
                        ti, targetScene, slot->midiClip.get(), lq,
                        &slot->clipAutomation, slot->followAction});
                }
            }
        }, evt);
    }

    m_transportPanel->setTransportState(m_displayPlaying, m_displayBeats,
                                     m_audioEngine.transport().bpm(),
                                     m_audioEngine.transport().numerator(),
                                     m_audioEngine.transport().denominator());
    m_transportPanel->setSelectedScene(m_selectedScene);

    // Keep detail panel synced with current target
    if (m_showDetailPanel) {
        switch (m_detailTarget) {
            case DetailTarget::Track:
                updateDetailForSelectedTrack();
                break;
            case DetailTarget::ReturnBus:
                updateDetailForReturnBus(m_detailReturnBus);
                break;
            case DetailTarget::Master:
                updateDetailForMaster();
                break;
        }
    }
}

void App::render() {
    m_mainWindow.makeCurrent();

    int w = m_mainWindow.getWidth();
    int h = m_mainWindow.getHeight();
    glViewport(0, 0, w, h);

    glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_renderer.beginFrame(w, h);

    // Keep the v2 viewport in sync — widgets that position overlays
    // (Dropdown flip, Tooltip clamp) read this.
    m_fw2Context.viewport = {0.0f, 0.0f,
                              static_cast<float>(w),
                              static_cast<float>(h)};

    // Advance v2 per-frame timers (tooltip show delay, context menu
    // hover-to-open submenu). Both measure wall-clock between frames
    // via steady_clock — immune to NTP jumps and DST.
    {
        using clk = std::chrono::steady_clock;
        static auto lastTick = clk::now();
        const auto now = clk::now();
        const float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;
        ui::fw2::TooltipManager::instance().tick(dt);
        ui::fw2::ContextMenuManager::instance().tick(dt);
        ui::fw2::FwDropDown::tickGlobal(dt);
    }

    // Compute widget tree layout and render all panels
    computeLayout();
    m_rootLayout->render(m_fw2Context);

    // Menu bar — v2 FwMenuBar renders via MenuBarWrapper in the
    // rootLayout tree above. Its dropdown popup lives on LayerStack
    // and paints during m_fw2LayerStack.paintLayers() below, so it
    // sits on top of panels automatically.

    // v1 context menu render call retired — fw2::ContextMenu paints
    // via LayerStack::paintLayers below.

    // Modal dialogs (rendered on top of everything)
    {
        float sw = static_cast<float>(w);
        float sh = static_cast<float>(h);
        ui::fw::Rect screenBounds{0, 0, sw, sh};

        // v1 confirm dialog retired — fw2::Dialog paints via
        // LayerStack::paintLayers in the block below.
        // v1 TextInputDialog retired — FwTextInputDialog paints via
        // LayerStack too.
        // v1 About dialog retired — fw2::Dialog paints via LayerStack.
        // v1 PreferencesDialog + ExportDialog retired — their fw2
        // versions paint via their LayerStack overlay entries in the
        // paintLayers block below.
    }

    // Toasts draw last so they float above every dialog/panel.
    m_toastManager.render(m_renderer, m_font,
                          m_mainWindow.getWidth(), m_mainWindow.getHeight());

    // v2 floating UI — modal scrim first (only when a modal is open),
    // then all layer entries in bottom-up z-order. Empty in practice
    // until the first v2 overlay widget lands, but the plumbing is
    // in place so those widgets can just push into m_fw2LayerStack.
    if (m_fw2LayerStack.hasModalActive()) {
        ui::fw2::paintModalScrim(m_fw2Context,
            ui::fw::Rect{0.0f, 0.0f,
                          static_cast<float>(m_mainWindow.getWidth()),
                          static_cast<float>(m_mainWindow.getHeight())});
    }
    m_fw2LayerStack.paintLayers(m_fw2Context,
        ui::fw::Rect{0.0f, 0.0f,
                      static_cast<float>(m_mainWindow.getWidth()),
                      static_cast<float>(m_mainWindow.getHeight())});

    m_renderer.endFrame();

    m_mainWindow.swap();

    // Visual output: render after the main UI has swapped. tick() saves and
    // restores the current GL context, so it's safe to call here without
    // disturbing the next main-window frame.
    if (m_visualEngine.isOutputVisible()) {
        const auto& transport = m_audioEngine.transport();
        const double sr = std::max(1.0, m_audioEngine.sampleRate());
        const double seconds = static_cast<double>(transport.positionInSamples()) / sr;
        const double beats   = transport.positionInBeats();
        m_visualEngine.tick(seconds, beats, transport.isPlaying());
    }
}

// ---------------------------------------------------------------------------
// Project file operations
// ---------------------------------------------------------------------------

// Insert a new empty scene just below the currently selected one
// (Ableton convention). Shared by the Scene menu item and the
// Ctrl+I keyboard shortcut.
void App::insertSceneAtSelection() {
    const int insertAt = std::clamp(m_selectedScene + 1,
                                     0, m_project.numScenes());
    m_project.insertScene(insertAt);
    markDirty();
    m_undoManager.push({"Insert Scene",
        [this, insertAt]{ m_project.deleteScene(insertAt); markDirty(); },
        [this, insertAt]{ m_project.insertScene(insertAt); markDirty(); },
        ""});
    // Move the selection to the new scene so repeated Ctrl+I keeps
    // stacking fresh scenes below.
    m_selectedScene = insertAt;
}

// Starter track mix for fresh / new projects. Leaves tracks created by
// Project::init() in place but renames + retypes the first few to
// "Audio 1/2 | MIDI 1/2 | Visual 1". MIDI slots get a stock
// SubtractiveSynth so they're audible out of the box. Engine sync is
// the caller's responsibility (syncTracksToEngine / startup flow).
void App::resetEngineState() {
    for (int t = 0; t < kMaxTracks; ++t) {
        m_audioEngine.setInstrument(t, nullptr);
        m_audioEngine.midiEffectChain(t).clear();
        m_audioEngine.mixer().trackEffects(t).clear();
        m_audioEngine.mixer().setTrackVolume(t, 1.0f);
        m_audioEngine.mixer().setTrackPan(t, 0.0f);
        m_audioEngine.mixer().setTrackMute(t, false);
        m_audioEngine.mixer().setTrackSolo(t, false);
    }
    for (int r = 0; r < kMaxReturnBuses; ++r)
        m_audioEngine.mixer().returnEffects(r).clear();
    m_audioEngine.mixer().masterEffects().clear();
    m_audioEngine.mixer().setMasterVolume(1.0f);
    m_audioEngine.sendCommand(audio::TransportSetBPMMsg{120.0});
}

void App::setupDefaultTracks() {
    struct Default { const char* name; Track::Type type; };
    static constexpr Default defaults[] = {
        {"Audio 1",  Track::Type::Audio},
        {"Audio 2",  Track::Type::Audio},
        {"MIDI 1",   Track::Type::Midi},
        {"MIDI 2",   Track::Type::Midi},
        {"Visual 1", Track::Type::Visual},
    };
    const int n = std::min(m_project.numTracks(),
                           static_cast<int>(std::size(defaults)));
    for (int i = 0; i < n; ++i) {
        m_project.track(i).name = defaults[i].name;
        m_project.track(i).type = defaults[i].type;
        if (defaults[i].type == Track::Type::Midi)
            m_audioEngine.setInstrument(i,
                std::make_unique<instruments::SubtractiveSynth>());
    }
}

void App::newProject() {
    LOG_INFO("User", "newProject");
    auto doNew = [this]() {
        m_audioEngine.sendCommand(audio::TransportStopMsg{});

        // Reset project to defaults
        m_project = Project();
        m_project.init();

        resetEngineState();

        setupDefaultTracks();
        syncTracksToEngine();

        m_projectPath.clear();
        m_projectDirty = false;
        // No project open → preset saves go to the global library only.
        PresetManager::setProjectRoot({});
        m_undoManager.clear();
        m_midiLearnManager.clearAll();
        m_selectedTrack = 0;
        m_detailTarget = DetailTarget::Track;
        m_detailReturnBus = -1;
        m_showDetailPanel = false;
        m_pianoRoll->close();
        updateWindowTitle();
        LOG_INFO("Project", "New project created");
    };

    if (m_projectDirty) {
        ui::fw2::ConfirmDialog::prompt("Save changes before creating a new project?",
            [this, doNew]() {
                if (!m_projectPath.empty()) {
                    doSaveProject(m_projectPath);
                } else {
                    saveProjectAs();
                }
                doNew();
            });
    } else {
        doNew();
    }
}

void App::openProject() {
    LOG_INFO("User", "openProject (showing folder dialog)");
    auto doOpen = [this]() {
        SDL_ShowOpenFolderDialog(onOpenFolderResult, this,
                                m_mainWindow.getHandle(), nullptr, false);
    };

    if (m_projectDirty) {
        ui::fw2::ConfirmDialog::prompt("Save changes before opening another project?",
            [this, doOpen]() {
                if (!m_projectPath.empty()) {
                    doSaveProject(m_projectPath);
                }
                doOpen();
            });
    } else {
        doOpen();
    }
}

void App::saveProject() {
    LOG_INFO("User", "saveProject path='%s'", m_projectPath.string().c_str());
    if (m_projectPath.empty()) {
        saveProjectAs();
        return;
    }
    doSaveProject(m_projectPath);
}

void App::saveProjectAs() {
    LOG_INFO("User", "saveProjectAs");
    SDL_ShowSaveFileDialog(onSaveFolderResult, this,
                           m_mainWindow.getHandle(), nullptr, 0, "Untitled.yawn");
}

void App::doSaveProject(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    fs::path projectDir = path;
    // Ensure .yawn extension
    if (projectDir.extension() != ".yawn")
        projectDir += ".yawn";

    if (ProjectSerializer::saveToFolder(projectDir, m_project, m_audioEngine, &m_midiLearnManager, &m_visualEngine)) {
        m_projectPath = projectDir;
        m_projectDirty = false;
        // Subsequent preset saves now mirror into <project>.yawn/presets/.
        PresetManager::setProjectRoot(m_projectPath);
        updateWindowTitle();
        LOG_INFO("Project", "Saved to: %s", projectDir.string().c_str());
        m_toastManager.show("Saved: " + projectDir.filename().string());
    } else {
        LOG_ERROR("Project", "Failed to save: %s", projectDir.string().c_str());
        m_toastManager.show("Save failed", 2.5f, ui::ToastManager::Severity::Error);
    }
}

void App::doOpenProject(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    fs::path projectDir = path;

    // Check for project.json inside the folder
    if (!fs::exists(projectDir / "project.json")) {
        LOG_ERROR("Project", "Not a valid project folder: %s",
                     projectDir.string().c_str());
        return;
    }

    m_audioEngine.sendCommand(audio::TransportStopMsg{});

    resetEngineState();

    Project loadedProject;
    if (ProjectSerializer::loadFromFolder(projectDir, loadedProject, m_audioEngine, &m_midiLearnManager, &m_visualEngine)) {
        m_project = std::move(loadedProject);
        // Defensive cleanup: a corrupt or maliciously crafted project
        // file could carry a cyclic sidechain graph that the engine
        // would refuse anyway, but having Project state inconsistent
        // with engine state is its own bug. Repair before syncing so
        // the engine sees a clean DAG.
        const int repaired = m_project.repairSidechainCycles();
        if (repaired > 0) {
            LOG_WARN("Sidechain",
                     "Project had %d sidechain cycle(s); cleared to -1",
                     repaired);
        }
        syncTracksToEngine();
        // Re-read any IR files referenced by ConvolutionReverb
        // effects in this project. ProjectSerializer's extra-state
        // hook preserves the file PATH (cheap, portable across
        // machines) but doesn't read the file itself — we do that
        // here on the UI thread, after the engine is wired up but
        // before audio starts touching the (currently empty) IR
        // engines.
        rehydrateConvolutionIRs();
        m_projectPath = projectDir;
        m_projectDirty = false;
        // Project-local preset folder (if any) is now visible to the
        // device Preset menu via PresetManager::listPresetsForDevice.
        PresetManager::setProjectRoot(m_projectPath);
        m_undoManager.clear();
        m_selectedTrack = 0;
        m_detailTarget = DetailTarget::Track;
        m_detailReturnBus = -1;
        m_showDetailPanel = false;
        m_pianoRoll->close();
        updateWindowTitle();
        LOG_INFO("Project", "Loaded: %s", projectDir.string().c_str());
        m_toastManager.show("Loaded: " + projectDir.filename().string());
    } else {
        LOG_ERROR("Project", "Failed to load: %s",
                     projectDir.string().c_str());
        m_toastManager.show("Load failed", 2.5f, ui::ToastManager::Severity::Error);
    }
}

bool App::loadIRIntoConvReverb(effects::ConvolutionReverb* eff,
                                 const std::string& path) {
    if (!eff || path.empty()) return false;
    util::AudioFileInfo info;
    auto buf = util::loadAudioFile(path, &info);
    if (!buf || buf->numFrames() <= 0) {
        LOG_WARN("ConvReverb", "Failed to load IR: %s", path.c_str());
        return false;
    }
    // Resample to host rate if the IR was captured at a different
    // sample rate. Without this a 44.1 kHz IR on a 48 kHz host
    // plays ~9 % low and short; on the project-load path that
    // would silently corrupt restored sessions.
    const double hostRate = m_audioEngine.sampleRate();
    std::shared_ptr<audio::AudioBuffer> ir = buf;
    if (info.sampleRate > 0 &&
        static_cast<double>(info.sampleRate) != hostRate) {
        ir = util::resampleBuffer(*buf,
            static_cast<double>(info.sampleRate), hostRate);
    }
    // Mono-sum (channel-major non-interleaved layout).
    const int frames = ir->numFrames();
    const int ch     = ir->numChannels();
    std::vector<float> mono(frames, 0.0f);
    if (ch == 1) {
        std::memcpy(mono.data(), ir->channelData(0),
                     frames * sizeof(float));
    } else if (ch > 1) {
        const float invCh = 1.0f / static_cast<float>(ch);
        for (int c = 0; c < ch; ++c) {
            const float* src = ir->channelData(c);
            for (int i = 0; i < frames; ++i)
                mono[i] += src[i] * invCh;
        }
    }
    eff->loadIRMono(mono.data(),
                     static_cast<int>(mono.size()),
                     hostRate);
    eff->setIRPath(path);
    LOG_INFO("ConvReverb",
             "Loaded IR: %s (%d frames, %d ch, %d Hz src)",
             path.c_str(), frames, ch, info.sampleRate);
    return true;
}

void App::rehydrateConvolutionIRs() {
    // Walk every effect chain in the mixer (per-track + return
    // buses + master) and reload any ConvolutionReverb whose IR
    // path was preserved in extraState but whose engine is empty
    // (i.e. the file hasn't been re-read yet). Same loop also
    // catches NeuralAmp .nam paths — both effects use the
    // saveExtraState path-storage pattern; NAM's model load is
    // self-contained inside setModelPath so we don't need a
    // separate App-side helper for it.
    auto walk = [this](effects::EffectChain& chain) {
        for (int i = 0; i < chain.count(); ++i) {
            auto* fx = chain.effectAt(i);
            if (!fx) continue;
            const std::string fid = fx->id();
            if (fid == "convreverb") {
                auto* cr = static_cast<effects::ConvolutionReverb*>(fx);
                if (cr->hasIR()) continue;
                const std::string p = cr->irPath();
                if (p.empty()) continue;
                loadIRIntoConvReverb(cr, p);
            } else if (fid == "neuralamp") {
                auto* na = static_cast<effects::NeuralAmp*>(fx);
                if (na->hasModel()) continue;
                const std::string p = na->modelPath();
                if (p.empty()) continue;
                // NeuralAmp::setModelPath does the load + Reset +
                // prewarm internally. setModelPath is idempotent
                // for the path itself; the modelPath field already
                // got restored via loadExtraState, so we set it
                // again to trigger the actual file read.
                na->setModelPath(p);
            }
        }
    };
    auto& mixer = m_audioEngine.mixer();
    for (int t = 0; t < kMaxTracks; ++t) walk(mixer.trackEffects(t));
    for (int r = 0; r < kMaxReturnBuses; ++r) walk(mixer.returnEffects(r));
    walk(mixer.masterEffects());
}

void App::syncTracksToEngine() {
    for (int i = 0; i < m_project.numTracks(); ++i) {
        const auto& trk = m_project.track(i);
        uint8_t type = (trk.type == Track::Type::Audio)  ? 0
                      : (trk.type == Track::Type::Midi)   ? 1
                                                           : 2;  // Visual
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{i, type});
        m_audioEngine.sendCommand(audio::SetTrackArmedMsg{i, trk.armed});
        m_audioEngine.sendCommand(audio::SetTrackMonitorMsg{i, static_cast<uint8_t>(trk.monitorMode)});
        m_audioEngine.sendCommand(audio::SetTrackAudioInputChMsg{i, trk.audioInputCh});
        m_audioEngine.sendCommand(audio::SetTrackMonoMsg{i, trk.mono});
        m_audioEngine.sendCommand(audio::SetSidechainSourceMsg{i, trk.sidechainSource});
        m_audioEngine.sendCommand(audio::SetResampleSourceMsg{i, trk.resampleSource});
        m_audioEngine.sendCommand(audio::SetTrackVolumeMsg{i, trk.volume});
        m_audioEngine.sendCommand(audio::SetTrackMuteMsg{i, trk.muted});
        m_audioEngine.sendCommand(audio::SetTrackSoloMsg{i, trk.soloed});
        if (trk.midiOutputPort >= 0)
            m_audioEngine.sendCommand(audio::SetTrackMidiOutputMsg{i, trk.midiOutputPort, trk.midiOutputChannel});
    }
}

void App::updateWindowTitle() {
    std::string title = "Y.A.W.N";
    if (!m_projectPath.empty()) {
        title += " - " + m_projectPath.stem().string();
    } else {
        title += " - Untitled";
    }
    if (m_projectDirty) title += " *";
    SDL_SetWindowTitle(m_mainWindow.getHandle(), title.c_str());
}

void App::switchToView(ViewMode mode) {
    m_project.setViewMode(mode);
    bool showArrangement = (mode == ViewMode::Arrangement);
    // ContentGrid and ArrangementPanel are both fw2 — swap the topLeft
    // slot between the two panels directly.
    m_sessionPanel->setVisible(!showArrangement);
    m_arrangementPanel->setVisible(showArrangement);
    m_contentGrid->setTopLeft(showArrangement
        ? static_cast<ui::fw2::Widget*>(m_arrangementPanel)
        : static_cast<ui::fw2::Widget*>(m_sessionPanel));
    m_arrangementPanel->setSelectedTrack(m_selectedTrack);

    // Activate/deactivate arrangement mode for all tracks
    for (int t = 0; t < m_project.numTracks(); ++t) {
        bool active = showArrangement && !m_project.track(t).arrangementClips.empty();
        m_project.track(t).arrangementActive = active;
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{t, active});
        if (active) syncArrangementClipsToEngine(t);
    }
}

void App::syncArrangementClipsToEngine(int trackIdx) {
    auto& track = m_project.track(trackIdx);
    std::vector<audio::ArrClipRef> refs;
    refs.reserve(track.arrangementClips.size());
    for (auto& ac : track.arrangementClips) {
        audio::ArrClipRef ref;
        ref.type = ac.type == ArrangementClip::Type::Audio
            ? audio::ArrClipRef::Type::Audio : audio::ArrClipRef::Type::Midi;
        ref.startBeat = ac.startBeat;
        ref.lengthBeats = ac.lengthBeats;
        ref.offsetBeats = ac.offsetBeats;
        ref.audioBuffer = ac.audioBuffer;
        ref.midiClip = ac.midiClip;
        refs.push_back(std::move(ref));
    }
    m_audioEngine.arrangementPlayback().submitTrackClips(trackIdx, std::move(refs));
}

void SDLCALL App::onOpenFolderResult(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_pendingOpenPath = filelist[0];
}

void SDLCALL App::onSaveFolderResult(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_pendingSavePath = filelist[0];
}

void SDLCALL App::onExportSaveResult(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_pendingExportPath = filelist[0];
}

// ---------------------------------------------------------------------------
// Export Audio
// ---------------------------------------------------------------------------

void App::openExportDialog() {
    ui::fw2::FwExportDialog::Config cfg;
    cfg.arrangementLengthBeats = m_project.arrangementLength();
    cfg.loopEnabled = m_audioEngine.transport().isLoopEnabled();
    cfg.loopStartBeats = m_audioEngine.transport().loopStartBeats();
    cfg.loopEndBeats = m_audioEngine.transport().loopEndBeats();
    cfg.sampleRate = static_cast<int>(m_audioEngine.sampleRate());

    m_exportDialog.setOnResult([this](ui::fw2::ExportResult result) {
        if (result == ui::fw2::ExportResult::OK) {
            // Show native save file dialog
            auto& cfg = m_exportDialog.config();
            const char* ext = util::formatExtension(cfg.format);
            LOG_INFO("Export", "Dialog OK, showing save dialog (format: %s)", ext);

            // Store filter data as members so they survive until async callback
            // SDL pattern wants just the extension without dot (e.g. "wav" not "*.wav")
            m_exportFilterDesc = "Audio Files (*";
            m_exportFilterDesc += ext;
            m_exportFilterDesc += ")";
            m_exportFilterPattern = ext + 1; // skip leading dot
            m_exportDefaultName = "export";
            m_exportDefaultName += ext;

            m_exportFilter.name = m_exportFilterDesc.c_str();
            m_exportFilter.pattern = m_exportFilterPattern.c_str();

            SDL_ShowSaveFileDialog(onExportSaveResult, this,
                                   m_mainWindow.getHandle(),
                                   &m_exportFilter, 1, m_exportDefaultName.c_str());
            const char* err = SDL_GetError();
            if (err && err[0])
                LOG_INFO("Export", "SDL_ShowSaveFileDialog error: %s", err);
        }
    });

    m_exportDialog.open(cfg);
}

void App::startExportRender(const std::string& filePath) {
    auto& cfg = m_exportDialog.config();

    // Determine render range
    double startBeat = 0.0;
    double endBeat = m_project.arrangementLength();
    if (cfg.scope == 1) {
        startBeat = cfg.loopStartBeats;
        endBeat = cfg.loopEndBeats;
    }

    // Build render config
    audio::RenderConfig renderCfg;
    renderCfg.startBeat = startBeat;
    renderCfg.endBeat = endBeat;
    renderCfg.targetSampleRate = cfg.sampleRate;
    renderCfg.channels = 2;

    // Ensure file has correct extension
    std::string outPath = filePath;
    const char* ext = util::formatExtension(cfg.format);
    if (outPath.size() < 4 || outPath.substr(outPath.size() - std::strlen(ext)) != ext) {
        outPath += ext;
    }

    // Copy format settings for the thread
    util::ExportFormat format = cfg.format;
    util::BitDepth bitDepth = cfg.bitDepth;
    int sampleRate = cfg.sampleRate;

    // Re-open the dialog in rendering mode
    m_exportDialog.open(m_exportDialog.config());
    m_exportDialog.setRendering(true);

    auto& progress = m_exportDialog.progress();

    // Launch render on a detached thread
    std::thread([this, renderCfg, outPath, format, bitDepth, sampleRate, &progress]() {
        auto buffer = audio::OfflineRenderer::render(m_audioEngine, renderCfg, progress);

        if (buffer && !progress.cancelled.load()) {
            bool ok = util::saveAudioBuffer(outPath, *buffer, sampleRate, format, bitDepth);
            if (!ok) {
                LOG_ERROR("Export", "Failed to write: %s", outPath.c_str());
                progress.failed.store(true);
            } else {
                LOG_INFO("Export", "Written: %s", outPath.c_str());
            }
        }
        progress.done.store(true);
    }).detach();
}

#ifdef YAWN_HAS_VST3
void App::openVST3Editor(vst3::VST3PluginInstance* instance,
                         const std::string& modulePath,
                         const std::string& classID,
                         const std::string& title) {
    if (!instance) {
        LOG_WARN("VST3", "openVST3Editor: null instance");
        return;
    }
    for (auto& ed : m_vst3Editors) {
        if (ed && ed->isOpen() && ed->instance() == instance) {
            LOG_INFO("VST3", "Editor already open for '%s'", title.c_str());
            return;
        }
    }
    LOG_INFO("VST3", "Opening editor for '%s'", title.c_str());
    auto editor = std::make_unique<vst3::VST3EditorWindow>();
    if (editor->open(instance, modulePath, classID, title)) {
        m_vst3Editors.push_back(std::move(editor));
    } else {
        LOG_ERROR("VST3", "Failed to open editor for '%s'", title.c_str());
    }
}
#endif

} // namespace yawn
