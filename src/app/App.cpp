#include "app/App.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/DrumRack.h"
#include "instruments/InstrumentRack.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Distortion.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include "Version.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>

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

    std::fprintf(stderr, "Warning: Could not find a system font. Text will not render.\n");
    return false;
}

void App::setupMenuBar() {
    // File menu
    m_menuBar.addMenu("File", {
        {"New Project",   "Ctrl+N",       [this]() { std::printf("New Project\n"); }},
        {"Open Project",  "Ctrl+O",       [this]() { std::printf("Open Project\n"); }},
        {"Save Project",  "Ctrl+S",       [this]() { std::printf("Save Project\n"); }},
        {"Save As...",    "Ctrl+Shift+S", nullptr},
        {"Export Audio",  "",             nullptr, true},
        {"Quit",          "Ctrl+Q",       [this]() { m_running = false; }, true},
    });

    // Edit menu
    m_menuBar.addMenu("Edit", {
        {"Undo",        "Ctrl+Z", nullptr},
        {"Redo",        "Ctrl+Y", nullptr},
        {"Preferences", "",       nullptr, true},
    });

    // View menu
    m_menuBar.addMenu("View", {
        {"Session View",    "",    nullptr},
        {"Arrangement View","",    nullptr, false, false},
        {"Toggle Mixer",    "M",   [this]() { m_showMixer = !m_showMixer; }},
        {"Detail Panel",    "D",   [this]() {
            m_showDetailPanel = !m_showDetailPanel;
            if (m_showDetailPanel) m_detailPanel.setOpen(true);
        }},
    });

    // Track menu
    m_menuBar.addMenu("Track", {
        {"Add Audio Track",  "",  [this]() {
            m_project.addTrack("Audio " + std::to_string(m_project.numTracks() + 1), Track::Type::Audio);
            std::printf("Added Audio track %d\n", m_project.numTracks());
        }},
        {"Add MIDI Track",   "",  [this]() {
            int idx = m_project.numTracks();
            m_project.addTrack("MIDI " + std::to_string(idx + 1), Track::Type::Midi);
            // Auto-assign a SubSynth to new MIDI tracks
            m_audioEngine.setInstrument(idx, std::make_unique<instruments::SubtractiveSynth>());
            std::printf("Added MIDI track %d (with SubSynth)\n", m_project.numTracks());
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
        {"About Y.A.W.N",     "",  [this]() { m_showAbout = true; }},
        {"Keyboard Shortcuts", "",  nullptr},
    });
}

void App::showTrackContextMenu(int trackIndex, float mx, float my) {
    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
    auto& track = m_project.track(trackIndex);

    std::vector<ui::ContextMenu::Item> items;

    // Track type selection (with confirmation dialog)
    items.push_back({"Set as Audio Track", [this, trackIndex]() {
        auto& trk = m_project.track(trackIndex);
        if (trk.type == Track::Type::Audio) return;
        m_confirmDialog.show(
            "Change track type? All devices will be removed.",
            [this, trackIndex]() {
                m_audioEngine.midiEffectChain(trackIndex).clear();
                m_audioEngine.mixer().trackEffects(trackIndex).clear();
                m_audioEngine.setInstrument(trackIndex, nullptr);
                m_project.track(trackIndex).type = Track::Type::Audio;
                m_detailPanel.clear();
                std::printf("Track %d changed to Audio\n", trackIndex + 1);
            });
    }, false, track.type != Track::Type::Audio});

    items.push_back({"Set as MIDI Track", [this, trackIndex]() {
        auto& trk = m_project.track(trackIndex);
        if (trk.type == Track::Type::Midi) return;
        m_confirmDialog.show(
            "Change track type? All devices will be removed.",
            [this, trackIndex]() {
                m_audioEngine.midiEffectChain(trackIndex).clear();
                m_audioEngine.mixer().trackEffects(trackIndex).clear();
                m_project.track(trackIndex).type = Track::Type::Midi;
                m_audioEngine.setInstrument(trackIndex,
                    std::make_unique<instruments::SubtractiveSynth>());
                m_detailPanel.clear();
                std::printf("Track %d changed to MIDI (SubSynth assigned)\n", trackIndex + 1);
            });
    }, false, track.type != Track::Type::Midi});

    // Separator + Instruments submenu
    std::vector<ui::ContextMenu::Item> instrItems;
    instrItems.push_back({"SubSynth", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::SubtractiveSynth>());
        std::printf("Track %d: SubSynth assigned\n", trackIndex + 1);
    }});
    instrItems.push_back({"FM Synth", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::FMSynth>());
        std::printf("Track %d: FM Synth assigned\n", trackIndex + 1);
    }});
    instrItems.push_back({"Sampler", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::Sampler>());
        std::printf("Track %d: Sampler assigned\n", trackIndex + 1);
    }});
    instrItems.push_back({"Drum Rack", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::DrumRack>());
        std::printf("Track %d: Drum Rack assigned\n", trackIndex + 1);
    }});
    instrItems.push_back({"Instrument Rack", [this, trackIndex]() {
        m_project.track(trackIndex).type = Track::Type::Midi;
        m_audioEngine.setInstrument(trackIndex, std::make_unique<instruments::InstrumentRack>());
        std::printf("Track %d: Instrument Rack assigned\n", trackIndex + 1);
    }});
    items.push_back({"Add Instrument", nullptr, true, true, std::move(instrItems)});

    // Audio effects submenu
    std::vector<ui::ContextMenu::Item> fxItems;
    fxItems.push_back({"Reverb", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Reverb>());
        std::printf("Track %d: Reverb added\n", trackIndex + 1);
    }});
    fxItems.push_back({"Delay", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Delay>());
        std::printf("Track %d: Delay added\n", trackIndex + 1);
    }});
    fxItems.push_back({"EQ", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::EQ>());
        std::printf("Track %d: EQ added\n", trackIndex + 1);
    }});
    fxItems.push_back({"Compressor", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Compressor>());
        std::printf("Track %d: Compressor added\n", trackIndex + 1);
    }});
    fxItems.push_back({"Filter", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Filter>());
        std::printf("Track %d: Filter added\n", trackIndex + 1);
    }});
    fxItems.push_back({"Chorus", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Chorus>());
        std::printf("Track %d: Chorus added\n", trackIndex + 1);
    }});
    fxItems.push_back({"Distortion", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Distortion>());
        std::printf("Track %d: Distortion added\n", trackIndex + 1);
    }});
    fxItems.push_back({"Oscilloscope", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::Oscilloscope>());
        std::printf("Track %d: Oscilloscope added\n", trackIndex + 1);
    }});
    fxItems.push_back({"Spectrum", [this, trackIndex]() {
        m_audioEngine.mixer().trackEffects(trackIndex).append(std::make_unique<effects::SpectrumAnalyzer>());
        std::printf("Track %d: Spectrum Analyzer added\n", trackIndex + 1);
    }});
    items.push_back({"Add Audio Effect", nullptr, false, true, std::move(fxItems)});

    // MIDI effects submenu
    std::vector<ui::ContextMenu::Item> midiItems;
    midiItems.push_back({"Arpeggiator", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::Arpeggiator>());
        std::printf("Track %d: Arpeggiator added\n", trackIndex + 1);
    }});
    midiItems.push_back({"Chord", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::Chord>());
        std::printf("Track %d: Chord added\n", trackIndex + 1);
    }});
    midiItems.push_back({"Scale", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::Scale>());
        std::printf("Track %d: Scale added\n", trackIndex + 1);
    }});
    midiItems.push_back({"Note Length", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::NoteLength>());
        std::printf("Track %d: Note Length added\n", trackIndex + 1);
    }});
    midiItems.push_back({"Velocity", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::VelocityEffect>());
        std::printf("Track %d: Velocity added\n", trackIndex + 1);
    }});
    midiItems.push_back({"Random", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::MidiRandom>());
        std::printf("Track %d: Random added\n", trackIndex + 1);
    }});
    midiItems.push_back({"Pitch", [this, trackIndex]() {
        m_audioEngine.midiEffectChain(trackIndex).addEffect(std::make_unique<midi::MidiPitch>());
        std::printf("Track %d: Pitch added\n", trackIndex + 1);
    }});
    items.push_back({"Add MIDI Effect", nullptr, false, true, std::move(midiItems)});

    m_contextMenu.open(mx, my, std::move(items));
}

