#include "livecode/LiveCodeManager.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"
#include "livecode/LiveCodeSong.h"
#include "midi/MidiClip.h"
#include "util/FileIO.h"
#include "util/Logger.h"
#include "util/MessageQueue.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace yawn {
namespace livecode {

// A callback is auto-cancelled after this many consecutive dispatch errors
// (a throwing recurring callback would otherwise spam the console every
// frame for the rest of the session).
static constexpr int kAutoCancelFailures = 3;

// Template created on the first RUN when no script exists (§5.1 layout;
// phase 0 ships the improv layer only — the song section arrives in
// phase 2 with the diff engine).
static const char* kTemplateScript =
"-- Y.A.W.N live code\n"
"-- Ctrl+L runs this file (as a new generation), Ctrl+Shift+L reloads it\n"
"-- from disk. improv.* callbacks are generation-tagged: re-running\n"
"-- replaces the previous generation at the next bar boundary.\n"
"\n"
"-- Callbacks fire slightly BEFORE their target time (schedule-ahead),\n"
"-- and receive the target beat / bar — pass it to yawn.note's at_beat\n"
"-- for sample-accurate placement.\n"
"\n"
"-- yawn.state survives reloads:\n"
"yawn.state = yawn.state or { counter = 0 }\n"
"\n"
"-- Log a line each bar:\n"
"improv.on_bar(function(bar)\n"
"    yawn.log(\"bar \" .. bar)\n"
"end)\n"
"\n"
"-- Fire a note on track 1 (0-based) each quarter note, on the beat:\n"
"improv.every(1, function(beat)\n"
"    yawn.note(1, 60 + (yawn.state.counter % 12), 110, 0.5, 0, beat)\n"
"    yawn.state.counter = yawn.state.counter + 1\n"
"end)\n";

static double wallNowSeconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Per-entry dispatch control block. The scheduler assigns ids AFTER the
// entry function is built, so the function captures this shared block and
// the id/ref are filled in immediately after insertion — before any fire
// can happen (all scheduler operations run on the UI thread).
struct EntryCtrl {
    uint64_t id = 0;
    int      ref = 0;
};

void LiveCodeManager::init(audio::AudioEngine* engine, Project* project) {
    m_audioEngine = engine;
    m_project = project;

    // Prerender worker pool — delivery/report hooks are wired by App
    // after init via prerenderManager().
    m_prerenderMgmt = std::make_unique<PrerenderManager>();
    m_prerenderMgmt->report = [this](const std::string& msg) {
        const int sev = (msg.find("failed") != std::string::npos ||
                         msg.find("cancelled") != std::string::npos) ? 1 : 0;
        pushConsole(sev, msg);
    };

    // Create the Lua state eagerly — cheap, and keeps the API surface
    // testable before the first RUN.
    m_lua = std::make_unique<LiveCodeEngine>();
    if (!m_lua->init(this))
        m_lua.reset();

    m_scheduler.setRemoveListener([this](uint64_t id) {
        auto it = m_entryRefs.find(id);
        if (it != m_entryRefs.end()) {
            if (m_lua) m_lua->unrefFunction(it->second);
            m_entryRefs.erase(it);
        }
        m_failCounts.erase(id);
    });
}

void LiveCodeManager::shutdown() {
    stop();
    m_scheduler.clearAll();   // fires onRemove (refs released)
    if (m_prerenderMgmt) {
        m_prerenderMgmt->shutdown();
        m_prerenderMgmt.reset();
    }
    m_lua.reset();
    m_liveGenerations.clear();
    m_audioEngine = nullptr;
    m_project = nullptr;
}

void LiveCodeManager::pushCommand(const audio::AudioCommand& cmd) {
    if (m_sendCommand) m_sendCommand(cmd);
    else if (m_audioEngine) m_audioEngine->sendCommand(cmd);
}

void LiveCodeManager::pushConsole(int severity, const std::string& text) {
    ConsoleLine line;
    line.severity = severity;
    line.text = text;
    m_console.push_back(std::move(line));
    if (m_console.size() > 256) m_console.pop_front();
    LOG_INFO("LiveCode", "[%d] %s", severity, text.c_str());
}

void LiveCodeManager::showToast(const std::string& msg, float dur, int severity) {
    if (m_toast) m_toast(msg, dur, severity);
}

std::string LiveCodeManager::defaultScriptPath() const {
    std::filesystem::path proj =
        m_projectPathProvider ? m_projectPathProvider() : std::filesystem::path();
    if (!proj.empty())
        return (proj / "livecode" / "main.lua").string();
    const char* home = std::getenv("HOME");
    if (home && *home)
        return (std::filesystem::path(home) / ".yawn" / "livecode" / "main.lua").string();
    return "livecode/main.lua";
}

// ── Entry creation ───────────────────────────────────────────────────────

void LiveCodeManager::setupProviders() {
    if (m_providersSet || !m_audioEngine) return;
    m_scheduler.setBeatProvider([this] {
        return m_audioEngine ? m_audioEngine->transport().positionInBeats() : 0.0;
    });
    m_scheduler.setPlayingProvider([this] {
        return m_audioEngine && m_audioEngine->transport().isPlaying();
    });
    m_scheduler.setSecondsProvider([this] {
        return m_wallSecondsOverride ? m_wallSecondsOverride() : wallNowSeconds();
    });
    m_scheduler.setBeatsPerBarProvider([this] {
        return m_audioEngine ? std::max(m_audioEngine->transport().beatsPerBar(), 1) : 4;
    });
    m_providersSet = true;
}

uint64_t LiveCodeManager::scheduleEvery(double intervalBeats, int repetitions) {
    if (!m_lua) return 0;
    const int ref = m_lua->refFunction(m_lua->state());
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return 0;

    const uint32_t gen = m_lua->generation();
    auto ctrl = std::make_shared<EntryCtrl>();
    ctrl->ref = ref;
    LiveCodeScheduler::Fn fn = [this, ctrl](double arg) {
        dispatchRef(ctrl->id, ctrl->ref, arg);
    };
    ScheduleHandle h = m_scheduler.every(intervalBeats, gen, std::move(fn), repetitions);
    ctrl->id = h.id;
    m_entryRefs[h.id] = ref;
    return h.id;
}

uint64_t LiveCodeManager::scheduleAtBeat(double beat) {
    if (!m_lua) return 0;
    const int ref = m_lua->refFunction(m_lua->state());
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return 0;

    const uint32_t gen = m_lua->generation();
    auto ctrl = std::make_shared<EntryCtrl>();
    ctrl->ref = ref;
    LiveCodeScheduler::Fn fn = [this, ctrl](double arg) {
        dispatchRef(ctrl->id, ctrl->ref, arg);
    };
    ScheduleHandle h = m_scheduler.atBeat(beat, gen, std::move(fn));
    ctrl->id = h.id;
    m_entryRefs[h.id] = ref;
    return h.id;
}

uint64_t LiveCodeManager::scheduleAfterBeats(double delta) {
    if (!m_lua) return 0;
    const int ref = m_lua->refFunction(m_lua->state());
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return 0;

    const uint32_t gen = m_lua->generation();
    auto ctrl = std::make_shared<EntryCtrl>();
    ctrl->ref = ref;
    LiveCodeScheduler::Fn fn = [this, ctrl](double arg) {
        dispatchRef(ctrl->id, ctrl->ref, arg);
    };
    ScheduleHandle h = m_scheduler.afterBeats(delta, gen, std::move(fn));
    ctrl->id = h.id;
    m_entryRefs[h.id] = ref;
    return h.id;
}

uint64_t LiveCodeManager::scheduleOnBar() {
    if (!m_lua) return 0;
    const int ref = m_lua->refFunction(m_lua->state());
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return 0;

    const uint32_t gen = m_lua->generation();
    auto ctrl = std::make_shared<EntryCtrl>();
    ctrl->ref = ref;
    LiveCodeScheduler::Fn fn = [this, ctrl](double arg) {
        dispatchRef(ctrl->id, ctrl->ref, arg);
    };
    ScheduleHandle h = m_scheduler.onBar(gen, std::move(fn));
    ctrl->id = h.id;
    m_entryRefs[h.id] = ref;
    return h.id;
}

uint64_t LiveCodeManager::scheduleAfterSeconds(double seconds) {
    if (!m_lua) return 0;
    const int ref = m_lua->refFunction(m_lua->state());
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return 0;

    const uint32_t gen = m_lua->generation();
    auto ctrl = std::make_shared<EntryCtrl>();
    ctrl->ref = ref;
    LiveCodeScheduler::Fn fn = [this, ctrl](double arg) {
        dispatchRef(ctrl->id, ctrl->ref, arg);
    };
    ScheduleHandle h = m_scheduler.afterSeconds(seconds, gen, std::move(fn));
    ctrl->id = h.id;
    m_entryRefs[h.id] = ref;
    return h.id;
}

void LiveCodeManager::scheduleNoteOff(int track, int pitch, int ch, double when,
                                      bool absoluteBeat) {
    if (absoluteBeat) {
        // Beat-anchored note-off → audio thread's pending queue
        // (sample-accurate; generation-free, survives STOP).
        pushCommand(audio::SendMidiToTrackMsg{
            track, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff),
            static_cast<uint8_t>(ch), static_cast<uint8_t>(pitch), 0, 0, 0,
            when});
        return;
    }
    // Wall-clock fallback (transport stopped): scheduler entry, generation
    // 0 = system so it survives STOP.
    const audio::SendMidiToTrackMsg msg = {
        track, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff),
        static_cast<uint8_t>(ch), static_cast<uint8_t>(pitch), 0, 0, 0};
    LiveCodeScheduler::Fn fn = [this, msg](double) { pushCommand(msg); };
    m_scheduler.afterSeconds(when, 0, std::move(fn));
}

