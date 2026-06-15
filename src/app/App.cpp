#include "app/App.h"
#include "Version.h"
#include "visual/LiveInputEnum.h"
#include "visual/VisualModBus.h"
#include "transcribe/AudioToMidi.h"
#include "ui/framework/v2/Fw2Painters.h"
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/Dialog.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Theme.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "instruments/KarplusStrong.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "instruments/Multisampler.h"
#include "instruments/InstrumentRack.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Limiter.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Phaser.h"
#include "effects/Wah.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"
#include "effects/Tuner.h"
#include "effects/BeatRepeat.h"
#include "effects/BufferRepeat.h"
#include "effects/Resampler.h"
#include "effects/ClockDrift.h"
#include "effects/CDError.h"
#include "effects/AutoPanner.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include "midi/LFO.h"
#include "util/ProjectSerializer.h"
#include "util/Logger.h"
#include "util/FileIO.h"
#include "util/MidiFileIO.h"
#include "audio/OfflineRenderer.h"
#include "visual/VideoImporter.h"   // visual::runFFmpeg
#include "stb_image_write.h"
#include "presets/PresetGenerator.h"
#include "presets/MidiLoopManager.h"
#include "presets/DrumPatterns.h"
#include "presets/MelodicPatterns.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cinttypes>
#include <cstring>
#include <thread>

