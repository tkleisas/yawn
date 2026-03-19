#pragma once

#include "ui/Window.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/SessionView.h"
#include "ui/MixerView.h"
#include "ui/Widget.h"
#include "ui/MenuBar.h"
#include "ui/DetailPanel.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "app/Project.h"
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

    bool loadClipToSlot(const std::string& path, int trackIndex, int sceneIndex);
    bool loadFont();
    void setupMenuBar();

    ui::Window m_mainWindow;
    ui::Renderer2D m_renderer;
    ui::Font m_font;
    ui::SessionView m_sessionView;
    ui::MixerView m_mixerView;
    ui::MenuBar m_menuBar;
    ui::DetailPanel m_detailPanel;
    ui::InputState m_inputState;

    audio::AudioEngine m_audioEngine;
    Project m_project;
    bool m_running = false;
    bool m_showMixer = true;
    bool m_showDetailPanel = false;
    int m_selectedTrack = 0;

    // Mouse tracking for drag
    float m_lastMouseX = 0;
    float m_lastMouseY = 0;

    // Track which scene/track to assign next dropped file to
    int m_nextDropTrack = 0;
    int m_nextDropScene = 0;

    // UI state from audio thread
    int64_t m_displayPosition = 0;
    double m_displayBeats = 0.0;
    bool m_displayPlaying = false;
};

} // namespace yawn