bool LiveCodeManager::cancelEntry(uint64_t id) {
    return m_scheduler.cancel(id);
}

void LiveCodeManager::clearCurrentGeneration() {
    if (m_hasActiveGen) m_scheduler.clearGeneration(m_activeGeneration);
}

void LiveCodeManager::dispatchRef(uint64_t id, int ref, double arg) {
    if (!m_lua) return;
    if (m_lua->callRef(ref, arg)) {
        m_failCounts.erase(id);
        return;
    }
    int& fails = m_failCounts[id];
    if (++fails >= kAutoCancelFailures) {
        pushConsole(1, "live code: callback disabled after repeated errors");
        cancelEntry(id);
    }
}

// ── Declarative song layer (phase 2) ─────────────────────────────────────

void LiveCodeManager::requestLaunchScene(int scene1) {
    m_pendingLaunchScene = scene1;
}

void LiveCodeManager::requestLaunchSceneOpts(int scene1,
                                             const std::string& quantize,
                                             bool fromStart) {
    m_pendingLaunchScene = scene1;
    m_pendingLaunchQuantize = quantize;
    m_pendingLaunchFromStart = fromStart;
}

void LiveCodeManager::launchSceneNow(int scene1) {
    if (!m_project || scene1 < 1 || scene1 > m_project->numScenes()) return;
    const int scene = scene1 - 1;

    // Resolve the quantize mode at fire time: a stopped transport
    // self-starts in lockstep (clips fire on the first block), a playing
    // transport keeps session-grid semantics (quantize to the next bar)
    // unless the script asked otherwise. Explicit opts override both.
    const bool wasPlaying =
        m_audioEngine && m_audioEngine->transport().isPlaying();
    audio::QuantizeMode q = audio::QuantizeMode::NextBar;
    if (m_pendingLaunchQuantize.empty()) {
        q = wasPlaying ? audio::QuantizeMode::NextBar : audio::QuantizeMode::None;
    } else if (m_pendingLaunchQuantize == "none") {
        q = audio::QuantizeMode::None;
    } else if (m_pendingLaunchQuantize == "beat") {
        q = audio::QuantizeMode::NextBeat;
    } else if (m_pendingLaunchQuantize == "bar") {
        q = audio::QuantizeMode::NextBar;
    } else {
        pushConsole(1, "song: unknown quantize '" + m_pendingLaunchQuantize +
                       "' (none|beat|bar) — using session default");
    }

    // A stopped transport starts the song from the top: seek to beat 0
    // BEFORE the launches so everything lands on the downbeat together
    // (the launch handlers auto-start the transport). The seek also
    // flushes parked at-beat notes inside the engine.
    if (!wasPlaying && m_pendingLaunchFromStart && m_audioEngine)
        pushCommand(audio::TransportSetPositionMsg{0});
    for (int t = 0; t < m_project->numTracks(); ++t) {
        auto* slot = m_project->getSlot(t, scene);
        if (slot && slot->audioClip) {
            pushCommand(audio::LaunchClipMsg{
                t, scene, slot->audioClip.get(), q,
                &slot->clipAutomation->lanes, slot->followAction});
            m_project->track(t).defaultScene = scene;
        } else if (slot && slot->midiClip) {
            pushCommand(audio::LaunchMidiClipMsg{
                t, scene, slot->midiClip.get(), q,
                &slot->clipAutomation->lanes, slot->followAction});
            m_project->track(t).defaultScene = scene;
        } else if (slot && slot->visualClip) {
            if (m_launchVisual)
                m_launchVisual(t, scene, slot->visualClip->firstShaderPath());
            m_project->track(t).defaultScene = scene;
        } else {
            // Empty slots stop whatever plays on that track (audio+MIDI)
            // in the same quantize envelope as the launches.
            if (m_project->track(t).defaultScene >= 0) {
                pushCommand(audio::StopClipMsg{t, q});
                pushCommand(audio::StopMidiClipMsg{t, q});
            }
            m_project->track(t).defaultScene = -1;
        }
    }
    // Reset the engines' quantize bookkeeping when self-starting: the
    // stale last-boundary index from the previous playback would else
    // hold the launch for a bar even in None mode's neighborhood.
    if (!wasPlaying && m_audioEngine) {
        m_audioEngine->resetClipQuantizeChecks();
    }
    pushConsole(0, "song: launching scene " + std::to_string(scene1) +
                   (q == audio::QuantizeMode::None ? " (now)" : " (quantized)"));
}

