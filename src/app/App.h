#pragma once

#include "ui/Window.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Widget.h"
#include "ui/MenuBar.h"
#include "ui/panels/DetailPanelWidget.h"
#include "ui/panels/PianoRollPanel.h"
#include "ui/VirtualKeyboard.h"
#include "ui/ContextMenu.h"
#include "ui/framework/AboutDialog.h"
#include "ui/framework/ConfirmDialogWidget.h"
#include "ui/panels/PreferencesDialog.h"
#include "ui/framework/FlexBox.h"
#include "ui/framework/UIContext.h"
#include "ui/framework/ContentGrid.h"
#include "ui/panels/MixerPanel.h"
#include "ui/panels/SessionPanel.h"
#include "ui/panels/TransportPanel.h"
#include "ui/panels/BrowserPanel.h"
#include "ui/panels/ReturnMasterPanel.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "app/Project.h"
#include "midi/MidiEngine.h"
#include "util/FileIO.h"
#include "util/AppSettings.h"
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
    bool loadSampleToSampler(const std::string& path, int trackIndex);
    bool loadFont();
    void setupMenuBar();
    void showTrackContextMenu(int trackIndex, float mx, float my);
    void showClipContextMenu(int trackIndex, int sceneIndex, float mx, float my);
    void updateDetailForSelectedTrack();

    // Project file operations
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void doSaveProject(const std::filesystem::path& path);
    void doOpenProject(const std::filesystem::path& path);
    void syncTracksToEngine();
    void updateWindowTitle();
    void markDirty() { m_projectDirty = true; updateWindowTitle(); }

    // SDL3 async file dialog callbacks
    static void SDLCALL onOpenFolderResult(void* userdata, const char* const* filelist, int filter);
    static void SDLCALL onSaveFolderResult(void* userdata, const char* const* filelist, int filter);

    ui::Window m_mainWindow;
    ui::Renderer2D m_renderer;
    ui::Font m_font;
    ui::MenuBar m_menuBar;
    ui::VirtualKeyboard m_virtualKeyboard;
    ui::ContextMenu m_contextMenu;
    ui::InputState m_inputState;

    // Widget tree for layout (replaces manual layout math in render/events)
    std::unique_ptr<ui::fw::FlexBox> m_rootLayout;
    ui::fw::Widget* m_menuBarW    = nullptr;  // owned by unique_ptr below
    ui::fw::DetailPanelWidget* m_detailPanel = nullptr;
    ui::fw::PianoRollPanel*    m_pianoRoll   = nullptr;
    ui::fw::MixerPanel*   m_mixerPanel   = nullptr;  // owned by unique_ptr below
    ui::fw::SessionPanel* m_sessionPanel = nullptr;  // owned by unique_ptr below
    ui::fw::TransportPanel*      m_transportPanel  = nullptr;
    ui::fw::ContentGrid*         m_contentGrid     = nullptr;
    ui::fw::BrowserPanel*        m_browserPanel    = nullptr;
    ui::fw::ReturnMasterPanel*   m_returnMasterPanel = nullptr;
    ui::fw::AboutDialog*          m_aboutDialog   = nullptr;
    ui::fw::ConfirmDialogWidget*  m_confirmDialog = nullptr;
    ui::fw::PreferencesDialog*    m_preferencesDialog = nullptr;
    std::vector<std::unique_ptr<ui::fw::Widget>> m_wrappers;
    ui::fw::UIContext m_uiContext;

    audio::AudioEngine m_audioEngine;
    midi::MidiEngine m_midiEngine;
    Project m_project;
    util::AppSettings m_settings;
    bool m_running = false;
    bool m_showMixer = true;
    bool m_showDetailPanel = false;
    int m_selectedTrack = 0;
    int m_selectedScene = 0;

    // Clip clipboard
    struct ClipboardData {
        enum class Type { None, Audio, Midi };
        Type type = Type::None;
        std::unique_ptr<audio::Clip> audioClip;
        std::unique_ptr<midi::MidiClip> midiClip;
        void clear() { type = Type::None; audioClip.reset(); midiClip.reset(); }
    };
    ClipboardData m_clipboard;

    // Cached system cursors
    SDL_Cursor* m_cursorDefault  = nullptr;
    SDL_Cursor* m_cursorEWResize = nullptr;
    SDL_Cursor* m_cursorNSResize = nullptr;
    SDL_Cursor* m_cursorMove     = nullptr;

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
    int m_nextDropScene = 0;

    // UI state from audio thread
    double m_displayBeats = 0.0;
    bool m_displayPlaying = false;
};

} // namespace yawn