namespace yawn {

// ---------------------------------------------------------------------------

// Insert a new empty scene just below the currently selected one
// (Ableton convention). Shared by the Scene menu item and the
// Ctrl+I keyboard shortcut.
void App::insertSceneAtSelection() {
    const int insertAt = std::clamp(m_selectedScene + 1,
                                     0, m_project.numScenes());
    sceneInsert(insertAt);
    markDirty();
    m_undoManager.push({"Insert Scene",
        [this, insertAt]{ sceneDelete(insertAt); markDirty(); },
        [this, insertAt]{ sceneInsert(insertAt); markDirty(); },
        ""});
    // Move the selection to the new scene so repeated Ctrl+I keeps
    // stacking fresh scenes below.
    m_selectedScene = insertAt;
}

// Stop every launched clip immediately so the audio engine drops the
// per-slot pointers (ClipPlayState::clip / clipAutomation) it caches from
// LaunchClipMsg/LaunchMidiClipMsg. MUST run before any structural scene
// edit: Project::insertScene/deleteScene/duplicateScene reallocate the
// per-track clip-slot vectors, moving every ClipSlot and dangling
// &slot->clipAutomation — which AutomationEngine::process dereferences on
// the audio thread every buffer (→ SIGSEGV). QuantizeMode::None makes the
// stop take effect on the engine's next callback rather than the next bar.
void App::stopAllClipsForSceneEdit() {
    for (int t = 0; t < m_project.numTracks(); ++t) {
        m_audioEngine.sendCommand(
            audio::StopClipMsg{t, audio::QuantizeMode::None});
        m_audioEngine.sendCommand(
            audio::StopMidiClipMsg{t, audio::QuantizeMode::None});
    }
}

void App::sceneInsert(int index) {
    stopAllClipsForSceneEdit();
    m_project.insertScene(index);
}

void App::sceneDelete(int index) {
    stopAllClipsForSceneEdit();
    m_project.deleteScene(index);
}

void App::sceneDuplicate(int index) {
    stopAllClipsForSceneEdit();
    m_project.duplicateScene(index);
}

// Starter track mix for fresh / new projects. Leaves tracks created by
// Project::init() in place but renames + retypes the first few to
// "Audio 1/2 | MIDI 1/2 | Visual 1". MIDI slots get a stock
// SubtractiveSynth so they're audible out of the box. Engine sync is
// the caller's responsibility (syncTracksToEngine / startup flow).
void App::resetEngineState() {
    // Drop any cached pointers in the detail panel BEFORE we tear
    // down the instruments / effect chains they reference. The panel
    // caches raw pointers (m_lastInst, m_drumRackInst, m_lastFxChain)
    // for its tick-time fingerprint; without this clear, the next
    // tick after a project load would dereference freed instrument
    // memory in dynamic_cast and crash with a Windows EH exception.
    if (m_detailPanel) m_detailPanel->clear();

    for (int t = 0; t < kMaxTracks; ++t) {
        m_audioEngine.setInstrument(t, nullptr);
        m_audioEngine.midiEffectChain(t).clear();
        m_audioEngine.mixer().trackEffects(t).clear();
        m_audioEngine.mixer().setTrackVolume(t, 1.0f);
        m_audioEngine.mixer().setTrackPan(t, 0.0f);
        m_audioEngine.mixer().setTrackMute(t, false);
        m_audioEngine.mixer().setTrackSolo(t, false);
    }
    for (int r = 0; r < kMaxReturnBuses; ++r)
        m_audioEngine.mixer().returnEffects(r).clear();
    m_audioEngine.mixer().masterEffects().clear();
    m_audioEngine.mixer().setMasterVolume(1.0f);
    // Reset tempo synchronously, NOT via the command queue. On the load
    // path resetEngineState() runs before the project loader, which sets
    // BPM with a direct transport().setBPM(). A queued reset would be
    // drained by the audio thread AFTER that direct write and clobber the
    // loaded tempo back to 120 (the "project BPM doesn't load" bug). A
    // direct write here preserves ordering and matches every other reset
    // in this function.
    m_audioEngine.transport().setBPM(120.0);
}

void App::setupDefaultTracks() {
    struct Default { const char* name; Track::Type type; };
    static constexpr Default defaults[] = {
        {"Audio 1",  Track::Type::Audio},
        {"Audio 2",  Track::Type::Audio},
        {"MIDI 1",   Track::Type::Midi},
        {"MIDI 2",   Track::Type::Midi},
        {"Visual 1", Track::Type::Visual},
    };
    const int n = std::min(m_project.numTracks(),
                           static_cast<int>(std::size(defaults)));
    for (int i = 0; i < n; ++i) {
        m_project.track(i).name = defaults[i].name;
        m_project.track(i).type = defaults[i].type;
        if (defaults[i].type == Track::Type::Midi)
            m_audioEngine.setInstrument(i,
                std::make_unique<instruments::SubtractiveSynth>());
    }
}

void App::newProject() {
    LOG_INFO("User", "newProject");
    auto doNew = [this]() {
        m_audioEngine.sendCommand(audio::TransportStopMsg{});

        // Reset project to defaults
        m_project = Project();
        m_project.init();

        resetEngineState();

        setupDefaultTracks();
        syncTracksToEngine();

        m_projectPath.clear();
        m_projectDirty = false;
        // No project open → preset saves go to the global library only.
        PresetManager::setProjectRoot({});
        m_undoManager.clear();
        m_midiLearnManager.clearAll();
        m_selectedTrack = 0;
        m_detailTarget = DetailTarget::Track;
        m_detailReturnBus = -1;
        m_showDetailPanel = false;
        m_pianoRoll->close();
        updateWindowTitle();
        LOG_INFO("Project", "New project created");
    };

    if (m_projectDirty) {
        ui::fw2::ConfirmDialog::prompt("Save changes before creating a new project?",
            [this, doNew]() {
                if (!m_projectPath.empty()) {
                    doSaveProject(m_projectPath);
                } else {
                    saveProjectAs();
                }
                doNew();
            });
    } else {
        doNew();
    }
}

void App::openProject() {
    LOG_INFO("User", "openProject (showing folder dialog)");
    auto doOpen = [this]() {
        SDL_ShowOpenFolderDialog(onOpenFolderResult, this,
                                m_mainWindow.getHandle(), nullptr, false);
    };

    if (m_projectDirty) {
        ui::fw2::ConfirmDialog::prompt("Save changes before opening another project?",
            [this, doOpen]() {
                if (!m_projectPath.empty()) {
                    doSaveProject(m_projectPath);
                }
                doOpen();
            });
    } else {
        doOpen();
    }
}

void App::saveProject() {
    LOG_INFO("User", "saveProject path='%s'", m_projectPath.string().c_str());
    if (m_projectPath.empty()) {
        saveProjectAs();
        return;
    }
    doSaveProject(m_projectPath);
}

void App::saveProjectAs() {
    LOG_INFO("User", "saveProjectAs");
    SDL_ShowSaveFileDialog(onSaveFolderResult, this,
                           m_mainWindow.getHandle(), nullptr, 0, "Untitled.yawn");
}

void App::doSaveProject(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    fs::path projectDir = path;
    // Ensure .yawn extension
    if (projectDir.extension() != ".yawn")
        projectDir += ".yawn";

    if (ProjectSerializer::saveToFolder(projectDir, m_project, m_audioEngine, &m_midiLearnManager, &m_visualEngine)) {
        m_projectPath = projectDir;
        m_projectDirty = false;
        // Subsequent preset saves now mirror into <project>.yawn/presets/.
        PresetManager::setProjectRoot(m_projectPath);
        updateWindowTitle();
        LOG_INFO("Project", "Saved to: %s", projectDir.string().c_str());
        m_toastManager.show("Saved: " + projectDir.filename().string());
    } else {
        LOG_ERROR("Project", "Failed to save: %s", projectDir.string().c_str());
        m_toastManager.show("Save failed", 2.5f, ui::ToastManager::Severity::Error);
    }
}

void App::doOpenProject(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    fs::path projectDir = path;

    // Check for project.json inside the folder
    if (!fs::exists(projectDir / "project.json")) {
        LOG_ERROR("Project", "Not a valid project folder: %s",
                     projectDir.string().c_str());
        return;
    }

    m_audioEngine.sendCommand(audio::TransportStopMsg{});

    resetEngineState();

    Project loadedProject;
    if (ProjectSerializer::loadFromFolder(projectDir, loadedProject, m_audioEngine, &m_midiLearnManager, &m_visualEngine)) {
        m_project = std::move(loadedProject);
        // Defensive cleanup: a corrupt or maliciously crafted project
        // file could carry a cyclic sidechain graph that the engine
        // would refuse anyway, but having Project state inconsistent
        // with engine state is its own bug. Repair before syncing so
        // the engine sees a clean DAG.
        const int repaired = m_project.repairSidechainCycles();
        if (repaired > 0) {
            LOG_WARN("Sidechain",
                     "Project had %d sidechain cycle(s); cleared to -1",
                     repaired);
        }
        syncTracksToEngine();
        // Re-read any IR files referenced by ConvolutionReverb
        // effects in this project. ProjectSerializer's extra-state
        // hook preserves the file PATH (cheap, portable across
        // machines) but doesn't read the file itself — we do that
        // here on the UI thread, after the engine is wired up but
        // before audio starts touching the (currently empty) IR
        // engines.
        rehydrateConvolutionIRs();
        m_projectPath = projectDir;
        m_projectDirty = false;
        // Project-local preset folder (if any) is now visible to the
        // device Preset menu via PresetManager::listPresetsForDevice.
        PresetManager::setProjectRoot(m_projectPath);
        m_undoManager.clear();
        m_selectedTrack = 0;
        m_detailTarget = DetailTarget::Track;
        m_detailReturnBus = -1;
        m_showDetailPanel = false;
        m_pianoRoll->close();
        // Sync the UI to the restored view mode. ProjectSerializer only
        // sets the Project::viewMode() flag on load; without this call the
        // UI keeps whatever view was active before (typically Session),
        // leaving the flag and the visible panels inconsistent. That
        // mismatch silently broke view-gated input — e.g. the track-header
        // right-click menu (gated on viewMode()==Session) stopped working
        // after loading an Arrangement-saved project. switchToView swaps
        // the panels unconditionally, so it reliably reconciles both.
        switchToView(m_project.viewMode());
        updateWindowTitle();
        LOG_INFO("Project", "Loaded: %s", projectDir.string().c_str());
        m_toastManager.show("Loaded: " + projectDir.filename().string());
    } else {
        LOG_ERROR("Project", "Failed to load: %s",
                     projectDir.string().c_str());
        m_toastManager.show("Load failed", 2.5f, ui::ToastManager::Severity::Error);
    }
}

bool App::loadIRIntoConvReverb(effects::ConvolutionReverb* eff,
                                 const std::string& path) {
    if (!eff || path.empty()) return false;
    util::AudioFileInfo info;
    auto buf = util::loadAudioFile(path, &info);
    if (!buf || buf->numFrames() <= 0) {
        LOG_WARN("ConvReverb", "Failed to load IR: %s", path.c_str());
        return false;
    }
    // Resample to host rate if the IR was captured at a different
    // sample rate. Without this a 44.1 kHz IR on a 48 kHz host
    // plays ~9 % low and short; on the project-load path that
    // would silently corrupt restored sessions.
    const double hostRate = m_audioEngine.sampleRate();
    std::shared_ptr<audio::AudioBuffer> ir = buf;
    if (info.sampleRate > 0 &&
        static_cast<double>(info.sampleRate) != hostRate) {
        ir = util::resampleBuffer(*buf,
            static_cast<double>(info.sampleRate), hostRate);
    }
    // Mono-sum (channel-major non-interleaved layout).
    const int frames = ir->numFrames();
    const int ch     = ir->numChannels();
    std::vector<float> mono(frames, 0.0f);
    if (ch == 1) {
        std::memcpy(mono.data(), ir->channelData(0),
                     frames * sizeof(float));
    } else if (ch > 1) {
        const float invCh = 1.0f / static_cast<float>(ch);
        for (int c = 0; c < ch; ++c) {
            const float* src = ir->channelData(c);
            for (int i = 0; i < frames; ++i)
                mono[i] += src[i] * invCh;
        }
    }
    eff->loadIRMono(mono.data(),
                     static_cast<int>(mono.size()),
                     hostRate);
    eff->setIRPath(path);
    LOG_INFO("ConvReverb",
             "Loaded IR: %s (%d frames, %d ch, %d Hz src)",
             path.c_str(), frames, ch, info.sampleRate);
    return true;
}

void App::rehydrateConvolutionIRs() {
    // Walk every effect chain in the mixer (per-track + return
    // buses + master) and reload any ConvolutionReverb whose IR
    // path was preserved in extraState but whose engine is empty
    // (i.e. the file hasn't been re-read yet). Same loop also
    // catches NeuralAmp .nam paths — both effects use the
    // saveExtraState path-storage pattern; NAM's model load is
    // self-contained inside setModelPath so we don't need a
    // separate App-side helper for it.
    auto walk = [this](effects::EffectChain& chain) {
        for (int i = 0; i < chain.count(); ++i) {
            auto* fx = chain.effectAt(i);
            if (!fx) continue;
            const std::string fid = fx->id();
            if (fid == "convreverb") {
                auto* cr = static_cast<effects::ConvolutionReverb*>(fx);
                if (cr->hasIR()) continue;
                const std::string p = cr->irPath();
                if (p.empty()) continue;
                loadIRIntoConvReverb(cr, p);
            } else if (fid == "neuralamp") {
                auto* na = static_cast<effects::NeuralAmp*>(fx);
                if (na->hasModel()) continue;
                const std::string p = na->modelPath();
                if (p.empty()) continue;
                // NeuralAmp::setModelPath does the load + Reset +
                // prewarm internally. setModelPath is idempotent
                // for the path itself; the modelPath field already
                // got restored via loadExtraState, so we set it
                // again to trigger the actual file read.
                na->setModelPath(p);
            }
        }
    };
    auto& mixer = m_audioEngine.mixer();
    for (int t = 0; t < kMaxTracks; ++t) walk(mixer.trackEffects(t));
    for (int r = 0; r < kMaxReturnBuses; ++r) walk(mixer.returnEffects(r));
    walk(mixer.masterEffects());
}

void App::syncTracksToEngine() {
    for (int i = 0; i < m_project.numTracks(); ++i) {
        const auto& trk = m_project.track(i);
        uint8_t type = (trk.type == Track::Type::Audio)  ? 0
                      : (trk.type == Track::Type::Midi)   ? 1
                                                           : 2;  // Visual
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{i, type});
        m_audioEngine.sendCommand(audio::SetTrackArmedMsg{i, trk.armed});
        m_audioEngine.sendCommand(audio::SetTrackMonitorMsg{i, static_cast<uint8_t>(trk.monitorMode)});
        m_audioEngine.sendCommand(audio::SetTrackAudioInputChMsg{i, trk.audioInputCh});
        m_audioEngine.sendCommand(audio::SetTrackMonoMsg{i, trk.mono});
        m_audioEngine.sendCommand(audio::SetSidechainSourceMsg{i, trk.sidechainSource});
        m_audioEngine.sendCommand(audio::SetResampleSourceMsg{i, trk.resampleSource});
        m_audioEngine.sendCommand(audio::SetTrackVolumeMsg{i, trk.volume});
        m_audioEngine.sendCommand(audio::SetTrackMuteMsg{i, trk.muted});
        m_audioEngine.sendCommand(audio::SetTrackSoloMsg{i, trk.soloed});
        if (trk.midiOutputPort >= 0)
            m_audioEngine.sendCommand(audio::SetTrackMidiOutputMsg{i, trk.midiOutputPort, trk.midiOutputChannel});
    }
}