bool LiveCodeManager::applySong(const SongModel& song) {
    SongApplyContext ctx;
    ctx.project = m_project;
    ctx.engine = m_audioEngine;
    ctx.engineSync = m_engineSync;
    ctx.markDirty = m_markDirty;
    ctx.setMidiClipLive = m_setMidiClipLive;

    const SongApplyReport rep = applySongModel(song, m_ownedTrackUids, ctx);
    for (const auto& op : rep.ops) pushConsole(0, "song: " + op);
    for (const auto& w : rep.warnings) pushConsole(1, "song: " + w);
    if (rep.changed) {
        showToast("Song applied: " + std::to_string(rep.ops.size()) + " edit(s)",
                  2.0f, 0);
        // Song applies bypass undo in v1 (§5.2) — the code is the undo
        // mechanism; note it once so the behaviour is discoverable.
        pushConsole(0, "song: edits bypass undo (code is the source of truth)");
    }
    return true;
}

// ── Project attachment (phase 5) ─────────────────────────────────────────

std::vector<yawn::Project::LiveCodeScriptDef>
LiveCodeManager::scanProjectScripts(const std::filesystem::path& projectPath) const {
    std::vector<yawn::Project::LiveCodeScriptDef> out;
    std::error_code ec;
    if (projectPath.empty()) return out;
    const auto dir = projectPath / "livecode";
    if (!std::filesystem::is_directory(dir, ec)) return out;

    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".lua") continue;
        yawn::Project::LiveCodeScriptDef def;
        def.name = entry.path().stem().string();
        def.path = "livecode/" + entry.path().filename().string();
        // Preserve a previously recorded autorun flag for the same path.
        for (const auto& existing : m_project ? m_project->liveCodeScripts()
                                              : std::vector<yawn::Project::LiveCodeScriptDef>{})
            if (existing.path == def.path) def.autorun = existing.autorun;
        out.push_back(std::move(def));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

