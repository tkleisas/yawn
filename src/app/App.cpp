#include "app/App.h"
#include "ui/framework/PanelWrappers.h"
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
#include "effects/Filter.h"
#include "effects/Chorus.h"
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

// Factory helpers for undo — create device by name (loses parameters but restores type)
static std::unique_ptr<instruments::Instrument> makeInstrumentByName(const std::string& n) {
    if (n == "Subtractive Synth") return std::make_unique<instruments::SubtractiveSynth>();
    if (n == "FM Synth")          return std::make_unique<instruments::FMSynth>();
    if (n == "Sampler")           return std::make_unique<instruments::Sampler>();
    if (n == "Drum Rack")         return std::make_unique<instruments::DrumRack>();
    if (n == "DrumSlop")          return std::make_unique<instruments::DrumSlop>();
    if (n == "Karplus-Strong")    return std::make_unique<instruments::KarplusStrong>();
    if (n == "Wavetable Synth")   return std::make_unique<instruments::WavetableSynth>();
    if (n == "Granular Synth")    return std::make_unique<instruments::GranularSynth>();
    if (n == "Vocoder")           return std::make_unique<instruments::Vocoder>();
    if (n == "Multisampler")      return std::make_unique<instruments::Multisampler>();
    if (n == "Instrument Rack")   return std::make_unique<instruments::InstrumentRack>();
    return nullptr;
}

static std::unique_ptr<effects::AudioEffect> makeAudioEffectByName(const std::string& n) {
    if (n == "Reverb")            return std::make_unique<effects::Reverb>();
    if (n == "Delay")             return std::make_unique<effects::Delay>();
    if (n == "EQ")                return std::make_unique<effects::EQ>();
    if (n == "Compressor")        return std::make_unique<effects::Compressor>();
    if (n == "Filter")            return std::make_unique<effects::Filter>();
    if (n == "Chorus")            return std::make_unique<effects::Chorus>();
    if (n == "Distortion")        return std::make_unique<effects::Distortion>();
    if (n == "Tape Emulation")    return std::make_unique<effects::TapeEmulation>();
    if (n == "Amp Simulator")     return std::make_unique<effects::AmpSimulator>();
    if (n == "Oscilloscope")      return std::make_unique<effects::Oscilloscope>();
    if (n == "Spectrum Analyzer" || n == "Spectrum") return std::make_unique<effects::SpectrumAnalyzer>();
    if (n == "Tuner")             return std::make_unique<effects::Tuner>();
    return nullptr;
}

static std::unique_ptr<midi::MidiEffect> makeMidiEffectByName(const std::string& n) {
    if (n == "Arpeggiator")    return std::make_unique<midi::Arpeggiator>();
    if (n == "Chord")          return std::make_unique<midi::Chord>();
    if (n == "Scale")          return std::make_unique<midi::Scale>();
    if (n == "Note Length")    return std::make_unique<midi::NoteLength>();
    if (n == "Velocity")       return std::make_unique<midi::VelocityEffect>();
    if (n == "Random" || n == "MIDI Random") return std::make_unique<midi::MidiRandom>();
    if (n == "Pitch" || n == "MIDI Pitch")   return std::make_unique<midi::MidiPitch>();
    if (n == "LFO")            return std::make_unique<midi::LFO>();
    return nullptr;
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
    // File menu
    m_menuBar.addMenu("File", {
        {"New Project",   "Ctrl+N",       [this]() { newProject(); }},
        {"Open Project",  "Ctrl+O",       [this]() { openProject(); }},
        {"Save Project",  "Ctrl+S",       [this]() { saveProject(); }},
        {"Save As...",    "Ctrl+Shift+S", [this]() { saveProjectAs(); }},
        {"Export Audio",  "",             [this]() { openExportDialog(); }, true},
        {"Quit",          "Ctrl+Q",       [this]() { m_running = false; }, true},
    });

    // Edit menu
    m_menuBar.addMenu("Edit", {
        {"Undo",        "Ctrl+Z", [this]() { if (m_undoManager.canUndo()) { m_undoManager.undo(); markDirty(); } }},
        {"Redo",        "Ctrl+Y", [this]() { if (m_undoManager.canRedo()) { m_undoManager.redo(); markDirty(); } }},
        {"Preferences", "",       [this]() {
            ui::fw::PreferencesDialog::State state;
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
            m_preferencesDialog->open(state, &m_audioEngine, &m_midiEngine);
        }},
    });

    // View menu
    m_menuBar.addMenu("View", {
        {"Session View",     "Tab", [this]() { switchToView(ViewMode::Session); }},
        {"Arrangement View", "Tab", [this]() { switchToView(ViewMode::Arrangement); }},
        {"Toggle Mixer",    "M",   [this]() { m_showMixer = !m_showMixer; }},
        {"Detail Panel",    "D",   [this]() {
            m_showDetailPanel = !m_showDetailPanel;
            if (m_showDetailPanel) m_detailPanel->setOpen(true);
        }},
    });

    // Track menu
    m_menuBar.addMenu("Track", {
        {"Add Audio Track",  "",  [this]() {
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
        }},
        {"Add MIDI Track",   "",  [this]() {
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
        }},
        {"Delete Track",     "",  nullptr, true},
        {"Rename Track",     "",  [this]() {
            if (m_sessionPanel->visible()) {
                m_sessionPanel->startTrackRename(m_selectedTrack);
            } else {
                m_arrangementPanel->startTrackRename(m_selectedTrack);
            }
            SDL_StartTextInput(m_mainWindow.getHandle());
        }},
    });

    // MIDI menu
    m_menuBar.addMenu("MIDI", {
        {"MIDI Devices",    "",  nullptr},
        {"MIDI Sync",       "",  nullptr},
        {"Link Settings",   "",  nullptr, true, false},
    });

    // Help menu
    m_menuBar.addMenu("Help", {
        {"About Y.A.W.N",     "",  [this]() { m_aboutDialog->setVisible(true); }},
        {"Keyboard Shortcuts", "",  nullptr},
    });
}

