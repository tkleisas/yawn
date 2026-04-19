#pragma once

#include "ui/Window.h"
#include "ui/Renderer.h"
#include "visual/VisualEngine.h"
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
#include "ui/panels/VisualParamsPanel.h"
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
#include "visual/VideoImporter.h"

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
    void showVisualKnobLFOMenu(int knobIdx, float mx, float my);

    // Shared launch logic for visual clips — routes either a
    // session-grid clip (via its source slot) or an arrangement clip
    // (via its inline VisualClip) through the same engine setup path
    // so behaviour stays consistent. `shaderPath` may be empty to
    // trigger the video/live/model fallback defaults.
    void launchVisualClipData(int track,
                               const visual::VisualClip& vc,
                               const std::string& shaderPath);
    // Arrangement playback — polled each frame; fires launch / clear
    // on visual tracks as the transport head crosses clip boundaries.
    void pollArrangementVisualPlayback();

    // Clear every visual track's layer and reset its launch state.
    // Called whenever the transport "Stop" gesture fires, so visuals
    // end in lockstep with audio / MIDI regardless of whether the
    // transport was actually playing.
    void stopAllVisualLayers();

    // Visual-knob automation — also polled from the main thread. The
    // project-side lane store (Project.track.automationLanes) is the
    // source of truth; visual knobs don't need audio-thread precision
    // so we skip the AutomationEngine/m_trackAutoLanes path entirely.
    // Precedence: arrangement lane overrides clip envelope (per the
    // H.3 design note).
    void pollVisualKnobAutomation();

    // Session-view follow actions for visual clips. Polled each frame
    // while transport is playing: once (transportBeat - launchBeat) ≥
    // barCount × beatsPerBar, rolls chanceA, resolves the action to
    // a target scene, and fires the session launch path. Mirrors the
    // audio ClipEngine follow-action behavior; shares target-scene
    // resolution via `resolveFollowActionScene`.
    void pollVisualFollowActions();
    int  resolveFollowActionScene(int track, int currentScene,
                                   FollowActionType action) const;
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
    visual::VisualEngine m_visualEngine;
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
    ui::fw::VisualParamsPanel*   m_visualParamsPanel = nullptr;
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

    // Target slot for the pending "Load Shader…" file dialog.
    int m_pendingShaderTrack = -1;
    int m_pendingShaderScene = -1;
    // Target slot for the pending "Set Video…" file dialog.
    int m_pendingVideoTrack = -1;
    int m_pendingVideoScene = -1;
    // Target slot for the pending "Set Model…" file dialog.
    int m_pendingModelTrack = -1;
    int m_pendingModelScene = -1;
    // Target slot for the pending "Set Scene Script…" file dialog.
    int m_pendingSceneTrack = -1;
    int m_pendingSceneScene = -1;

    // Active video imports (transcoding in the background via ffmpeg).
    struct PendingVideoImport {
        int track;
        int scene;
        std::string sourcePath;
        std::unique_ptr<visual::VideoImporter> importer;
    };
    std::vector<PendingVideoImport> m_pendingImports;

    void startVideoImport(int track, int scene, const std::string& sourcePath);
    void onVideoImportDone(PendingVideoImport& pi);

    // Shader-path helpers (localization + resolution).
    // localizeShader copies an externally-picked .frag into
    //   <project>/shaders/<stem>.frag and returns the project-relative
    //   path ("shaders/<stem>.frag"). If a file with the same stem
    //   already exists in the project it's reused (shared-by-default).
    //   Returns the original path if the project isn't saved yet.
    // resolveShaderPath turns a stored path (relative or absolute) into
    //   something loadShaderFromFile can open. Relative paths are
    //   resolved against the project root.
    std::string localizeShader(const std::string& sourcePath);
    std::string resolveShaderPath(const std::string& storedPath) const;

    // Model-path equivalents — same contract as the shader helpers but
    // write into <project>/models/ and resolve "models/..." prefixes.
    // localizeModel only copies .glb (self-contained) so references
    // embedded in a .gltf don't silently break; .gltf paths pass through
    // unchanged with a warning.
    std::string localizeModel(const std::string& sourcePath);
    std::string resolveModelPath(const std::string& storedPath) const;

    // Scene-script helpers — same contract as the shader/model pair
    // but write into <project>/scripts/ and resolve "scripts/..." .
    std::string localizeScene(const std::string& sourcePath);
    std::string resolveScenePath(const std::string& storedPath) const;

    // Per-(track, knob) last-seen version from VisualKnobBus, so we only
    // act on fresh MIDI CC writes (avoids stomping on user drags).
    uint32_t m_visualKnobBusVersions[kMaxTracks][8] = {};

    // Arrangement playback tracking — per visual track, the index of
    // the currently-active arrangement clip (-1 = none / in a gap).
    // Compared against each frame's lookup to detect transitions.
    int m_activeArrVisualClip[kMaxTracks] = {};
    bool m_activeArrInit = false;

    // Tracks the last-seen transport stop counter so we can clear
    // visual layers exactly once per stop-press — including stops
    // fired while the transport wasn't playing (session-only visual
    // launches), which a play→stop edge detector would miss.
    uint64_t m_lastSeenStopCounter = 0;

    // Per-track launch beat of the currently-playing session visual
    // clip (used by the follow-action poller to decide when barCount
    // bars have elapsed). NaN-equivalent = not launched.
    static constexpr double kNoVisualLaunch = -1.0;
    double m_visualLaunchBeat[kMaxTracks] = {};
    int    m_visualLaunchScene[kMaxTracks] = {};
    bool   m_visualLaunchInit = false;

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