void LiveCodeManager::onProjectOpened(const std::filesystem::path& projectPath,
                                      Project& project) {
    m_pendingAutorun.clear();
    for (const auto& def : project.liveCodeScripts()) {
        if (!def.autorun) continue;
        const auto path = projectPath / def.path;
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            m_pendingAutorun.push_back(path);
        else
            pushConsole(1, "live code: autorun script missing: " + def.path);
    }
    if (!m_pendingAutorun.empty())
        pushConsole(0, "live code: " + std::to_string(m_pendingAutorun.size()) +
                       " script(s) queued for autorun");
}

// ── MIDI clip install + buffer handles (phase 4) ─────────────────────────

bool LiveCodeManager::setMidiClipLive(int track, int scene,
                                      std::unique_ptr<midi::MidiClip> clip) {
    if (!clip || !m_project) return false;
    if (track < 0 || track >= m_project->numTracks() ||
        scene < 0 || scene >= m_project->numScenes()) return false;
    midi::MidiClip* np = nullptr;
    if (m_setMidiClipLive) {
        np = m_setMidiClipLive(track, scene, std::move(clip));
    } else {
        np = m_project->setMidiClip(track, scene, std::move(clip));
    }
    if (m_markDirty) m_markDirty();
    return np != nullptr;
}

