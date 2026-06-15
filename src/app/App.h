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
#include "transcribe/StemSeparation.h"
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

namespace effects { class ConvolutionReverb; class NeuralAmp; }
namespace midi { class LFO; }

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

    // Clip-pointer graveyard now lives in Project so all slot
    // mutations (setClip / moveSlot / copySlot / clearSlot) flush
    // the OLD clip into it automatically. App just drains it once
    // per frame from update().

    bool loadClipToSlot(const std::string& path, int trackIndex, int sceneIndex);
    bool loadClipToArrangement(const std::string& path, int trackIndex, double beatPos);
    bool loadSampleToSampler(const std::string& path, int trackIndex);
    bool loadLoopToDrumSlop(const std::string& path, int trackIndex);
    bool loadSampleToDrumRack(const std::string& path, int trackIndex);
    bool loadSampleToGranular(const std::string& path, int trackIndex);
    bool loadModulatorToVocoder(const std::string& path, int trackIndex);

    // Buffer-based companions (used by in-app drag-drop where the
    // audio is already loaded into memory as part of a clip — we
    // skip the file-read + resample). Buffer is assumed to already
    // match the engine sample rate (clips always do post-load).
    bool loadBufferToSampler(std::shared_ptr<audio::AudioBuffer> buf,
                              const std::string& name, int trackIndex);
    bool loadBufferToDrumSlop(std::shared_ptr<audio::AudioBuffer> buf,
                               const std::string& name, int trackIndex);
    bool loadBufferToGranular(std::shared_ptr<audio::AudioBuffer> buf,
                               const std::string& name, int trackIndex);
    bool loadBufferToVocoder(std::shared_ptr<audio::AudioBuffer> buf,
                              const std::string& name, int trackIndex);
    bool loadFont();
    void setupMenuBar();
    void showTrackContextMenu(int trackIndex, float mx, float my);
    void showClipContextMenu(int trackIndex, int sceneIndex, float mx, float my);
    void showSceneContextMenu(int sceneIndex, float mx, float my);
    void showArrangementClipContextMenu(int trackIndex, int clipIdx, float mx, float my);
    void showVisualKnobLFOMenu(int knobIdx, float mx, float my);
    void showShaderLibraryMenu(float mx, float my);

    // "Custom…" numeric entry for preset menus. promptNumber opens the
    // modal text dialog pre-filled with `def` and hands the raw string to
    // `onText` on confirm (SDL text-input start/stop handled here). The
    // typed wrappers parse + clamp to [lo,hi] and call apply(value);
    // promptScale lets the user enter display units (e.g. 0.01 to type a
    // percent for a 0..1 param) — default shown = current/promptScale.
    // No undo/markDirty here: the apply callback owns persistence, matching
    // each menu's preset items.
    void promptNumber(const char* title, const std::string& def,
                      std::function<void(const std::string&)> onText);
    void promptCustomFloat(const char* title, float current, float lo, float hi,
                           float promptScale, std::function<void(float)> apply);
    void promptCustomInt(const char* title, int current, int lo, int hi,
                         std::function<void(int)> apply);

    // MIDI LFO modulation-target picker (shown from the LFO device strip).
    // Builds a grouped menu of every modulatable target reachable from the
    // LFO at (track, chainSlot): instrument / audio-fx / midi-fx / mixer
    // params on its own track, plus visual-layer knobs and shader params on
    // any visual track. lfoTargetName() resolves the current target to a
    // display string; lfoAt() resolves the LFO pointer (null if the slot
    // isn't an LFO).
    void showLfoTargetMenu(int track, int chainSlot, float mx, float my);
    std::string lfoTargetName(int track, int chainSlot);
    midi::LFO*  lfoAt(int track, int chainSlot);
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
    // Stamp a session-launched visual clip's time origin: switches the
    // layer to wall-clock iTime and records the launch beat + scene so
    // the per-clip visual envelope poller (pollVisualKnobAutomation) has
    // a time reference. Every session launch path must call this after
    // launchVisualClipData, or envelopes never advance.
    void stampVisualLaunch(int track, int scene);
    // Launch a visual clip honouring the slot's launchQuantize. When the
    // transport is (or is about to be) advancing and quantize != None, the
    // launch is deferred to the next bar/beat boundary (via
    // pollVisualLaunchQueue) so the video starts in sync with the quantized
    // audio/MIDI clips in the same scene instead of immediately.
    // transportWillPlay tells us a launched audio/MIDI clip (or count-in)
    // will start the transport even though it isn't playing yet — without
    // it, a visual evaluated before the transport spins up would fire a bar
    // early. Pass false for a lone visual launch (nothing to sync to while
    // stopped → launch now).
    void launchVisualClipQuantized(int track, int scene, bool transportWillPlay);
    // Fire any pending quantized visual launches whose boundary has been
    // reached. Polled each frame.
    void pollVisualLaunchQueue();
    // Arrangement playback — polled each frame; fires launch / clear
    // on visual tracks as the transport head crosses clip boundaries.
    void pollArrangementVisualPlayback();

    // Clear every visual track's layer and reset its launch state.
    // Called whenever the transport "Stop" gesture fires, so visuals
    // end in lockstep with audio / MIDI regardless of whether the
    // transport was actually playing.
    void stopAllVisualLayers();

    // "Stop Clips" — stop every audio/MIDI/visual clip and wipe each
    // track's launch memory (defaultScene) so the next transport Play
    // starts nothing. Wired to the session-view corner Stop-All button.
    void stopAllClips();

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
    // Load a .mid loop file into the (track, scene) slot with undo + a
    // status toast. Shared by the Loops-tab double-click (selected slot)
    // and the drag-to-slot drop (slot under the cursor). Returns true if
    // the loop was loaded.
    bool loadMidiLoopIntoSlot(const std::string& path, int track, int scene);
    void updateDetailForSelectedTrack();
    void updateDetailForReturnBus(int bus);
    void updateDetailForMaster();
    void switchToView(ViewMode mode);
    void syncArrangementClipsToEngine(int trackIdx);
    // Empty a track's (or every track's) arrangement lane and return it to
    // session control. Undoable. See the "Clear Arrangement" menu items.
    void clearTrackArrangement(int trackIdx);
    void clearAllArrangements();

    // Record session → arrangement. toggleArrangementRecord arms/disarms
    // (disarm offers to bounce the take); pollArrangementRecord runs each
    // frame to capture the playing session clip per track; commit writes
    // the take into the arrangement (punch-overwrite).
    void toggleArrangementRecord();
    void pollArrangementRecord();
    void commitArrangementTake();
    int  currentSessionScene(int track);  // playing session scene, -1 none

    // Atomically replace the live MIDI clip identified by `oldClip`
    // (located in a session slot or an arrangement clip) with `newClip`,
    // retiring the old object through the clip graveyard and live-
    // re-pointing the audio engine so a playing clip keeps going without
    // a restart. Returns the new live pointer (owned by the model), or
    // nullptr if `oldClip` was not found. Backs the piano-roll edit
    // clone-swap (PianoRollPanel::setOnReplaceClip).
    midi::MidiClip* replaceEditedMidiClip(const midi::MidiClip* oldClip,
                                          std::unique_ptr<midi::MidiClip> newClip);

    // Install a MIDI clip into a known (track, scene) slot, retiring the
    // previous clip via the graveyard and live-re-pointing the audio
    // engine if that slot is currently playing (so swapping a loop on a
    // playing slot takes effect immediately instead of on next launch).
    // Caller handles markDirty/undo. Returns the new live pointer.
    midi::MidiClip* setMidiClipLive(int track, int scene,
                                    std::unique_ptr<midi::MidiClip> newClip);

    // Project file operations
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void doSaveProject(const std::filesystem::path& path);
    void doOpenProject(const std::filesystem::path& path);
    void openExportDialog();

    // Procedural preset generation (Tools menu). Runs the generator on
    // a detached worker thread (full catalog ≈ 6 s) and posts a
    // completion toast from update(). `selectedDeviceOnly` limits the
    // batch to the currently-selected track's instrument.
    void startPresetGeneration(float alienRatio, bool selectedDeviceOnly);
    void startExportRender(const std::string& filePath);
    // Demucs stem separation: kick off a worker for the audio clip at
    // (track, scene); applyStemResult() runs on the main thread from
    // update() when the worker finishes (creates 4 stem tracks).
    void startStemSeparation(int trackIndex, int sceneIndex);
    void applyStemResult();
    void syncTracksToEngine();
    void setupDefaultTracks();
    // Load an IR file into a ConvolutionReverb effect (read via
    // FileIO::loadAudioFile, resample to host rate if needed,
    // mono-sum, push to engine, store path). Shared by the
    // user-clicks-Load-IR dialog callback AND the project-load
    // rehydration path so the same decode/resample/mono-sum logic
    // applies to both. Returns true on success.
    bool loadIRIntoConvReverb(effects::ConvolutionReverb* eff,
                               const std::string& path);
    // After project load, walk every effect on every chain and re-
    // read any IR files referenced by ConvolutionReverb extra-state.
    // Without this the IR path is preserved but the actual sample
    // data isn't, so the device sounds passthrough until the user
    // re-clicks Load.
    void rehydrateConvolutionIRs();
    void resetEngineState();
    void insertSceneAtSelection();

    // Structural scene edits reallocate Project::m_clipSlots, moving every
    // ClipSlot and dangling the &slot->clipAutomation pointers the audio
    // thread reads in AutomationEngine::process. These wrappers stop every
    // launched clip first (immediate, so the engine drops those pointers),
    // then mutate — route ALL scene insert/delete/duplicate through them,
    // including undo/redo lambdas. See stopAllClipsForSceneEdit().
    void stopAllClipsForSceneEdit();
    void sceneInsert(int index);
    void sceneDelete(int index);
    void sceneDuplicate(int index);
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

    // Debounced settings save (e.g. piano-roll draw velocity changes during
    // a drag) — m_settingsDirty set on change, flushed in update() once it's
    // been stable for a short while, so a drag isn't a write storm.
    bool m_settingsDirty    = false;
    int  m_settingsDirtyAge = 0;
    // The scene chosen as the target row for the next session-record
    // gesture, set when the user presses Record on the transport. -1
    // when transport recording is not armed. Drives the red record-
    // target ring on SessionPanel's scene label.
    int m_recordTargetScene = -1;

    // Background preset-generation state (Tools menu). The worker
    // thread is detached; these atomics hand the result back to the
    // UI thread, which posts a toast in update() when m_genFinished
    // flips true.
    std::atomic<bool> m_genRunning{false};
    std::atomic<bool> m_genFinished{false};
    std::atomic<int>  m_genCount{0};
    std::atomic<int>  m_genValid{0};
    // After generation, presets are re-indexed by an async library
    // scan; this UI-thread frame counter waits for that scan to finish
    // (≥0 = active) before refreshing the Browser's preset list.
    int               m_genRescanWait = -1;
    // One-shot Browser refresh after the initial startup FullScan
    // finishes (≥0 = waiting). Needed because the tabs first refresh
    // against an empty DB; without this the seeded factory loops (and
    // any indexed presets/files) wouldn't appear until a restart.
    int               m_initialScanRefresh = 0;

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

    // MIDI-loop drag-to-slot arming. A mouse-down on a Loops-tab row
    // arms a potential drag (stores the .mid path + down position); the
    // global mouse-move handler promotes it to an active DragManager
    // drag once the cursor moves past a small threshold. Cleared on
    // mouse-up regardless of whether a drop landed.
    std::string m_loopDragArmedPath;
    bool        m_loopDragArmed  = false;
    float       m_loopDragStartX = 0;
    float       m_loopDragStartY = 0;

    // Track which scene/track to assign next dropped file to
    int m_nextDropScene = 0;

    // Multi-file video drops: SDL fires one DROP_FILE per file, all
    // sharing the same drop point. We anchor the first video on the slot
    // under the cursor, then cascade later files in the same batch down
    // successive scenes so N videos fill N slots instead of colliding in
    // one. Reset to -1 on DROP_BEGIN; -1 means no batch is in progress.
    int m_videoDropTrack = -1;
    int m_videoDropScene = -1;

    // Live drag-drop cursor position. SDL_EVENT_DROP_FILE carries (0,0) on
    // some platforms (notably Linux), so we track SDL_EVENT_DROP_POSITION
    // during the drag and use it (or the OS mouse state) to resolve which
    // slot a dropped file lands on. m_haveDropPos guards against backends
    // that don't emit DROP_POSITION at all.
    float m_lastDropX   = 0.0f;
    float m_lastDropY   = 0.0f;
    bool  m_haveDropPos = false;

    // Target slot for the pending "Load Shader…" file dialog.
    int m_pendingShaderTrack = -1;
    int m_pendingShaderScene = -1;
    // Target effect for the pending "Load IR…" dialog from the
    // ConvolutionReverb display panel. SDL_ShowOpenFileDialog's
    // C-style callback only carries a void* userdata, not arbitrary
    // captures, so the effect pointer is stashed here between
    // dialog-open and callback-fires.
    effects::ConvolutionReverb* m_pendingConvIRReverb = nullptr;
    // Same SDL-callback userdata stash trick for .nam loading
    // on the NeuralAmp device's "Load Model…" button.
    effects::NeuralAmp*         m_pendingNamEffect    = nullptr;
    // Target slot for the pending "Set Video…" file dialog.
    int m_pendingVideoTrack = -1;
    int m_pendingVideoScene = -1;
    // Target slot for the pending "Set Model…" file dialog.
    int m_pendingModelTrack = -1;
    int m_pendingModelScene = -1;
    // Target slot for the pending "Set Scene Script…" file dialog.
    int m_pendingSceneTrack = -1;
    int m_pendingSceneScene = -1;

    // In-flight Demucs stem-separation job (one at a time). The worker
    // thread fills `output` then sets `done` (release); update() reads it
    // (acquire) on the main thread and applies it. `cancel` is polled by
    // the worker; `phase`/`fraction` drive progress toasts.
    struct PendingStem {
        std::atomic<bool>  active{false};
        std::atomic<bool>  done{false};
        std::atomic<bool>  cancel{false};
        std::atomic<int>   phase{0};        // 0 = download, 1 = separate
        std::atomic<float> fraction{0.0f};
        transcribe::StemOutput output;       // valid once done == true
        int trackIndex = 0;
        int sceneIndex = 0;
        std::string baseName;
        int lastBucket = -1;                 // main-thread: last progress toast bucket
    };
    std::unique_ptr<PendingStem> m_pendingStem;

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

    // Multi-model scene helpers. A visual clip's model list is the
    // primary `modelPath` (index 0) followed by `extraModelPaths`.
    //   addModelToClip:     localize + append; the first added model
    //                       becomes the primary (clears the other
    //                       iChannel2 sources, seeds name/colour).
    //   removeModelFromClip: drop list index N; if the primary goes, the
    //                       next model is promoted to primary.
    //   reloadVisualClipModels: re-upload the layer's model set if this
    //                       clip is the track's launched scene.
    // All mark the project dirty and refresh the engine.
    void addModelToClip(int track, int scene, const std::string& sourcePath);
    void removeModelFromClip(int track, int scene, int listIndex);
    void reloadVisualClipModels(int track, int scene);
    // Assign a library model to the selected visual clip (Models browser
    // tab double-click) — sets it as the clip's primary model.
    void assignModelFromLibrary(const std::string& sourcePath);

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

    // Pending quantized visual-clip launches (UI thread). Audio/MIDI clips
    // honour slot.launchQuantize — the audio engine defers them to a
    // bar/beat boundary. Visual clips must defer to the SAME boundary or
    // the video starts before the audio on a scene launch. m_visualHasPending
    // zero-inits to false = nothing pending, so no explicit init is needed.
    bool   m_visualHasPending[kMaxTracks] = {};
    int    m_visualPendingScene[kMaxTracks] = {};
    double m_visualPendingFireBeat[kMaxTracks] = {};

    // ── Record session → arrangement (Ableton-style capture) ──
    // While armed, each frame we poll the actually-playing session clip
    // per track (so follow-actions / scene launches are captured for
    // free) and accumulate per-track "take" intervals in timeline beats.
    // On disarm we offer to bounce the take into the arrangement (punch-
    // overwriting the recorded range). Capture-then-commit, discardable.
    struct TakeInterval { int scene; double startBeat; double stopBeat; };
    bool   m_arrRecording = false;
    double m_arrRecStartBeat = 0.0;        // beat when recording armed
    int    m_arrRecScene[kMaxTracks] = {}; // open interval's scene, -1 = none
    double m_arrRecStart[kMaxTracks] = {}; // open interval's start beat
    std::array<std::vector<TakeInterval>, kMaxTracks> m_arrRecTake{};

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