void App::updateWindowTitle() {
    std::string title = "Y.A.W.N";
    if (!m_projectPath.empty()) {
        title += " - " + m_projectPath.stem().string();
    } else {
        title += " - Untitled";
    }
    if (m_projectDirty) title += " *";
    SDL_SetWindowTitle(m_mainWindow.getHandle(), title.c_str());
}

void App::switchToView(ViewMode mode) {
    m_project.setViewMode(mode);
    bool showArrangement = (mode == ViewMode::Arrangement);
    // ContentGrid and ArrangementPanel are both fw2 — swap the topLeft
    // slot between the two panels directly.
    m_sessionPanel->setVisible(!showArrangement);
    m_arrangementPanel->setVisible(showArrangement);
    m_contentGrid->setTopLeft(showArrangement
        ? static_cast<ui::fw2::Widget*>(m_arrangementPanel)
        : static_cast<ui::fw2::Widget*>(m_sessionPanel));
    m_arrangementPanel->setSelectedTrack(m_selectedTrack);

    // Activate/deactivate arrangement mode for all tracks
    for (int t = 0; t < m_project.numTracks(); ++t) {
        bool active = showArrangement && !m_project.track(t).arrangementClips.empty();
        m_project.track(t).arrangementActive = active;
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{t, active});
        if (active) syncArrangementClipsToEngine(t);
    }
}