uint64_t LiveCodeManager::loadAudioFileHandle(const std::string& path) {
    if (path.empty()) { pushConsole(1, "audio: empty path"); return 0; }
    util::AudioFileInfo info;
    auto buf = util::loadAudioFile(path, &info);
    if (!buf) { pushConsole(1, "audio: cannot load " + path); return 0; }
    // Clips/buffers must match the engine rate (same rule as file import).
    if (m_audioEngine && info.sampleRate > 0) {
        const double dst = m_audioEngine->sampleRate();
        if (std::abs(static_cast<double>(info.sampleRate) - dst) > 0.5) {
            auto resampled = util::resampleBuffer(*buf, info.sampleRate, dst);
            if (resampled) buf = resampled;
        }
    }
    const uint64_t id = m_nextBufferHandle++;
    m_bufferHandles[id] = std::move(buf);
    return id;
}

bool LiveCodeManager::saveAudioBufferHandle(uint64_t handle, const std::string& path,
                                            const std::string& format,
                                            const std::string& bitDepth) {
    auto it = m_bufferHandles.find(handle);
    if (it == m_bufferHandles.end() || !it->second) {
        pushConsole(1, "audio: unknown handle " + std::to_string(handle));
        return false;
    }
    util::ExportFormat fmt = util::ExportFormat::WAV;
    if (format == "flac" || format == "FLAC") fmt = util::ExportFormat::FLAC;
    else if (format == "ogg" || format == "OGG") fmt = util::ExportFormat::OGG;
    util::BitDepth depth = util::BitDepth::Float32;
    if (bitDepth == "i16" || bitDepth == "int16") depth = util::BitDepth::Int16;
    else if (bitDepth == "i24" || bitDepth == "int24") depth = util::BitDepth::Int24;
    const bool ok = util::saveAudioBuffer(path, *it->second,
        m_audioEngine ? static_cast<int>(m_audioEngine->sampleRate()) : 48000,
        fmt, depth);
    if (!ok) pushConsole(1, "audio: save failed " + path);
    return ok;
}

