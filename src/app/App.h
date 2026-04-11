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
#include "ui/framework/TextInputDialogWidget.h"
#include "ui/panels/PreferencesDialog.h"
#include "ui/framework/ExportDialog.h"
#include "ui/framework/FlexBox.h"
#include "ui/framework/UIContext.h"
#include "ui/framework/ContentGrid.h"
#include "ui/panels/MixerPanel.h"
#include "ui/panels/SessionPanel.h"
#include "ui/panels/ArrangementPanel.h"
#include "ui/panels/TransportPanel.h"
#include "ui/panels/BrowserPanel.h"
#include "library/LibraryDatabase.h"
#include "library/LibraryScanner.h"
#include "controllers/ControllerManager.h"
#include "ui/panels/ReturnMasterPanel.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "app/Project.h"
#include "midi/MidiEngine.h"
#include "midi/MidiMapping.h"
#include "util/FileIO.h"
#include "util/AppSettings.h"
#include "util/UndoManager.h"
#include "util/IconLoader.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <filesystem>

#include "presets/PresetManager.h"
#include "presets/DevicePresetHelpers.h"

#ifdef YAWN_HAS_VST3
#include "vst3/VST3Scanner.h"
#include "vst3/VST3EditorWindow.h"
#include "vst3/VST3Instrument.h"
#include "vst3/VST3Effect.h"
#endif

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
    bool loadClipToArrangement(const std::string& path, int trackIndex, double beatPos);
    bool loadSampleToSampler(const std::string& path, int trackIndex);
    bool loadLoopToDrumSlop(const std::string& path, int trackIndex);
    bool loadSampleToDrumRack(const std::string& path, int trackIndex);
    bool loadSampleToGranular(const std::string& path, int trackIndex);
    bool loadModulatorToVocoder(const std::string& path, int trackIndex);
    bool loadFont();
    void setupMenuBar();
    void showTrackContextMenu(int trackIndex, float mx, float my);
    void showClipContextMenu(int trackIndex, int sceneIndex, float mx, float my);
    void showSceneContextMenu(int sceneIndex, float mx, float my);
    void performClipDragDrop(int srcT, int srcS, int dstT, int dstS, bool isCopy);
    void updateDetailForSelectedTrack();
    void updateDetailForReturnBus(int bus);
    void updateDetailForMaster();
    void switchToView(ViewMode mode);
    void syncArrangementClipsToEngine(int trackIdx);

    // Project file operations
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void doSaveProject(const std::filesystem::path& path);
    void doOpenProject(const std::filesystem::path& path);
    void openExportDialog();
    void startExportRender(const std::string& filePath);
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
    ui::fw::ArrangementPanel* m_arrangementPanel = nullptr;
    ui::fw::TransportPanel*      m_transportPanel  = nullptr;
    ui::fw::ContentGrid*         m_contentGrid     = nullptr;
    ui::fw::BrowserPanel*        m_browserPanel    = nullptr;
    ui::fw::ReturnMasterPanel*   m_returnMasterPanel = nullptr;
    ui::fw::AboutDialog*          m_aboutDialog   = nullptr;
    ui::fw::ConfirmDialogWidget*  m_confirmDialog = nullptr;
    ui::fw::TextInputDialogWidget* m_textInputDialog = nullptr;
    ui::fw::PreferencesDialog*    m_preferencesDialog = nullptr;
    ui::fw::ExportDialog*         m_exportDialog = nullptr;
    std::vector<std::unique_ptr<ui::fw::Widget>> m_wrappers;
    ui::fw::UIContext m_uiContext;

    audio::AudioEngine m_audioEngine;
    midi::MidiEngine m_midiEngine;
    midi::MidiMonitorBuffer m_midiMonitor;
    midi::MidiLearnManager m_midiLearnManager;
    Project m_project;
    undo::UndoManager m_undoManager;
    util::AppSettings m_settings;
    library::LibraryDatabase m_libraryDb;
    std::unique_ptr<library::LibraryScanner> m_libraryScanner;
    controllers::ControllerManager m_controllerManager;
    bool m_running = false;
    uint64_t m_lastFrameTicks = 0;
    GLuint m_iconTexture = 0;
    bool m_showMixer = true;
    bool m_showReturns = true;
    bool m_showDetailPanel = false;
    int m_selectedTrack = 0;
    int m_selectedScene = 0;

    // Detail panel target: what the detail panel is currently showing
    enum class DetailTarget { Track, ReturnBus, Master };
    DetailTarget m_detailTarget = DetailTarget::Track;
    int m_detailReturnBus = -1;  // which return bus (0..kMaxReturnBuses-1) when target==ReturnBus

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
    std::string m_pendingExportPath;
    static void SDLCALL onExportSaveResult(void* userdata, const char* const* filelist, int filter);

    // Persistent storage for async SDL file dialog filter (must outlive the dialog)
    SDL_DialogFileFilter m_exportFilter{};
    std::string m_exportFilterDesc;
    std::string m_exportFilterPattern;
    std::string m_exportDefaultName;

    // Mouse tracking for drag
    float m_lastMouseX = 0;
    float m_lastMouseY = 0;

    // Track which scene/track to assign next dropped file to
    int m_nextDropScene = 0;

    // UI state from audio thread
    double m_displayBeats = 0.0;
    bool m_displayPlaying = false;

#ifdef YAWN_HAS_VST3
    std::unique_ptr<vst3::VST3Scanner> m_vst3Scanner;
    std::vector<std::unique_ptr<vst3::VST3EditorWindow>> m_vst3Editors;

    void openVST3Editor(vst3::VST3PluginInstance* instance,
                        const std::string& modulePath,
                        const std::string& classID,
                        const std::string& title);
#endif
};

} // namespace yawn
