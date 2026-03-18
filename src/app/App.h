#pragma once

#include "ui/Window.h"
#include "audio/AudioEngine.h"

namespace yawn {

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init();
    void run();
    void shutdown();

    bool isRunning() const { return m_running; }

private:
    void processEvents();
    void update();
    void render();
    void renderTransportInfo();

    ui::Window m_mainWindow;
    audio::AudioEngine m_audioEngine;
    bool m_running = false;

    // UI state from audio thread
    int64_t m_displayPosition = 0;
    double m_displayBeats = 0.0;
    bool m_displayPlaying = false;
};

} // namespace yawn