bool LiveCodeManager::loadSampleIntoTrack(int track, uint64_t handle,
                                          const std::string& kind) {
    auto it = m_bufferHandles.find(handle);
    if (it == m_bufferHandles.end() || !it->second) {
        pushConsole(1, "audio: unknown handle " + std::to_string(handle));
        return false;
    }
    auto* pm = m_prerenderMgmt.get();
    if (!pm) return false;
    const std::string name = "sample_" + std::to_string(handle);
    bool ok = false;
    if (kind == "sampler" || kind.empty()) {
        if (pm->deliverSampler) ok = pm->deliverSampler(it->second, name, track);
    } else if (kind == "granular") {
        if (pm->deliverGranular) ok = pm->deliverGranular(it->second, name, track);
    } else if (kind == "drumslop") {
        if (pm->deliverDrumSlop) ok = pm->deliverDrumSlop(it->second, name, track);
    } else if (kind == "vocoder") {
        if (pm->deliverVocoder) ok = pm->deliverVocoder(it->second, name, track);
    } else {
        pushConsole(1, "audio: unknown sample kind '" + kind + "'");
    }
    if (!ok) pushConsole(1, "audio: track " + std::to_string(track) +
                           " has no matching " + (kind.empty() ? "sampler" : kind) + " instrument");
    return ok;
}

// ── Programmatic buffer handles (LiveCodeBuffers) ────────────────────────

uint64_t LiveCodeManager::newBufferHandle(std::shared_ptr<audio::AudioBuffer> buf) {
    if (!buf) return 0;
    const uint64_t id = m_nextBufferHandle++;
    m_bufferHandles[id] = std::move(buf);
    return id;
}

std::shared_ptr<audio::AudioBuffer>
LiveCodeManager::bufferHandle(uint64_t id) const {
    auto it = m_bufferHandles.find(id);
    return it == m_bufferHandles.end() ? nullptr : it->second;
}

bool LiveCodeManager::freeBufferHandle(uint64_t id) {
    return m_bufferHandles.erase(id) > 0;
}

// ── RUN / STOP / RELOAD / UPDATE ─────────────────────────────────────────

bool LiveCodeManager::runScript(const std::string& explicitPath) {
    if (!m_audioEngine) {
        pushConsole(2, "live code: engine not ready");
        return false;
    }
    const std::string path = explicitPath.empty() ? defaultScriptPath() : explicitPath;

    // First run with no script file → create the commented template.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) {
            pushConsole(2, "live code: cannot create " + path);
            showToast("Live code: cannot create script file", 2.5f, 2);
            return false;
        }
        f << kTemplateScript;
        pushConsole(0, "live code: created " + path + " — edit it, then run again");
        showToast("Created live code script — edit it, then Ctrl+L", 3.0f, 0);
        return true;
    }

    if (!m_lua) {
        m_lua = std::make_unique<LiveCodeEngine>();
        if (!m_lua->init(this)) {
            m_lua.reset();
            return false;
        }
    }
    setupProviders();

    const uint32_t oldGen = m_hasActiveGen ? m_activeGeneration : 0;
    const uint32_t newGen = m_nextGeneration++;
    m_lua->setGeneration(newGen);
    return finishGenerationRun(oldGen, newGen, m_lua->runFile(path));
}

bool LiveCodeManager::runScriptSource(const std::string& code) {
    if (!m_audioEngine) {
        pushConsole(2, "live code: engine not ready");
        return false;
    }
    if (!m_lua) {
        m_lua = std::make_unique<LiveCodeEngine>();
        if (!m_lua->init(this)) {
            m_lua.reset();
            return false;
        }
    }
    setupProviders();

    const uint32_t oldGen = m_hasActiveGen ? m_activeGeneration : 0;
    const uint32_t newGen = m_nextGeneration++;
    m_lua->setGeneration(newGen);
    return finishGenerationRun(oldGen, newGen, m_lua->runString(code));
}

