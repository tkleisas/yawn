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
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include "util/ProjectSerializer.h"
#include "util/Logger.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cinttypes>
#include <cstring>

namespace yawn {

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
        {"Export Audio",  "",             nullptr, true},
        {"Quit",          "Ctrl+Q",       [this]() { m_running = false; }, true},
    });

    // Edit menu
    m_menuBar.addMenu("Edit", {
        {"Undo",        "Ctrl+Z", nullptr},
        {"Redo",        "Ctrl+Y", nullptr},
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
            m_preferencesDialog->open(state, &m_audioEngine, &m_midiEngine);
        }},
    });

    // View menu
    m_menuBar.addMenu("View", {
        {"Session View",    "",    nullptr},
        {"Arrangement View","",    nullptr, false, false},
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
            LOG_INFO("Audio", "Added Audio track %d", m_project.numTracks());
        }},
        {"Add MIDI Track",   "",  [this]() {
            int idx = m_project.numTracks();
            m_project.addTrack("MIDI " + std::to_string(idx + 1), Track::Type::Midi);
            m_audioEngine.sendCommand(audio::SetTrackTypeMsg{idx, 1});
            m_audioEngine.setInstrument(idx, std::make_unique<instruments::SubtractiveSynth>());
            markDirty();
            LOG_INFO("MIDI", "Added MIDI track %d (with SubSynth)", m_project.numTracks());
        }},
        {"Delete Track",     "",  nullptr, true},
        {"Rename Track",     "",  nullptr},
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
    auto mixerP     = std::make_unique<MixerPanel>();
    auto browserP   = std::make_unique<BrowserPanel>();
    auto returnMstP = std::make_unique<ReturnMasterPanel>();
    auto gridP      = std::make_unique<ContentGrid>();
    auto detailP    = std::make_unique<DetailPanelWidget>();
    auto pianoP     = std::make_unique<PianoRollPanel>();
    auto aboutDlg   = std::make_unique<AboutDialog>();
    auto confirmDlg = std::make_unique<ConfirmDialogWidget>();
    auto prefsDlg   = std::make_unique<PreferencesDialog>();

    // ContentGrid fills remaining space
    gridP->setSizePolicy(SizePolicy::flexMin(1.0f, 200.0f));

    // Store raw pointers for quick access
    m_menuBarW          = menuW.get();
    m_transportPanel    = transportP.get();
    m_sessionPanel      = sessionP.get();
    m_mixerPanel        = mixerP.get();
    m_browserPanel      = browserP.get();
    m_returnMasterPanel = returnMstP.get();
    m_contentGrid       = gridP.get();
    m_detailPanel       = detailP.get();
    m_pianoRoll         = pianoP.get();
    m_aboutDialog       = aboutDlg.get();
    m_confirmDialog     = confirmDlg.get();
    m_preferencesDialog = prefsDlg.get();

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

            // Apply metronome settings to audio engine
            m_audioEngine.sendCommand(audio::MetronomeSetVolumeMsg{s.metronomeVolume});
            m_audioEngine.sendCommand(audio::MetronomeSetModeMsg{s.metronomeMode});
            m_audioEngine.sendCommand(audio::TransportSetCountInMsg{s.countInBars});
            m_transportPanel->setCountInBars(s.countInBars);

            util::AppSettings::save(m_settings);
        }
    });

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

    // Transfer ownership
    m_wrappers.push_back(std::move(menuW));
    m_wrappers.push_back(std::move(transportP));
    m_wrappers.push_back(std::move(sessionP));
    m_wrappers.push_back(std::move(mixerP));
    m_wrappers.push_back(std::move(browserP));
    m_wrappers.push_back(std::move(returnMstP));
    m_wrappers.push_back(std::move(gridP));
    m_wrappers.push_back(std::move(detailP));
    m_wrappers.push_back(std::move(pianoP));
    m_wrappers.push_back(std::move(aboutDlg));
    m_wrappers.push_back(std::move(confirmDlg));
    m_wrappers.push_back(std::move(prefsDlg));

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
    instrItems.push_back({"SubSynth", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::SubtractiveSynth>());
        markDirty();
    }});
    instrItems.push_back({"FM Synth", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::FMSynth>());
        markDirty();
    }});
    instrItems.push_back({"Sampler", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::Sampler>());
        markDirty();
    }});
    instrItems.push_back({"Drum Rack", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::DrumRack>());
        markDirty();
    }});
    instrItems.push_back({"DrumSlop", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::DrumSlop>());
        markDirty();
    }});
    instrItems.push_back({"Karplus-Strong", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::KarplusStrong>());
        markDirty();
    }});
    instrItems.push_back({"Wavetable Synth", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::WavetableSynth>());
        markDirty();
    }});
    instrItems.push_back({"Granular Synth", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::GranularSynth>());
        markDirty();
    }});
    instrItems.push_back({"Instrument Rack", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{trackIndex, 1});
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::InstrumentRack>());
        markDirty();
    }});
    items.push_back({"Add Instrument", nullptr, true, true, std::move(instrItems)});

    // Audio effects submenu
    std::vector<ui::ContextMenu::Item> fxItems;
    fxItems.push_back({"Reverb", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Reverb>());
        markDirty();
    }});
    fxItems.push_back({"Delay", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Delay>());
        markDirty();
    }});
    fxItems.push_back({"EQ", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::EQ>());
        markDirty();
    }});
    fxItems.push_back({"Compressor", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Compressor>());
        markDirty();
    }});
    fxItems.push_back({"Filter", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Filter>());
        markDirty();
    }});
    fxItems.push_back({"Chorus", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Chorus>());
        markDirty();
    }});
    fxItems.push_back({"Distortion", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Distortion>());
        markDirty();
    }});
    fxItems.push_back({"Tape Emulation", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::TapeEmulation>());
        markDirty();
    }});
    fxItems.push_back({"Amp Simulator", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::AmpSimulator>());
        markDirty();
    }});
    fxItems.push_back({"Oscilloscope", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Oscilloscope>());
        markDirty();
    }});
    fxItems.push_back({"Spectrum", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::SpectrumAnalyzer>());
        markDirty();
    }});
    items.push_back({"Add Audio Effect", nullptr, false, true, std::move(fxItems)});

    // MIDI effects submenu
    std::vector<ui::ContextMenu::Item> midiItems;
    midiItems.push_back({"Arpeggiator", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::Arpeggiator>());
        markDirty();
    }});
    midiItems.push_back({"Chord", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::Chord>());
        markDirty();
    }});
    midiItems.push_back({"Scale", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::Scale>());
        markDirty();
    }});
    midiItems.push_back({"Note Length", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::NoteLength>());
        markDirty();
    }});
    midiItems.push_back({"Velocity", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::VelocityEffect>());
        markDirty();
    }});
    midiItems.push_back({"Random", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::MidiRandom>());
        markDirty();
    }});
    midiItems.push_back({"Pitch", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::MidiPitch>());
        markDirty();
    }});
    items.push_back({"Add MIDI Effect", nullptr, false, true, std::move(midiItems)});

    // Record quantize submenu
    auto curRQ = track.recordQuantize;
    std::vector<ui::ContextMenu::Item> rqItems;
    rqItems.push_back({"None", [this, trackIndex]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::None;
    }, false, curRQ != audio::QuantizeMode::None});
    rqItems.push_back({"Beat", [this, trackIndex]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBeat;
    }, false, curRQ != audio::QuantizeMode::NextBeat});
    rqItems.push_back({"Bar", [this, trackIndex]() {
        m_project.track(trackIndex).recordQuantize = audio::QuantizeMode::NextBar;
    }, false, curRQ != audio::QuantizeMode::NextBar});
    items.push_back({"Record Quantize", nullptr, false, true, std::move(rqItems)});

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
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Audio;
            m_clipboard.audioClip = s->audioClip->clone();
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            s->audioClip.reset();
        } else if (s && s->midiClip) {
            m_clipboard.clear();
            m_clipboard.type = ClipboardData::Type::Midi;
            m_clipboard.midiClip = s->midiClip->clone();
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            s->midiClip.reset();
        }
        markDirty();
    }, false, hasClip});

    items.push_back({"Paste", [this, trackIndex, sceneIndex]() {
        auto* s = m_project.getSlot(trackIndex, sceneIndex);
        if (!s) return;
        if (m_clipboard.type == ClipboardData::Type::Audio && m_clipboard.audioClip) {
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            s->clear();
            s->audioClip = m_clipboard.audioClip->clone();
        } else if (m_clipboard.type == ClipboardData::Type::Midi && m_clipboard.midiClip) {
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            s->clear();
            s->midiClip = m_clipboard.midiClip->clone();
        }
        markDirty();
    }, false, hasClipboard});

    items.push_back({"Duplicate", [this, trackIndex, sceneIndex]() {
        auto* src = m_project.getSlot(trackIndex, sceneIndex);
        if (!src || src->empty()) return;
        for (int s = sceneIndex + 1; s < m_project.numScenes(); ++s) {
            auto* dst = m_project.getSlot(trackIndex, s);
            if (dst && dst->empty()) {
                if (src->audioClip) dst->audioClip = src->audioClip->clone();
                else if (src->midiClip) dst->midiClip = src->midiClip->clone();
                m_selectedScene = s;
                m_sessionPanel->setSelectedScene(s);
                markDirty();
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
            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{trackIndex});
            s->clear();
            markDirty();
        }
    }, false, hasClip});

    items.push_back({"Rename", [this, trackIndex, sceneIndex]() {
        // TODO: open rename dialog
        (void)trackIndex; (void)sceneIndex;
    }, false, hasClip});

    m_contextMenu.open(mx, my, std::move(items));
}

