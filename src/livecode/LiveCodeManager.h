#pragma once

// LiveCodeManager — lifecycle owner of the live-coding layer (RUN / STOP /
// reload, per-frame scheduler tick, console, project path resolution).
// Mirrors ControllerManager's wiring style (see docs/live-coding.md §2/§5).
//
// Generation semantics: a successful runScript() creates generation N+1 and
// schedules a swap entry that drops the previous generation's callbacks at
// the next bar boundary (immediately when the transport is stopped). A
// failed run drops only the new generation's entries — the previous
// generation keeps performing (last-good state, §1 G6).

#include "livecode/LiveCodeEngine.h"
#include "livecode/LiveCodeScheduler.h"
#include "livecode/PrerenderManager.h"
#include "app/Project.h"
#include "util/MessageQueue.h"

#include <chrono>
#include <cctype>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace yawn {

class Project;
namespace audio { class AudioEngine; class PrerenderSpec; }
namespace midi { class MidiClip; }

namespace livecode {

class PrerenderManager;

class LiveCodeManager {
public:
    struct ConsoleLine {
        int severity = 0;     // 0=info, 1=warn, 2=error (matches toasts)
        std::string text;
    };

    LiveCodeManager() = default;
    ~LiveCodeManager() { shutdown(); }

    LiveCodeManager(const LiveCodeManager&) = delete;
    LiveCodeManager& operator=(const LiveCodeManager&) = delete;

    void init(audio::AudioEngine* engine, Project* project);
    void shutdown();

    void setToastHandler(std::function<void(const std::string&, float, int)> fn) {
        m_toast = std::move(fn);
    }
    // Test hook: when set, commands go here instead of the engine.
    void setCommandSender(std::function<void(const audio::AudioCommand&)> fn) {
        m_sendCommand = std::move(fn);
    }
    void setProjectPathProvider(std::function<std::filesystem::path()> fn) {
        m_projectPathProvider = std::move(fn);
    }
    // Deterministic wall-clock override (tests). Applied immediately if
    // the scheduler providers are already installed.
    void setWallSecondsProvider(std::function<double()> fn) {
        m_wallSecondsOverride = std::move(fn);
        if (m_providersSet) m_scheduler.setSecondsProvider(m_wallSecondsOverride);
    }

    // RUN — executes the script file as a new generation. Creates a
    // commented template file (and toasts) if the script doesn't exist
    // yet. Returns false on hard failure (I/O error, parse/runtime error
    // is reported but not fatal — the old generation keeps running).
    bool runScript(const std::string& explicitPath = {});

    // RUN the given source string as a new generation (in-app editor:
    // Ctrl+Enter evaluates the buffer without touching the script file).
    // Same generation semantics as runScript.
    bool runScriptSource(const std::string& code);
    // STOP — drop every user generation's scheduled callbacks. State
    // (yawn.state, closures in the Lua state) survives.
    void stop();
    // Reload — stop + recreate the Lua state (yawn.state migrated) + run.
    void reload();
    bool isRunning() const { return m_lua && m_lua->valid(); }
    bool isActive() const { return m_hasActiveGen; }

    // Per-frame tick — fires due scheduler entries. Called from App::update().
    void update();

    // Script path resolution: <projectFolder>/livecode/main.lua when a
    // project folder is set, else ~/.yawn/livecode/main.lua.
    std::string defaultScriptPath() const;

    // ── Project attachment (phase 5, §6.1) ──
    // Scan <projectFolder>/livecode/*.lua into script definitions
    // (preserving autorun flags already recorded in the project).
    std::vector<yawn::Project::LiveCodeScriptDef>
    scanProjectScripts(const std::filesystem::path& projectPath) const;
    // Called by App after a project open: queues autorun scripts.
    void onProjectOpened(const std::filesystem::path& projectPath,
                         Project& project);

    // Console (ring buffer, newest last) for the future console panel.
    const std::deque<ConsoleLine>& console() const { return m_console; }
    void pushConsole(int severity, const std::string& text);
    void showToast(const std::string& msg, float dur = 1.5f, int severity = 0);