void App::syncArrangementClipsToEngine(int trackIdx) {
    auto& track = m_project.track(trackIdx);
    std::vector<audio::ArrClipRef> refs;
    refs.reserve(track.arrangementClips.size());
    for (auto& ac : track.arrangementClips) {
        audio::ArrClipRef ref;
        ref.type = ac.type == ArrangementClip::Type::Audio
            ? audio::ArrClipRef::Type::Audio : audio::ArrClipRef::Type::Midi;
        ref.startBeat = ac.startBeat;
        ref.lengthBeats = ac.lengthBeats;
        ref.offsetBeats = ac.offsetBeats;
        ref.loop = ac.loop;
        ref.stretch = ac.stretch;
        ref.audioBuffer = ac.audioBuffer;
        ref.midiClip = ac.midiClip;
        refs.push_back(std::move(ref));
    }
    m_audioEngine.arrangementPlayback().submitTrackClips(trackIdx, std::move(refs));
}

// Drop a track back to a clean session-controlled state: empty its
// arrangement lane and stop any arrangement-driven playback. Used by the
// "Clear Arrangement" menu items; undoable.
void App::clearTrackArrangement(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= m_project.numTracks()) return;
    auto& track = m_project.track(trackIdx);
    if (track.arrangementClips.empty() && !track.arrangementActive) return;

    // Snapshot for undo — ArrangementClip is copyable (visual clips deep-clone).
    auto oldClips = track.arrangementClips;
    const bool wasActive = track.arrangementActive;

    auto doClear = [this, trackIdx]() {
        auto& t = m_project.track(trackIdx);
        t.arrangementClips.clear();
        t.arrangementActive = false;   // back to session control
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{trackIdx, false});
        syncArrangementClipsToEngine(trackIdx);
        if (t.type == Track::Type::Visual) {
            m_visualEngine.clearLayer(trackIdx);
            m_activeArrVisualClip[trackIdx] = -1;
        }
        m_project.updateArrangementLength();
        markDirty();
    };
    doClear();

    m_undoManager.push({"Clear Arrangement",
        [this, trackIdx, oldClips, wasActive]() {
            auto& t = m_project.track(trackIdx);
            t.arrangementClips = oldClips;
            t.arrangementActive = wasActive;
            m_audioEngine.sendCommand(
                audio::SetTrackArrActiveMsg{trackIdx, wasActive});
            if (wasActive) syncArrangementClipsToEngine(trackIdx);
            m_project.updateArrangementLength();
            markDirty();
        },
        doClear});
}

// Clear every track's arrangement in one undoable step.
void App::clearAllArrangements() {
    const int n = m_project.numTracks();
    std::vector<std::pair<std::vector<ArrangementClip>, bool>> snapshot(n);
    bool anything = false;
    for (int t = 0; t < n; ++t) {
        auto& tr = m_project.track(t);
        snapshot[t] = {tr.arrangementClips, tr.arrangementActive};
        if (!tr.arrangementClips.empty() || tr.arrangementActive) anything = true;
    }
    if (!anything) return;

    auto doClear = [this, n]() {
        for (int t = 0; t < n; ++t) {
            auto& tr = m_project.track(t);
            tr.arrangementClips.clear();
            tr.arrangementActive = false;
            m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{t, false});
            syncArrangementClipsToEngine(t);
            if (tr.type == Track::Type::Visual) {
                m_visualEngine.clearLayer(t);
                m_activeArrVisualClip[t] = -1;
            }
        }
        m_project.updateArrangementLength();
        markDirty();
    };
    doClear();

    m_undoManager.push({"Clear All Arrangements",
        [this, snapshot]() {
            for (int t = 0; t < static_cast<int>(snapshot.size())
                              && t < m_project.numTracks(); ++t) {
                auto& tr = m_project.track(t);
                tr.arrangementClips = snapshot[t].first;
                tr.arrangementActive = snapshot[t].second;
                m_audioEngine.sendCommand(
                    audio::SetTrackArrActiveMsg{t, snapshot[t].second});
                if (snapshot[t].second) syncArrangementClipsToEngine(t);
            }
            m_project.updateArrangementLength();
            markDirty();
        },
        doClear});
}

// ── Record session → arrangement ───────────────────────────────────────────

int App::currentSessionScene(int track) {
    if (track < 0 || track >= m_project.numTracks()) return -1;
    switch (m_project.track(track).type) {
        case Track::Type::Audio: {
            const auto& st = m_audioEngine.clipEngine().trackState(track);
            return st.active ? st.sceneIndex : -1;
        }
        case Track::Type::Midi:
            return m_audioEngine.midiClipEngine().isTrackPlaying(track)
                ? m_audioEngine.midiClipEngine().trackState(track).sceneIndex : -1;
        default:
            // Visual: the App tracks the launched session visual clip's scene
            // per track (set on launch, cleared to -1 on stop/follow-stop).
            return m_visualLaunchScene[track];
    }
}

void App::toggleArrangementRecord() {
    if (!m_arrRecording) {
        m_arrRecording   = true;
        if (m_transportPanel) m_transportPanel->setArrangementRecArmed(true);
        m_arrRecStartBeat = m_audioEngine.transport().positionInBeats();
        for (int t = 0; t < kMaxTracks; ++t) {
            m_arrRecScene[t] = -1;
            m_arrRecStart[t] = 0.0;
            m_arrRecTake[t].clear();
        }
        m_toastManager.show("Arrangement record armed — play and trigger clips",
                            2.5f, ui::ToastManager::Severity::Info);
        return;
    }
    // Disarm: close any still-open intervals at the current beat, then offer
    // to commit the take.
    m_arrRecording = false;
    if (m_transportPanel) m_transportPanel->setArrangementRecArmed(false);
    const double beat = m_audioEngine.transport().positionInBeats();
    int total = 0;
    for (int t = 0; t < kMaxTracks; ++t) {
        if (m_arrRecScene[t] >= 0) {
            m_arrRecTake[t].push_back({m_arrRecScene[t], m_arrRecStart[t], beat});
            m_arrRecScene[t] = -1;
        }
        total += static_cast<int>(m_arrRecTake[t].size());
    }
    if (total == 0) {
        m_toastManager.show("Arrangement record: nothing captured",
                            2.0f, ui::ToastManager::Severity::Info);
        return;
    }
    ui::fw2::ConfirmDialog::prompt(
        "Bounce " + std::to_string(total) + " captured clip(s) to the arrangement?",
        [this]() { commitArrangementTake(); });
}