void App::updateDetailForSelectedTrack() {
    if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) {
        m_detailPanel->clear();
        return;
    }

    // Check if the selected slot has an audio clip — show clip detail view
    auto* audioClip = m_project.getClip(m_selectedTrack, m_selectedScene);
    if (audioClip) {
        auto* fxChain = &m_audioEngine.mixer().trackEffects(m_selectedTrack);
        m_detailPanel->setAudioClip(audioClip, fxChain,
                                     static_cast<int>(m_audioEngine.sampleRate()));
        return;
    }

    auto* midiChain = &m_audioEngine.midiEffectChain(m_selectedTrack);
    auto* inst = m_audioEngine.instrument(m_selectedTrack);
    auto* fxChain = &m_audioEngine.mixer().trackEffects(m_selectedTrack);

    m_detailPanel->setDeviceChain(midiChain, inst, fxChain);
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

    loadFont();

    m_project.init(8, 8);
    m_virtualKeyboard.init(&m_audioEngine);
    setupMenuBar();
    buildWidgetTree();
    m_pianoRoll->setTransport(&m_audioEngine.transport());
    m_sessionPanel->init(&m_project, &m_audioEngine);
    m_mixerPanel->init(&m_project, &m_audioEngine, &m_midiEngine);
    m_transportPanel->init(&m_project, &m_audioEngine);
    m_returnMasterPanel->init(&m_project, &m_audioEngine);

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

    // Apply metronome settings from saved preferences
    m_audioEngine.sendCommand(audio::MetronomeSetVolumeMsg{m_settings.metronomeVolume});
    m_audioEngine.sendCommand(audio::MetronomeSetModeMsg{m_settings.metronomeMode});
    m_audioEngine.sendCommand(audio::TransportSetCountInMsg{m_settings.countInBars});
    m_transportPanel->setCountInBars(m_settings.countInBars);

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
        if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
        switch (type) {
            case ui::fw::DetailPanelWidget::DeviceType::MidiFx:
                m_audioEngine.midiEffectChain(m_selectedTrack).removeEffect(chainIndex);
                // Send all-notes-off (CC 123) to clear any stuck notes
                m_audioEngine.sendCommand(audio::SendMidiToTrackMsg{
                    m_selectedTrack,
                    (uint8_t)midi::MidiMessage::Type::ControlChange,
                    0, 0, 0, 0, 123});
                LOG_INFO("MIDI", "Removed MIDI effect %d from track %d", chainIndex, m_selectedTrack + 1);
                break;
            case ui::fw::DetailPanelWidget::DeviceType::AudioFx:
                m_audioEngine.mixer().trackEffects(m_selectedTrack).remove(chainIndex);
                LOG_INFO("Audio", "Removed audio effect %d from track %d", chainIndex, m_selectedTrack + 1);
                break;
            default:
                break;
        }
        m_detailPanel->clear();  // Force rebuild on next frame
        markDirty();
    });

    m_detailPanel->setOnMoveDevice([this](ui::fw::DetailPanelWidget::DeviceType type, int fromIdx, int toIdx) {
        if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
        switch (type) {
            case ui::fw::DetailPanelWidget::DeviceType::MidiFx:
                m_audioEngine.midiEffectChain(m_selectedTrack).moveEffect(fromIdx, toIdx);
                LOG_INFO("MIDI", "Moved MIDI effect %d to %d on track %d", fromIdx, toIdx, m_selectedTrack + 1);
                break;
            case ui::fw::DetailPanelWidget::DeviceType::AudioFx:
                m_audioEngine.mixer().trackEffects(m_selectedTrack).moveEffect(fromIdx, toIdx);
                LOG_INFO("Audio", "Moved audio effect %d to %d on track %d", fromIdx, toIdx, m_selectedTrack + 1);
                break;
            default:
                break;
        }
        m_detailPanel->clear();  // Force rebuild on next frame
        markDirty();
    });

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
    m_audioEngine.shutdown();
    // Destroy cached cursors
    if (m_cursorDefault)  SDL_DestroyCursor(m_cursorDefault);
    if (m_cursorEWResize) SDL_DestroyCursor(m_cursorEWResize);
    if (m_cursorNSResize) SDL_DestroyCursor(m_cursorNSResize);
    if (m_cursorMove)     SDL_DestroyCursor(m_cursorMove);
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

    m_audioEngine.sendCommand(audio::LaunchClipMsg{trackIndex, sceneIndex, clipPtr,
        m_project.getSlot(trackIndex, sceneIndex) ? m_project.getSlot(trackIndex, sceneIndex)->launchQuantize : audio::QuantizeMode::NextBar});
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
                            auto* slot = m_project.getSlot(m_selectedTrack, m_selectedScene);
                            if (slot && slot->audioClip) {
                                m_clipboard.clear();
                                m_clipboard.type = ClipboardData::Type::Audio;
                                m_clipboard.audioClip = slot->audioClip->clone();
                                m_audioEngine.sendCommand(audio::StopClipMsg{m_selectedTrack});
                                slot->audioClip.reset();
                                markDirty();
                            } else if (slot && slot->midiClip) {
                                m_clipboard.clear();
                                m_clipboard.type = ClipboardData::Type::Midi;
                                m_clipboard.midiClip = slot->midiClip->clone();
                                m_audioEngine.sendCommand(audio::StopMidiClipMsg{m_selectedTrack});
                                slot->midiClip.reset();
                                markDirty();
                            }
                            break;
                        }
                        case SDLK_V: { // Paste clip
                            if (m_clipboard.type == ClipboardData::Type::Audio && m_clipboard.audioClip) {
                                auto* slot = m_project.getSlot(m_selectedTrack, m_selectedScene);
                                if (slot) {
                                    m_audioEngine.sendCommand(audio::StopClipMsg{m_selectedTrack});
                                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{m_selectedTrack});
                                    slot->clear();
                                    slot->audioClip = m_clipboard.audioClip->clone();
                                    markDirty();
                                }
                            } else if (m_clipboard.type == ClipboardData::Type::Midi && m_clipboard.midiClip) {
                                auto* slot = m_project.getSlot(m_selectedTrack, m_selectedScene);
                                if (slot) {
                                    m_audioEngine.sendCommand(audio::StopClipMsg{m_selectedTrack});
                                    m_audioEngine.sendCommand(audio::StopMidiClipMsg{m_selectedTrack});
                                    slot->clear();
                                    slot->midiClip = m_clipboard.midiClip->clone();
                                    markDirty();
                                }
                            }
                            break;
                        }
                        case SDLK_D: { // Duplicate clip to next empty slot below
                            auto* srcSlot = m_project.getSlot(m_selectedTrack, m_selectedScene);
                            if (srcSlot && !srcSlot->empty()) {
                                for (int s = m_selectedScene + 1; s < m_project.numScenes(); ++s) {
                                    auto* dst = m_project.getSlot(m_selectedTrack, s);
                                    if (dst && dst->empty()) {
                                        if (srcSlot->audioClip)
                                            dst->audioClip = srcSlot->audioClip->clone();
                                        else if (srcSlot->midiClip)
                                            dst->midiClip = srcSlot->midiClip->clone();
                                        m_selectedScene = s;
                                        m_sessionPanel->setSelectedScene(m_selectedScene);
                                        markDirty();
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

                // Detail panel knob text-edit mode
                if (m_showDetailPanel && m_detailPanel->hasEditingKnob()) {
                    if (m_detailPanel->forwardKeyDown(static_cast<int>(event.key.key))) {
                        if (!m_detailPanel->hasEditingKnob())
                            SDL_StopTextInput(m_mainWindow.getHandle());
                        break;
                    }
                }

                // Piano roll keyboard shortcuts
                if (m_pianoRoll->isOpen()) {
                    if (m_pianoRoll->handleKeyDown(static_cast<int>(event.key.key))) {
                        if (!m_pianoRoll->isOpen()) {
                            // Piano roll was closed (Escape)
                        }
                        break;
                    }
                }

                // Virtual keyboard (intercepts musical keys before shortcuts)
                if (m_virtualKeyboard.onKeyDown(event.key.key))
                    break;

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

                    case SDLK_M:
                        if (!shift) m_showMixer = !m_showMixer;
                        break;

                    case SDLK_D:
                        if (!shift) {
                            m_showDetailPanel = !m_showDetailPanel;
                            if (m_showDetailPanel) m_detailPanel->setOpen(true);
                        }
                        break;

                    case SDLK_DELETE:
                    case SDLK_BACKSPACE: {
                        auto* slot = m_project.getSlot(m_selectedTrack, m_selectedScene);
                        if (slot && !slot->empty()) {
                            m_audioEngine.sendCommand(audio::StopClipMsg{m_selectedTrack});
                            m_audioEngine.sendCommand(audio::StopMidiClipMsg{m_selectedTrack});
                            slot->clear();
                            markDirty();
                        }
                        break;
                    }

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

                // Menu bar hover
                m_menuBar.handleMouseMove(mx, my);

                // InputState hover + drag (computes dx/dy internally)
                m_inputState.onMouseMove(mx, my);

                // Forward drag via widget tree mouse capture
                if (ui::fw::Widget::capturedWidget()) {
                    ui::fw::MouseMoveEvent me;
                    me.x = mx; me.y = my;
                    m_rootLayout->dispatchMouseMove(me);
                }

                // ContentGrid hover detection for divider cursor
                {
                    ui::fw::MouseMoveEvent me;
                    me.x = mx; me.y = my;
                    m_contentGrid->onMouseMove(me);
                }

                // Update cursor shape based on content grid divider hover
                if (m_contentGrid->wantsHorizontalResize() && m_contentGrid->wantsVerticalResize()) {
                    SDL_SetCursor(m_cursorMove);
                } else if (m_contentGrid->wantsHorizontalResize()) {
                    SDL_SetCursor(m_cursorEWResize);
                } else if (m_contentGrid->wantsVerticalResize()) {
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

                // Right-click on track headers → open context menu
                if (rightClick) {
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
                            m_project.setMidiClip(dblTrack, dblScene, std::move(newClip));
                            m_pianoRoll->setClip(clipPtr, dblTrack);
                            m_pianoRoll->setOpen(true);
                            m_selectedTrack = dblTrack;
                            markDirty();
                            break;
                        }
                    }
                }
                // Transport panel single-click (tap tempo button, dismiss editing)
                {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = ui::fw::MouseButton::Left;
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
                    m_contentGrid->dispatchMouseDown(me);
                    int selTrack = m_sessionPanel->lastClickTrack();
                    if (selTrack >= 0) {
                        m_selectedTrack = selTrack;
                        m_virtualKeyboard.setTargetTrack(selTrack);
                        m_mixerPanel->setSelectedTrack(selTrack);
                    }
                    int selScene = m_sessionPanel->lastClickScene();
                    if (selScene >= 0) {
                        m_selectedScene = selScene;
                    }
                    // Right-click on clip slot → show clip context menu
                    int rcTrack = m_sessionPanel->lastRightClickTrack();
                    int rcScene = m_sessionPanel->lastRightClickScene();
                    if (rcTrack >= 0 && rcScene >= 0) {
                        m_selectedTrack = rcTrack;
                        m_selectedScene = rcScene;
                        showClipContextMenu(rcTrack, rcScene, mx, my);
                    }
                }
                if (wasEditing && !m_transportPanel->isEditing())
                    SDL_StopTextInput(m_mainWindow.getHandle());
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                float mx = event.button.x;
                float my = event.button.y;
                int btn = event.button.button;
                m_inputState.onMouseUp(mx, my, btn);
                // Release mouse capture (mixer fader drag etc.)
                if (ui::fw::Widget::capturedWidget()) {
                    ui::fw::MouseEvent me;
                    me.x = mx; me.y = my;
                    me.button = (btn == SDL_BUTTON_RIGHT) ? ui::fw::MouseButton::Right : ui::fw::MouseButton::Left;
                    m_rootLayout->dispatchMouseUp(me);
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
                    se.dx = dx; se.dy = dy;
                    m_mixerPanel->onScroll(se);
                } else if (m_lastMouseY >= sb.y && m_lastMouseY < sb.y + sb.h) {
                    m_sessionPanel->handleScroll(dx, dy);
                }
                break;
            }

            case SDL_EVENT_DROP_FILE: {
                const char* file = event.drop.data;
                if (file) {
                    float dropX = event.drop.x;
                    float dropY = event.drop.y;

                    // Check if drop is over the detail panel (for Sampler/DrumSlop)
                    auto db = m_detailPanel->bounds();
                    if (m_detailPanel->isOpen() && dropY >= db.y && dropY < db.y + db.h
                        && dropX >= db.x && dropX < db.x + db.w) {
                        if (!loadSampleToSampler(file, m_selectedTrack))
                            loadLoopToDrumSlop(file, m_selectedTrack);
                        break;
                    }

                    // Try to determine target track/scene from drop position
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

                    // If the target track has a Sampler or DrumSlop, load sample/loop
                    if (loadSampleToSampler(file, targetTrack)) {
                        // Sample loaded into Sampler — done
                    } else if (loadLoopToDrumSlop(file, targetTrack)) {
                        // Loop loaded into DrumSlop — done
                    } else {
                        loadClipToSlot(file, targetTrack, targetScene);
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
                m_transportPanel->setRecordState(msg.recording, msg.countingIn, msg.countInProgress);
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
                            m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{ti, si, clipPtr, lq});
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
                            m_audioEngine.sendCommand(audio::LaunchClipMsg{ti, si, clipPtr, lq});
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
        }, evt);
    }

    m_transportPanel->setTransportState(m_displayPlaying, m_displayBeats,
                                     m_audioEngine.transport().bpm(),
                                     m_audioEngine.transport().numerator(),
                                     m_audioEngine.transport().denominator());
    m_transportPanel->setSelectedScene(m_selectedScene);

    // Keep detail panel synced with selected track
    if (m_showDetailPanel) {
        updateDetailForSelectedTrack();
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
        if (m_aboutDialog->isVisible()) {
            m_aboutDialog->layout(screenBounds, m_uiContext);
            m_aboutDialog->paint(m_uiContext);
        }
        if (m_preferencesDialog->isOpen()) {
            m_preferencesDialog->layout(screenBounds, m_uiContext);
            m_preferencesDialog->paint(m_uiContext);
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
        m_selectedTrack = 0;
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

    if (ProjectSerializer::saveToFolder(projectDir, m_project, m_audioEngine)) {
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
    if (ProjectSerializer::loadFromFolder(projectDir, loadedProject, m_audioEngine)) {
        m_project = std::move(loadedProject);
        syncTracksToEngine();
        m_projectPath = projectDir;
        m_projectDirty = false;
        m_selectedTrack = 0;
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

} // namespace yawn
