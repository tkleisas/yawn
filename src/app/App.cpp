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
    // Try common system font paths
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

    // Init 2D renderer
    if (!m_renderer.init()) {
        std::fprintf(stderr, "Failed to initialize 2D renderer\n");
        return false;
    }

    // Load font
    loadFont();

    // Init project
    m_project.init(8, 8);

    // Init session view
    m_sessionView.init(&m_project, &m_audioEngine);

    // Init mixer view
    m_mixerView.init(&m_project, &m_audioEngine);

    // Init audio engine
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

    // Extract just the filename for display
    std::string name = path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos) name = name.substr(pos + 1);
    // Remove extension
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

    // Auto-launch
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

                switch (event.key.key) {
                    case SDLK_ESCAPE:
                        m_running = false;
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
                        static int qMode = 2;
                        qMode = (qMode + 1) % 3;
                        m_audioEngine.sendCommand(
                            audio::SetQuantizeMsg{static_cast<audio::QuantizeMode>(qMode)});
                        const char* names[] = {"None", "Beat", "Bar"};
                        std::printf("Quantize: %s\n", names[qMode]);
                        break;
                    }

                    case SDLK_M:
                        if (!shift) {
                            m_showMixer = !m_showMixer;
                        }
                        break;

                    default:
                        break;
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                float mx = event.button.x;
                float my = event.button.y;
                bool rightClick = (event.button.button == SDL_BUTTON_RIGHT);
                // Try mixer first (it's at the bottom), then session view
                if (!m_showMixer || !m_mixerView.handleClick(mx, my, rightClick)) {
                    m_sessionView.handleClick(mx, my, rightClick);
                }
                break;
            }

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(m_mainWindow.getHandle())) {
                    m_running = false;
                }
                break;

            case SDL_EVENT_DROP_FILE: {
                const char* file = event.drop.data;
                if (file) {
                    loadClipToSlot(file, m_nextDropTrack, m_nextDropScene);
                    // Advance to next slot
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

    float mixerH = m_showMixer ? m_mixerView.preferredHeight() : 0.0f;
    float sessionH = static_cast<float>(h) - mixerH;

    m_sessionView.render(m_renderer, m_font, 0, 0,
                          static_cast<float>(w), sessionH);

    if (m_showMixer) {
        m_mixerView.render(m_renderer, m_font, 0, sessionH,
                            static_cast<float>(w), mixerH);
    }

    m_renderer.endFrame();

    m_mainWindow.swap();
}

} // namespace yawn