void App::pollArrangementRecord() {
    if (!m_arrRecording) return;
    auto& transport = m_audioEngine.transport();
    if (!transport.isPlaying()) return;
    const double beat = transport.positionInBeats();
    const int n = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < n; ++t) {
        const int scene = currentSessionScene(t);
        if (scene == m_arrRecScene[t]) continue;   // no transition
        if (m_arrRecScene[t] >= 0)                  // close the previous interval
            m_arrRecTake[t].push_back({m_arrRecScene[t], m_arrRecStart[t], beat});
        m_arrRecScene[t] = scene;                   // open new (or gap)
        m_arrRecStart[t] = beat;
    }
}

void App::commitArrangementTake() {
    // Launches are bar/beat-quantized, so the ~1-frame poll latency rounds
    // back to the boundary — snap interval edges to the nearest beat.
    auto snap = [](double b) { return std::round(b); };
    const int n = m_project.numTracks();
    int written = 0;
    for (int t = 0; t < n && t < kMaxTracks; ++t) {
        if (m_arrRecTake[t].empty()) continue;
        auto& track = m_project.track(t);
        auto& clips = track.arrangementClips;

        // Punch range = the span this track's take covers.
        double pStart = 1e18, pEnd = -1e18;
        for (const auto& iv : m_arrRecTake[t]) {
            pStart = std::min(pStart, snap(iv.startBeat));
            pEnd   = std::max(pEnd,   snap(iv.stopBeat));
        }
        // Punch-overwrite: drop existing clips overlapping that range.
        clips.erase(std::remove_if(clips.begin(), clips.end(),
            [&](const ArrangementClip& ac){ return ac.overlaps(pStart, pEnd); }),
            clips.end());

        // One arrangement clip per captured interval, referencing the slot.
        for (const auto& iv : m_arrRecTake[t]) {
            const double s = snap(iv.startBeat), e = snap(iv.stopBeat);
            if (e - s < 0.25) continue;             // skip sub-beat blips
            auto* slot = m_project.getSlot(t, iv.scene);
            if (!slot) continue;
            ArrangementClip ac;
            ac.startBeat   = s;
            ac.lengthBeats = e - s;
            ac.colorIndex  = track.colorIndex;
            if (slot->audioClip && slot->audioClip->buffer) {
                ac.type        = ArrangementClip::Type::Audio;
                ac.audioBuffer = slot->audioClip->buffer;
                ac.loop        = slot->audioClip->looping;
                ac.name        = slot->audioClip->name.empty()
                                 ? "audio" : slot->audioClip->name;
            } else if (slot->midiClip) {
                ac.type        = ArrangementClip::Type::Midi;
                ac.midiClip    = std::shared_ptr<midi::MidiClip>(
                                     slot->midiClip->clone().release());
                ac.name        = slot->midiClip->name().empty()
                                 ? "midi" : slot->midiClip->name();
            } else if (slot->visualClip) {
                ac.type        = ArrangementClip::Type::Visual;
                ac.visualClip  = slot->visualClip->clone();
                ac.stretch     = slot->visualClip->tempoSync;
                ac.name        = slot->visualClip->name.empty()
                                 ? "visual" : slot->visualClip->name;
            } else {
                continue;
            }
            clips.push_back(std::move(ac));
            ++written;
        }
        track.sortArrangementClips();
        if (!track.arrangementActive) {
            track.arrangementActive = true;
            m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{t, true});
        }
        syncArrangementClipsToEngine(t);
        m_arrRecTake[t].clear();
    }
    m_project.updateArrangementLength();
    markDirty();
    m_toastManager.show("Bounced " + std::to_string(written) + " clip(s) to arrangement",
                        2.5f, ui::ToastManager::Severity::Info);
}

midi::MidiClip* App::setMidiClipLive(int track, int scene,
                                     std::unique_ptr<midi::MidiClip> newClip) {
    const midi::MidiClip* oldPtr = m_project.getMidiClip(track, scene);
    midi::MidiClip* np = m_project.setMidiClip(track, scene, std::move(newClip));
    if (oldPtr && np)
        m_audioEngine.sendCommand(audio::SwapMidiClipMsg{track, oldPtr, np});
    return np;
}

midi::MidiClip* App::replaceEditedMidiClip(const midi::MidiClip* oldClip,
                                           std::unique_ptr<midi::MidiClip> newClip) {
    if (!oldClip || !newClip) return nullptr;

    // Session slots — setMidiClip graveyards the previous clip (so the
    // audio thread's cached pointer stays valid until it processes the
    // swap), then SwapMidiClipMsg re-points a playing/pending track.
    for (int t = 0; t < m_project.numTracks(); ++t) {
        for (int s = 0; s < m_project.numScenes(); ++s) {
            if (m_project.getMidiClip(t, s) != oldClip) continue;
            midi::MidiClip* np = m_project.setMidiClip(t, s, std::move(newClip));
            m_audioEngine.sendCommand(audio::SwapMidiClipMsg{t, oldClip, np});
            markDirty();
            return np;
        }
    }

    // Arrangement clips — playback holds shared_ptr copies submitted via
    // a mutex-guarded queue, so swapping the shared_ptr and re-submitting
    // keeps the old data alive for the audio thread until it adopts the
    // new list.
    for (int t = 0; t < m_project.numTracks(); ++t) {
        for (auto& ac : m_project.track(t).arrangementClips) {
            if (ac.midiClip.get() != oldClip) continue;
            ac.midiClip = std::shared_ptr<midi::MidiClip>(std::move(newClip));
            if (m_project.track(t).arrangementActive)
                syncArrangementClipsToEngine(t);
            markDirty();
            return ac.midiClip.get();
        }
    }

    return nullptr;
}