void App::updateDetailForSelectedTrack() {
    if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) {
        m_detailPanel.clear();
        return;
    }

    auto* midiChain = &m_audioEngine.midiEffectChain(m_selectedTrack);
    auto* inst = m_audioEngine.instrument(m_selectedTrack);
    auto* fxChain = &m_audioEngine.mixer().trackEffects(m_selectedTrack);

    m_detailPanel.setDeviceChain(midiChain, inst, fxChain);
}

bool App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
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

    if (!m_renderer.init()) {
        std::fprintf(stderr, "Failed to initialize 2D renderer\n");
        return false;
    }

    loadFont();

    m_project.init(8, 8);
    m_sessionView.init(&m_project, &m_audioEngine);
    m_mixerView.init(&m_project, &m_audioEngine);
    m_virtualKeyboard.init(&m_audioEngine);
    setupMenuBar();

    audio::AudioEngineConfig audioConfig;
    audioConfig.sampleRate = 44100.0;
    audioConfig.framesPerBuffer = 256;
    audioConfig.outputChannels = 2;

    if (!m_audioEngine.init(audioConfig)) {
        std::fprintf(stderr, "Warning: Audio engine failed to initialize\n");
    } else if (!m_audioEngine.start()) {
        std::fprintf(stderr, "Warning: Audio engine failed to start\n");
    }

    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

    m_running = true;

    // Wire up detail panel remove device callback
    m_detailPanel.setOnRemoveDevice([this](ui::DetailPanel::DeviceType type, int chainIndex) {
        if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
        switch (type) {
            case ui::DetailPanel::DeviceType::MidiFx:
                m_audioEngine.midiEffectChain(m_selectedTrack).removeEffect(chainIndex);
                // Send all-notes-off (CC 123) to clear any stuck notes
                m_audioEngine.sendCommand(audio::SendMidiToTrackMsg{
                    m_selectedTrack,
                    (uint8_t)midi::MidiMessage::Type::ControlChange,
                    0, 0, 0, 0, 123});
                std::printf("Removed MIDI effect %d from track %d\n", chainIndex, m_selectedTrack + 1);
                break;
            case ui::DetailPanel::DeviceType::AudioFx:
                m_audioEngine.mixer().trackEffects(m_selectedTrack).remove(chainIndex);
                std::printf("Removed audio effect %d from track %d\n", chainIndex, m_selectedTrack + 1);
                break;
            default:
                break;
        }
        m_detailPanel.clear();  // Force rebuild on next frame
    });

    std::printf("\nY.A.W.N initialized successfully\n");
    std::printf("  [Space] Play/Stop        [Up/Down] BPM +/-\n");
    std::printf("  [Home] Reset position    [M] Toggle mixer\n");
    std::printf("  [D] Toggle detail panel\n");
    std::printf("  Virtual keyboard: Q2W3ER5T6Y7UI9O0P  [Z/X] Octave down/up\n");
    std::printf("  Click clip slots to launch/stop\n");
    std::printf("  Drag & drop audio files to load clips\n");
    std::printf("  [Esc] Quit\n\n");
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
    m_renderer.shutdown();
    m_font.destroy();
    m_mainWindow.destroy();
    SDL_Quit();
    std::printf("Y.A.W.N shutdown complete\n");
}

