#include "app/App.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>

namespace yawn {

App::~App() {
    shutdown();
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

    ui::WindowConfig config;
    config.title = "Y.A.W.N — Yet Another Audio Workstation New";
    config.width = 1280;
    config.height = 800;
    config.resizable = true;

    if (!m_mainWindow.create(config)) {
        return false;
    }

    // Initialize audio engine
    audio::AudioEngineConfig audioConfig;
    audioConfig.sampleRate = 44100.0;
    audioConfig.framesPerBuffer = 256;
    audioConfig.outputChannels = 2;

    if (!m_audioEngine.init(audioConfig)) {
        std::fprintf(stderr, "Warning: Audio engine failed to initialize\n");
    } else if (!m_audioEngine.start()) {
        std::fprintf(stderr, "Warning: Audio engine failed to start\n");
    }

    m_running = true;
    std::printf("\nY.A.W.N initialized successfully\n");
    std::printf("  [Space] Play/Stop   [T] Toggle test tone (440Hz)\n");
    std::printf("  [Up/Down] BPM +/-   [Home] Reset position\n");
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
    m_mainWindow.destroy();
    SDL_Quit();
    std::printf("Y.A.W.N shutdown complete\n");
}

void App::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) break;

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
                        // Toggle 440Hz test tone
                        {
                            static bool toneOn = false;
                            toneOn = !toneOn;
                            m_audioEngine.sendCommand(audio::TestToneMsg{toneOn, 440.0f});
                            std::printf("Test tone: %s\n", toneOn ? "ON" : "OFF");
                        }
                        break;

                    case SDLK_UP:
                        {
                            double newBpm = m_audioEngine.transport().bpm() + 1.0;
                            if (newBpm > 300.0) newBpm = 300.0;
                            m_audioEngine.sendCommand(audio::TransportSetBPMMsg{newBpm});
                        }
                        break;

                    case SDLK_DOWN:
                        {
                            double newBpm = m_audioEngine.transport().bpm() - 1.0;
                            if (newBpm < 20.0) newBpm = 20.0;
                            m_audioEngine.sendCommand(audio::TransportSetBPMMsg{newBpm});
                        }
                        break;

                    case SDLK_HOME:
                        m_audioEngine.sendCommand(audio::TransportSetPositionMsg{0});
                        break;

                    default:
                        break;
                }
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(m_mainWindow.getHandle())) {
                    m_running = false;
                }
                break;

            default:
                break;
        }
    }
}

void App::update() {
    // Drain audio events (position updates ~30Hz)
    audio::AudioEvent evt;
    while (m_audioEngine.pollEvent(evt)) {
        std::visit([this](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, audio::TransportPositionUpdate>) {
                m_displayPosition = msg.positionInSamples;
                m_displayBeats = msg.positionInBeats;
                m_displayPlaying = msg.isPlaying;
            }
        }, evt);
    }
}

void App::render() {
    m_mainWindow.makeCurrent();

    int w = m_mainWindow.getWidth();
    int h = m_mainWindow.getHeight();
    glViewport(0, 0, w, h);

    glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderTransportInfo();

    m_mainWindow.swap();
}

void App::renderTransportInfo() {
    // For now, output to window title until we have proper text rendering
    double bpm = m_audioEngine.transport().bpm();
    int bar = static_cast<int>(m_displayBeats / 4.0) + 1;
    int beat = static_cast<int>(std::fmod(m_displayBeats, 4.0)) + 1;

    char title[256];
    std::snprintf(title, sizeof(title),
        "Y.A.W.N  |  %s  |  %3.0f BPM  |  %d.%d",
        m_displayPlaying ? "PLAYING" : "STOPPED",
        bpm, bar, beat);

    SDL_SetWindowTitle(m_mainWindow.getHandle(), title);
}

} // namespace yawn
