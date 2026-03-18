#pragma once

#include "ui/Window.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "util/FileIO.h"
#include <vector>
#include <memory>

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

    bool loadClipFromFile(const std::string& path, int trackIndex);

    ui::Window m_mainWindow;
    audio::AudioEngine m_audioEngine;
    bool m_running = false;

    // Loaded clips (owned here, pointers passed to audio thread)
    std::vector<std::unique_ptr<audio::Clip>> m_clips;

    // UI state from audio thread
    int64_t m_displayPosition = 0;
    double m_displayBeats = 0.0;
    bool m_displayPlaying = false;
};

} // namespace yawn