void SDLCALL App::onOpenFolderResult(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_pendingOpenPath = filelist[0];
}

void SDLCALL App::onSaveFolderResult(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_pendingSavePath = filelist[0];
}

void SDLCALL App::onExportSaveResult(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_pendingExportPath = filelist[0];
}

// ---------------------------------------------------------------------------
// Export Video (offline arrangement render → mp4)
// ---------------------------------------------------------------------------

void SDLCALL App::onVideoExportSaveResult(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;
    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_pendingVideoExportPath = filelist[0];
}

void App::openVideoExportDialog() {
    static const SDL_DialogFileFilter filter = { "Video (*.mp4)", "mp4" };
    SDL_ShowSaveFileDialog(onVideoExportSaveResult, this,
                           m_mainWindow.getHandle(), &filter, 1, "export.mp4");
}

// Blocking offline render of the arrangement to an mp4. Runs on the main
// (GL) thread, so the UI is unresponsive while it works — progress goes to
// yawn.log and a toast lands when done. v1: 640×360 @ 30 fps, H.264 + AAC.
void App::exportVideo(const std::string& filePath) {
    namespace fs = std::filesystem;
    const double bpm        = std::max(1.0, m_audioEngine.transport().bpm());
    const double sampleRate = std::max(1.0, m_audioEngine.sampleRate());
    const double lengthBeats = m_project.arrangementLength();
    if (lengthBeats <= 0.0) {
        m_toastManager.show("Nothing on the arrangement timeline to export",
                            2.5f, ui::ToastManager::Severity::Info);
        return;
    }
    constexpr int kFps = 30;
    const double lengthSec = lengthBeats * 60.0 / bpm;
    const int totalFrames = std::max(1, static_cast<int>(std::ceil(lengthSec * kFps)));

    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path() /
        ("yawn_vexport_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    LOG_INFO("VideoExport", "Rendering %d frames (%.1fs @ %d fps) → %s",
             totalFrames, lengthSec, kFps, filePath.c_str());

    // Force arrangement playback on for the bounce (both the engine, for
    // audio, and the project flag, which the visual poller reads); remember
    // the prior state to restore afterwards.
    const int nTracks = m_project.numTracks();
    std::vector<uint8_t> restoreArr(static_cast<size_t>(nTracks), 0);
    for (int t = 0; t < nTracks; ++t) {
        restoreArr[t] = m_project.track(t).arrangementActive ? 1 : 0;
        const bool hasArr = !m_project.track(t).arrangementClips.empty();
        m_project.track(t).arrangementActive = hasArr;
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{t, hasArr});
        if (hasArr) syncArrangementClipsToEngine(t);
    }

    // 1) Audio bounce → WAV (OfflineRenderer stops/restarts the stream).
    audio::RenderConfig rc;
    rc.startBeat = 0.0; rc.endBeat = lengthBeats;
    rc.targetSampleRate = static_cast<int>(sampleRate); rc.channels = 2;
    audio::RenderProgress prog;
    auto audioBuf = audio::OfflineRenderer::render(m_audioEngine, rc, prog);
    const fs::path wavPath = tmp / "audio.wav";
    bool haveAudio = audioBuf &&
        util::saveAudioBuffer(wavPath.string(), *audioBuf, static_cast<int>(sampleRate));

    // 2) Video pass — drive the transport per frame, render offline, write PNGs.
    auto& transport = m_audioEngine.transport();
    const int64_t savedPos = transport.positionInSamples();
    const bool savedPlaying = transport.isPlaying();
    transport.stop();   // we set the position; the callback must not advance it
    for (int i = 0; i < kMaxTracks; ++i) m_activeArrVisualClip[i] = -1;

    m_visualEngine.beginOfflineRender();
    const int w = m_visualEngine.compositeWidth();
    const int h = m_visualEngine.compositeHeight();
    std::vector<uint8_t> rgba;
    char nameBuf[64];
    for (int f = 0; f < totalFrames; ++f) {
        const double sec  = static_cast<double>(f) / kFps;
        const double beat = sec * bpm / 60.0;
        transport.setPositionInSamples(static_cast<int64_t>(sec * sampleRate));
        pollArrangementVisualPlayback();                 // launch/clear arr clips
        m_visualEngine.tick(sec, beat, /*playing*/true); // offline → renders to FBO
        if (m_visualEngine.readComposite(rgba)) {
            std::snprintf(nameBuf, sizeof(nameBuf), "f_%06d.png", f);
            stbi_write_png((tmp / nameBuf).string().c_str(), w, h, 4,
                           rgba.data(), w * 4);
        }
        if ((f % 30) == 0)
            LOG_INFO("VideoExport", "  frame %d / %d", f, totalFrames);
    }
    m_visualEngine.endOfflineRender();

    // Restore transport + arrangement-active state.
    transport.setPositionInSamples(savedPos);
    if (savedPlaying) transport.play();
    for (int t = 0; t < nTracks; ++t) {
        m_project.track(t).arrangementActive = (restoreArr[t] != 0);
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{t, restoreArr[t] != 0});
    }

    // 3) Encode + mux with ffmpeg.
    std::string outPath = filePath;
    if (outPath.size() < 4 || outPath.substr(outPath.size() - 4) != ".mp4")
        outPath += ".mp4";
    std::vector<std::string> args = {
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-framerate", std::to_string(kFps),
        "-i", (tmp / "f_%06d.png").string(),
    };
    if (haveAudio) { args.push_back("-i"); args.push_back(wavPath.string()); }
    args.insert(args.end(), { "-c:v", "libx264", "-pix_fmt", "yuv420p",
                              "-preset", "medium", "-crf", "20" });
    if (haveAudio) args.insert(args.end(), { "-c:a", "aac", "-shortest" });
    args.push_back(outPath);
    const bool encOk = visual::runFFmpegCommand(args);

    fs::remove_all(tmp, ec);
    if (encOk)
        m_toastManager.show("Exported video: " + fs::path(outPath).filename().string(),
                            3.5f, ui::ToastManager::Severity::Info);
    else
        m_toastManager.show("Video export failed (is ffmpeg installed?)",
                            3.5f, ui::ToastManager::Severity::Error);
}

