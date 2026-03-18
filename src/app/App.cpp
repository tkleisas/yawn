#include "app/App.h"
#include "util/FileIO.h"
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
    std::printf("  [Space] Play/Stop        [T] Toggle test tone\n");
    std::printf("  [Up/Down] BPM +/-        [Home] Reset position\n");
    std::printf("  [1-9] Launch clip on track 1-9\n");
    std::printf("  [Shift+1-9] Stop track 1-9\n");
    std::printf("  [Q] Quantize: None/Beat/Bar\n");
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
    m_clips.clear();
    m_mainWindow.destroy();
    SDL_Quit();
    std::printf("Y.A.W.N shutdown complete\n");
}

bool App::loadClipFromFile(const std::string& path, int trackIndex) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        std::printf("Resampling from %d Hz to %.0f Hz...\n",
            info.sampleRate, m_audioEngine.sampleRate());
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    auto clip = std::make_unique<audio::Clip>();
    clip->name = path;
    clip->buffer = buffer;
    clip->looping = true;
    clip->gain = 0.8f;

    std::printf("Clip loaded on track %d: %s (%lld frames, %d ch)\n",
        trackIndex + 1, clip->name.c_str(),
        static_cast<long long>(buffer->numFrames()), buffer->numChannels());

    m_audioEngine.sendCommand(audio::LaunchClipMsg{trackIndex, clip.get()});
    m_clips.push_back(std::move(clip));
    return true;
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

                {
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
                            std::printf("Test tone: %s\n", toneOn ? "ON" : "OFF");
                        }
                        break;

                    case SDLK_UP: {
                        double newBpm = m_audioEngine.transport().bpm() + 1.0;
                        if (newBpm > 300.0) newBpm = 300.0;
                        m_audioEngine.sendCommand(audio::TransportSetBPMMsg{newBpm});
                        break;
                    }

                    case SDLK_DOWN: {
                        double newBpm = m_audioEngine.transport().bpm() - 1.0;
                        if (newBpm < 20.0) newBpm = 20.0;
                        m_audioEngine.sendCommand(audio::TransportSetBPMMsg{newBpm});
                        break;
                    }

                    case SDLK_HOME:
                        m_audioEngine.sendCommand(audio::TransportSetPositionMsg{0});
                        break;

                    case SDLK_Q: {
                        static int qMode = 2;
                        qMode = (qMode + 1) % 3;
                        auto mode = static_cast<audio::QuantizeMode>(qMode);
                        m_audioEngine.sendCommand(audio::SetQuantizeMsg{mode});
                        const char* names[] = {"None", "Beat", "Bar"};
                        std::printf("Quantize: %s\n", names[qMode]);
                        break;
                    }

                    case SDLK_1: case SDLK_2: case SDLK_3:
                    case SDLK_4: case SDLK_5: case SDLK_6:
                    case SDLK_7: case SDLK_8: case SDLK_9: {
                        int trackIndex = static_cast<int>(event.key.key - SDLK_1);
                        if (shift) {
                            m_audioEngine.sendCommand(audio::StopClipMsg{trackIndex});
                            std::printf("Stop track %d\n", trackIndex + 1);
                        } else if (!m_clips.empty()) {
                            size_t clipIdx = trackIndex % m_clips.size();
                            m_audioEngine.sendCommand(
                                audio::LaunchClipMsg{trackIndex, m_clips[clipIdx].get()});
                            std::printf("Launch clip on track %d\n", trackIndex + 1);
                        }
                        break;
                    }

                    default:
                        break;
                }
                }
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(m_mainWindow.getHandle())) {
                    m_running = false;
                }
                break;

            case SDL_EVENT_DROP_FILE: {
                const char* file = event.drop.data;
                if (file) {
                    int trackIndex = static_cast<int>(m_clips.size()) % 9;
                    loadClipFromFile(file, trackIndex);
                }
                break;
            }

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
        "Y.A.W.N  |  %s  |  %3.0f BPM  |  %d.%d  |  %d clips loaded",
        m_displayPlaying ? "PLAYING" : "STOPPED",
        bpm, bar, beat,
        static_cast<int>(m_clips.size()));

    SDL_SetWindowTitle(m_mainWindow.getHandle(), title);
}

} // namespace yawn