void App::buildWidgetTree() {
    using namespace ui::fw;

    m_rootLayout = std::make_unique<FlexBox>(Direction::Column);
    m_rootLayout->setAlign(Align::Stretch);

    auto menuW      = std::make_unique<MenuBarWrapper>(m_menuBar);
    auto transportP = std::make_unique<TransportPanel>();
    auto sessionP   = std::make_unique<SessionPanel>();
    auto arrP       = std::make_unique<ArrangementPanel>();
    auto mixerP     = std::make_unique<MixerPanel>();
    auto browserP   = std::make_unique<BrowserPanel>();
    auto returnMstP = std::make_unique<ReturnMasterPanel>();
    auto gridP      = std::make_unique<ContentGrid>();
    auto detailP    = std::make_unique<DetailPanelWidget>();
    auto pianoP     = std::make_unique<PianoRollPanel>();
    auto aboutDlg   = std::make_unique<AboutDialog>();
    auto confirmDlg = std::make_unique<ConfirmDialogWidget>();
    auto textInputDlg = std::make_unique<TextInputDialogWidget>();
    auto prefsDlg   = std::make_unique<PreferencesDialog>();
    auto exportDlg  = std::make_unique<ExportDialog>();

    // ContentGrid fills remaining space
    gridP->setSizePolicy(SizePolicy::flexMin(1.0f, 200.0f));

    // Store raw pointers for quick access
    m_menuBarW          = menuW.get();
    m_transportPanel    = transportP.get();
    m_sessionPanel      = sessionP.get();
    m_arrangementPanel  = arrP.get();
    m_mixerPanel        = mixerP.get();
    m_browserPanel      = browserP.get();
    m_returnMasterPanel = returnMstP.get();
    m_contentGrid       = gridP.get();
    m_detailPanel       = detailP.get();
    m_pianoRoll         = pianoP.get();
    m_aboutDialog       = aboutDlg.get();
    m_confirmDialog     = confirmDlg.get();
    m_textInputDialog   = textInputDlg.get();
    m_preferencesDialog = prefsDlg.get();
    m_exportDialog      = exportDlg.get();

    m_preferencesDialog->setOnResult([this](ui::fw::DialogResult result) {
        if (result == ui::fw::DialogResult::OK) {
            auto& s = m_preferencesDialog->state();
            const auto& oldCfg = m_audioEngine.config();
            bool audioChanged = (s.sampleRate != oldCfg.sampleRate ||
                                 s.bufferSize != oldCfg.framesPerBuffer ||
                                 s.selectedOutputDevice != oldCfg.outputDevice ||
                                 s.selectedInputDevice != oldCfg.inputDevice);
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
    m_arrangementPanel->setVisible(false); // start in session view

    // Wire the 4-quadrant layout
    m_contentGrid->setChildren(m_sessionPanel, m_browserPanel,
                               m_mixerPanel, m_returnMasterPanel);

    m_rootLayout->addChild(m_menuBarW);
    m_rootLayout->addChild(m_transportPanel);
    m_rootLayout->addChild(m_contentGrid);
    m_rootLayout->addChild(m_detailPanel);
    m_rootLayout->addChild(m_pianoRoll);

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

    // Transfer ownership
    m_wrappers.push_back(std::move(menuW));
    m_wrappers.push_back(std::move(transportP));
    m_wrappers.push_back(std::move(sessionP));
    m_wrappers.push_back(std::move(arrP));
    m_wrappers.push_back(std::move(mixerP));
    m_wrappers.push_back(std::move(browserP));
    m_wrappers.push_back(std::move(returnMstP));
    m_wrappers.push_back(std::move(gridP));
    m_wrappers.push_back(std::move(detailP));
    m_wrappers.push_back(std::move(pianoP));
    m_wrappers.push_back(std::move(aboutDlg));
    m_wrappers.push_back(std::move(confirmDlg));
    m_wrappers.push_back(std::move(textInputDlg));
    m_wrappers.push_back(std::move(prefsDlg));
    m_wrappers.push_back(std::move(exportDlg));

    m_uiContext.renderer = &m_renderer;
    m_uiContext.font     = &m_font;
}

void App::computeLayout() {
    using namespace ui::fw;

    int w = m_mainWindow.getWidth();
    int h = m_mainWindow.getHeight();

    // Update panel visibility
    m_detailPanel->setVisible(m_showDetailPanel);
    m_pianoRoll->setVisible(m_pianoRoll->isOpen());

    // ContentGrid manages session + mixer + browser + returns visibility
    // MixerPanel visibility is controlled via the content grid's bottom row
    m_mixerPanel->setVisible(m_showMixer);
    m_returnMasterPanel->setVisible(m_showMixer);
    m_returnMasterPanel->setShowReturns(m_showReturns);

    Constraints c = Constraints::tight(static_cast<float>(w), static_cast<float>(h));
    m_rootLayout->measure(c, m_uiContext);
    m_rootLayout->layout(Rect{0, 0, static_cast<float>(w), static_cast<float>(h)}, m_uiContext);
}

void App::showTrackContextMenu(int trackIndex, float mx, float my) {
    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
    auto& track = m_project.track(trackIndex);

    std::vector<ui::ContextMenu::Item> items;

    // Track type selection (with confirmation dialog)
    items.push_back({"Set as Audio Track", [this, trackIndex]() {
        auto& trk = m_project.track(trackIndex);
        if (trk.type == Track::Type::Audio) return;
        m_confirmDialog->prompt(
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
        m_confirmDialog->prompt(
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

    // Separator + Instruments submenu
    std::vector<ui::ContextMenu::Item> instrItems;
    auto addInstrItem = [&](const char* label, auto factory) {
        instrItems.push_back({label, [this, trackIndex, label, factory]() {
            auto oldType = m_project.track(trackIndex).type;
            std::string oldInstr;
            auto* inst = m_audioEngine.instrument(trackIndex);
            if (inst) oldInstr = inst->name();
            uint8_t oldTypeVal = (oldType == Track::Type::Audio) ? 0 : 1;
            m_project.track(trackIndex).type = Track::Type::Midi;
            m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
            m_audioEngine.setInstrument(trackIndex, factory());
            markDirty();
            std::string newInstr = label;
            m_undoManager.push({"Set Instrument: " + newInstr,
                [this, trackIndex, oldType, oldTypeVal, oldInstr]{
                    m_project.track(trackIndex).type = oldType;
                    m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, oldTypeVal});
                    m_audioEngine.setInstrument(trackIndex, makeInstrumentByName(oldInstr));
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
    addFxItem("Filter",      [](){ return std::make_unique<effects::Filter>(); });
    addFxItem("Chorus",      [](){ return std::make_unique<effects::Chorus>(); });
    addFxItem("Distortion",  [](){ return std::make_unique<effects::Distortion>(); });
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
            fxItems.push_back({label, [this, trackIndex, modulePath, classID]() {
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

    // Delete track (with confirmation)
    bool canDelete = m_project.numTracks() > 1;
    items.push_back({"Delete Track", [this, trackIndex]() {
        m_confirmDialog->prompt(
            "This will stop playback and delete the track. Continue?",
            [this, trackIndex]() {
                // Stop transport and all clips
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
            });
    }, true, canDelete});

    m_contextMenu.open(mx, my, std::move(items));
}

void App::showClipContextMenu(int trackIndex, int sceneIndex, float mx, float my) {
    std::vector<ui::ContextMenu::Item> items;
    auto* slot = m_project.getSlot(trackIndex, sceneIndex);
    bool hasClip = slot && !slot->empty();
    bool hasClipboard = m_clipboard.type != ClipboardData::Type::None;

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
                if (src->audioClip) dst->audioClip = src->audioClip->clone();
                else if (src->midiClip) dst->midiClip = src->midiClip->clone();
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
                            if (src2->audioClip) dst2->audioClip = src2->audioClip->clone();
                            else if (src2->midiClip) dst2->midiClip = src2->midiClip->clone();
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
            if (s->audioClip) oldAudio.reset(s->audioClip->clone().release());
            if (s->midiClip) oldMidi.reset(s->midiClip->clone().release());
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            s->clear();
            markDirty();
            m_undoManager.push({"Delete Clip",
                [this, trackIndex, sceneIndex, oldAudio, oldMidi]{
                    auto* s2 = m_project.getSlot(trackIndex, sceneIndex);
                    if (!s2) return;
                    if (oldAudio) s2->audioClip = oldAudio->clone();
                    if (oldMidi) s2->midiClip = oldMidi->clone();
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

    m_contextMenu.open(mx, my, std::move(items));
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

    m_contextMenu.open(mx, my, std::move(items));
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
    if (displayScale > 0.0f) ui::Theme::scaleFactor = displayScale;

    if (!m_renderer.init()) {
        LOG_ERROR("UI", "Failed to initialize 2D renderer");
        return false;
    }

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

    m_project.init(8, 8);
    m_virtualKeyboard.init(&m_audioEngine);
    setupMenuBar();
    buildWidgetTree();
    m_aboutDialog->setIconTexture(m_iconTexture);
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
        addFx("Filter",          [](){ return std::make_unique<effects::Filter>(); });
        addFx("Chorus",          [](){ return std::make_unique<effects::Chorus>(); });
        addFx("Distortion",      [](){ return std::make_unique<effects::Distortion>(); });
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

        m_contextMenu.open(mx, my, std::move(fxItems));
    };

    m_returnMasterPanel->setOnReturnRightClick([this, buildFxMenu](int bus, float mx, float my) {
        buildFxMenu(DetailTarget::ReturnBus, bus, mx, my);
    });
    m_returnMasterPanel->setOnMasterRightClick([this, buildFxMenu](float mx, float my) {
        buildFxMenu(DetailTarget::Master, -1, mx, my);
    });

    m_settings = util::AppSettings::load();

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

    // Wire MidiEngine: scan ports, open inputs, connect to AudioEngine
    m_midiEngine.refreshPorts();
    if (!m_settings.enabledMidiInputs.empty()) {
        for (int i : m_settings.enabledMidiInputs)
            m_midiEngine.openInputPort(i);
    } else {
        for (int i = 0; i < m_midiEngine.availableInputCount(); ++i)
            m_midiEngine.openInputPort(i);
    }
    for (int i : m_settings.enabledMidiOutputs)
        m_midiEngine.openOutputPort(i);
    m_audioEngine.setMidiEngine(&m_midiEngine);
    m_midiEngine.setMonitorBuffer(&m_midiMonitor);

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
    m_detailPanel->setOnRemoveDevice([this](ui::fw::DetailPanelWidget::DeviceType type, int chainIndex) {
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
            case ui::fw::DetailPanelWidget::DeviceType::MidiFx: {
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
                        auto inst = makeMidiEffectByName(fxName);
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
            case ui::fw::DetailPanelWidget::DeviceType::AudioFx: {
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
                        auto inst = makeAudioEffectByName(fxName);
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

    m_detailPanel->setOnMoveDevice([this](ui::fw::DetailPanelWidget::DeviceType type, int fromIdx, int toIdx) {
        switch (type) {
            case ui::fw::DetailPanelWidget::DeviceType::MidiFx: {
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
            case ui::fw::DetailPanelWidget::DeviceType::AudioFx: {
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

    // Wire preset click: open context menu with preset list + Save
    m_detailPanel->setOnPresetClick([this](ui::fw::DetailPanelWidget::DeviceType type,
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

        if (type == ui::fw::DetailPanelWidget::DeviceType::Instrument) {
            if (m_detailTarget != DetailTarget::Track) return;
            if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
            auto* inst = m_audioEngine.instrument(m_selectedTrack);
            if (!inst) return;
            deviceId = inst->id();
            deviceName = inst->name();
        } else if (type == ui::fw::DetailPanelWidget::DeviceType::AudioFx) {
            auto* fx = getAudioEffect(chainIndex);
            if (!fx) return;
            deviceId = fx->id();
            deviceName = fx->name();
        } else if (type == ui::fw::DetailPanelWidget::DeviceType::MidiFx) {
            if (m_detailTarget != DetailTarget::Track) return;
            if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
            auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
            if (!fx) return;
            deviceId = fx->id();
            deviceName = fx->name();
        }
        int t = m_selectedTrack;

        auto presets = PresetManager::listPresetsForDevice(deviceId);
        std::vector<ui::ContextMenu::Item> items;

        // Add "Save Preset..." item
        auto target = m_detailTarget;
        int bus = m_detailReturnBus;
        items.push_back({"Save Preset...", [this, type, chainIndex, deviceId, deviceName, target, bus]() {
            SDL_StartTextInput(m_mainWindow.getHandle());
            m_textInputDialog->prompt("Save Preset", "My Preset",
                [this, type, chainIndex, deviceId, deviceName, target, bus](const std::string& name) {
                    std::filesystem::path saved;
                    if (type == ui::fw::DetailPanelWidget::DeviceType::Instrument) {
                        auto* inst = m_audioEngine.instrument(m_selectedTrack);
                        if (inst) saved = saveDevicePreset(name, *inst);
                    } else if (type == ui::fw::DetailPanelWidget::DeviceType::AudioFx) {
                        effects::AudioEffect* fx = nullptr;
                        switch (target) {
                            case DetailTarget::Track:     fx = m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex); break;
                            case DetailTarget::ReturnBus: fx = m_audioEngine.mixer().returnEffects(bus).effectAt(chainIndex); break;
                            case DetailTarget::Master:    fx = m_audioEngine.mixer().masterEffects().effectAt(chainIndex); break;
                        }
                        if (fx) saved = saveDevicePreset(name, *fx);
                    } else if (type == ui::fw::DetailPanelWidget::DeviceType::MidiFx) {
                        auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
                        if (fx) saved = saveDevicePreset(name, *fx);
                    }
                    if (!saved.empty()) {
                        m_detailPanel->setDevicePresetName(type, chainIndex, name);
                        LOG_INFO("Preset", "Saved '%s' for %s", name.c_str(), deviceName.c_str());
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
                    if (type == ui::fw::DetailPanelWidget::DeviceType::Instrument) {
                        auto* inst = m_audioEngine.instrument(m_selectedTrack);
                        if (inst) ok = loadDevicePreset(path, *inst);
                    } else if (type == ui::fw::DetailPanelWidget::DeviceType::AudioFx) {
                        effects::AudioEffect* fx = nullptr;
                        switch (target) {
                            case DetailTarget::Track:     fx = m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex); break;
                            case DetailTarget::ReturnBus: fx = m_audioEngine.mixer().returnEffects(bus).effectAt(chainIndex); break;
                            case DetailTarget::Master:    fx = m_audioEngine.mixer().masterEffects().effectAt(chainIndex); break;
                        }
                        if (fx) ok = loadDevicePreset(path, *fx);
                    } else if (type == ui::fw::DetailPanelWidget::DeviceType::MidiFx) {
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

        m_contextMenu.open(mx, my, std::move(items));
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
    // Stop library scanner before anything else
    if (m_libraryScanner) m_libraryScanner->stop();
    m_libraryScanner.reset();
    m_libraryDb.close();

#ifdef YAWN_HAS_VST3
    m_vst3Editors.clear();
    m_vst3Scanner.reset();
#endif
    m_audioEngine.shutdown();
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

    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);

    auto clip = std::make_unique<audio::Clip>();
    clip->name = name;
    clip->buffer = buffer;
    clip->looping = true;
    clip->gain = 0.8f;

    auto* clipPtr = m_project.setClip(trackIndex, sceneIndex, std::move(clip));

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

    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);

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

    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    // Convert non-interleaved AudioBuffer to interleaved for Sampler::loadSample
    int frames = buffer->numFrames();
    int channels = buffer->numChannels();
    std::vector<float> interleaved(static_cast<size_t>(frames) * channels);
    for (int ch = 0; ch < channels; ++ch) {
        const float* src = buffer->channelData(ch);
        for (int i = 0; i < frames; ++i)
            interleaved[static_cast<size_t>(i) * channels + ch] = src[i];
    }

    sampler->loadSample(interleaved.data(), frames, channels);

    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
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

    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    int frames = buffer->numFrames();
    int channels = buffer->numChannels();
    std::vector<float> interleaved(static_cast<size_t>(frames) * channels);
    for (int ch = 0; ch < channels; ++ch) {
        const float* src = buffer->channelData(ch);
        for (int i = 0; i < frames; ++i)
            interleaved[static_cast<size_t>(i) * channels + ch] = src[i];
    }

    ds->loadLoop(interleaved.data(), frames, channels);

    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
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

    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    int frames = buffer->numFrames();
    int channels = buffer->numChannels();
    std::vector<float> interleaved(static_cast<size_t>(frames) * channels);
    for (int ch = 0; ch < channels; ++ch) {
        const float* src = buffer->channelData(ch);
        for (int i = 0; i < frames; ++i)
            interleaved[static_cast<size_t>(i) * channels + ch] = src[i];
    }

    rack->loadPad(rack->selectedPad(), interleaved.data(), frames, channels);

    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
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

    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    int frames = buffer->numFrames();
    int channels = buffer->numChannels();
    std::vector<float> interleaved(static_cast<size_t>(frames) * channels);
    for (int ch = 0; ch < channels; ++ch) {
        const float* src = buffer->channelData(ch);
        for (int i = 0; i < frames; ++i)
            interleaved[static_cast<size_t>(i) * channels + ch] = src[i];
    }

    granular->loadSample(interleaved.data(), frames, channels);

    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
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

    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    int frames = buffer->numFrames();
    int channels = buffer->numChannels();
    std::vector<float> interleaved(static_cast<size_t>(frames) * channels);
    for (int ch = 0; ch < channels; ++ch) {
        const float* src = buffer->channelData(ch);
        for (int i = 0; i < frames; ++i)
            interleaved[static_cast<size_t>(i) * channels + ch] = src[i];
    }

    vocoder->loadModulatorSample(interleaved.data(), frames, channels);

    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
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

void App::processEvents() {
    computeLayout();  // ensure widget bounds are current for hit-testing

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.repeat) break;
                bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                bool ctrl  = (event.key.mod & SDL_KMOD_CTRL)  != 0;

                // Block keys when confirm dialog or about is open
                if (m_confirmDialog->isOpen()) {
                    if (event.key.key == SDLK_ESCAPE) m_confirmDialog->dismiss();
                    if (event.key.key == SDLK_RETURN) {
                        ui::fw::KeyEvent ke; ke.keyCode = 13;
                        m_confirmDialog->onKeyDown(ke);
                    }
                    break;
                }
                if (m_textInputDialog->isOpen()) {
                    ui::fw::KeyEvent ke;
                    ke.keyCode = static_cast<int>(event.key.key);
                    m_textInputDialog->onKeyDown(ke);
                    break;
                }
                if (m_aboutDialog->isVisible()) {
                    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN)
                        m_aboutDialog->setVisible(false);
                    break;
                }

                // Block keys when preferences dialog is open
                if (m_preferencesDialog->isOpen()) {
                    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN) {
                        ui::fw::KeyEvent ke;
                        ke.keyCode = (event.key.key == SDLK_ESCAPE) ? 27 : 13;
                        m_preferencesDialog->onKeyDown(ke);
                    }
                    break;
                }

                // Block keys when export dialog is open
                if (m_exportDialog->isOpen()) {
                    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN) {
                        ui::fw::KeyEvent ke;
                        ke.keyCode = (event.key.key == SDLK_ESCAPE) ? 27 : 13;
                        m_exportDialog->onKeyDown(ke);
                    }
                    break;
                }

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
                    break;
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
                    break;
                }

                // InputState keyboard forwarding (for focused widgets)
                if (m_inputState.focused()) {
                    if (m_inputState.onKeyDown(static_cast<int>(event.key.key), ctrl, shift))
                        break;
                }

                // Track rename keyboard handling
                if (m_sessionPanel->isRenamingTrack()) {
                    bool wasRenaming = true;
                    m_sessionPanel->handleRenameKeyDown(static_cast<int>(event.key.key));
                    if (!m_sessionPanel->isRenamingTrack())
                        SDL_StopTextInput(m_mainWindow.getHandle());
                    if (wasRenaming) break;
                }
                if (m_arrangementPanel->isRenamingTrack()) {
                    bool wasRenaming = true;
                    m_arrangementPanel->handleRenameKeyDown(static_cast<int>(event.key.key));
                    if (!m_arrangementPanel->isRenamingTrack())
                        SDL_StopTextInput(m_mainWindow.getHandle());
                    if (wasRenaming) break;
                }

                // Detail panel knob text-edit mode
                if (m_showDetailPanel && m_detailPanel->hasEditingKnob()) {
                    if (m_detailPanel->forwardKeyDown(static_cast<int>(event.key.key))) {
                        if (!m_detailPanel->hasEditingKnob())
                            SDL_StopTextInput(m_mainWindow.getHandle());
                        break;
                    }
                }
                // Browser panel knob text-edit mode
                if (m_browserPanel->hasEditingKnob()) {
                    if (m_browserPanel->forwardKeyDown(static_cast<int>(event.key.key))) {
                        if (!m_browserPanel->hasEditingKnob())
                            SDL_StopTextInput(m_mainWindow.getHandle());
                        break;
                    }
                }

                // Piano roll keyboard shortcuts
                if (m_pianoRoll->isOpen()) {
                    bool prCtrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
                    if (m_pianoRoll->handleKeyDown(static_cast<int>(event.key.key), prCtrl)) {
                        if (!m_pianoRoll->isOpen()) {
                            // Piano roll was closed (Escape)
                        }
                        break;
                    }
                }

                // Virtual keyboard (intercepts musical keys before shortcuts)
                // Skip when renaming a track — keys are for text input, not notes
                if (!m_sessionPanel->isRenamingTrack() && !m_arrangementPanel->isRenamingTrack()) {
                    if (m_virtualKeyboard.onKeyDown(event.key.key))
                        break;
                }

                // Detail panel arrow key navigation
                if (m_showDetailPanel && m_detailPanel->isFocused()) {
                    if (event.key.key == SDLK_LEFT) { m_detailPanel->scrollLeft(); break; }
                    if (event.key.key == SDLK_RIGHT) { m_detailPanel->scrollRight(); break; }
                }

                switch (event.key.key) {
                    case SDLK_ESCAPE:
                        if (m_contextMenu.isOpen()) {
                            m_contextMenu.close();
                        } else if (m_menuBar.isOpen()) {
                            m_menuBar.close();
                        } else {
                            m_running = false;
                        }
                        break;

                    case SDLK_SPACE:
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

                    case SDLK_M:
                        if (!shift) m_showMixer = !m_showMixer;
                        break;

                    case SDLK_D:
                        if (ctrl && !shift && m_project.viewMode() == ViewMode::Arrangement) {
                            ui::fw::KeyEvent ke;
                            ke.keyCode = 'd';
                            ke.mods.ctrl = true;
                            m_arrangementPanel->onKeyDown(ke);
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
                            ui::fw::KeyEvent ke;
                            ke.keyCode = static_cast<int>(event.key.key);
                            m_arrangementPanel->onKeyDown(ke);
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
                            ui::fw::KeyEvent ke;
                            ke.keyCode = static_cast<int>(event.key.key);
                            m_arrangementPanel->onKeyDown(ke);
                        }
                        break;

                    default:
                        break;
                }
                break;
            }

            case SDL_EVENT_KEY_UP: {
                // Virtual keyboard note-off
                m_virtualKeyboard.onKeyUp(event.key.key);
                break;
            }

            case SDL_EVENT_TEXT_INPUT: {
                // Text input dialog (modal)
                if (m_textInputDialog->isOpen()) {
                    ui::fw::TextInputEvent te;
                    std::strncpy(te.text, event.text.text, sizeof(te.text) - 1);
                    m_textInputDialog->onTextInput(te);
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

                // Context menu hover
                m_contextMenu.handleMouseMove(mx, my);

                // Detail panel context menu hover
                if (m_showDetailPanel)
                    m_detailPanel->handleDeviceContextMenuMouseMove(mx, my);

                // Menu bar hover
                m_menuBar.handleMouseMove(mx, my);

                // InputState hover + drag (computes dx/dy internally)
                m_inputState.onMouseMove(mx, my);

                // Forward drag via widget tree mouse capture
                if (ui::fw::Widget::capturedWidget()) {
                    ui::fw::MouseMoveEvent me;
                    me.x = mx; me.y = my;
                    auto sdlMod = SDL_GetModState();
                    me.mods.ctrl  = (sdlMod & SDL_KMOD_CTRL)  != 0;
                    me.mods.shift = (sdlMod & SDL_KMOD_SHIFT) != 0;
                    me.mods.alt   = (sdlMod & SDL_KMOD_ALT)   != 0;
                    m_rootLayout->dispatchMouseMove(me);
                }

                // ContentGrid hover detection for divider cursor
                {
                    ui::fw::MouseMoveEvent me;
                    me.x = mx; me.y = my;
                    m_contentGrid->onMouseMove(me);
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

                // Confirm dialog takes top priority (modal)
                if (m_confirmDialog->isOpen()) {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = ui::fw::MouseButton::Left;
                    m_confirmDialog->onMouseDown(me);
                    break;
                }

                // Text input dialog (modal)
                if (m_textInputDialog->isOpen()) {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = ui::fw::MouseButton::Left;
                    m_textInputDialog->onMouseDown(me);
                    break;
                }

                // About dialog (modal)
                if (m_aboutDialog->isVisible()) {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = ui::fw::MouseButton::Left;
                    m_aboutDialog->onMouseDown(me);
                    break;
                }

                // Preferences dialog (modal)
                if (m_preferencesDialog->isOpen()) {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = ui::fw::MouseButton::Left;
                    m_preferencesDialog->onMouseDown(me);
                    break;
                }

                // Export dialog (modal)
                if (m_exportDialog->isOpen()) {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = ui::fw::MouseButton::Left;
                    m_exportDialog->onMouseDown(me);
                    break;
                }

                // Context menu takes priority when open
                if (m_contextMenu.isOpen()) {
                    m_contextMenu.handleClick(mx, my);
                    break;
                }

                // Priority: menu bar → input state widgets → mixer → session
                if (m_menuBar.contains(mx, my) || (my < m_menuBar.height() && !m_menuBar.isOpen())) {
                    m_menuBar.handleClick(mx, my);
                    break;
                }

                // Close menu if open and clicked elsewhere
                if (m_menuBar.isOpen()) {
                    m_menuBar.close();
                    break;
                }

                // InputState widgets
                if (m_inputState.onMouseDown(mx, my, btn))
                    break;

                // Detail panel — dispatch via widget tree
                bool rightClick = (btn == SDL_BUTTON_RIGHT);
                bool hadEditingKnob = m_showDetailPanel && m_detailPanel->hasEditingKnob();
                if (m_showDetailPanel) {
                    if (rightClick) {
                        if (m_detailPanel->handleRightClick(mx, my)) {
                            m_detailPanel->setFocused(true);
                            break;
                        }
                    } else {
                        ui::fw::MouseEvent me;
                        me.x = mx; me.y = my;
                        me.button = ui::fw::MouseButton::Left;
                        if (m_detailPanel->onMouseDown(me)) {
                            // Start/stop SDL text input for knob edit mode
                            if (m_detailPanel->hasEditingKnob() && !hadEditingKnob)
                                SDL_StartTextInput(m_mainWindow.getHandle());
                            else if (!m_detailPanel->hasEditingKnob() && hadEditingKnob)
                                SDL_StopTextInput(m_mainWindow.getHandle());
                            break;
                        }
                    }
                }

                // Clicking outside detail panel — cancel any editing knob
                if (hadEditingKnob) {
                    m_detailPanel->cancelEditingKnobs();
                    SDL_StopTextInput(m_mainWindow.getHandle());
                }
                m_detailPanel->setFocused(false);

                // Browser panel — dispatch via widget tree
                {
                    bool hadBrowserKnob = m_browserPanel->hasEditingKnob();
                    if (!rightClick) {
                        ui::fw::MouseEvent me;
                        me.x = mx; me.y = my;
                        me.button = ui::fw::MouseButton::Left;
                        if (m_browserPanel->onMouseDown(me)) {
                            if (m_browserPanel->hasEditingKnob() && !hadBrowserKnob)
                                SDL_StartTextInput(m_mainWindow.getHandle());
                            else if (!m_browserPanel->hasEditingKnob() && hadBrowserKnob)
                                SDL_StopTextInput(m_mainWindow.getHandle());
                            break;
                        }
                    }
                    if (hadBrowserKnob) {
                        m_browserPanel->cancelEditingKnobs();
                        SDL_StopTextInput(m_mainWindow.getHandle());
                    }
                }

                // Piano roll click handling
                if (m_pianoRoll->isOpen()) {
                    if (rightClick) {
                        if (m_pianoRoll->handleRightClick(mx, my)) break;
                    } else {
                        ui::fw::MouseEvent me;
                        me.x = mx; me.y = my;
                        me.button = ui::fw::MouseButton::Left;
                        if (m_pianoRoll->onMouseDown(me)) {
                            markDirty();
                            break;
                        }
                    }
                }

                // Right-click on track headers → open context menu (session view only)
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

                // Existing view handlers: mixer → session
                // Handle double-click for BPM/time sig editing (now on transport panel)
                if (event.button.clicks >= 2 && btn == SDL_BUTTON_LEFT) {
                    if (m_transportPanel->handleDoubleClick(mx, my)) {
                        SDL_StartTextInput(m_mainWindow.getHandle());
                        break;
                    }
                    // Double-click on a MIDI clip slot → open piano roll
                    int dblTrack = -1, dblScene = -1;
                    if (m_sessionPanel->getSlotAt(mx, my, dblTrack, dblScene)) {
                        auto* slot = m_project.getSlot(dblTrack, dblScene);
                        if (slot && slot->midiClip) {
                            m_pianoRoll->setClip(slot->midiClip.get(), dblTrack);
                            m_pianoRoll->setOpen(true);
                            m_selectedTrack = dblTrack;
                            break;
                        }
                        // Double-click empty slot on MIDI track → create new clip
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
                // Transport panel click (tap tempo, dismiss editing, MIDI Learn right-click)
                {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = rightClick ? ui::fw::MouseButton::Right : ui::fw::MouseButton::Left;
                    m_transportPanel->onMouseDown(me);
                }
                // Stop text input if editing was cancelled by clicking elsewhere
                bool wasEditing = m_transportPanel->isEditing();
                // Dispatch click through content grid (handles dividers + children)
                {
                    m_sessionPanel->clearLastClickTrack();
                    m_sessionPanel->clearRightClick();
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = rightClick ? ui::fw::MouseButton::Right : ui::fw::MouseButton::Left;
                    me.clickCount = event.button.clicks;
                    auto sdlMod = SDL_GetModState();
                    me.mods.ctrl  = (sdlMod & SDL_KMOD_CTRL)  != 0;
                    me.mods.shift = (sdlMod & SDL_KMOD_SHIFT) != 0;
                    me.mods.alt   = (sdlMod & SDL_KMOD_ALT)   != 0;
                    m_contentGrid->dispatchMouseDown(me);
                    // Start SDL text input if a track rename started
                    if (m_sessionPanel->isRenamingTrack() || m_arrangementPanel->isRenamingTrack()) {
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
                    if (selScene >= 0) {
                        m_selectedScene = selScene;
                    }
                    if (selTrack >= 0 || selScene >= 0) {
                        updateDetailForSelectedTrack();
                    }
                    // Right-click on clip slot → show clip context menu
                    int rcTrack = m_sessionPanel->lastRightClickTrack();
                    int rcScene = m_sessionPanel->lastRightClickScene();
                    if (rcTrack >= 0 && rcScene >= 0) {
                        m_selectedTrack = rcTrack;
                        m_selectedScene = rcScene;
                        showClipContextMenu(rcTrack, rcScene, mx, my);
                    }
                    // Right-click on scene label → show scene context menu
                    int rcSceneLabel = m_sessionPanel->rightClickSceneLabel();
                    if (rcSceneLabel >= 0) {
                        m_selectedScene = rcSceneLabel;
                        showSceneContextMenu(rcSceneLabel, mx, my);
                    }
                }
                if (wasEditing && !m_transportPanel->isEditing()) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                float mx = event.button.x;
                float my = event.button.y;
                int btn = event.button.button;
                m_inputState.onMouseUp(mx, my, btn);
                // Release mouse capture (mixer fader drag, clip drag, etc.)
                if (ui::fw::Widget::capturedWidget()) {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = (btn == SDL_BUTTON_RIGHT) ? ui::fw::MouseButton::Right : ui::fw::MouseButton::Left;
                    auto sdlMod = SDL_GetModState();
                    me.mods.ctrl  = (sdlMod & SDL_KMOD_CTRL)  != 0;
                    me.mods.shift = (sdlMod & SDL_KMOD_SHIFT) != 0;
                    me.mods.alt   = (sdlMod & SDL_KMOD_ALT)   != 0;
                    m_rootLayout->dispatchMouseUp(me);
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
                    ui::Theme::scaleFactor = SDL_GetWindowDisplayScale(m_mainWindow.getHandle());
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
                auto sb = m_sessionPanel->bounds();
                auto mb = m_mixerPanel->bounds();
                auto db = m_detailPanel->bounds();
                auto pb = m_pianoRoll->bounds();

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
                    ui::fw::ScrollEvent se;
                    se.x = m_lastMouseX; se.y = m_lastMouseY;
                    se.dx = dx; se.dy = dy;
                    m_mixerPanel->onScroll(se);
                } else if (m_lastMouseY >= sb.y && m_lastMouseY < sb.y + sb.h) {
                    if (m_project.viewMode() == ViewMode::Arrangement) {
                        auto ab = m_arrangementPanel->bounds();
                        if (m_lastMouseX >= ab.x && m_lastMouseX < ab.x + ab.w) {
                            auto mod = SDL_GetModState();
                            bool ctrl  = (mod & SDL_KMOD_CTRL) != 0;
                            bool shift = (mod & SDL_KMOD_SHIFT) != 0;
                            ui::fw::ScrollEvent se;
                            se.x = m_lastMouseX; se.y = m_lastMouseY;
                            se.dx = dx; se.dy = dy;
                            se.mods.ctrl = ctrl; se.mods.shift = shift;
                            m_arrangementPanel->onScroll(se);
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
                        float arrGridX = ab.x + ui::fw::ArrangementPanel::kTrackHeaderW;
                        float arrGridY = ab.y + ui::fw::ArrangementPanel::kRulerH;
                        if (dropX >= arrGridX && dropY >= arrGridY) {
                            float relX = (dropX - arrGridX) + m_arrangementPanel->scrollX();
                            float relY = (dropY - arrGridY) + m_arrangementPanel->scrollY();
                            int trackIdx = m_arrangementPanel->trackAtY(relY);
                            double beatPos = static_cast<double>(relX) / m_arrangementPanel->zoom();
                            beatPos = m_arrangementPanel->snapBeat(beatPos);
                            if (trackIdx >= 0 && trackIdx < m_project.numTracks()) {
                                if (m_project.track(trackIdx).type == Track::Type::Audio) {
                                    loadClipToArrangement(file, trackIdx, beatPos);
                                } else {
                                    // For MIDI tracks, try loading as sample to instrument
                                    if (!loadSampleToSampler(file, trackIdx))
                                        if (!loadSampleToDrumRack(file, trackIdx))
                                            if (!loadSampleToGranular(file, trackIdx))
                                                LOG_WARN("Drop", "Cannot drop audio on MIDI arrangement track");
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
    if (m_exportDialog->isRendering() && m_exportDialog->progress().done.load()) {
        m_exportDialog->setRendering(false);
        m_exportDialog->forceClose();
        if (m_exportDialog->progress().failed.load()) {
            LOG_ERROR("Export", "Render failed");
        } else if (m_exportDialog->progress().cancelled.load()) {
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
                    m_detailPanel->viewMode() == ui::fw::DetailPanelWidget::ViewMode::AudioClip &&
                    !msg.isMidi) {
                    m_detailPanel->setClipPlayPosition(msg.playPosition);
                    m_detailPanel->setClipPlaying(msg.playing);
                }
                // Forward MIDI playhead to piano roll
                if (msg.isMidi && msg.trackIndex == m_pianoRoll->trackIndex() &&
                    m_pianoRoll->isOpen()) {
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
                            auto newClip = std::make_unique<midi::MidiClip>(data.lengthBeats);
                            newClip->setName("Rec " + std::to_string(ti + 1));
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
                            auto lq = slot ? slot->launchQuantize : audio::QuantizeMode::NextBar;
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
                            // New recording: create clip
                            auto clip = std::make_unique<audio::Clip>();
                            clip->name = "Rec " + std::to_string(ti + 1) + "-" + std::to_string(si + 1);
                            clip->buffer = audioBuffer;
                            clip->looping = true;
                            clip->gain = 1.0f;
                            m_project.setClip(ti, si, std::move(clip));
                            LOG_INFO("Audio", "Audio recorded: Track %d, Scene %d, %" PRId64 " frames",
                                        ti + 1, si + 1, data.frameCount);
                        }
                        markDirty();

                        auto* clipPtr = m_project.getClip(ti, si);
                        if (clipPtr) {
                            auto* recSlot = m_project.getSlot(ti, si);
                            auto lq = recSlot ? recSlot->launchQuantize : audio::QuantizeMode::NextBar;
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

    // Compute widget tree layout and render all panels
    computeLayout();
    m_rootLayout->render(m_uiContext);

    // Menu bar rendered last so dropdown menus appear on top of panels
    m_menuBar.render(m_renderer, m_font, static_cast<float>(w));

    // Context menu (very top layer)
    m_contextMenu.render(m_renderer, m_font);

    // Modal dialogs (rendered on top of everything)
    {
        float sw = static_cast<float>(w);
        float sh = static_cast<float>(h);
        ui::fw::Rect screenBounds{0, 0, sw, sh};

        if (m_confirmDialog->isOpen()) {
            m_confirmDialog->layout(screenBounds, m_uiContext);
            m_confirmDialog->paint(m_uiContext);
        }
        if (m_textInputDialog->isOpen()) {
            m_textInputDialog->layout(screenBounds, m_uiContext);
            m_textInputDialog->paint(m_uiContext);
        }
        if (m_aboutDialog->isVisible()) {
            m_aboutDialog->layout(screenBounds, m_uiContext);
            m_aboutDialog->paint(m_uiContext);
        }
        if (m_preferencesDialog->isOpen()) {
            m_preferencesDialog->layout(screenBounds, m_uiContext);
            m_preferencesDialog->paint(m_uiContext);
        }
        if (m_exportDialog->isOpen()) {
            m_exportDialog->layout(screenBounds, m_uiContext);
            m_exportDialog->paint(m_uiContext);
        }
    }

    m_renderer.endFrame();

    m_mainWindow.swap();
}

// ---------------------------------------------------------------------------
// Project file operations
// ---------------------------------------------------------------------------

void App::newProject() {
    auto doNew = [this]() {
        m_audioEngine.sendCommand(audio::TransportStopMsg{});

        // Reset project to defaults
        m_project = Project();
        m_project.init(4, 4);

        // Reset audio engine state (instruments, effects, mixer)
        for (int i = 0; i < 16; ++i) {
            m_audioEngine.setInstrument(i, nullptr);
            m_audioEngine.midiEffectChain(i).clear();
            m_audioEngine.mixer().trackEffects(i).clear();
        }
        for (int r = 0; r < 4; ++r)
            m_audioEngine.mixer().returnEffects(r).clear();
        m_audioEngine.mixer().masterEffects().clear();

        // Reset mixer volumes/pans
        for (int i = 0; i < 16; ++i) {
            m_audioEngine.mixer().setTrackVolume(i, 1.0f);
            m_audioEngine.mixer().setTrackPan(i, 0.0f);
            m_audioEngine.mixer().setTrackMute(i, false);
            m_audioEngine.mixer().setTrackSolo(i, false);
        }
        m_audioEngine.mixer().setMasterVolume(1.0f);
        m_audioEngine.sendCommand(audio::TransportSetBPMMsg{120.0});

        syncTracksToEngine();

        m_projectPath.clear();
        m_projectDirty = false;
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
        m_confirmDialog->prompt("Save changes before creating a new project?",
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
    auto doOpen = [this]() {
        SDL_ShowOpenFolderDialog(onOpenFolderResult, this,
                                m_mainWindow.getHandle(), nullptr, false);
    };

    if (m_projectDirty) {
        m_confirmDialog->prompt("Save changes before opening another project?",
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
    if (m_projectPath.empty()) {
        saveProjectAs();
        return;
    }
    doSaveProject(m_projectPath);
}

void App::saveProjectAs() {
    SDL_ShowSaveFileDialog(onSaveFolderResult, this,
                           m_mainWindow.getHandle(), nullptr, 0, "Untitled.yawn");
}

void App::doSaveProject(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    fs::path projectDir = path;
    // Ensure .yawn extension
    if (projectDir.extension() != ".yawn")
        projectDir += ".yawn";

    if (ProjectSerializer::saveToFolder(projectDir, m_project, m_audioEngine, &m_midiLearnManager)) {
        m_projectPath = projectDir;
        m_projectDirty = false;
        updateWindowTitle();
        LOG_INFO("Project", "Saved to: %s", projectDir.string().c_str());
    } else {
        LOG_ERROR("Project", "Failed to save: %s", projectDir.string().c_str());
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

    // Clear current engine state before loading
    for (int i = 0; i < 16; ++i) {
        m_audioEngine.setInstrument(i, nullptr);
        m_audioEngine.midiEffectChain(i).clear();
        m_audioEngine.mixer().trackEffects(i).clear();
    }
    for (int r = 0; r < 4; ++r)
        m_audioEngine.mixer().returnEffects(r).clear();
    m_audioEngine.mixer().masterEffects().clear();

    Project loadedProject;
    if (ProjectSerializer::loadFromFolder(projectDir, loadedProject, m_audioEngine, &m_midiLearnManager)) {
        m_project = std::move(loadedProject);
        syncTracksToEngine();
        m_projectPath = projectDir;
        m_projectDirty = false;
        m_undoManager.clear();
        m_selectedTrack = 0;
        m_detailTarget = DetailTarget::Track;
        m_detailReturnBus = -1;
        m_showDetailPanel = false;
        m_pianoRoll->close();
        updateWindowTitle();
        LOG_INFO("Project", "Loaded: %s", projectDir.string().c_str());
    } else {
        LOG_ERROR("Project", "Failed to load: %s",
                     projectDir.string().c_str());
    }
}

void App::syncTracksToEngine() {
    for (int i = 0; i < m_project.numTracks(); ++i) {
        const auto& trk = m_project.track(i);
        uint8_t type = (trk.type == Track::Type::Midi) ? 1 : 0;
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
    m_sessionPanel->setVisible(!showArrangement);
    m_arrangementPanel->setVisible(showArrangement);
    m_contentGrid->setTopLeft(showArrangement
        ? static_cast<ui::fw::Widget*>(m_arrangementPanel)
        : static_cast<ui::fw::Widget*>(m_sessionPanel));
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
    ui::fw::ExportDialog::Config cfg;
    cfg.arrangementLengthBeats = m_project.arrangementLength();
    cfg.loopEnabled = m_audioEngine.transport().isLoopEnabled();
    cfg.loopStartBeats = m_audioEngine.transport().loopStartBeats();
    cfg.loopEndBeats = m_audioEngine.transport().loopEndBeats();
    cfg.sampleRate = static_cast<int>(m_audioEngine.sampleRate());

    m_exportDialog->setOnResult([this](ui::fw::DialogResult result) {
        if (result == ui::fw::DialogResult::OK) {
            // Show native save file dialog
            auto& cfg = m_exportDialog->config();
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

    m_exportDialog->open(cfg);
}

void App::startExportRender(const std::string& filePath) {
    if (!m_exportDialog) return;

    auto& cfg = m_exportDialog->config();

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
    m_exportDialog->open(m_exportDialog->config());
    m_exportDialog->setRendering(true);

    auto& progress = m_exportDialog->progress();

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