bool App::loadClipToSlot(const std::string& path, int trackIndex, int sceneIndex) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        std::printf("Resampling from %d Hz to %.0f Hz...\n",
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

    std::printf("Loaded '%s' -> Track %d, Scene %d\n",
        name.c_str(), trackIndex + 1, sceneIndex + 1);

    m_audioEngine.sendCommand(audio::LaunchClipMsg{trackIndex, clipPtr});
    return true;
}

void App::processEvents() {
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
                if (m_confirmDialog.isOpen()) {
                    if (event.key.key == SDLK_ESCAPE) m_confirmDialog.close();
                    break;
                }
                if (m_showAbout) {
                    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN)
                        m_showAbout = false;
                    break;
                }

                // Keyboard shortcuts for menus (Ctrl combos always take priority)
                if (ctrl) {
                    switch (event.key.key) {
                        case SDLK_Q: m_running = false; break;
                        default: break;
                    }
                    break;
                }

                // InputState keyboard forwarding (for focused widgets)
                if (m_inputState.focused()) {
                    if (m_inputState.onKeyDown(static_cast<int>(event.key.key), ctrl, shift))
                        break;
                }

                // Virtual keyboard (intercepts musical keys before shortcuts)
                if (m_virtualKeyboard.onKeyDown(event.key.key))
                    break;

                // Detail panel arrow key navigation
                if (m_showDetailPanel && m_detailPanel.isFocused()) {
                    if (event.key.key == SDLK_LEFT) { m_detailPanel.scrollLeft(); break; }
                    if (event.key.key == SDLK_RIGHT) { m_detailPanel.scrollRight(); break; }
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

                    case SDLK_UP: {
                        double newBpm = m_audioEngine.transport().bpm() + 1.0;
                        m_audioEngine.sendCommand(audio::TransportSetBPMMsg{std::min(newBpm, 300.0)});
                        break;
                    }
                    case SDLK_DOWN: {
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
                            if (m_showDetailPanel) m_detailPanel.setOpen(true);
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

                // Forward drag to mixer if it's dragging
                if (m_showMixer) {
                    m_mixerView.handleDrag(mx, my);
                }

                // Forward drag to detail panel if it's dragging
                if (m_showDetailPanel) {
                    m_detailPanel.handleDrag(mx, my);
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                float mx = event.button.x;
                float my = event.button.y;
                int btn = event.button.button;

                // Confirm dialog takes top priority (modal)
                if (m_confirmDialog.isOpen()) {
                    float sw = static_cast<float>(m_mainWindow.getWidth());
                    float sh = static_cast<float>(m_mainWindow.getHeight());
                    m_confirmDialog.handleClick(mx, my, sw, sh);
                    break;
                }

                // About dialog (modal)
                if (m_showAbout) {
                    float sw = static_cast<float>(m_mainWindow.getWidth());
                    float sh = static_cast<float>(m_mainWindow.getHeight());
                    float dw = 420, dh = 200;
                    float dx = (sw - dw) * 0.5f, dy = (sh - dh) * 0.5f;
                    float btnH = 36;
                    float btnHitW = 100;
                    float btnX = dx + (dw - btnHitW) * 0.5f;
                    float btnY = dy + dh - btnH - 14;
                    if (mx >= btnX && mx < btnX + btnHitW && my >= btnY && my < btnY + btnH)
                        m_showAbout = false;
                    break;  // modal — consume click
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

                // Detail panel — use same layout math as render()
                bool rightClick = (btn == SDL_BUTTON_RIGHT);
                if (m_showDetailPanel) {
                    float menuH2 = m_menuBar.height();
                    float mixerH2 = m_showMixer ? m_mixerView.preferredHeight() : 0.0f;
                    float detailH2 = m_detailPanel.height();
                    float avail2 = static_cast<float>(m_mainWindow.getHeight()) - menuH2 - mixerH2 - detailH2;
                    float sessionH2 = std::min(m_sessionView.preferredHeight(), std::max(100.0f, avail2));
                    float detailY = menuH2 + sessionH2 + mixerH2;

                    if (rightClick) {
                        if (m_detailPanel.handleRightClick(mx, my, 0, detailY, static_cast<float>(m_mainWindow.getWidth()))) {
                            m_detailPanel.setFocused(true);
                            break;
                        }
                    } else {
                        if (m_detailPanel.handleClick(mx, my, 0, detailY, static_cast<float>(m_mainWindow.getWidth()))) {
                            // Focus is set inside handleClick
                            break;
                        }
                    }
                }

                // Clicking outside detail panel clears focus
                m_detailPanel.setFocused(false);

                // Right-click on track headers → open context menu
                if (rightClick) {
                    float menuH = m_menuBar.height();
                    float headerY = menuH + ui::Theme::kTransportBarHeight;
                    float headerEnd = headerY + ui::Theme::kTrackHeaderHeight;
                    float gridX = ui::Theme::kSceneLabelWidth;
                    if (my >= headerY && my < headerEnd && mx >= gridX) {
                        float contentMX = mx + m_sessionView.scrollX();
                        int trackIdx = static_cast<int>((contentMX - gridX) / ui::Theme::kTrackWidth);
                        if (trackIdx >= 0 && trackIdx < m_project.numTracks()) {
                            m_selectedTrack = trackIdx;
                            m_virtualKeyboard.setTargetTrack(trackIdx);
                            m_sessionView.setSelectedTrack(trackIdx);
                            m_mixerView.setSelectedTrack(trackIdx);
                            showTrackContextMenu(trackIdx, mx, my);
                            break;
                        }
                    }
                }

                // Existing view handlers: mixer → session
                if (!m_showMixer || !m_mixerView.handleClick(mx, my, rightClick)) {
                    int selTrack = -1;
                    if (m_sessionView.handleClick(mx, my, rightClick, &selTrack)) {
                        if (selTrack >= 0) {
                            m_selectedTrack = selTrack;
                            m_virtualKeyboard.setTargetTrack(selTrack);
                            m_mixerView.setSelectedTrack(selTrack);
                        }
                    }
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                float mx = event.button.x;
                float my = event.button.y;
                int btn = event.button.button;
                m_inputState.onMouseUp(mx, my, btn);
                m_mixerView.handleRelease();
                m_detailPanel.handleRelease();
                break;
            }

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(m_mainWindow.getHandle())) {
                    m_running = false;
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL: {
                float dx = event.wheel.x;
                float dy = event.wheel.y;
                float menuH = m_menuBar.height();
                float mixerH = m_showMixer ? m_mixerView.preferredHeight() : 0.0f;
                float detailH = m_showDetailPanel ? m_detailPanel.height() : 0.0f;
                float avail = static_cast<float>(m_mainWindow.getHeight()) - menuH - mixerH - detailH;
                float sessionH = std::min(m_sessionView.preferredHeight(), std::max(100.0f, avail));
                float detailY = menuH + sessionH + mixerH;

                if (m_showDetailPanel && m_lastMouseY >= detailY) {
                    m_detailPanel.handleScroll(dx, dy);
                } else if (m_lastMouseY >= menuH && m_lastMouseY < menuH + sessionH) {
                    m_sessionView.handleScroll(dx, dy);
                }
                break;
            }

            case SDL_EVENT_DROP_FILE: {
                const char* file = event.drop.data;
                if (file) {
                    // Try to determine target track/scene from mouse position
                    float menuH = m_menuBar.height();
                    float headerY = menuH + ui::Theme::kTransportBarHeight + ui::Theme::kTrackHeaderHeight;
                    float gridX = ui::Theme::kSceneLabelWidth;
                    int targetTrack = m_selectedTrack;
                    int targetScene = m_nextDropScene;

                    if (m_lastMouseY >= headerY && m_lastMouseX >= gridX) {
                        float contentMX = m_lastMouseX + m_sessionView.scrollX();
                        float contentMY = m_lastMouseY + m_sessionView.scrollY();
                        int t = static_cast<int>((contentMX - gridX) / ui::Theme::kTrackWidth);
                        int s = static_cast<int>((contentMY - headerY) / ui::Theme::kClipSlotHeight);
                        if (t >= 0 && t < m_project.numTracks()) targetTrack = t;
                        if (s >= 0 && s < m_project.numScenes()) targetScene = s;
                    }

                    loadClipToSlot(file, targetTrack, targetScene);
                    // Advance scene for next drop on same track
                    m_nextDropScene = targetScene + 1;
                    if (m_nextDropScene >= m_project.numScenes()) m_nextDropScene = 0;
                }
                break;
            }

            default:
                break;
        }
    }
}

void App::update() {
    audio::AudioEvent evt;
    while (m_audioEngine.pollEvent(evt)) {
        std::visit([this](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, audio::TransportPositionUpdate>) {
                m_displayPosition = msg.positionInSamples;
                m_displayBeats = msg.positionInBeats;
                m_displayPlaying = msg.isPlaying;
            }
            else if constexpr (std::is_same_v<T, audio::ClipStateUpdate>) {
                m_sessionView.updateClipState(msg.trackIndex, msg.playing, msg.playPosition);
            }
            else if constexpr (std::is_same_v<T, audio::MeterUpdate>) {
                m_mixerView.updateMeter(msg.trackIndex, msg.peakL, msg.peakR);
            }
        }, evt);
    }

    m_sessionView.setTransportState(m_displayPlaying, m_displayBeats,
                                     m_audioEngine.transport().bpm());

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

    float menuH = m_menuBar.height();
    float mixerH = m_showMixer ? m_mixerView.preferredHeight() : 0.0f;
    float detailH = m_showDetailPanel ? m_detailPanel.height() : 0.0f;
    float bottomPanels = mixerH + detailH;
    float available = static_cast<float>(h) - menuH - bottomPanels;
    float sessionH = std::min(m_sessionView.preferredHeight(), std::max(100.0f, available));

    // Layout top-to-bottom: menu → session → mixer → detail panel
    float sessionY = menuH;
    float mixerY   = sessionY + sessionH;
    float detailY  = mixerY + mixerH;

    // Session view (transport + track headers + clip grid)
    m_sessionView.render(m_renderer, m_font, 0, sessionY,
                          static_cast<float>(w), sessionH);

    // Mixer section
    if (m_showMixer) {
        m_mixerView.render(m_renderer, m_font, 0, mixerY,
                            static_cast<float>(w), mixerH);
    }

    // Detail panel / device strip (bottom)
    if (m_showDetailPanel) {
        m_detailPanel.render(m_renderer, m_font, 0, detailY,
                              static_cast<float>(w));
    }

    // Menu bar (drawn last, on top of everything)
    m_menuBar.render(m_renderer, m_font, static_cast<float>(w));

    // Context menu (very top layer)
    m_contextMenu.render(m_renderer, m_font);

    // Confirm dialog (topmost modal overlay)
    m_confirmDialog.render(m_renderer, m_font,
                           static_cast<float>(w), static_cast<float>(h));

    // About dialog
#ifndef YAWN_TEST_BUILD
    if (m_showAbout) {
        float sw = static_cast<float>(w);
        float sh = static_cast<float>(h);
        float dw = 420, dh = 200;
        float dx = (sw - dw) * 0.5f, dy = (sh - dh) * 0.5f;

        // Dimmed overlay
        m_renderer.drawRect(0, 0, sw, sh, ui::Color{0, 0, 0, 140});
        // Dialog
        m_renderer.drawRect(dx, dy, dw, dh, ui::Color{45, 45, 52, 255});
        m_renderer.drawRectOutline(dx, dy, dw, dh, ui::Color{75, 75, 85, 255});

        float ts = 14.0f / ui::Theme::kFontSize;
        float tsSmall = 11.0f / ui::Theme::kFontSize;
        float lineH = 20.0f;
        float textX = dx + 20;
        float textY = dy + 24;

        m_font.drawText(m_renderer, "Y.A.W.N", textX, textY, 18.0f / ui::Theme::kFontSize,
                        ui::Color{100, 180, 255, 255});
        textY += lineH + 4;
        m_font.drawText(m_renderer, "Yetanother Audio Workstation New", textX, textY, tsSmall,
                        ui::Theme::textSecondary);
        textY += lineH;
        m_font.drawText(m_renderer, "Version " YAWN_VERSION_STRING, textX, textY, ts,
                        ui::Theme::textPrimary);
        textY += lineH + 4;
        m_font.drawText(m_renderer, "Made with AI-Sloptronic(TM) technology", textX, textY,
                        tsSmall, ui::Color{180, 140, 255, 255});
        textY += lineH;
        m_font.drawText(m_renderer, "PM: Tasos Kleisas  |  Chief Engineer: Claude (Anthropic)",
                        textX, textY, tsSmall, ui::Theme::textSecondary);

        // OK button
        float btnScale = 13.0f / ui::Theme::kFontSize;
        float textH = m_font.lineHeight(btnScale);
        float okW = m_font.textWidth("OK", btnScale);
        float btnPadX = 24.0f;
        float btnW = okW + btnPadX * 2;
        float btnH = 36;
        float btnX = dx + (dw - btnW) * 0.5f;
        float btnY = dy + dh - btnH - 14;
        m_renderer.drawRect(btnX, btnY, btnW, btnH, ui::Color{60, 120, 200, 255});
        float textOffY = (btnH - textH) * 0.5f;
        m_font.drawText(m_renderer, "OK", btnX + (btnW - okW) * 0.5f, btnY + textOffY,
                        btnScale, ui::Theme::textPrimary);
    }
#endif

    m_renderer.endFrame();

    m_mainWindow.swap();
}

} // namespace yawn
