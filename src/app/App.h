#pragma once

#include "ui/Window.h"
#include "ui/Renderer.h"
#include "visual/VisualEngine.h"
#include "visual/ShaderLibrary.h"
#include "ui/Font.h"
#include "ui/ToastManager.h"
#include "ui/Widget.h"
#include "ui/framework/v2/MenuBar.h"
#include "ui/panels/DetailPanelWidget.h"
#include "ui/panels/PianoRollPanel.h"
#include "ui/VirtualKeyboard.h"
#include "ui/ContextMenu.h"
// v1 AboutDialog retired — fw2 About is inline in the menu handler.
// v1 ConfirmDialogWidget retired — fw2::ConfirmDialog lives on
// LayerStack::Modal (include "ui/framework/v2/Dialog.h" to use it).
#include "ui/framework/v2/TextInputDialog.h"
#include "ui/panels/PreferencesDialog.h"
#include "ui/framework/v2/ExportDialog.h"
#include "ui/framework/v2/AutoSampleDialog.h"
#include "ui/framework/v2/FlexBox.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/FontAdapter.h"
#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/ContentGrid.h"
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
    void handleKeyEvent(const SDL_Event& event);
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
    void showArrangementClipContextMenu(int trackIndex, int clipIdx, float mx, float my);
    void showVisualKnobLFOMenu(int knobIdx, float mx, float my);
    void showShaderLibraryMenu(float mx, float my);
    // Right-click menu for "Map to Macro N" on a parameter knob.
    // Builds a context menu showing each of the 8 macros as a target;
    // if the same target is already mapped, also offers "Unmap" and
    // dims the macro it's currently routed through. Sets the new
    // mapping to the macro's full 0..1 range — sub-range editing
    // arrives in phase 4.4 alongside the macro device card UI.
    void showMacroMappingMenu(const MacroTarget& target, float mx, float my);

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

    // Phase 4.1 — apply per-track macro mappings each frame. Walks
    // every track's MacroDevice, lerps each mapping's macro value
    // through its [rangeMin, rangeMax], and pushes the result into
    // the appropriate engine API (visual source / chain only in
    // 4.1; audio + MIDI targets land in 4.2).
    void applyMacroMappings();
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
    void setupDefaultTracks();
    void resetEngineState();
    void insertSceneAtSelection();
    void updateWindowTitle();
    void markDirty() { m_projectDirty = true; updateWindowTitle(); }

    // SDL3 async file dialog callbacks
    static void SDLCALL onOpenFolderResult(void* userdata, const char* const* filelist, int filter);
    static void SDLCALL onSaveFolderResult(void* userdata, const char* const* filelist, int filter);

    ui::Window m_mainWindow;
    visual::VisualEngine m_visualEngine;
    visual::ShaderLibrary m_shaderLibrary;
    ui::Renderer2D m_renderer;
    ui::Font m_font;
    ui::ToastManager m_toastManager;
    ::yawn::ui::fw2::FwMenuBar m_menuBar;
    ui::VirtualKeyboard m_virtualKeyboard;
    // v1 m_contextMenu retired — replaced by fw2::ContextMenu (LayerStack).
    ui::InputState m_inputState;

    // Widget tree for layout. fw2 throughout — every panel plugs into
    // m_rootLayout (a fw2::FlexBox) directly. v1 wrappers retired in
    // Phase 2 of the framework migration.
    std::unique_ptr<ui::fw2::FlexBox> m_rootLayout;
    std::unique_ptr<ui::fw2::DetailPanelWidget> m_detailPanelOwner;
    ui::fw2::DetailPanelWidget*                 m_detailPanel  = nullptr;
    std::unique_ptr<ui::fw2::PianoRollPanel> m_pianoRollOwner;
    ui::fw2::PianoRollPanel*                 m_pianoRoll   = nullptr;
    std::unique_ptr<ui::fw2::MixerPanel> m_mixerPanelOwner;
    ui::fw2::MixerPanel*                 m_mixerPanel  = nullptr;
    std::unique_ptr<ui::fw2::SessionPanel> m_sessionPanelOwner;
    ui::fw2::SessionPanel*                 m_sessionPanel  = nullptr;
    std::unique_ptr<ui::fw2::ArrangementPanel> m_arrangementPanelOwner;
    ui::fw2::ArrangementPanel*                 m_arrangementPanel = nullptr;
    std::unique_ptr<ui::fw2::TransportPanel> m_transportPanelOwner;
    ui::fw2::TransportPanel*                 m_transportPanel  = nullptr;
    std::unique_ptr<ui::fw2::ContentGrid>    m_contentGridOwner;
    ui::fw2::ContentGrid*                    m_contentGrid  = nullptr;
    std::unique_ptr<ui::fw2::BrowserPanel> m_browserPanelOwner;
    ui::fw2::BrowserPanel*                 m_browserPanel  = nullptr;
    std::unique_ptr<ui::fw2::ReturnMasterPanel> m_returnMasterPanelOwner;
    ui::fw2::ReturnMasterPanel*                 m_returnMasterPanel  = nullptr;
    std::unique_ptr<ui::fw2::VisualParamsPanel> m_visualParamsPanelOwner;
    ui::fw2::VisualParamsPanel*                 m_visualParamsPanel  = nullptr;
    // v1 m_aboutDialog retired — use fw2::Dialog.
    // v1 m_confirmDialog retired — use fw2::ConfirmDialog.
    // TextInputDialog migrated to fw2 — value-typed member now.
    ui::fw2::FwTextInputDialog    m_textInputDialog;
    // PreferencesDialog migrated to fw2 — value-typed member since it's
    // no longer a Widget subclass; lifetime is the App itself.
    ui::fw2::FwPreferencesDialog  m_preferencesDialog;
    ui::fw2::FwExportDialog       m_exportDialog;
    // Auto-Sample dialog — modal that drives audio::AutoSampleWorker
    // when the user clicks "Auto-Sample" on a Multisampler instrument.
    // tick() runs each frame from App::update() to advance the worker
    // and resolve the test-note Note-Off timer.
    ui::fw2::FwAutoSampleDialog   m_autoSampleDialog;

    // v2 UI framework (ui-v2 in-progress). Coexists with v1 for now —
    // we'll migrate panels over one by one. FontAdapter bridges v1's
    // Font to v2's TextMetrics interface; LayerStack hosts floating UI
    // (modals, dropdowns, tooltips, toasts) outside the main tree.
    ui::fw2::UIContext   m_fw2Context;
    std::unique_ptr<ui::fw2::FontAdapter> m_fw2FontAdapter;
    ui::fw2::LayerStack  m_fw2LayerStack;

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
    bool m_showReturns = false;
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

    // Arrangement clipboard — separate from session clipboard because
    // arrangement clips carry timeline metadata (startBeat, lengthBeats,
    // offsetBeats, type, colorIndex) in addition to the underlying
    // audio/MIDI/visual payload. hasArrangementClip() tells callers
    // whether Paste is meaningful.
    ArrangementClip m_arrangementClipboard;
    bool            m_arrangementClipboardValid = false;

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
