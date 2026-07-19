// App_Frame.cpp — the per-frame update() (audio-event polling, async
// worker completion, UI timers) and render(). Split out of App.cpp.
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

void App::update() {
    // A video export runs as a frame-driven state machine (GL frame render on
    // this thread, sliced; audio bounce + ffmpeg on a worker). Advance it here.
    // While it's live, skip every other main-thread op that touches the visual
    // engine (model thumbnails, layer polls, the output tick) so we don't fight
    // the export's transport scrubbing.
    if (m_videoExport) pollVideoExport();
    const bool vExport = isVideoExporting();

    // The transport's inline BPM / time-signature edit is entered through the
    // number field's gesture SM, which fires beginEdit outside the mousedown
    // handler's one-shot SDL_StartTextInput window — so without this, digit
    // TEXT_INPUT events never start flowing and you can't type a tempo (only
    // Backspace, a KEY_DOWN, works). Reconcile SDL text input with the edit
    // state every frame: turn it on while editing if it isn't already.
    if (m_transportPanel->isEditing() &&
        !SDL_TextInputActive(m_mainWindow.getHandle())) {
        SDL_StartTextInput(m_mainWindow.getHandle());
    }

    // Debounced settings flush — save ~0.75 s after the last change so a
    // velocity drag doesn't write the settings file every frame.
    if (m_settingsDirty && ++m_settingsDirtyAge > 45) {
        util::AppSettings::save(m_settings);
        m_settingsDirty = false;
        m_settingsDirtyAge = 0;
    }

    // Generate model-library thumbnails for the Models browser tab at a
    // safe point (frame start, no active 2D batch) — ensureModelThumbnail
    // switches to the output GL context to render. Budget a couple per
    // frame so the first reveal doesn't hitch with many models.
    if (!vExport && m_browserPanel &&
        m_browserPanel->activeTab() == ui::fw2::BrowserPanel::Tab::Models) {
        int budget = 2;
        for (const auto& e : m_browserPanel->modelsTab().entries()) {
            if (m_visualEngine.cachedModelThumbnail(e.path) != 0) continue;
            m_visualEngine.ensureModelThumbnail(e.path);
            if (--budget <= 0) break;
        }
    }

    // Background preset generation finished → toast the result on the
    // UI thread (the worker can't safely touch the toast manager).
    if (m_genFinished.exchange(false)) {
        std::string msg = "Generated " + std::to_string(m_genCount.load()) +
                          " presets (" + std::to_string(m_genValid.load()) +
                          " audible)";
        m_toastManager.show(msg, 3.5f, ui::ToastManager::Severity::Info);
        // Re-index the presets folder so the new files land in the
        // library DB, then refresh the Browser once the scan finishes.
        if (m_libraryScanner) {
            m_libraryScanner->scanPresets();
            m_genRescanWait = 0;
        }
    }
    // Race-safe Browser refresh after the post-generation rescan:
    // wait a few frames for the async scan to actually start, and only
    // refresh once it has finished (isScanning() back to false).
    if (m_genRescanWait >= 0 && m_libraryScanner) {
        ++m_genRescanWait;
        if (m_genRescanWait > 5 && !m_libraryScanner->isScanning()) {
            m_browserPanel->presetsTab().refreshList();
            m_genRescanWait = -1;
        }
    }
    // One-shot Browser refresh once the initial startup scan completes,
    // so seeded factory loops and freshly-indexed files/presets show up
    // without needing a restart.
    if (m_initialScanRefresh >= 0 && m_libraryScanner) {
        ++m_initialScanRefresh;
        if (m_initialScanRefresh > 5 && !m_libraryScanner->isScanning()) {
            m_browserPanel->filesTab().refreshTree();
            m_browserPanel->presetsTab().refreshList();
            m_browserPanel->loopsTab().refreshList();
            m_initialScanRefresh = -1;
        }
    }

    // Update animation timer for session panel (recording pulse, playback pulse)
    uint64_t now = SDL_GetTicks();
    float dt = (m_lastFrameTicks > 0) ? (now - m_lastFrameTicks) / 1000.0f : 0.0f;
    m_lastFrameTicks = now;
    m_sessionPanel->updateAnimTimer(dt);

    // Drop any clips that have been waiting in the project's
    // graveyard long enough that the audio thread can't possibly
    // still be using them. See Project::graveyardSlotClips() for
    // the rationale.
    m_project.purgeClipGraveyard();

    // Free arrangement clip lists the audio thread swapped out and
    // retired; they can hold the last shared_ptr to clip data.
    m_audioEngine.arrangementPlayback().collectRetired();

    // Free engine-side retired objects (instruments, effects, sample
    // buffers) whose audio-callback grace period has elapsed.
    m_audioEngine.pollRetirements();

    // Piano roll holds a raw midi::MidiClip* pointer to whatever the
    // user opened for editing. Several slot-mutation paths (recording
    // finalize, paste, scene insert, project load) can replace the
    // slot's MidiClip out from under that pointer; the project's clip
    // graveyard keeps the old object alive ~5 s, then frees it — and
    // the next render frame UAFs reading m_clip->name() in
    // PianoRollPanel::renderToolbar (manifests as a slow std::bad_alloc
    // crash from the resulting garbage-length string copies).
    //
    // Defensive sweep: if the panel's clip pointer no longer matches
    // any slot's midiClip OR any arrangement clip's midiClip, it's
    // stale → close the panel rather than dereference freed memory.
    // O(tracks × scenes + arrangement clips) per frame; cheap.
    if (m_pianoRoll && m_pianoRoll->isOpen() && m_pianoRoll->clip()) {
        const midi::MidiClip* held = m_pianoRoll->clip();
        bool stillValid = false;
        for (int t = 0; t < m_project.numTracks() && !stillValid; ++t) {
            for (int s = 0; s < m_project.numScenes(); ++s) {
                if (m_project.getMidiClip(t, s) == held) {
                    stillValid = true;
                    break;
                }
            }
            if (stillValid) break;
            for (const auto& ac : m_project.track(t).arrangementClips) {
                if (ac.midiClip.get() == held) {
                    stillValid = true;
                    break;
                }
            }
        }
        if (!stillValid) {
            LOG_WARN("UI", "PianoRoll: held clip %p no longer in project — closing",
                      static_cast<const void*>(held));
            m_pianoRoll->close();
        }
    }

    // Detail panel open/close height animation. Lives outside fw2's
    // measure cache so a re-layout in the same frame doesn't read a
    // stale height; tick() advances the animated height and bumps the
    // panel's local version so the next measure() pass picks it up.
    m_detailPanel->tick();

    // Piano roll uses the same animated-height + measure-cache pattern
    // — tick() advances the open/close animation and invalidates so
    // the new height is visible on the next frame. Without this, the
    // panel opens to a stub that shows only the toolbar (cached at the
    // first sampled m_animatedHeight) and the resize handle does
    // nothing visible.
    m_pianoRoll->tick();

    // Auto-Sample dialog — runs the worker state machine each frame
    // when a capture is in progress, and resolves the test-note Note-Off
    // wall-clock timer regardless. Cheap when the dialog is idle.
    m_autoSampleDialog.tick();

    // Transport stop: whenever the stop-counter advances (even while
    // transport is already stopped), clear every visual layer so
    // session-launched shaders / models / videos go dark in lockstep
    // with the audio / MIDI scheduleStop path.
    if (!vExport)
        m_visualController->onTransportStopCounter(
            m_audioEngine.transport().stopCounter());

    // Arrangement-driven visual clip playback: detect which clip is
    // under the transport head on each visual track and fire launch /
    // clear on transitions. Main-thread polling is precise enough for
    // visual cues (16 ms granularity at 60 Hz).
    if (!vExport) pollArrangementVisualPlayback();

    // Capture the actually-playing session clip per track while arrangement
    // record is armed (follow-actions / scene launches included for free).
    pollArrangementRecord();

    // Per-track visual-knob automation lanes — evaluated here rather
    // than on the audio thread (visuals don't need that precision).
    if (!vExport) pollVisualKnobAutomation();

    // Per-track macro mappings — same once-per-frame cadence as
    // automation. Pulls each macro's live LFO-modulated value from
    // the engine and pushes through to every mapped target so
    // shader / chain params follow the macro in lock-step.
    if (!vExport) applyMacroMappings();

    // Fire any quantized visual-clip launches whose bar/beat boundary has
    // arrived, so video starts in sync with the scene's audio/MIDI clips.
    if (!vExport) pollVisualLaunchQueue();

    // Session-view follow actions for visual clips. Fires Next /
    // Random / etc. once barCount bars have elapsed since launch.
    if (!vExport) pollVisualFollowActions();

    // Video imports — advance each background transcode, apply results.
    m_visualController->pollVideoImports();

    // Poll the Demucs stem-separation worker (one at a time). On completion
    // apply on the main thread (track creation). Live progress is drawn as
    // an overlay in render() from the atomic fraction.
    if (m_pendingStem && m_pendingStem->active.load() &&
        m_pendingStem->done.load(std::memory_order_acquire)) {
        applyStemResult();
    }

    // Pull any fresh MIDI CC values for visual-knob targets off the bus
    // and apply them to the layer + the track-level macro device.
    // Audio thread writes, UI thread reads once per frame. Hardware
    // controllers (Push / Move encoders 1..8) feed straight into the
    // track macros via this path.
    m_visualController->pollVisualKnobBus();

    // Apply MIDI-LFO → visual modulation. A midi::LFO living in any track's
    // MIDI effect chain can target a visual layer's A..H knob or shader
    // @range param (on a different, visual-type track). The audio thread
    // published each such LFO's output to VisualModBus keyed by (track,
    // slot); here we clear last frame's offsets on every visual layer and
    // re-accumulate the current contributions. Clearing first makes removal
    // / bypass restore the base value automatically.
    for (int t = 0; t < m_project.numTracks() && t < kMaxTracks; ++t) {
        if (m_project.track(t).type == Track::Type::Visual)
            m_visualEngine.clearLayerMods(t);
    }
    for (int t = 0; t < kMaxTracks; ++t) {
        auto& chain = m_audioEngine.midiEffectChain(t);
        for (int e = 0; e < chain.count(); ++e) {
            auto* fx = chain.effect(e);
            if (!fx || fx->modulationTargetType() < midi::LFO::TgtVisualKnob)
                continue;
            const int vt = fx->modulationVisualTrack();
            if (vt < 0 || vt >= m_project.numTracks() || vt >= kMaxTracks)
                continue;
            if (m_project.track(vt).type != Track::Type::Visual) continue;
            const float mod = visual::VisualModBus::instance().read(t, e);
            if (fx->modulationTargetType() == midi::LFO::TgtVisualKnob)
                m_visualEngine.addLayerKnobMod(vt, fx->modulationTargetParam(), mod);
            else
                m_visualEngine.addLayerParamMod(vt, fx->modulationTargetName(), mod);
        }
    }

    // Push live modulated knob values into the visual-params panel so the
    // A..H arcs breathe with their LFOs. Only active while the panel is
    // visible — audio/midi tracks get cleared overrides.
    if (m_visualParamsPanel->isVisible() &&
        m_selectedTrack >= 0 && m_selectedTrack < m_project.numTracks() &&
        m_project.track(m_selectedTrack).type == Track::Type::Visual) {
        float disp[8];
        for (int i = 0; i < 8; ++i)
            disp[i] = m_visualEngine.getLayerKnobDisplayValue(m_selectedTrack, i);
        m_visualParamsPanel->setKnobDisplayValues(disp);
    } else {
        m_visualParamsPanel->setKnobDisplayValues(nullptr);
    }

    // Poll controller scripts (MIDI input → Lua callbacks)
    m_controllerManager.update();

    // Sync controller session focus rectangle to SessionPanel
    {
        const auto& focus = m_controllerManager.sessionFocus();
        m_sessionPanel->setControllerGridRegion(
            focus.trackOffset, focus.sceneOffset, focus.active);
    }

    // Process pending file dialog results (from SDL async callbacks)
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        if (!m_pendingOpenPath.empty()) {
            std::string path = std::move(m_pendingOpenPath);
            m_pendingOpenPath.clear();
            doOpenProject(path);
        }
        if (!m_pendingSavePath.empty()) {
            std::string path = std::move(m_pendingSavePath);
            m_pendingSavePath.clear();
            doSaveProject(path);
        }
        if (!m_pendingExportPath.empty()) {
            std::string path = std::move(m_pendingExportPath);
            m_pendingExportPath.clear();
            startExportRender(path);
        }
        if (!m_pendingVideoExportPath.empty()) {
            std::string path = std::move(m_pendingVideoExportPath);
            m_pendingVideoExportPath.clear();
            exportVideo(path);   // kicks off the worker-thread render
        }
    }