// ---------------------------------------------------------------------------
// Export Audio
// ---------------------------------------------------------------------------

void App::openExportDialog() {
    ui::fw2::FwExportDialog::Config cfg;
    cfg.arrangementLengthBeats = m_project.arrangementLength();
    cfg.loopEnabled = m_audioEngine.transport().isLoopEnabled();
    cfg.loopStartBeats = m_audioEngine.transport().loopStartBeats();
    cfg.loopEndBeats = m_audioEngine.transport().loopEndBeats();
    cfg.sampleRate = static_cast<int>(m_audioEngine.sampleRate());

    m_exportDialog.setOnResult([this](ui::fw2::ExportResult result) {
        if (result == ui::fw2::ExportResult::OK) {
            // Show native save file dialog
            auto& cfg = m_exportDialog.config();
            const char* ext = util::formatExtension(cfg.format);
            LOG_INFO("Export", "Dialog OK, showing save dialog (format: %s)", ext);

            // Store filter data as members so they survive until async callback
            // SDL pattern wants just the extension without dot (e.g. "wav" not "*.wav")
            m_exportFilterDesc = "Audio Files (*";
            m_exportFilterDesc += ext;
            m_exportFilterDesc += ")";
            m_exportFilterPattern = ext + 1; // skip leading dot
            m_exportDefaultName = "export";
            m_exportDefaultName += ext;

            m_exportFilter.name = m_exportFilterDesc.c_str();
            m_exportFilter.pattern = m_exportFilterPattern.c_str();

            SDL_ShowSaveFileDialog(onExportSaveResult, this,
                                   m_mainWindow.getHandle(),
                                   &m_exportFilter, 1, m_exportDefaultName.c_str());
            const char* err = SDL_GetError();
            if (err && err[0])
                LOG_INFO("Export", "SDL_ShowSaveFileDialog error: %s", err);
        }
    });

    m_exportDialog.open(cfg);
}

void App::startExportRender(const std::string& filePath) {
    auto& cfg = m_exportDialog.config();

    // Determine render range
    double startBeat = 0.0;
    double endBeat = m_project.arrangementLength();
    if (cfg.scope == 1) {
        startBeat = cfg.loopStartBeats;
        endBeat = cfg.loopEndBeats;
    }

    // Build render config
    audio::RenderConfig renderCfg;
    renderCfg.startBeat = startBeat;
    renderCfg.endBeat = endBeat;
    renderCfg.targetSampleRate = cfg.sampleRate;
    renderCfg.channels = 2;

    // Ensure file has correct extension
    std::string outPath = filePath;
    const char* ext = util::formatExtension(cfg.format);
    if (outPath.size() < 4 || outPath.substr(outPath.size() - std::strlen(ext)) != ext) {
        outPath += ext;
    }

    // Copy format settings for the thread
    util::ExportFormat format = cfg.format;
    util::BitDepth bitDepth = cfg.bitDepth;
    int sampleRate = cfg.sampleRate;

    // Re-open the dialog in rendering mode
    m_exportDialog.open(m_exportDialog.config());
    m_exportDialog.setRendering(true);

    auto& progress = m_exportDialog.progress();

    // Export is an arrangement bounce. In Session view the arrangement tracks
    // are inactive (processAudio plays the clip launcher instead), so the
    // render would be silent. Force arrangement playback on for the bounce
    // and restore each track's prior state once it's done. We capture the
    // restore flags by value so the render thread only re-sends them (no
    // project reads off the UI thread).
    const int nTracks = m_project.numTracks();
    std::vector<uint8_t> restoreArrActive(static_cast<size_t>(nTracks), 0);
    for (int t = 0; t < nTracks; ++t) {
        restoreArrActive[t] = m_project.track(t).arrangementActive ? 1 : 0;
        const bool hasArr = !m_project.track(t).arrangementClips.empty();
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{t, hasArr});
        if (hasArr) syncArrangementClipsToEngine(t);
    }

    // Launch render on a detached thread
    std::thread([this, renderCfg, outPath, format, bitDepth, sampleRate,
                 nTracks, restoreArrActive, &progress]() {
        auto buffer = audio::OfflineRenderer::render(m_audioEngine, renderCfg, progress);

        // Restore each track's arrangement-active state.
        for (int t = 0; t < nTracks; ++t)
            m_audioEngine.sendCommand(
                audio::SetTrackArrActiveMsg{t, restoreArrActive[t] != 0});

        if (buffer && !progress.cancelled.load()) {
            bool ok = util::saveAudioBuffer(outPath, *buffer, sampleRate, format, bitDepth);
            if (!ok) {
                LOG_ERROR("Export", "Failed to write: %s", outPath.c_str());
                progress.failed.store(true);
            } else {
                LOG_INFO("Export", "Written: %s", outPath.c_str());
            }
        }
        progress.done.store(true);
    }).detach();
}

void App::startStemSeparation(int trackIndex, int sceneIndex) {
    if (!transcribe::stemSeparationAvailable()) return;
    if (m_pendingStem && m_pendingStem->active.load() && !m_pendingStem->done.load()) {
        m_toastManager.show("Stem separation already running…", 2.0f,
                            ui::ToastManager::Severity::Info);
        return;
    }
    auto* slot = m_project.getSlot(trackIndex, sceneIndex);
    if (!slot || !slot->audioClip || !slot->audioClip->buffer) return;

    // Keep the source buffer alive for the worker via shared_ptr; both the
    // engine and the worker only READ it, so concurrent access is safe.
    std::shared_ptr<audio::AudioBuffer> buf = slot->audioClip->buffer;
    const double sr = m_audioEngine.sampleRate();
    const std::string base = slot->audioClip->name.empty()
                             ? "Audio" : slot->audioClip->name;

    m_pendingStem = std::make_unique<PendingStem>();
    m_pendingStem->active.store(true);
    m_pendingStem->trackIndex = trackIndex;
    m_pendingStem->sceneIndex = sceneIndex;
    m_pendingStem->baseName   = base;
    PendingStem* ps = m_pendingStem.get();

    m_toastManager.show(
        transcribe::stemModelPresent()
            ? "Stem separation started (a few minutes on CPU; Esc to cancel)…"
            : "Stem separation: downloading model (~170 MB), then separating…",
        4.0f, ui::ToastManager::Severity::Info);

    std::thread([this, ps, buf, sr]() {
        transcribe::StemProgress prog =
            [ps](const std::string& phase, float frac) {
                ps->phase.store(phase == "download" ? 0 : 1);
                ps->fraction.store(frac);
            };
        transcribe::StemOutput out =
            transcribe::separateStems(*buf, sr, prog, ps->cancel);
        ps->output = std::move(out);
        ps->done.store(true, std::memory_order_release);
    }).detach();
}

