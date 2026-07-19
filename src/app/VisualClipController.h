#pragma once

#include "core/Constants.h"
#include "audio/FollowAction.h"
#include "visual/VisualClip.h"
#include "visual/VideoImporter.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace yawn {

class Project;
namespace audio { class AudioEngine; }
namespace visual { class VisualEngine; }
class VisualClipController;

// Services VisualClipController needs from the application shell
// (toasts, session-grid indicators, undo-adjacent dirty marking,
// project-root path, current selection). Implemented by App.
// Kept narrow on purpose: the controller owns the visual-clip state
// machine, the shell only renders side effects.
class VisualClipHost {
public:
    virtual ~VisualClipHost() = default;
    virtual void vccMarkDirty() = 0;
    virtual void vccToast(const std::string& msg, float seconds, int severity) = 0;
    virtual void vccUpdateClipState(int track, bool playing, int64_t playPos, int scene) = 0;
    virtual void vccSetSlotImporting(int track, int scene, bool importing) = 0;
    virtual void vccSetSlotImportProgress(int track, int scene, float progress) = 0;
    virtual void vccSetPanelKnobValues(const float* knobs) = 0;
    virtual bool vccLoadClipToSlot(const std::string& path, int track, int scene) = 0;
    virtual void vccSyncTracksToEngine() = 0;
    virtual std::filesystem::path vccProjectPath() const = 0;
    virtual int vccSelectedTrack() const = 0;
    virtual int vccSelectedScene() const = 0;
};

// Visual-clip state machine, extracted from App (which had grown
// ~1,100 lines of visual glue plus ~40 per-track fields).
//
// Owns: session launch queue (quantized), arrangement playback
// tracking, follow actions, visual-knob automation + macro mapping
// evaluation, knob-CC bus polling, video imports, and shader/model/
// scene asset localization. Everything runs on the UI thread, driven
// by per-frame poll* calls from the App frame loop.
class VisualClipController {
public:
    VisualClipController(Project& project,
                         audio::AudioEngine& engine,
                         visual::VisualEngine& visualEngine,
                         VisualClipHost& host);

    // ── Launch / playback ──
    void stampVisualLaunch(int track, int scene);
    void launchVisualClipQuantized(int track, int scene, bool transportWillPlay);
    void launchVisualClipData(int track, const visual::VisualClip& vc,
                              const std::string& shaderPath);
    void pollVisualLaunchQueue();
    void pollArrangementVisualPlayback();
    void pollVisualFollowActions();
    int  resolveFollowActionScene(int track, int currentScene,
                                  FollowActionType action) const;
    void stopAllVisualLayers();
    void onTransportStopCounter(uint64_t stopCount);

    // ── Automation / modulation ──
    void pollVisualKnobAutomation();
    void pollVisualKnobBus();
    void applyMacroMappings();
    void applyAudioMacroModulation(double beat, double wall);

    // ── Video imports ──
    void startVideoImport(int track, int scene, const std::string& sourcePath);
    void pollVideoImports();

    // ── Asset paths ──
    std::string localizeShader(const std::string& sourcePath);
    std::string resolveShaderPath(const std::string& storedPath) const;
    std::string localizeModel(const std::string& sourcePath);
    std::string resolveModelPath(const std::string& storedPath) const;
    std::string localizeScene(const std::string& sourcePath);
    std::string resolveScenePath(const std::string& storedPath) const;

    // ── Model clip ops / slot content ──
    void addModelToClip(int track, int scene, const std::string& sourcePath);
    void removeModelFromClip(int track, int scene, int listIndex);
    void reloadVisualClipModels(int track, int scene);
    void assignModelFromLibrary(const std::string& sourcePath);
    void addImageToSlot(int track, int scene, const std::string& sourcePath);

    // ── State access for App reset paths ──
    void resetArrangementTracking(int track);
    void resetAllArrangementTracking();
    void resetLaunchState(int track);
    int  activeVisualScene(int track) const;
    int  arrActiveClip(int track) const {
        return (track >= 0 && track < kMaxTracks)
                 ? m_activeArrVisualClip[track] : -1;
    }

private:
    struct PendingVideoImport {
        int track;
        int scene;
        std::string sourcePath;
        std::unique_ptr<visual::VideoImporter> importer;
    };
    void onVideoImportDone(PendingVideoImport& pi);

    Project&            m_project;
    audio::AudioEngine& m_engine;
    visual::VisualEngine& m_visual;
    VisualClipHost&     m_host;

    // Per-track launch beat of the currently-playing session visual
    // clip (used by the follow-action poller to decide when barCount
    // bars have elapsed). kNoVisualLaunch = not launched.
    static constexpr double kNoVisualLaunch = -1.0;
    double m_visualLaunchBeat[kMaxTracks] = {};
    int    m_visualLaunchScene[kMaxTracks] = {};
    bool   m_visualLaunchInit = false;

    // Pending quantized visual launches (see launchVisualClipQuantized).
    bool   m_visualHasPending[kMaxTracks] = {};
    int    m_visualPendingScene[kMaxTracks] = {};
    double m_visualPendingFireBeat[kMaxTracks] = {};

    // Arrangement playback tracking — per visual track, the index of
    // the currently-active arrangement clip (-1 = none / in a gap).
    int  m_activeArrVisualClip[kMaxTracks] = {};
    bool m_activeArrInit = false;

    // Last-seen transport stop counter — clear visual layers exactly
    // once per stop-press, including stops fired while stopped.
    uint64_t m_lastSeenStopCounter = 0;

    // Per-(track, knob) last-seen version from VisualKnobBus, so we
    // only act on fresh MIDI CC writes (avoids stomping user drags).
    uint32_t m_visualKnobBusVersions[kMaxTracks][8] = {};

    // Active video imports (transcoding in the background via ffmpeg).
    std::vector<PendingVideoImport> m_pendingImports;
};

} // namespace yawn