#ifdef YAWN_HAS_VST3
    // Poll parameter changes from VST3 editor processes
    for (auto& ed : m_vst3Editors) {
        if (ed && ed->isOpen()) ed->pollParamChanges();
    }
    // Clean up closed VST3 editor windows
    m_vst3Editors.erase(
        std::remove_if(m_vst3Editors.begin(), m_vst3Editors.end(),
                        [](const auto& w) { return !w || !w->isOpen(); }),
        m_vst3Editors.end());
#endif

    // Check if render completed
    if (m_exportDialog.isRendering() && m_exportDialog.progress().done.load()) {
        m_exportDialog.setRendering(false);
        m_exportDialog.forceClose();
        if (m_exportDialog.progress().failed.load()) {
            LOG_ERROR("Export", "Render failed");
        } else if (m_exportDialog.progress().cancelled.load()) {
            LOG_INFO("Export", "Render cancelled by user");
        } else {
            LOG_INFO("Export", "Export completed successfully");
        }
    }

    audio::AudioEvent evt;
    while (m_audioEngine.pollEvent(evt)) {
        std::visit([this](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, audio::TransportPositionUpdate>) {
                m_displayBeats = msg.positionInBeats;
                m_displayPlaying = msg.isPlaying;
            }
            else if constexpr (std::is_same_v<T, audio::ClipStateUpdate>) {
                m_sessionPanel->updateClipState(msg.trackIndex, msg.playing, msg.playPosition,
                                                msg.playingScene, msg.isMidi, msg.clipLengthBeats);
                m_sessionPanel->setTrackRecording(msg.trackIndex, msg.recording, msg.recordingScene);
                // Sync defaultScene for follow actions (engine changed playing scene)
                if (msg.playing && msg.playingScene >= 0 &&
                    msg.trackIndex >= 0 && msg.trackIndex < m_project.numTracks()) {
                    m_project.track(msg.trackIndex).defaultScene = msg.playingScene;
                }
                // Forward playhead to detail panel waveform
                if (msg.trackIndex == m_selectedTrack &&
                    m_detailPanel->viewMode() == ui::fw2::DetailPanelWidget::ViewMode::AudioClip &&
                    !msg.isMidi) {
                    m_detailPanel->setClipPlayPosition(msg.playPosition);
                    m_detailPanel->setClipPlaying(msg.playing);
                    m_detailPanel->setTransportBPM(m_audioEngine.transport().bpm());
                }
                // Forward MIDI playhead to piano roll — only when the
                // clip currently playing on this track is the SAME
                // clip the piano roll is editing. Without this guard,
                // launching scene 1 while the piano roll showed the
                // scene-0 clip animated the playhead on scene 0's
                // notes (the message's trackIndex matches but the
                // clip identity doesn't). Arrangement-source piano
                // roll keeps the existing behaviour for now (its
                // playhead semantics depend on transport position +
                // clip start offset, not on session-clip launch).
                if (msg.isMidi && msg.trackIndex == m_pianoRoll->trackIndex() &&
                    m_pianoRoll->isOpen() &&
                    m_pianoRoll->source() == ui::fw2::PianoRollPanel::Source::Session) {
                    auto* playingClip = (msg.playingScene >= 0 && msg.playing)
                        ? m_project.getMidiClip(msg.trackIndex, msg.playingScene)
                        : nullptr;
                    if (playingClip == m_pianoRoll->clip()) {
                        double beats = static_cast<double>(msg.playPosition) / 1000000.0;
                        m_pianoRoll->setPlayBeat(beats, msg.playing);
                    } else {
                        // Different clip launched on this track (or
                        // nothing playing) — freeze the piano roll's
                        // playhead so it stops animating over notes
                        // that aren't currently playing.
                        m_pianoRoll->setPlayBeat(0.0, false);
                    }
                } else if (msg.isMidi && msg.trackIndex == m_pianoRoll->trackIndex() &&
                           m_pianoRoll->isOpen()) {
                    // Arrangement source — keep existing behaviour.
                    double beats = static_cast<double>(msg.playPosition) / 1000000.0;
                    m_pianoRoll->setPlayBeat(beats, msg.playing);
                }
            }
            else if constexpr (std::is_same_v<T, audio::MeterUpdate>) {
                if (msg.trackIndex >= 0)
                    m_mixerPanel->updateMeter(msg.trackIndex, msg.peakL, msg.peakR);
                else
                    m_returnMasterPanel->updateMeter(msg.trackIndex, msg.peakL, msg.peakR);
            }
            else if constexpr (std::is_same_v<T, audio::TransportRecordStateUpdate>) {
                m_transportPanel->setRecordState(msg.recording, msg.countingIn, msg.countInProgress, msg.countInBeats);
                m_sessionPanel->setGlobalRecordArmed(msg.recording);
                // Clear the SessionPanel's record-target scene
                // indicator whenever transport recording goes off,
                // regardless of HOW it went off (transport Stop,
                // Space key, project load, scene-launch finishing
                // a fixed-length take, etc.) — previously the
                // indicator only cleared via the Record-button
                // disarm callback, so it stayed stuck on the scene
                // label after Space-stop.
                if (!msg.recording && m_recordTargetScene >= 0) {
                    m_recordTargetScene = -1;
                    m_sessionPanel->setRecordTargetScene(-1);
                }
            }
            else if constexpr (std::is_same_v<T, audio::MidiRecordCompleteEvent>) {
                int ti = msg.trackIndex;
                auto& data = m_audioEngine.recordedMidiData(ti);
                if (data.ready.load(std::memory_order_acquire)) {
                    int si = data.sceneIndex;
                    if (ti >= 0 && si >= 0 && (!data.notes.empty() || !data.ccs.empty())) {
                        auto* existingClip = m_project.getMidiClip(ti, si);
                        if (data.overdub && existingClip) {
                            // Overdub onto a clip that may be playing — mutating
                            // it in place would race the audio thread's note
                            // scan, so merge into a clone and swap it in.
                            auto merged = existingClip->clone();
                            for (auto& note : data.notes)
                                merged->addNote(note);
                            for (auto& cc : data.ccs)
                                merged->addCC(cc);
                            if (data.lengthBeats > merged->lengthBeats())
                                merged->setLengthBeats(data.lengthBeats);
                            setMidiClipLive(ti, si, std::move(merged));
                        } else {
                            auto* recSlot0 = m_project.getSlot(ti, si);
                            const bool shouldLoop = recSlot0 ? recSlot0->recordLoop : true;
                            auto newClip = std::make_unique<midi::MidiClip>(data.lengthBeats);
                            newClip->setName("Rec " + std::to_string(ti + 1));
                            newClip->setLoop(shouldLoop);
                            for (auto& note : data.notes)
                                newClip->addNote(note);
                            for (auto& cc : data.ccs)
                                newClip->addCC(cc);
                            m_project.setMidiClip(ti, si, std::move(newClip));
                        }
                        markDirty();

                        auto* clipPtr = m_project.getMidiClip(ti, si);
                        if (clipPtr) {
                            auto* slot = m_project.getSlot(ti, si);
                            // Launch immediately when auto-stopped (fixed-duration) to avoid
                            // an empty bar caused by the UI round-trip delay
                            auto lq = data.autoStopped ? audio::QuantizeMode::None
                                : (slot ? slot->launchQuantize : audio::QuantizeMode::NextBar);
                            m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{ti, si, clipPtr, lq,
                                slot ? &slot->clipAutomation->lanes : nullptr,
                                slot ? slot->followAction : FollowAction{}});
                        }
                    }
                    // Always clear recording state in UI
                    m_sessionPanel->setTrackRecording(ti, false, -1);
                    data.ready.store(false, std::memory_order_release);
                }
            }
            else if constexpr (std::is_same_v<T, audio::AudioRecordCompleteEvent>) {
                int ti = msg.trackIndex;
                auto& data = m_audioEngine.recordedAudioData(ti);
                if (data.ready.load(std::memory_order_acquire)) {
                    int si = data.sceneIndex;
                    if (ti >= 0 && si >= 0 && data.frameCount > 0) {
                        // Create AudioBuffer from recorded data. The
                        // take may be tightly packed (trim/clamp path)
                        // or zero-copy with the recording pool's native
                        // stride (common path) — honor strideFrames.
                        const int64_t stride = (data.strideFrames > 0)
                            ? data.strideFrames : data.frameCount;
                        auto audioBuffer = std::make_shared<audio::AudioBuffer>(
                            data.channels, static_cast<int>(data.frameCount));
                        for (int ch = 0; ch < data.channels; ++ch) {
                            std::memcpy(
                                audioBuffer->channelData(ch),
                                data.buffer.data() + ch * stride,
                                data.frameCount * sizeof(float));
                        }

                        auto* existingSlot = m_project.getSlot(ti, si);
                        auto* existingClip = existingSlot ? existingSlot->audioClip.get() : nullptr;

                        if (data.overdub && existingClip && existingClip->buffer) {
                            // Overdub: mix new audio into existing clip buffer
                            auto& existBuf = existingClip->buffer;
                            int mixFrames = std::min(
                                static_cast<int>(data.frameCount), existBuf->numFrames());
                            int mixCh = std::min(data.channels, existBuf->numChannels());
                            for (int ch = 0; ch < mixCh; ++ch) {
                                float* dst = existBuf->channelData(ch);
                                const float* src = audioBuffer->channelData(ch);
                                for (int f = 0; f < mixFrames; ++f)
                                    dst[f] += src[f];
                            }
                            LOG_INFO("Audio", "Audio overdub: Track %d, Scene %d, %d frames mixed",
                                        ti + 1, si + 1, mixFrames);
                        } else {
                            // New recording: create clip. Loop flag
                            // comes from the slot's recordLoop (user
                            // decision per-slot) — defaults to true.
                            auto* recSlot0 = m_project.getSlot(ti, si);
                            const bool shouldLoop = recSlot0 ? recSlot0->recordLoop : true;
                            auto clip = std::make_unique<audio::Clip>();
                            clip->name = "Rec " + std::to_string(ti + 1) + "-" + std::to_string(si + 1);
                            clip->buffer = audioBuffer;
                            clip->looping = shouldLoop;
                            clip->gain = 1.0f;
                            clip->originalBPM = m_audioEngine.transport().bpm();
                            // Project::setClip auto-graveyards any
                            // existing clip in the slot, so the audio
                            // thread's cached state.clip stays valid
                            // until it processes the LaunchClipMsg
                            // below.
                            m_project.setClip(ti, si, std::move(clip));
                            LOG_INFO("Audio", "Audio recorded: Track %d, Scene %d, %" PRId64 " frames",
                                        ti + 1, si + 1, data.frameCount);
                        }
                        markDirty();

                        auto* clipPtr = m_project.getClip(ti, si);
                        if (clipPtr) {
                            auto* recSlot = m_project.getSlot(ti, si);
                            auto lq = data.autoStopped ? audio::QuantizeMode::None
                                : (recSlot ? recSlot->launchQuantize : audio::QuantizeMode::NextBar);
                            m_audioEngine.sendCommand(audio::LaunchClipMsg{ti, si, clipPtr, lq,
                                recSlot ? &recSlot->clipAutomation->lanes : nullptr,
                                recSlot ? recSlot->followAction : FollowAction{}});
                        }
                    }
                    // Clear recording state in UI
                    m_sessionPanel->setTrackRecording(ti, false, -1);
                    // Return the take buffer to the recording pool
                    // (zero-copy round-trip for the next take).
                    m_audioEngine.releaseRecordedAudioBuffer(ti);
                    data.ready.store(false, std::memory_order_release);
                }
            }
            else if constexpr (std::is_same_v<T, audio::RecordBufferFullEvent>) {
                LOG_WARN("Audio", "Recording buffer full on Track %d (%.0f sec limit). Auto-saved.",
                         msg.trackIndex + 1, 300.0);
            }
            else if constexpr (std::is_same_v<T, audio::FollowActionTriggeredEvent>) {
                // Resolve follow action target and launch the next clip
                int ti = msg.trackIndex;
                int si = msg.sceneIndex;
                int numScenes = m_project.numScenes();
                if (ti < 0 || ti >= m_project.numTracks() || numScenes <= 0) return;

                // Build list of occupied scenes for this track
                std::vector<int> occupied;
                for (int s = 0; s < numScenes; ++s) {
                    auto* slot = m_project.getSlot(ti, s);
                    if (slot && !slot->empty()) occupied.push_back(s);
                }
                if (occupied.empty()) return;

                int targetScene = -1;
                switch (msg.resolvedAction) {
                    case FollowActionType::Next: {
                        // Find next occupied scene after current
                        for (int s : occupied) {
                            if (s > si) { targetScene = s; break; }
                        }
                        if (targetScene < 0) targetScene = occupied.front(); // wrap
                        break;
                    }
                    case FollowActionType::Previous: {
                        for (int i = (int)occupied.size() - 1; i >= 0; --i) {
                            if (occupied[i] < si) { targetScene = occupied[i]; break; }
                        }
                        if (targetScene < 0) targetScene = occupied.back(); // wrap
                        break;
                    }
                    case FollowActionType::First:
                        targetScene = occupied.front();
                        break;
                    case FollowActionType::Last:
                        targetScene = occupied.back();
                        break;
                    case FollowActionType::Random:
                        targetScene = occupied[std::rand() % occupied.size()];
                        break;
                    case FollowActionType::Any: {
                        // Any other clip (exclude self)
                        std::vector<int> others;
                        for (int s : occupied) { if (s != si) others.push_back(s); }
                        if (!others.empty())
                            targetScene = others[std::rand() % others.size()];
                        else
                            targetScene = si; // only one clip, play again
                        break;
                    }
                    default: return;
                }

                if (targetScene < 0) return;
                auto* slot = m_project.getSlot(ti, targetScene);
                if (!slot || slot->empty()) return;

                // Launch immediately, NOT with the slot's launchQuantize. The
                // follow action already fired at the bar boundary it was timed
                // to; re-quantizing to the next bar would make the current clip
                // loop one extra bar before the next one starts.
                auto lq = audio::QuantizeMode::None;
                if (slot->audioClip) {
                    m_audioEngine.sendCommand(audio::LaunchClipMsg{
                        ti, targetScene, slot->audioClip.get(), lq,
                        &slot->clipAutomation->lanes, slot->followAction});
                } else if (slot->midiClip) {
                    m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{
                        ti, targetScene, slot->midiClip.get(), lq,
                        &slot->clipAutomation->lanes, slot->followAction});
                }
            }
        }, evt);
    }

    m_transportPanel->setTransportState(m_displayPlaying, m_displayBeats,
                                     m_audioEngine.transport().bpm(),
                                     m_audioEngine.transport().numerator(),
                                     m_audioEngine.transport().denominator());
    m_transportPanel->setSelectedScene(m_selectedScene);

    // Keep detail panel synced with current target
    if (m_showDetailPanel) {
        switch (m_detailTarget) {
            case DetailTarget::Track:
                updateDetailForSelectedTrack();
                break;
            case DetailTarget::ReturnBus:
                updateDetailForReturnBus(m_detailReturnBus);
                break;
            case DetailTarget::Master:
                updateDetailForMaster();
                break;
        }
    }
}

