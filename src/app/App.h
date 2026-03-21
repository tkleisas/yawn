#pragma once

#include "ui/Window.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Widget.h"
#include "ui/MenuBar.h"
#include "ui/DetailPanel.h"
#include "ui/PianoRoll.h"
#include "ui/VirtualKeyboard.h"
#include "ui/ContextMenu.h"
#include "ui/ConfirmDialog.h"
#include "ui/framework/FlexBox.h"
#include "ui/framework/UIContext.h"
#include "ui/panels/MixerPanel.h"
#include "ui/panels/SessionPanel.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "app/Project.h"
#include "util/FileIO.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <filesystem>

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
    void computeLayout();
    void buildWidgetTree();

    bool loadClipToSlot(const std::string& path, int trackIndex, int sceneIndex);
    bool loadFont();
    void setupMenuBar();
    void showTrackContextMenu(int trackIndex, float mx, float my);
    void updateDetailForSelectedTrack();

    // Project file operations
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void doSaveProject(const std::filesystem::path& path);
    void doOpenProject(const std::filesystem::path& path);
    void updateWindowTitle();
    void markDirty() { m_projectDirty = true; updateWindowTitle(); }

    // SDL3 async file dialog callbacks
    static void SDLCALL onOpenFolderResult(void* userdata, const char* const* filelist, int filter);
    static void SDLCALL onSaveFolderResult(void* userdata, const char* const* filelist, int filter);

    ui::Window m_mainWindow;
    ui::Renderer2D m_renderer;
    ui::Font m_font;
    ui::MenuBar m_menuBar;
    ui::DetailPanel m_detailPanel;
    ui::PianoRoll m_pianoRoll;
    ui::VirtualKeyboard m_virtualKeyboard;
    ui::ContextMenu m_contextMenu;
    ui::ConfirmDialog m_confirmDialog;
    bool m_showAbout = false;
    ui::InputState m_inputState;

    // Widget tree for layout (replaces manual layout math in render/events)
    std::unique_ptr<ui::fw::FlexBox> m_rootLayout;
    ui::fw::Widget* m_menuBarW    = nullptr;  // owned by unique_ptr below
    ui::fw::Widget* m_detailW     = nullptr;
    ui::fw::Widget* m_pianoW      = nullptr;
    ui::fw::MixerPanel*   m_mixerPanel   = nullptr;  // owned by unique_ptr below
    ui::fw::SessionPanel* m_sessionPanel = nullptr;  // owned by unique_ptr below
    std::vector<std::unique_ptr<ui::fw::Widget>> m_wrappers;
    ui::fw::UIContext m_uiContext;

    audio::AudioEngine m_audioEngine;
    Project m_project;
    bool m_running = false;
    bool m_showMixer = true;
    bool m_showDetailPanel = false;
    int m_selectedTrack = 0;

    // Project persistence state
    std::filesystem::path m_projectPath;   // Empty = untitled
    bool m_projectDirty = false;

    // Pending file dialog results (set from SDL callbacks, processed on main thread)
    std::mutex m_dialogMutex;
    std::string m_pendingOpenPath;
    std::string m_pendingSavePath;

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
