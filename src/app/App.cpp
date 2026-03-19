#include "app/App.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>

namespace yawn {

App::~App() {
    shutdown();
}

bool App::loadFont() {
    const char* fontPaths[] = {
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
        {"Detail Panel",    "D",   [this]() { m_showDetailPanel = !m_showDetailPanel; }},
    });

    // Track menu
    m_menuBar.addMenu("Track", {
        {"Add Audio Track",  "",  [this]() { std::printf("Add Audio Track\n"); }},
        {"Add MIDI Track",   "",  [this]() { std::printf("Add MIDI Track\n"); }},
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
        {"About Y.A.W.N",     "",  [this]() { std::printf("Y.A.W.N v0.1.0\n"); }},
        {"Keyboard Shortcuts", "",  nullptr},
    });
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
    std::printf("\nY.A.W.N initialized successfully\n");
    std::printf("  [Space] Play/Stop        [T] Toggle test tone\n");
    std::printf("  [Up/Down] BPM +/-        [Home] Reset position\n");
    std::printf("  [Q] Quantize: None/Beat/Bar\n");
    std::printf("  [M] Toggle mixer panel\n");
    std::printf("  [D] Toggle detail panel\n");
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

                // Keyboard shortcuts for menus
                if (ctrl) {
                    switch (event.key.key) {
                        case SDLK_Q: m_running = false; break;
                        default: break;
                    }
                }

                // InputState keyboard forwarding (for focused widgets)
                if (m_inputState.focused()) {
                    if (m_inputState.onKeyDown(static_cast<int>(event.key.key), ctrl, shift))
                        break;
                }

                switch (event.key.key) {
                    case SDLK_ESCAPE:
                        if (m_menuBar.isOpen()) {
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

                    case SDLK_T:
                        if (!shift) {
                            static bool toneOn = false;
                            toneOn = !toneOn;
                            m_audioEngine.sendCommand(audio::TestToneMsg{toneOn, 440.0f});
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

                    case SDLK_Q: {
                        if (!ctrl) {
                            static int qMode = 2;
                            qMode = (qMode + 1) % 3;
                            m_audioEngine.sendCommand(
                                audio::SetQuantizeMsg{static_cast<audio::QuantizeMode>(qMode)});
                            const char* names[] = {"None", "Beat", "Bar"};
                            std::printf("Quantize: %s\n", names[qMode]);
                        }
                        break;
                    }

                    case SDLK_M:
                        if (!shift) m_showMixer = !m_showMixer;
                        break;

                    case SDLK_D:
                        if (!shift && !ctrl) m_showDetailPanel = !m_showDetailPanel;
                        break;

                    default:
                        break;
                }
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

                // Detail panel (at the very bottom)
                bool rightClick = (btn == SDL_BUTTON_RIGHT);
                if (m_showDetailPanel) {
                    int wh = m_mainWindow.getHeight();
                    float detailH = m_detailPanel.height();
                    float detailY = static_cast<float>(wh) - detailH;

                    if (rightClick) {
                        if (m_detailPanel.handleRightClick(mx, my, 0, detailY, static_cast<float>(m_mainWindow.getWidth())))
                            break;
                    } else {
                        if (m_detailPanel.handleClick(mx, my, 0, detailY, static_cast<float>(m_mainWindow.getWidth())))
                            break;
                    }
                }

                // Existing view handlers: mixer → session
                if (!m_showMixer || !m_mixerView.handleClick(mx, my, rightClick)) {
                    m_sessionView.handleClick(mx, my, rightClick);
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
                // Forward scroll to session view when mouse is over it
                float menuH = m_menuBar.height();
                float mixerH = m_showMixer ? m_mixerView.preferredHeight() : 0.0f;
                float detailH = m_showDetailPanel ? m_detailPanel.height() : 0.0f;
                float sessionH = std::max(100.0f, static_cast<float>(m_mainWindow.getHeight()) - menuH - mixerH - detailH);
                if (m_lastMouseY >= menuH && m_lastMouseY < menuH + sessionH) {
                    m_sessionView.handleScroll(dx, dy);
                }
                break;
            }

            case SDL_EVENT_DROP_FILE: {
                const char* file = event.drop.data;
                if (file) {
                    loadClipToSlot(file, m_nextDropTrack, m_nextDropScene);
                    m_nextDropTrack++;
                    if (m_nextDropTrack >= m_project.numTracks()) {
                        m_nextDropTrack = 0;
                        m_nextDropScene++;
                        if (m_nextDropScene >= m_project.numScenes()) {
                            m_nextDropScene = 0;
                        }
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
    float sessionH = std::max(100.0f, static_cast<float>(h) - menuH - bottomPanels);

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

    m_renderer.endFrame();

    m_mainWindow.swap();
}

} // namespace yawn