bool LiveCodeManager::finishGenerationRun(uint32_t oldGen, uint32_t newGen, bool ok) {
    if (!ok) {
        // Drop the new generation's entries; the previous generation keeps
        // performing (§1 G6).
        m_scheduler.clearGeneration(newGen);
        m_pendingLaunchScene = -1;   // never launch a failed run
        pushConsole(1, "live code: run failed — previous generation continues");
        showToast("Live code error (see log)", 2.5f, 2);
        return false;
    }

    m_activeGeneration = newGen;
    m_hasActiveGen = true;
    if (std::find(m_liveGenerations.begin(), m_liveGenerations.end(), newGen)
            == m_liveGenerations.end())
        m_liveGenerations.push_back(newGen);

    // ── Declarative song layer (§5): harvest + reconcile after the body
    // ran (the script's `song` table is now defined).
    {
        SongModel song;
        std::string perr;
        if (m_lua->harvestSong(song, perr)) {
            if (!song.tracks.empty() || song.bpm || song.scenes)
                applySong(song);
        } else {
            pushConsole(2, "live code: song parse error — " + perr);
            showToast("Live code: song parse error (see log)", 2.5f, 2);
        }
    }

    // Deferred scene launch (yawn.launch_scene from the script body) —
    // runs after the song apply so the newest clips are what launches.
    if (m_pendingLaunchScene > 0) {
        const int scene1 = m_pendingLaunchScene;
        m_pendingLaunchScene = -1;
        launchSceneNow(scene1);
    }

    // Swap out the previous generation at the next bar boundary (§3.3) —
    // immediately when the transport is stopped.
    if (oldGen != 0 && oldGen != newGen) {
        auto clearOld = [this, oldGen](double) {
            m_scheduler.clearGeneration(oldGen);
            auto& gens = m_liveGenerations;
            gens.erase(std::remove(gens.begin(), gens.end(), oldGen), gens.end());
        };
        const auto& t = m_audioEngine->transport();
        if (t.isPlaying()) {
            const int bpb = std::max(t.beatsPerBar(), 1);
            const double nextBar =
                (std::floor(t.positionInBeats() / bpb) + 1.0) * bpb;
            m_scheduler.atBeat(nextBar, newGen, clearOld);
        } else {
            clearOld(0.0);
        }
    }

    pushConsole(0, "live code: generation " + std::to_string(newGen) + " running");
    showToast("Live code running (gen " + std::to_string(newGen) + ")", 1.5f, 0);
    return true;
}

void LiveCodeManager::stop() {
    for (uint32_t g : m_liveGenerations)
        m_scheduler.clearGeneration(g);
    m_liveGenerations.clear();
    m_hasActiveGen = false;
    m_activeGeneration = 0;
    // Generation-0 entries (already-triggered notes' offs) keep firing.
}

void LiveCodeManager::reload() {
    LiveCodeEngine::StateMap migrated;
    if (m_lua) migrated = m_lua->harvestState();
    stop();
    m_scheduler.clearAll();   // gen-0 note-offs too — the state is recreated
    if (m_lua) {
        m_lua->shutdown();
        m_lua.reset();
    }
    m_lua = std::make_unique<LiveCodeEngine>();
    if (!m_lua->init(this)) {
        m_lua.reset();
        return;
    }
    m_lua->injectState(migrated);
    runScript();
}

void LiveCodeManager::update() {
    // Convert the lookahead horizon to beats for the current tempo.
    if (m_audioEngine) {
        const double bpm = m_audioEngine->transport().bpm();
        m_scheduler.setLookaheadBeats(m_lookaheadSec * bpm / 60.0);
    }
    // Autorun scripts queued by onProjectOpened — one per frame so a
    // batch of scripts doesn't stall a single frame.
    if (!m_pendingAutorun.empty()) {
        const std::filesystem::path path = m_pendingAutorun.front();
        m_pendingAutorun.erase(m_pendingAutorun.begin());
        runScript(path.string());
    }
    m_scheduler.tick();
    // Deliver completed prerender jobs (delivery hooks are App-provided).
    if (m_prerenderMgmt) m_prerenderMgmt->poll();
}

void LiveCodeManager::setLatePolicy(bool drop) {
    m_scheduler.setLateDrop(drop);
    pushCommand(audio::SetSchedLatePolicyMsg{drop});
}

void LiveCodeManager::setLookahead(double seconds) {
    m_lookaheadSec = std::clamp(seconds, 0.001, 0.5);
}

} // namespace livecode
} // namespace yawn