void App::applyStemResult() {
    if (!m_pendingStem) return;
    PendingStem& ps = *m_pendingStem;
    const transcribe::StemOutput& out = ps.output;

    if (out.cancelled) {
        m_toastManager.show("Stem separation cancelled", 2.5f,
                            ui::ToastManager::Severity::Info);
    } else if (!out.ok) {
        m_toastManager.show(
            "Stem separation failed: " +
                (out.error.empty() ? std::string("unknown") : out.error),
            4.0f, ui::ToastManager::Severity::Warn);
    } else {
        // Snapshot the stems (shared_ptr buffers) so undo/redo can recreate
        // the tracks. createTracks appends one audio track per stem with
        // the stem buffer as its scene clip.
        auto buffers = std::make_shared<std::vector<std::shared_ptr<audio::AudioBuffer>>>();
        auto names   = std::make_shared<std::vector<std::string>>();
        for (size_t s = 0; s < out.stems.size(); ++s) {
            if (!out.stems[s]) continue;
            buffers->push_back(out.stems[s]);
            names->push_back(out.names[s]);
        }
        const std::string base  = ps.baseName;
        const int         scene = ps.sceneIndex;
        const int         count = static_cast<int>(buffers->size());

        auto createTracks = [this, buffers, names, base, scene]() {
            for (size_t s = 0; s < buffers->size(); ++s) {
                const int idx = m_project.numTracks();
                const std::string name = base + " " + (*names)[s];
                m_project.addTrack(name, Track::Type::Audio);
                m_audioEngine.sendCommand(audio::SetTrackTypeMsg{idx, 0});
                auto clip = std::make_unique<audio::Clip>();
                clip->name   = name;
                clip->buffer = (*buffers)[s];
                m_project.setClip(idx, scene, std::move(clip));
            }
            syncTracksToEngine();   // engine learns the new tracks + clips
            markDirty();
        };

        createTracks();

        // Undoable: drop the `count` tracks we appended (they're the last
        // N — same assumption as the Add-Track undo). Redo recreates them.
        m_undoManager.push({
            "Separate Stems",
            [this, count]() {
                for (int i = 0; i < count && m_project.numTracks() > 0; ++i)
                    m_project.removeLastTrack();
                syncTracksToEngine();
                markDirty();
            },
            createTracks,
            ""});

        m_toastManager.show(
            "Stems ready: " + std::to_string(count) + " new tracks (" + base + ")",
            4.0f, ui::ToastManager::Severity::Info);
    }
    m_pendingStem.reset();
}

void App::startPresetGeneration(float alienRatio, bool selectedDeviceOnly) {
    using namespace yawn::presets;
    if (m_genRunning.load()) {
        m_toastManager.show("Preset generation already running…", 2.0f,
                            ui::ToastManager::Severity::Info);
        return;
    }

    GenOptions opt;
    opt.alienNameRatio = alienRatio;
    opt.validate = true;
    opt.sampleRate = m_audioEngine.sampleRate();

    std::vector<GenSpec> catalog;
    if (selectedDeviceOnly) {
        auto* inst = m_audioEngine.instrument(m_selectedTrack);
        if (!inst || !PresetGenerator::isSupported(inst->id())) {
            m_toastManager.show(
                "Selected track's instrument can't be auto-generated",
                3.0f, ui::ToastManager::Severity::Warn);
            return;
        }
        std::string id = inst->id();
        catalog.push_back({id, PresetGenerator::kindOf(id),
                           id == "instrack" ? 40 : 30});
    } else {
        catalog = PresetGenerator::defaultCatalog();
    }

    m_genRunning.store(true);
    m_genFinished.store(false);
    m_toastManager.show("Generating presets… (this takes a few seconds)",
                        2.5f, ui::ToastManager::Severity::Info);

    // Detached worker — the generator builds its OWN device instances
    // via Factory, so it shares no state with the live audio thread.
    // Capture opt/catalog by value so nothing dangles.
    std::thread([this, opt, catalog]() {
        PresetGenerator gen(opt);
        std::vector<GeneratedPreset> results = gen.generateBatch(catalog);
        int valid = 0;
        for (const auto& r : results) if (r.valid) ++valid;
        m_genCount.store(static_cast<int>(results.size()));
        m_genValid.store(valid);
        m_genRunning.store(false);
        m_genFinished.store(true);  // publish last → update() reads counts
    }).detach();
}

#ifdef YAWN_HAS_VST3
void App::openVST3Editor(vst3::VST3PluginInstance* instance,
                         const std::string& modulePath,
                         const std::string& classID,
                         const std::string& title) {
    if (!instance) {
        LOG_WARN("VST3", "openVST3Editor: null instance");
        return;
    }
    for (auto& ed : m_vst3Editors) {
        if (ed && ed->isOpen() && ed->instance() == instance) {
            LOG_INFO("VST3", "Editor already open for '%s'", title.c_str());
            return;
        }
    }
    LOG_INFO("VST3", "Opening editor for '%s'", title.c_str());
    auto editor = std::make_unique<vst3::VST3EditorWindow>();
    if (editor->open(instance, modulePath, classID, title)) {
        m_vst3Editors.push_back(std::move(editor));
    } else {
        LOG_ERROR("VST3", "Failed to open editor for '%s'", title.c_str());
    }
}
#endif

} // namespace yawn