    // Test hooks.
    size_t pendingAutorunForTest() const { return m_pendingAutorun.size(); }

    // Status getters for the console UI.
    uint32_t activeGeneration() const { return m_activeGeneration; }
    size_t   pendingSchedules() const { return m_scheduler.pending(); }
    void     clearConsole() { m_console.clear(); }

    void pushCommand(const audio::AudioCommand& cmd);

    // Case-insensitive parameter-name comparison (Lua-side sugar).
    static bool nameEquals(const char* a, const char* b) {
        while (*a && *b) {
            const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(*a++)));
            const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(*b++)));
            if (ca != cb) return false;
        }
        return *a == *b;
    }

    // Accessors for the Lua API C functions.
    audio::AudioEngine* audioEngine() const { return m_audioEngine; }
    Project*            project()     const { return m_project; }
    LiveCodeEngine*     luaEngine()   const { return m_lua.get(); }

    // ── Scheduler entry creation (called from improv.* C functions; the
    //    Lua function to schedule sits on the stack top and gets reffed).
    uint64_t scheduleEvery(double intervalBeats, int repetitions);
    uint64_t scheduleAtBeat(double beat);
    uint64_t scheduleAfterBeats(double delta);
    uint64_t scheduleOnBar();
    uint64_t scheduleAfterSeconds(double seconds);
    // Note-off scheduler entry (generation 0 = system, survives STOP so
    // triggered notes still release).
    void scheduleNoteOff(int track, int pitch, int ch, double when,
                         bool absoluteBeat);
    bool cancelEntry(uint64_t id);
    void clearCurrentGeneration();

    // Late policy for missed windows (scheduler + audio queue together).
    void setLatePolicy(bool drop);
    // Lookahead horizon in seconds (clamped 1 ms .. 0.5 s; default 0.10).
    // Converted to beats per tick using the current tempo.
    void setLookahead(double seconds);
    double lookaheadSeconds() const { return m_lookaheadSec; }

    // ── Scene launch (yawn.launch_scene) ──
    // Called from the script body — defers to AFTER the declarative song
    // apply so the freshly applied clips are the ones that launch.
    void requestLaunchScene(int scene1);
    void requestLaunchSceneOpts(int scene1, const std::string& quantize,
                                bool fromStart);
    // Direct launch mirror of SessionPanel::launchScene (audio + MIDI
    // clips quantized, empty slots stop). Visual slots are reported
    // through the App-provided hook (null → skipped with a warning).
    void setLaunchVisualHook(std::function<void(int, int, const std::string&)> fn) {
        m_launchVisual = std::move(fn);
    }
    void launchSceneNow(int scene1);

    // ── Declarative song layer (phase 2, §5) ──
    // App-provided hooks for the edit paths that need App coordination.
    void setEngineSyncHook(std::function<void()> fn) { m_engineSync = std::move(fn); }
    void setMarkDirtyHook(std::function<void()> fn) { m_markDirty = std::move(fn); }
    void setMidiClipLiveHook(
        std::function<midi::MidiClip*(int, int, std::unique_ptr<midi::MidiClip>)> fn) {
        m_setMidiClipLive = std::move(fn);
    }
    // Reconcile the project to `song` (idempotent; unchanged = no-op).
    bool applySong(const SongModel& song);
    // The set of script-owned track uids (persists across applies).
    const std::set<uint64_t>& ownedTrackUids() const { return m_ownedTrackUids; }

    // ── Prerender (phase 3, §4) ──
    // Enqueue a render job (spec value-copied). Returns job id (>0) or 0.
    uint64_t prerender(audio::PrerenderSpec spec, PrerenderManager::Target target) {
        if (!m_prerenderMgmt) return 0;
        return m_prerenderMgmt->enqueue(std::move(spec), std::move(target));
    }
    bool cancelPrerender(uint64_t id) {
        if (!m_prerenderMgmt) return false;
        m_prerenderMgmt->cancel(id);
        return true;
    }
    int prerenderActive() const {
        return m_prerenderMgmt ? m_prerenderMgmt->activeCount() : 0;
    }
    // Delivery/notification hooks — fed by App (bind m_prerenderHooks
    // once after init).
    PrerenderManager* prerenderManager() { return m_prerenderMgmt.get(); }

    // MIDI clip install — hook when App wired it, project fallback
    // otherwise. Returns true when the clip landed.
    bool setMidiClipLive(int track, int scene,
                         std::unique_ptr<midi::MidiClip> clip);

    // ── Buffer handles (phase 4): script-side audio material ──
    uint64_t loadAudioFileHandle(const std::string& path);
    bool saveAudioBufferHandle(uint64_t handle, const std::string& path,
                               const std::string& format,
                               const std::string& bitDepth);
    bool loadSampleIntoTrack(int track, uint64_t handle,
                             const std::string& kind);