void App::render() {
    m_mainWindow.makeCurrent();

    int w = m_mainWindow.getWidth();
    int h = m_mainWindow.getHeight();
    glViewport(0, 0, w, h);

    glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_renderer.beginFrame(w, h);

    // Keep the v2 viewport in sync — widgets that position overlays
    // (Dropdown flip, Tooltip clamp) read this.
    m_fw2Context.viewport = {0.0f, 0.0f,
                              static_cast<float>(w),
                              static_cast<float>(h)};

    // Advance v2 per-frame timers (tooltip show delay, context menu
    // hover-to-open submenu). Both measure wall-clock between frames
    // via steady_clock — immune to NTP jumps and DST.
    {
        using clk = std::chrono::steady_clock;
        static auto lastTick = clk::now();
        const auto now = clk::now();
        const float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;
        ui::fw2::TooltipManager::instance().tick(dt);
        ui::fw2::ContextMenuManager::instance().tick(dt);
        ui::fw2::FwDropDown::tickGlobal(dt);
    }

    // Compute widget tree layout and render all panels
    computeLayout();
    m_rootLayout->render(m_fw2Context);

    // Menu bar — v2 FwMenuBar renders via MenuBarWrapper in the
    // rootLayout tree above. Its dropdown popup lives on LayerStack
    // and paints during m_fw2LayerStack.paintLayers() below, so it
    // sits on top of panels automatically.

    // v1 context menu render call retired — fw2::ContextMenu paints
    // via LayerStack::paintLayers below.

    // Modal dialogs (rendered on top of everything)
    {
        float sw = static_cast<float>(w);
        float sh = static_cast<float>(h);
        ui::fw::Rect screenBounds{0, 0, sw, sh};

        // v1 confirm dialog retired — fw2::Dialog paints via
        // LayerStack::paintLayers in the block below.
        // v1 TextInputDialog retired — FwTextInputDialog paints via
        // LayerStack too.
        // v1 About dialog retired — fw2::Dialog paints via LayerStack.
        // v1 PreferencesDialog + ExportDialog retired — their fw2
        // versions paint via their LayerStack overlay entries in the
        // paintLayers block below.
    }

    // Toasts draw last so they float above every dialog/panel.
    m_toastManager.render(m_renderer, m_font,
                          m_mainWindow.getWidth(), m_mainWindow.getHeight());

    // Live stem-separation progress overlay (centered box + bar). Drawn
    // from the worker's atomic progress; Esc cancels (see key handler).
    if (m_pendingStem && m_pendingStem->active.load() &&
        !m_pendingStem->done.load()) {
        const auto* ps = m_pendingStem.get();
        const float sw = static_cast<float>(w), sh = static_cast<float>(h);
        const float bw = 380.0f, bh = 96.0f;
        const float bx = (sw - bw) * 0.5f, by = (sh - bh) * 0.5f;
        m_renderer.drawRect(bx - 2, by - 2, bw + 4, bh + 4, ui::Color{0, 0, 0, 180});
        m_renderer.drawRect(bx, by, bw, bh, ui::Color{38, 38, 46, 255});
        m_renderer.drawRectOutline(bx, by, bw, bh, ui::Color{95, 95, 115, 255}, 1.0f);

        const float sc  = 13.0f / ui::Theme::kFontSize;
        const int   pct = static_cast<int>(ps->fraction.load() * 100.0f);
        const char* phase = ps->phase.load() == 0 ? "Downloading model\xE2\x80\xA6"
                                                   : "Separating stems\xE2\x80\xA6";
        m_font.drawText(m_renderer, phase, bx + 16, by + 14, sc,
                        ui::Color{225, 225, 235, 255});

        const float barX = bx + 16, barY = by + 44, barW = bw - 32, barH = 16;
        m_renderer.drawRect(barX, barY, barW, barH, ui::Color{22, 22, 28, 255});
        m_renderer.drawRect(barX, barY, barW * static_cast<float>(pct) / 100.0f,
                            barH, ui::Color{80, 160, 225, 255});
        m_renderer.drawRectOutline(barX, barY, barW, barH, ui::Color{70, 70, 85, 255}, 1.0f);

        char pbuf[16];
        std::snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
        m_font.drawText(m_renderer, pbuf, barX, barY + barH + 6, sc,
                        ui::Color{185, 190, 200, 255});
        const char* hint = "Esc to cancel";
        m_font.drawText(m_renderer, hint,
                        barX + barW - m_font.textWidth(hint, sc),
                        barY + barH + 6, sc, ui::Color{150, 155, 165, 255});
    }

    // v2 floating UI — modal scrim first (only when a modal is open),
    // then all layer entries in bottom-up z-order. Empty in practice
    // until the first v2 overlay widget lands, but the plumbing is
    // in place so those widgets can just push into m_fw2LayerStack.
    if (m_fw2LayerStack.hasModalActive()) {
        ui::fw2::paintModalScrim(m_fw2Context,
            ui::fw::Rect{0.0f, 0.0f,
                          static_cast<float>(m_mainWindow.getWidth()),
                          static_cast<float>(m_mainWindow.getHeight())});
    }
    m_fw2LayerStack.paintLayers(m_fw2Context,
        ui::fw::Rect{0.0f, 0.0f,
                      static_cast<float>(m_mainWindow.getWidth()),
                      static_cast<float>(m_mainWindow.getHeight())});

    m_renderer.endFrame();

    m_mainWindow.swap();

    // Visual output: render after the main UI has swapped. tick() saves and
    // restores the current GL context, so it's safe to call here without
    // disturbing the next main-window frame. Skipped during a video export —
    // the worker thread owns the output context then.
    if (!isVideoExporting() && m_visualEngine.isOutputVisible()) {
        const auto& transport = m_audioEngine.transport();
        const double sr = std::max(1.0, m_audioEngine.sampleRate());
        const double seconds = static_cast<double>(transport.positionInSamples()) / sr;
        const double beats   = transport.positionInBeats();
        m_visualEngine.tick(seconds, beats, transport.isPlaying());
    }
}

// ---------------------------------------------------------------------------
// Project file operations

} // namespace yawn