private:
    // Shared tail of runScript/runScriptSource: register the new
    // generation as active, harvest the `song` table, schedule the
    // previous generation's swap-out. On failure drops the new gen's
    // entries and reports (old gen keeps performing).
    bool finishGenerationRun(uint32_t oldGen, uint32_t newGen, bool ok);
    // Scheduler-dispatch: call the reffed Lua function with one numeric arg.
    void dispatchRef(uint64_t id, int ref, double arg);
    // Installs transport/beat providers into the scheduler (once).
    void setupProviders();

    audio::AudioEngine* m_audioEngine = nullptr;
    Project*            m_project = nullptr;
    std::unique_ptr<LiveCodeEngine> m_lua;
    LiveCodeScheduler   m_scheduler;

    uint32_t            m_nextGeneration = 1;   // 0 reserved for note-offs
    uint32_t            m_activeGeneration = 0;
    bool                m_hasActiveGen = false;
    // Generation ids that exist (for stop(): clear each user gen ≥ 1).
    std::vector<uint32_t> m_liveGenerations;

    // Per-entry Lua function refs (unreffed on entry removal).
    std::map<uint64_t, int> m_entryRefs;
    // Consecutive-error counters per entry; ≥3 cancels the entry.
    std::map<uint64_t, int> m_failCounts;

    std::function<void(const std::string&, float, int)> m_toast;
    std::function<void(const audio::AudioCommand&)> m_sendCommand;
    std::function<std::filesystem::path()> m_projectPathProvider;
    std::function<double()> m_wallSecondsOverride;

    // Song layer hooks + ownership (phase 2).
        std::function<void()> m_engineSync;
    std::function<void(int, int, const std::string&)> m_launchVisual;
    int m_pendingLaunchScene = -1;   // deferred yawn.launch_scene (1-based)
    // Launch options for the pending request: quantize mode ("none" |
    // "beat" | "bar", empty = resolve at fire time — none when stopped,
    // bar when playing) + whether to seek to beat 0 first when stopped.
    std::string m_pendingLaunchQuantize;
    bool        m_pendingLaunchFromStart = true;
    std::function<void()> m_markDirty;
    std::function<midi::MidiClip*(int, int, std::unique_ptr<midi::MidiClip>)>
        m_setMidiClipLive;
    std::set<uint64_t> m_ownedTrackUids;

    // Prerender worker pool (phase 3).
    std::unique_ptr<PrerenderManager> m_prerenderMgmt;

    // Buffer handles (phase 4): id → loaded audio material.
    uint64_t m_nextBufferHandle = 1;
    std::map<uint64_t, std::shared_ptr<audio::AudioBuffer>> m_bufferHandles;

    std::deque<ConsoleLine> m_console;
    bool m_providersSet = false;
    double m_lookaheadSec = 0.10;   // schedule-ahead horizon (§3.2)
    // Error-toast rate limit (one toast per second while a callback keeps
    // throwing — the console always records every occurrence).
    std::chrono::steady_clock::time_point m_lastErrorToast{};
    bool m_errorToastPending = false;

    // Autorun queue (phase 5): scripts to run once after a project open.
    std::vector<std::filesystem::path> m_pendingAutorun;
};

} // namespace livecode
} // namespace yawn
