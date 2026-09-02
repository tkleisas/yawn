// App_Lifecycle.cpp — init() (SDL/GL/audio/MIDI/panel wiring), run()
// and shutdown(). Split out of App.cpp.
#include "app/App.h"
#include "Version.h"
#include "visual/LiveInputEnum.h"
#include "visual/FfmpegShim.h"
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

bool App::init() {
    // App metadata must be set before SDL_Init so Wayland uses the right
    // app_id (must match the .desktop file basename to get the dock icon)
    // and X11 gets a proper WM_CLASS.
    SDL_SetAppMetadata("Y.A.W.N", YAWN_VERSION_STRING, "com.yawn.daw");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG_ERROR("App", "Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }

    // GL context attributes are owned by ui::Window::create — it runs
    // a fallback chain (3.3 core strict → relaxed framebuffer → legacy
    // request) for machines whose drivers reject the strict
    // core-profile/pixel-format combination, and shows an actionable
    // message box if no usable context exists at all.

    ui::WindowConfig winConfig;
    winConfig.title = "Y.A.W.N — Yet Another Audio Workstation New";
    winConfig.width = 1280;
    winConfig.height = 800;
    winConfig.resizable = true;

    if (!m_mainWindow.create(winConfig)) return false;

    // Query display DPI scale for HiDPI support
    float displayScale = SDL_GetWindowDisplayScale(m_mainWindow.getHandle());
    if (displayScale > 0.0f) {
        ui::Theme::scaleFactor = displayScale;
        // fw2 context needs the same scale — its dispatchMouseMove
        // divides raw event deltas by dpiScale to produce the
        // "logical" deltas widgets base drag math on. Without this,
        // HiDPI knobs would feel stiffer than their v1 counterparts
        // (or too fast, depending on which units SDL emits).
        m_fw2Context.setDpiScale(displayScale);
    }

    if (!m_renderer.init()) {
        LOG_ERROR("UI", "Failed to initialize 2D renderer");
        return false;
    }

    // Visual engine: creates secondary output window (hidden until toggled).
    // Shares GL resources with the main context, so creation happens while
    // the main context is still current.
    if (!m_visualEngine.init()) {
        LOG_WARN("Visual", "VisualEngine init failed — visual output disabled");
    }
    m_visualEngine.setAudioEngine(&m_audioEngine);
    m_mainWindow.makeCurrent();  // init() left the main context current, but be explicit

    // Load application icon — set window icon and create GL texture for About dialog
    {
        auto icon = util::loadIcon("assets/icon.png");
        if (!icon.pixels.empty()) {
            // SDL3 window icon
            SDL_Surface* surface = SDL_CreateSurfaceFrom(
                icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
                icon.pixels.data(), icon.width * 4);
            if (surface) {
                SDL_SetWindowIcon(m_mainWindow.getHandle(), surface);
                SDL_DestroySurface(surface);
            }
            // GL texture for About dialog
            m_iconTexture = util::createGLTexture(icon);
        }
    }

    loadFont();

    m_project.init();  // uses kDefaultNumTracks / kDefaultNumScenes
    setupDefaultTracks();
    // The visual-clip state machine — needs project + engine + visual
    // engine + host (this) in place; created before any launch path
    // can fire (menus/panels wire up in buildWidgetTree just below).
    m_visualController = std::make_unique<VisualClipController>(
        m_project, m_audioEngine, m_visualEngine, *this);
    m_virtualKeyboard.init(&m_audioEngine);
    setupMenuBar();
    buildWidgetTree();
    // v1 m_aboutDialog retired — fw2 About dialog is text-only for now.
    m_pianoRoll->setTransport(&m_audioEngine.transport());
    // Default new-note velocity: seed from saved settings, persist (debounced
    // via m_settingsDirty) when the user drags the toolbar Vel control or a
    // note's velocity bar.
    m_pianoRoll->setDrawVelocity(m_settings.pianoRollVelocity);
    m_pianoRoll->setOnDrawVelocityChanged([this](int vel7) {
        m_settings.pianoRollVelocity = vel7;
        m_settingsDirty = true;
        m_settingsDirtyAge = 0;
    });
    // Piano-roll structural edits never mutate the live clip in place
    // (that races the audio thread's note scan → UAF). They clone, edit
    // the clone, and ask us to swap it in atomically.
    m_pianoRoll->setOnReplaceClip(
        [this](const midi::MidiClip* oldClip,
               std::unique_ptr<midi::MidiClip> newClip) {
            return replaceEditedMidiClip(oldClip, std::move(newClip));
        });
    m_sessionPanel->init(&m_project, &m_audioEngine, &m_undoManager);
    m_mixerPanel->init(&m_project, &m_audioEngine, &m_midiEngine, &m_undoManager);
    m_mixerPanel->setLearnManager(&m_midiLearnManager);
    m_detailPanel->setLearnManager(&m_midiLearnManager);
    m_transportPanel->init(&m_project, &m_audioEngine, &m_undoManager);
    m_transportPanel->setLearnManager(&m_midiLearnManager);

    // Virtual-keyboard note velocity is set from the transport bar's
    // Vel selector (replaces the old MIDI-menu velocity presets). Sync
    // the initial level (Norm = 96) so the displayed level and the
    // keyboard agree from the start.
    m_transportPanel->setOnVelocityChanged([this](uint8_t vel7) {
        m_virtualKeyboard.setVelocity(vel7);
    });
    m_transportPanel->setVelocityLevel(2);   // Norm
    m_virtualKeyboard.setVelocity(96);

    // Session-style record orchestration. The Record button on the
    // transport panel calls back here so we can:
    //   * pick the target scene (selected scene, defaulting to 0)
    //   * for every track:
    //       - armed → start per-track recording into the target slot
    //         (audio = new take; MIDI = new clip — overdub flag stays
    //         false so existing clip is replaced rather than layered)
    //       - not armed → launch that scene's slot if it has a clip
    //         (audio / MIDI / visual)
    //   * send TransportRecordMsg LAST so the engine's auto-arm loop
    //     sees recording=true on each track and skips re-init
    //   * push the target scene to SessionPanel for the red record-
    //     target indicator next to the scene label
    //
    // Disarm path just sends TransportRecordMsg(false) which gracefully
    // finalises every per-track recording the engine knows about.
    //
    // Lua + MIDI controller paths still send TransportRecordMsg
    // directly (bypassing this orchestration); the AudioEngine's
    // handler now auto-arms BOTH armed MIDI and armed Audio tracks
    // for those callers, but they don't get the non-armed-track
    // launch behaviour. That's acceptable — the UI button is the
    // canonical entry point for the full session-record gesture.
    // Master automation-arm wiring. The transport panel flips the
    // visual state on click and tells us; we update the project
    // (so save/load round-trips) and forward to the engine via a
    // SetGlobalAutoRecordMsg. Initial state pushed below from the
    // currently-loaded project's globalAutoRecord field.
    m_transportPanel->setOnAutoArmPressed([this](bool armed) {
        m_project.setGlobalAutoRecord(armed);
        m_audioEngine.sendCommand(audio::SetGlobalAutoRecordMsg{armed});
        markDirty();
    });
    // ▸ARR transport button — arms/disarms session→arrangement capture
    // (and on disarm pops the "Bounce N captured clips?" confirm).
    m_transportPanel->setOnArrRecPressed([this]() { toggleArrangementRecord(); });
    m_transportPanel->setGlobalAutoArmed(m_project.globalAutoRecord());
    m_audioEngine.sendCommand(audio::SetGlobalAutoRecordMsg{
        m_project.globalAutoRecord()});

    // Scene-launch orchestration — left-click on the scene label
    // (column to the left of the clip grid) fires the same per-track
    // logic the Record button does PLUS the existing "stop empty-slot
    // tracks" behaviour the scene-click used to have. So one gesture
    // covers the band-practice workflow:
    //
    //   "I have armed track A and clips on B/C/D. Click scene 2 →
    //    metronome count-in, B/C/D play scene 2's clips, A records
    //    into scene 2's slot for as long as I'm holding the key."
    //
    // Differs from setOnRecordPressed in two ways:
    //   * Non-armed track with empty slot → STOPS that track (scene-
    //     click semantics) — Record button leaves it alone instead.
    //   * Transport-record state only flips ON when at least one
    //     track is armed, so the all-launch case (no armed tracks)
    //     doesn't unexpectedly arm transport recording.
    m_sessionPanel->setOnSceneLaunch([this](int sceneIdx) {
        if (sceneIdx < 0 || sceneIdx >= m_project.numScenes()) return;
        bool anyArmed = false;
        for (int t = 0; t < m_project.numTracks(); ++t) {
            if (m_project.track(t).armed) { anyArmed = true; break; }
        }

        // Will this launch advance the transport? It does if it's already
        // playing, if an armed track triggers a record count-in, or if the
        // scene has any audio/MIDI clip (whose LaunchClipMsg starts the
        // transport). Visual clips then defer to the same bar instead of
        // firing a bar early.
        bool willPlay = m_audioEngine.transport().isPlaying() || anyArmed;
        for (int t = 0; t < m_project.numTracks() && !willPlay; ++t) {
            auto* s = m_project.getSlot(t, sceneIdx);
            if (s && (s->audioClip || s->midiClip)) willPlay = true;
        }

        // ── PASS 1: armed tracks → Start*RecordMsg FIRST. ──
        // This ensures the StartMidiRecordMsg / StartAudioRecordMsg
        // handlers see `!m_transport.isPlaying()` and trigger the
        // count-in path (beginCountIn + pendingStart). If we sent
        // launches first, the LaunchClipMsg handler would call
        // m_transport.play() before the record-start handlers, and
        // the latter would fall into the "transport already playing,
        // no count-in" branch — recording would stamp recordStartBeat
        // immediately and capture 1 bar of "music played during
        // count-in" as the first bar of the clip.
        for (int t = 0; t < m_project.numTracks(); ++t) {
            auto& trk = m_project.track(t);
            if (!trk.armed) continue;
            // Pre-allocate this track's recording pool on the UI
            // thread so the engine's start handler never resizes on
            // the audio thread.
            m_audioEngine.prepareRecording(t);
            auto* armedSlot = m_project.getSlot(t, sceneIdx);
            const int recBars = armedSlot ? armedSlot->recordLengthBars : 0;
            if (trk.type == Track::Type::Midi) {
                m_audioEngine.sendCommand(audio::StartMidiRecordMsg{
                    t, sceneIdx, trk.midiOverdub, recBars});
            } else if (trk.type == Track::Type::Audio) {
                m_audioEngine.sendCommand(audio::StartAudioRecordMsg{
                    t, sceneIdx, /*overdub*/false, recBars});
            }
            trk.defaultScene = sceneIdx;
            // Visual tracks aren't recordable; ignore arming.
        }

        // ── PASS 2: non-armed tracks → launch / stop. ──
        // Now that count-in has been initialised by pass 1 (if any
        // track was armed and required it), launches use NextBar
        // quantize and naturally land on the post-count-in boundary
        // via my AudioEngine resetQuantizeCheck() at the count-in
        // → play transition.
        for (int t = 0; t < m_project.numTracks(); ++t) {
            auto& trk = m_project.track(t);
            if (trk.armed) continue;
            auto* slot = m_project.getSlot(t, sceneIdx);
            if (slot && slot->audioClip) {
                m_audioEngine.sendCommand(audio::LaunchClipMsg{
                    t, sceneIdx, slot->audioClip.get(),
                    slot->launchQuantize, &slot->clipAutomation->lanes,
                    slot->followAction, slot->autoRecordDisabled});
                trk.defaultScene = sceneIdx;
            } else if (slot && slot->midiClip) {
                m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{
                    t, sceneIdx, slot->midiClip.get(),
                    slot->launchQuantize, &slot->clipAutomation->lanes,
                    slot->followAction, slot->autoRecordDisabled});
                trk.defaultScene = sceneIdx;
            } else if (slot && slot->visualClip) {
                // Honour the slot's launchQuantize so the video/shader
                // starts on the same bar boundary as the quantized
                // audio/MIDI clips in this scene instead of immediately.
                launchVisualClipQuantized(t, sceneIdx, willPlay);
                trk.defaultScene = sceneIdx;
                m_sessionPanel->updateClipState(t, true, 0,
                                                  sceneIdx, false, 0.0);
            } else if (trk.type != Track::Type::Visual) {
                // Empty slot on a non-armed audio/MIDI track →
                // stop whatever was playing. Matches the inline
                // SessionPanel scene-click semantics.
                m_audioEngine.sendCommand(audio::StopClipMsg{t});
                m_audioEngine.sendCommand(audio::StopMidiClipMsg{t});
                trk.defaultScene = -1;
            } else {
                // Empty slot on a visual track → clear the layer.
                m_visualEngine.clearLayer(t);
                m_sessionPanel->updateClipState(t, false, 0, -1,
                                                  false, 0.0);
                trk.defaultScene = -1;
            }
        }

        // Set transport-record arm + indicator only when SOMETHING
        // armed asked to record. Pure launch (no armed tracks) leaves
        // transport recording alone.
        if (anyArmed) {
            m_recordTargetScene = sceneIdx;
            m_sessionPanel->setRecordTargetScene(sceneIdx);
            m_audioEngine.sendCommand(audio::TransportRecordMsg{
                true, sceneIdx});
        }
        markDirty();
    });

    m_transportPanel->setOnRecordPressed([this](bool arm) {
        if (arm) {
            const int targetScene = std::max(0, m_selectedScene);
            m_recordTargetScene = targetScene;
            m_sessionPanel->setRecordTargetScene(targetScene);

            // PASS 1: armed tracks → Start*RecordMsg first so the
            // count-in branch fires before any LaunchClipMsg starts
            // the transport. See setOnSceneLaunch comment for the
            // full rationale.
            for (int t = 0; t < m_project.numTracks(); ++t) {
                const auto& trk = m_project.track(t);
                if (!trk.armed) continue;
                // Pre-allocate the recording pool on the UI thread
                // (see setOnSceneLaunch).
                m_audioEngine.prepareRecording(t);
                auto* armedSlot = m_project.getSlot(t, targetScene);
                const int recBars = armedSlot ? armedSlot->recordLengthBars : 0;
                if (trk.type == Track::Type::Midi) {
                    m_audioEngine.sendCommand(audio::StartMidiRecordMsg{
                        t, targetScene, trk.midiOverdub, recBars});
                } else if (trk.type == Track::Type::Audio) {
                    // Audio recording is always new-take per the
                    // v0.61 design discussion — no per-track
                    // overdub flag.
                    m_audioEngine.sendCommand(audio::StartAudioRecordMsg{
                        t, targetScene, /*overdub*/false, recBars});
                }
                // Visual tracks aren't recordable; ignore arming
                // them silently.
            }
            // PASS 2: non-armed tracks → launches.
            for (int t = 0; t < m_project.numTracks(); ++t) {
                const auto& trk = m_project.track(t);
                if (trk.armed) continue;
                auto* slot = m_project.getSlot(t, targetScene);
                if (!slot || slot->empty()) continue;
                const auto lq = slot->launchQuantize;
                if (slot->audioClip) {
                    m_audioEngine.sendCommand(audio::LaunchClipMsg{
                        t, targetScene, slot->audioClip.get(), lq,
                        &slot->clipAutomation->lanes, slot->followAction,
                        slot->autoRecordDisabled});
                } else if (slot->midiClip) {
                    m_audioEngine.sendCommand(audio::LaunchMidiClipMsg{
                        t, targetScene, slot->midiClip.get(), lq,
                        &slot->clipAutomation->lanes, slot->followAction,
                        slot->autoRecordDisabled});
                } else if (slot->visualClip) {
                    launchVisualClipQuantized(t, targetScene,
                                              /*transportWillPlay*/true);
                    m_sessionPanel->updateClipState(t, /*playing*/true,
                                                      /*playPos*/0, targetScene);
                }
            }
            m_audioEngine.sendCommand(audio::TransportRecordMsg{true, targetScene});
        } else {
            m_audioEngine.sendCommand(audio::TransportRecordMsg{false, 0});
            m_recordTargetScene = -1;
            m_sessionPanel->setRecordTargetScene(-1);
        }
    });

    m_returnMasterPanel->init(&m_project, &m_audioEngine, &m_undoManager);
    m_returnMasterPanel->setLearnManager(&m_midiLearnManager);
    m_mixerPanel->setOnReturnToggle([this](bool show) {
        m_showReturns = show;
    });
    m_mixerPanel->setOnTrackSelected([this](int t) {
        LOG_INFO("User", "selectTrack %d (via mixer)", t);
        m_selectedTrack = t;
        m_detailTarget = DetailTarget::Track;
        m_virtualKeyboard.setTargetTrack(t);
        m_sessionPanel->setSelectedTrack(t);
        m_arrangementPanel->setSelectedTrack(t);
        if (m_showDetailPanel) updateDetailForSelectedTrack();
    });

    // Wire return/master strip clicks → open detail panel
    m_returnMasterPanel->setOnReturnClick([this](int bus) {
        m_detailTarget = DetailTarget::ReturnBus;
        m_detailReturnBus = bus;
        m_showDetailPanel = true;
        m_detailPanel->setOpen(true);
        updateDetailForReturnBus(bus);
    });
    m_returnMasterPanel->setOnMasterClick([this]() {
        m_detailTarget = DetailTarget::Master;
        m_detailReturnBus = -1;
        m_showDetailPanel = true;
        m_detailPanel->setOpen(true);
        updateDetailForMaster();
    });

    // Right-click on return/master strip → show "Add Effect" context menu
    // resolveChain returns the correct EffectChain* at execution time
    auto buildFxMenu = [this](DetailTarget target, int bus, float mx, float my) {
        auto resolveChain = [this, target, bus]() -> effects::EffectChain* {
            if (target == DetailTarget::ReturnBus) return &m_audioEngine.mixer().returnEffects(bus);
            if (target == DetailTarget::Master)    return &m_audioEngine.mixer().masterEffects();
            return nullptr;
        };

        using namespace ui::fw2::Menu;
        using ui::fw2::MenuEntry;
        std::vector<MenuEntry> fxItems;
        auto addFx = [&](const char* label, auto factory) {
            fxItems.push_back(item(label, [this, label, factory, target, bus, resolveChain]() {
                auto* chain = resolveChain();
                if (!chain) return;
                chain->append(factory());
                int slot = chain->count() - 1;
                markDirty();
                std::string fxName = label;
                m_undoManager.push({"Add Effect: " + fxName,
                    [this, resolveChain, slot]{
                        if (auto* c = resolveChain()) c->removeRetired(slot);
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, resolveChain, factory]{
                        if (auto* c = resolveChain()) c->append(factory());
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                // Open detail panel to show the new effect
                m_detailTarget = target;
                m_detailReturnBus = bus;
                m_showDetailPanel = true;
                m_detailPanel->setOpen(true);
                m_detailPanel->clear(); // force rebuild
            }));
        };
        // Built-in audio effects — from the single descriptor table
        // (util/Factory.h), same as every other Add-Effect menu.
        for (const auto& d : audioEffectDescriptors())
            addFx(d.displayName, d.make);

#ifdef YAWN_HAS_VST3
        if (m_vst3Scanner && !m_vst3Scanner->effects().empty()) {
            fxItems.push_back(separator());
            fxItems.push_back(header("── VST3 ──"));
            for (auto& info : m_vst3Scanner->effects()) {
                std::string vlabel = info.name;
                if (!info.vendor.empty()) vlabel += " (" + info.vendor + ")";
                std::string modulePath = info.modulePath;
                std::string classID = info.classIDString;
                fxItems.push_back(item(vlabel, [this, modulePath, classID, target, bus, resolveChain]() {
                    auto* chain = resolveChain();
                    if (!chain) return;
                    chain->append(std::make_unique<vst3::VST3Effect>(modulePath, classID));
                    markDirty();
                    m_detailTarget = target;
                    m_detailReturnBus = bus;
                    m_showDetailPanel = true;
                    m_detailPanel->setOpen(true);
                    m_detailPanel->clear();
                }));
            }
        }
#endif

        ui::fw2::ContextMenu::show(std::move(fxItems),
                                     ui::fw::Point{mx, my});
    };

    m_returnMasterPanel->setOnReturnRightClick([this, buildFxMenu](int bus, float mx, float my) {
        buildFxMenu(DetailTarget::ReturnBus, bus, mx, my);
    });
    m_returnMasterPanel->setOnMasterRightClick([this, buildFxMenu](float mx, float my) {
        buildFxMenu(DetailTarget::Master, -1, mx, my);
    });

    m_settings = util::AppSettings::load();

    // Apply persisted UI font scale to the fw2 theme before any widget
    // measures itself — multiplying the baked-in font sizes makes every
    // v2 widget (menu bar, dropdowns, dialogs …) pick up the scale
    // through theme().metrics.
    {
        ui::fw2::Theme t;
        const float s = std::max(0.5f, std::min(3.0f, m_settings.fontScale));
        t.metrics.fontSize      *= s;
        t.metrics.fontSizeSmall *= s;
        t.metrics.fontSizeLarge *= s;
        ui::fw2::setTheme(std::move(t));
    }

    audio::AudioEngineConfig audioConfig;
    audioConfig.sampleRate = m_settings.sampleRate;
    audioConfig.framesPerBuffer = m_settings.bufferSize;
    audioConfig.outputChannels = 2;
    audioConfig.outputDevice = m_settings.outputDevice;
    audioConfig.inputDevice = m_settings.inputDevice;

    if (!m_audioEngine.init(audioConfig)) {
        LOG_WARN("Audio", "Audio engine failed to initialize");
    } else if (!m_audioEngine.start()) {
        LOG_WARN("Audio", "Audio engine failed to start");
    }

    // ── Controller scripting (must init BEFORE MidiEngine to claim ports) ──
    m_controllerManager.init(&m_audioEngine, &m_project);
    m_controllerManager.setSelectedTrackGetter([this]() { return m_selectedTrack; });
    m_controllerManager.setSelectedTrackSetter([this](int t) {
        if (t >= 0 && t < m_project.numTracks()) {
            LOG_INFO("User", "selectTrack %d (via controller)", t);
            m_selectedTrack = t;
            if (m_sessionPanel) m_sessionPanel->setSelectedTrack(t);
        }
    });
    m_controllerManager.setCommandSender([this](const audio::AudioCommand& cmd) {
        m_audioEngine.sendCommand(cmd);
    });
    m_controllerManager.setTapTempoHandler([this]() {
        if (m_transportPanel) m_transportPanel->tapTempo();
    });
    m_controllerManager.setClipStateGetter([this](int track, int scene)
        -> controllers::ControllerManager::ClipSlotState {
        controllers::ControllerManager::ClipSlotState st;
        if (track < 0 || track >= m_project.numTracks()) return st;
        if (scene < 0 || scene >= m_project.numScenes()) return st;
        auto* slot = m_project.getSlot(track, scene);
        if (slot) {
            if (slot->audioClip) st.type = 1;
            else if (slot->midiClip) st.type = 2;
        }
        st.armed = m_project.track(track).armed;
        if (m_sessionPanel) {
            auto& ts = m_sessionPanel->trackState(track);
            st.playing = ts.playing && ts.playingScene == scene;
            st.recording = ts.recording && ts.recordingScene == scene;
        }
        return st;
    });
    m_controllerManager.setClipLauncher([this](int track, int scene) {
        if (track < 0 || track >= m_project.numTracks()) return;
        if (scene < 0 || scene >= m_project.numScenes()) return;
        if (m_sessionPanel)
            m_sessionPanel->launchClipSlot(track, scene);
    });
    m_controllerManager.setSceneLauncher([this](int scene) {
        if (scene < 0 || scene >= m_project.numScenes()) return;
        if (m_sessionPanel)
            m_sessionPanel->launchScene(scene);
    });
    m_controllerManager.setToastHandler(
        [this](const std::string& msg, float dur, int sev) {
            m_toastManager.show(msg, dur,
                static_cast<ui::ToastManager::Severity>(sev));
        });
    m_controllerManager.scanScripts("scripts/controllers");
    m_controllerManager.autoConnect();

    // Wire MidiEngine: scan ports, open inputs, connect to AudioEngine
    // Skip ports claimed by controller scripts (exclusive access on Windows)
    auto claimedInputs = m_controllerManager.claimedInputPortNames();
    m_midiEngine.refreshPorts();
    LOG_INFO("MIDI", "Available inputs: %d, claimed by controller: %d",
             m_midiEngine.availableInputCount(), static_cast<int>(claimedInputs.size()));
    for (int i = 0; i < m_midiEngine.availableInputCount(); ++i)
        LOG_INFO("MIDI", "  Input %d: '%s'", i, m_midiEngine.availableInputName(i).c_str());
    for (auto& cn : claimedInputs)
        LOG_INFO("MIDI", "  Claimed: '%s'", cn.c_str());

    if (!m_settings.enabledMidiInputs.empty()) {
        LOG_INFO("MIDI", "Opening user-configured inputs (%d)", static_cast<int>(m_settings.enabledMidiInputs.size()));
        for (int i : m_settings.enabledMidiInputs) {
            bool claimed = false;
            if (i < m_midiEngine.availableInputCount()) {
                for (auto& cn : claimedInputs)
                    if (m_midiEngine.availableInputName(i) == cn)
                    { claimed = true; break; }
            }
            if (!claimed) {
                LOG_INFO("MIDI", "  Opening input %d: '%s'", i,
                         i < m_midiEngine.availableInputCount() ? m_midiEngine.availableInputName(i).c_str() : "?");
                m_midiEngine.openInputPort(i);
            } else {
                LOG_INFO("MIDI", "  Skipping input %d: '%s' (claimed)", i, m_midiEngine.availableInputName(i).c_str());
            }
        }
    } else {
        LOG_INFO("MIDI", "Opening all available inputs");
        for (int i = 0; i < m_midiEngine.availableInputCount(); ++i) {
            bool claimed = false;
            for (auto& cn : claimedInputs)
                if (m_midiEngine.availableInputName(i) == cn)
                { claimed = true; break; }
            if (!claimed) {
                LOG_INFO("MIDI", "  Opening input %d: '%s'", i, m_midiEngine.availableInputName(i).c_str());
                m_midiEngine.openInputPort(i);
            } else {
                LOG_INFO("MIDI", "  Skipping input %d: '%s' (claimed)", i, m_midiEngine.availableInputName(i).c_str());
            }
        }
    }
    for (int i : m_settings.enabledMidiOutputs)
        m_midiEngine.openOutputPort(i);
    m_audioEngine.setMidiEngine(&m_midiEngine);
    m_midiEngine.setMonitorBuffer(&m_midiMonitor);
    // Controller-claimed ports bypass MidiEngine — route them into the
    // same monitor buffer so the MIDI Monitor panel shows Push / Move /
    // etc. activity just like any other input. Port-index base 100
    // keeps controller entries visually separate from regular inputs.
    m_controllerManager.setMonitorBuffer(&m_midiMonitor, 100);

    // Wire MIDI Learn manager
    m_midiEngine.setLearnManager(&m_midiLearnManager);
    m_midiEngine.setCommandSender([this](const audio::AudioCommand& cmd) {
        m_audioEngine.sendCommand(cmd);
    });

    // Wire MIDI monitor to browser panel
    m_browserPanel->setMidiMonitor(&m_midiMonitor);
    m_browserPanel->setPortNameFn([this](int idx) -> std::string {
        if (idx >= 0 && idx < m_midiEngine.availableInputCount())
            return m_midiEngine.availableInputName(idx);
        // Controller-claimed ports are forwarded to the monitor with
        // portIdxBase >= 100 (set in setMonitorBuffer above). Surface
        // them with the connected script's name, so the panel column
        // reads e.g. "Ableton Move" instead of a bare "100".
        if (idx >= 100 && m_controllerManager.isConnected())
            return m_controllerManager.connectedName();
        return std::to_string(idx);
    });

    // ── Library database & scanner ──
    if (m_libraryDb.open()) {
        m_libraryScanner = std::make_unique<library::LibraryScanner>(m_libraryDb);
        m_browserPanel->setLibraryDatabase(&m_libraryDb);
        m_browserPanel->setLibraryScanner(m_libraryScanner.get());

        // Files tab: "Add Folder" button → open folder dialog
        m_browserPanel->filesTab().setOnAddFolder([this]() {
            SDL_ShowOpenFolderDialog([](void* ud, const char* const* filelist, int) {
                auto* self = static_cast<App*>(ud);
                if (filelist && filelist[0]) {
                    self->m_libraryDb.addLibraryPath(filelist[0]);
                    auto paths = self->m_libraryDb.getLibraryPaths();
                    if (!paths.empty()) {
                        auto& lp = paths.back();
                        self->m_libraryScanner->scanLibraryPath(lp.id, lp.path);
                    }
                    self->m_browserPanel->filesTab().refreshTree();
                }
            }, this, m_mainWindow.getHandle(), nullptr, false);
        });

        // Files tab: double-click → load into selected clip slot
        m_browserPanel->filesTab().setOnFileDoubleClick([this](const std::string& path) {
            loadClipToSlot(path, m_selectedTrack, m_selectedScene);
        });

        // Files tab: remove folder
        m_browserPanel->filesTab().setOnRemoveFolder([this](int64_t pathId) {
            m_libraryDb.removeLibraryPath(pathId);
            m_browserPanel->filesTab().refreshTree();
        });

        // Files tab: rescan folder
        m_browserPanel->filesTab().setOnRescanFolder([this](int64_t pathId, const std::string& path) {
            m_libraryScanner->scanLibraryPath(pathId, path);
        });

        // Presets tab: double-click → load preset on selected track
        m_browserPanel->presetsTab().setOnPresetDoubleClick(
            [this](const std::string& path, const std::string& deviceId) {
                if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
                PresetData data;
                if (!PresetManager::loadPreset(path, data)) return;

                // Try to match against instrument first, then effects
                auto* inst = m_audioEngine.instrument(m_selectedTrack);
                if (inst && inst->id() == deviceId) {
                    loadDevicePreset(path, *inst);
                    updateDetailForSelectedTrack();
                    return;
                }
                auto& fxChain = m_audioEngine.mixer().trackEffects(m_selectedTrack);
                for (int i = 0; i < fxChain.count(); ++i) {
                    auto* fx = fxChain.effectAt(i);
                    if (fx && fx->id() == deviceId) {
                        loadDevicePreset(path, *fx);
                        updateDetailForSelectedTrack();
                        return;
                    }
                }
            });

        // Loops tab: double-click → load the .mid into the selected MIDI
        // slot (m_selectedTrack / m_selectedScene).
        m_browserPanel->loopsTab().setOnLoopDoubleClick(
            [this](const std::string& path) {
                loadMidiLoopIntoSlot(path, m_selectedTrack, m_selectedScene);
            });

        // Loops tab: mouse-down on a row arms a potential drag-to-slot.
        // The global mouse-move handler promotes it once the cursor
        // moves past the threshold; mouse-up drops it onto the session
        // cell under the pointer.
        m_browserPanel->loopsTab().setOnLoopDragStart(
            [this](const std::string& path) {
                m_loopDragArmedPath = path;
                m_loopDragArmed     = true;
                m_loopDragStartX    = m_lastMouseX;
                m_loopDragStartY    = m_lastMouseY;
            });

        // Seed the factory drum-loop construction kit + the melodic
        // (bass/lead/chord/misc) loops (idempotent — only writes patterns
        // that don't already exist) before the scan indexes them.
        DrumPatterns::seedFactoryLoops();
        MelodicPatterns::seedFactoryLoops();

        // Initial data load
        m_browserPanel->filesTab().refreshTree();
        m_browserPanel->presetsTab().refreshList();
        m_browserPanel->loopsTab().refreshList();

        // Start background scan
        m_libraryScanner->startFullScan();
    }

    // ── Models tab (independent of the loop DB) ──
    {
        std::vector<std::string> dirs;
        dirs.push_back((std::filesystem::current_path() / "assets" / "examples" / "3d").string());
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE");
        std::error_code mec;
        std::filesystem::path userModels =
            std::filesystem::path(home ? home : ".") / ".yawn" / "models3d";
        std::filesystem::create_directories(userModels, mec);
        dirs.push_back(userModels.string());
        m_browserPanel->modelsTab().setLibraryDirs(dirs, /*bundledCount*/1);
        m_browserPanel->modelsTab().setOnAssign(
            [this](const std::string& absPath) { assignModelFromLibrary(absPath); });
        // Render-time cache lookup; generation runs at frame-start (update()).
        m_browserPanel->modelsTab().setThumbnailLookup(
            [this](const std::string& p) -> unsigned {
                return m_visualEngine.cachedModelThumbnail(p);
            });
    }

    // Apply metronome settings from saved preferences
    m_audioEngine.sendCommand(audio::MetronomeSetVolumeMsg{m_settings.metronomeVolume});
    m_audioEngine.sendCommand(audio::MetronomeSetModeMsg{m_settings.metronomeMode});
    m_audioEngine.sendCommand(audio::TransportSetCountInMsg{m_settings.countInBars});
    m_transportPanel->setCountInBars(m_settings.countInBars);
    m_transportPanel->setMetronomeVisualStyle(m_settings.metronomeVisualStyle);
    m_transportPanel->setLinkAllowed(m_settings.linkEnabled);

    // Apply Plugin Delay Compensation toggle from saved preferences.
    // setPdcEnabled is a plain bool flag read by the audio thread —
    // safe to set without going through the message queue.
    m_audioEngine.mixer().setPdcEnabled(m_settings.latencyCompensation);
    m_audioEngine.mixer().setMasterOversample(m_settings.masterOversample);

    // Apply Ableton Link toggle from saved preferences. The transport
    // panel's Link button reads this same flag, so the saved state
    // shows up correctly after restart.
    m_audioEngine.linkManager().enable(m_settings.linkEnabled);
    m_audioEngine.linkManager().enableStartStopSync(m_settings.linkStartStopSync);

    // Sync project track properties to the audio engine
    syncTracksToEngine();

    // Wire mixer arm button to MidiEngine
    m_mixerPanel->setOnTrackArmedChanged([this](int trackIndex, bool armed) {
        LOG_INFO("User", "armTrack %d %s", trackIndex, armed ? "armed" : "disarmed");
        m_midiEngine.setTrackArmed(trackIndex, armed);
        // Arming a MIDI track selects it for virtual keyboard input
        if (armed && trackIndex < m_project.numTracks() &&
            m_project.track(trackIndex).type == Track::Type::Midi) {
            m_selectedTrack = trackIndex;
            m_virtualKeyboard.setTargetTrack(trackIndex);
            m_sessionPanel->setSelectedTrack(trackIndex);
            m_mixerPanel->setSelectedTrack(trackIndex);
        }
    });

    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

    // Cache system cursors
    m_cursorDefault  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    m_cursorEWResize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    m_cursorNSResize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    m_cursorMove     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);

    m_running = true;

    // Wire up detail panel remove device callback
    m_detailPanel->setOnRemoveDevice([this](ui::fw2::DetailPanelWidget::DeviceType type, int chainIndex) {
        const char* typeName =
            (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx)     ? "midiFx" :
            (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) ? "instrument" :
            (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx)    ? "audioFx" :
                                                                            "?";
        const char* targetName =
            (m_detailTarget == DetailTarget::Track)     ? "track" :
            (m_detailTarget == DetailTarget::ReturnBus) ? "returnBus" :
                                                          "master";
        LOG_INFO("User", "removeDevice type=%s target=%s/%d chainIdx=%d",
                 typeName, targetName,
                 m_detailTarget == DetailTarget::Track     ? m_selectedTrack :
                 m_detailTarget == DetailTarget::ReturnBus ? m_detailReturnBus : 0,
                 chainIndex);
        // Resolve which effect chain to operate on based on detail target
        auto getAudioFxChain = [this]() -> effects::EffectChain* {
            switch (m_detailTarget) {
                case DetailTarget::Track:
                    if (m_selectedTrack >= 0 && m_selectedTrack < m_project.numTracks())
                        return &m_audioEngine.mixer().trackEffects(m_selectedTrack);
                    return nullptr;
                case DetailTarget::ReturnBus:
                    if (m_detailReturnBus >= 0 && m_detailReturnBus < kMaxReturnBuses)
                        return &m_audioEngine.mixer().returnEffects(m_detailReturnBus);
                    return nullptr;
                case DetailTarget::Master:
                    return &m_audioEngine.mixer().masterEffects();
            }
            return nullptr;
        };

        switch (type) {
            case ui::fw2::DetailPanelWidget::DeviceType::MidiFx: {
                // MIDI effects only apply to tracks
                if (m_detailTarget != DetailTarget::Track) break;
                int t = m_selectedTrack;
                if (t < 0 || t >= m_project.numTracks()) break;
                auto* fx = m_audioEngine.midiEffectChain(t).effect(chainIndex);
                std::string fxName = fx ? fx->name() : "";
                m_audioEngine.midiEffectChain(t).removeEffectRetired(chainIndex);
                m_audioEngine.sendCommand(audio::SendMidiToTrackMsg{
                    t, (uint8_t)midi::MidiMessage::Type::ControlChange, 0, 0, 0, 0, 123});
                LOG_INFO("MIDI", "Removed MIDI effect %d from track %d", chainIndex, t + 1);
                m_undoManager.push({"Remove MIDI Effect",
                    [this, t, chainIndex, fxName]{
                        auto inst = createMidiEffectByName(fxName);
                        if (inst) {
                            auto& chain = m_audioEngine.midiEffectChain(t);
                            chain.addEffect(std::move(inst));
                            for (int i = chain.count() - 1; i > chainIndex; --i)
                                chain.moveEffect(i, i - 1);
                        }
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, t, chainIndex]{
                        m_audioEngine.midiEffectChain(t).removeEffectRetired(chainIndex);
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            case ui::fw2::DetailPanelWidget::DeviceType::AudioFx: {
                auto* chain = getAudioFxChain();
                if (!chain) break;
                auto* fx = chain->effectAt(chainIndex);
                std::string fxName = fx ? fx->name() : "";
                chain->removeRetired(chainIndex);

                // Capture target state for undo
                auto target = m_detailTarget;
                int bus = m_detailReturnBus;
                int t = m_selectedTrack;

                auto resolveChain = [this, target, bus, t]() -> effects::EffectChain* {
                    switch (target) {
                        case DetailTarget::Track:     return &m_audioEngine.mixer().trackEffects(t);
                        case DetailTarget::ReturnBus: return &m_audioEngine.mixer().returnEffects(bus);
                        case DetailTarget::Master:    return &m_audioEngine.mixer().masterEffects();
                    }
                    return nullptr;
                };

                std::string label = (target == DetailTarget::Track)
                    ? "Remove Audio Effect" : (target == DetailTarget::ReturnBus)
                    ? "Remove Return Effect" : "Remove Master Effect";

                m_undoManager.push({label,
                    [this, resolveChain, chainIndex, fxName]{
                        auto inst = createAudioEffectByName(fxName);
                        if (inst) {
                            if (auto* c = resolveChain()) c->insert(chainIndex, std::move(inst));
                        }
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, resolveChain, chainIndex]{
                        if (auto* c = resolveChain()) c->removeRetired(chainIndex);
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            default:
                break;
        }
        m_detailPanel->clear();
        markDirty();
    });

    m_detailPanel->setOnMoveDevice([this](ui::fw2::DetailPanelWidget::DeviceType type, int fromIdx, int toIdx) {
        switch (type) {
            case ui::fw2::DetailPanelWidget::DeviceType::MidiFx: {
                if (m_detailTarget != DetailTarget::Track) break;
                int t = m_selectedTrack;
                if (t < 0 || t >= m_project.numTracks()) break;
                m_audioEngine.midiEffectChain(t).moveEffect(fromIdx, toIdx);
                m_audioEngine.sendCommand(audio::ResetMidiEffectChainMsg{t});
                LOG_INFO("MIDI", "Moved MIDI effect %d to %d on track %d", fromIdx, toIdx, t + 1);
                m_undoManager.push({"Move MIDI Effect",
                    [this, t, fromIdx, toIdx]{
                        m_audioEngine.midiEffectChain(t).moveEffect(toIdx, fromIdx);
                        m_audioEngine.sendCommand(audio::ResetMidiEffectChainMsg{t});
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, t, fromIdx, toIdx]{
                        m_audioEngine.midiEffectChain(t).moveEffect(fromIdx, toIdx);
                        m_audioEngine.sendCommand(audio::ResetMidiEffectChainMsg{t});
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            case ui::fw2::DetailPanelWidget::DeviceType::AudioFx: {
                // Resolve the target chain
                effects::EffectChain* chain = nullptr;
                auto target = m_detailTarget;
                int bus = m_detailReturnBus;
                int t = m_selectedTrack;
                switch (target) {
                    case DetailTarget::Track:
                        if (t >= 0 && t < m_project.numTracks())
                            chain = &m_audioEngine.mixer().trackEffects(t);
                        break;
                    case DetailTarget::ReturnBus:
                        if (bus >= 0 && bus < kMaxReturnBuses)
                            chain = &m_audioEngine.mixer().returnEffects(bus);
                        break;
                    case DetailTarget::Master:
                        chain = &m_audioEngine.mixer().masterEffects();
                        break;
                }
                if (!chain) break;
                chain->moveEffect(fromIdx, toIdx);

                auto resolveChain = [this, target, bus, t]() -> effects::EffectChain* {
                    switch (target) {
                        case DetailTarget::Track:     return &m_audioEngine.mixer().trackEffects(t);
                        case DetailTarget::ReturnBus: return &m_audioEngine.mixer().returnEffects(bus);
                        case DetailTarget::Master:    return &m_audioEngine.mixer().masterEffects();
                    }
                    return nullptr;
                };

                std::string label = (target == DetailTarget::Track)
                    ? "Move Audio Effect" : (target == DetailTarget::ReturnBus)
                    ? "Move Return Effect" : "Move Master Effect";

                m_undoManager.push({label,
                    [this, resolveChain, fromIdx, toIdx]{
                        if (auto* c = resolveChain()) c->moveEffect(toIdx, fromIdx);
                        m_detailPanel->clear(); markDirty();
                    },
                    [this, resolveChain, fromIdx, toIdx]{
                        if (auto* c = resolveChain()) c->moveEffect(fromIdx, toIdx);
                        m_detailPanel->clear(); markDirty();
                    }, ""});
                break;
            }
            default:
                break;
        }
        m_detailPanel->clear();
        markDirty();
    });

    // Wire automation recording: knob touch → AutoParamTouchMsg
    m_detailPanel->setOnParamTouch([this](int track, uint8_t tt, int ci, int pi, float v, bool touching) {
        m_audioEngine.sendCommand(audio::AutoParamTouchMsg{track, tt, ci, pi, v, touching});
    });

    // Clip-automation edit commit (lane add / point add from the
    // envelope editor). Copy-on-write: swap the slot's automation box
    // via replaceSlotAutomation (old box retires to the graveyard),
    // notify the engine so a playing clip re-points next block, then
    // rebind the panel to the new box.
    m_detailPanel->setOnClipAutomationCommit(
        [this](int track, std::vector<automation::AutomationLane> lanes) {
            auto* slot = m_project.getSlot(track, m_selectedScene);
            if (!slot) return;
            m_project.replaceSlotAutomation(*slot, std::move(lanes));
            publishClipAutomation(track, m_selectedScene);
            m_detailPanel->setClipAutomation(&slot->clipAutomation->lanes, track);
            markDirty();
        });

    // Right-click on an instrument / audio FX / MIDI FX knob →
    // build the MacroTarget for it and open the macro-mapping
    // context menu rooted at the click. The chain index is only
    // meaningful for the audio/MIDI effect kinds; instrument
    // mappings ignore it (target.index defaulted to 0).
    m_detailPanel->setOnParamRightClick(
        [this](int trackIdx, ui::fw2::DetailPanelWidget::DeviceType type,
               int chainIndex, const std::string& paramName,
               int /*paramIdx*/, float mx, float my) {
            if (trackIdx < 0) return;
            // Selecting a different track via the macro menu would
            // show the wrong track's macros — sync the selection so
            // showMacroMappingMenu (which reads m_selectedTrack)
            // operates on the device the user actually clicked.
            m_selectedTrack = trackIdx;
            MacroTarget t;
            t.paramName = paramName;
            switch (type) {
                case ui::fw2::DetailPanelWidget::DeviceType::Instrument:
                    t.kind  = MacroTarget::Kind::AudioInstrumentParam;
                    t.index = 0;
                    break;
                case ui::fw2::DetailPanelWidget::DeviceType::AudioFx:
                    t.kind  = MacroTarget::Kind::AudioEffectParam;
                    t.index = chainIndex;
                    break;
                case ui::fw2::DetailPanelWidget::DeviceType::MidiFx:
                    t.kind  = MacroTarget::Kind::MidiEffectParam;
                    t.index = chainIndex;
                    break;
            }
            showMacroMappingMenu(t, mx, my);
        });

    // Wire preset click: open context menu with preset list + Save
    m_detailPanel->setOnPresetClick([this](ui::fw2::DetailPanelWidget::DeviceType type,
                                           int chainIndex, float mx, float my) {
        // Resolve device pointer and device ID
        std::string deviceId;
        std::string deviceName;

        // Helper to get audio effect from the right chain
        auto getAudioEffect = [this](int chainIndex) -> effects::AudioEffect* {
            switch (m_detailTarget) {
                case DetailTarget::Track:
                    return m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex);
                case DetailTarget::ReturnBus:
                    return m_audioEngine.mixer().returnEffects(m_detailReturnBus).effectAt(chainIndex);
                case DetailTarget::Master:
                    return m_audioEngine.mixer().masterEffects().effectAt(chainIndex);
            }
            return nullptr;
        };

        if (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) {
            if (m_detailTarget != DetailTarget::Track) return;
            if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
            auto* inst = m_audioEngine.instrument(m_selectedTrack);
            if (!inst) return;
            deviceId = inst->id();
            deviceName = inst->name();
        } else if (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx) {
            auto* fx = getAudioEffect(chainIndex);
            if (!fx) return;
            deviceId = fx->id();
            deviceName = fx->name();
        } else if (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx) {
            if (m_detailTarget != DetailTarget::Track) return;
            if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
            auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
            if (!fx) return;
            deviceId = fx->id();
            deviceName = fx->name();
        }
        auto presets = PresetManager::listPresetsForDevice(deviceId);
        using namespace ui::fw2::Menu;
        using ui::fw2::MenuEntry;
        std::vector<MenuEntry> items;

        // Add "Save Preset..." item
        auto target = m_detailTarget;
        int bus = m_detailReturnBus;
        items.push_back(item("Save Preset...", [this, type, chainIndex, deviceId, deviceName, target, bus]() {
            SDL_StartTextInput(m_mainWindow.getHandle());
            m_textInputDialog.prompt("Save Preset", "My Preset",
                [this, type, chainIndex, deviceId, deviceName, target, bus](const std::string& name) {
                    std::filesystem::path saved;
                    if (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) {
                        auto* inst = m_audioEngine.instrument(m_selectedTrack);
                        if (inst) saved = saveDevicePreset(name, *inst);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx) {
                        effects::AudioEffect* fx = nullptr;
                        switch (target) {
                            case DetailTarget::Track:     fx = m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex); break;
                            case DetailTarget::ReturnBus: fx = m_audioEngine.mixer().returnEffects(bus).effectAt(chainIndex); break;
                            case DetailTarget::Master:    fx = m_audioEngine.mixer().masterEffects().effectAt(chainIndex); break;
                        }
                        if (fx) saved = saveDevicePreset(name, *fx);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx) {
                        auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
                        if (fx) saved = saveDevicePreset(name, *fx);
                    }
                    if (!saved.empty()) {
                        m_detailPanel->setDevicePresetName(type, chainIndex, name);
                        LOG_INFO("Preset", "Saved '%s' for %s", name.c_str(), deviceName.c_str());
                        // Re-scan the presets folder so the new file lands
                        // in the library DB → Browser's Presets tab picks
                        // it up on its next render. Without this, the file
                        // is on disk but the DB-backed list stays stale
                        // until the next full library scan / app restart.
                        // scanPresets() is async (worker thread) and does
                        // an UPSERT per file, so re-running it on every
                        // save is cheap (~ms for hundreds of files).
                        if (m_libraryScanner) m_libraryScanner->scanPresets();
                    }
                });
        }));

        // Separator if there are presets
        if (!presets.empty()) {
            items.push_back(separator());
        }

        // Add each preset as a loadable item
        for (const auto& preset : presets) {
            std::filesystem::path path = preset.filePath;
            std::string pName = preset.name;
            items.push_back(item(preset.name,
                [this, type, chainIndex, path, pName, target, bus]() {
                    bool ok = false;
                    if (type == ui::fw2::DetailPanelWidget::DeviceType::Instrument) {
                        auto* inst = m_audioEngine.instrument(m_selectedTrack);
                        if (inst) ok = loadDevicePreset(path, *inst);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::AudioFx) {
                        effects::AudioEffect* fx = nullptr;
                        switch (target) {
                            case DetailTarget::Track:     fx = m_audioEngine.mixer().trackEffects(m_selectedTrack).effectAt(chainIndex); break;
                            case DetailTarget::ReturnBus: fx = m_audioEngine.mixer().returnEffects(bus).effectAt(chainIndex); break;
                            case DetailTarget::Master:    fx = m_audioEngine.mixer().masterEffects().effectAt(chainIndex); break;
                        }
                        if (fx) ok = loadDevicePreset(path, *fx);
                    } else if (type == ui::fw2::DetailPanelWidget::DeviceType::MidiFx) {
                        auto* fx = m_audioEngine.midiEffectChain(m_selectedTrack).effect(chainIndex);
                        if (fx) ok = loadDevicePreset(path, *fx);
                    }
                    if (ok) {
                        m_detailPanel->setDevicePresetName(type, chainIndex, pName);
                        m_detailPanel->clear(); // force rebuild
                        LOG_INFO("Preset", "Loaded '%s'", pName.c_str());
                    }
                }
            ));
        }

        ui::fw2::ContextMenu::show(std::move(items),
                                 ui::fw::Point{mx, my});
    });

    // Wire the LFO target picker: the LFO device strip is clickable and
    // shows its modulation target by name. App owns the engine/visual
    // enumeration, so it builds the menu and resolves the display name.
    m_detailPanel->setOnLfoTargetMenu(
        [this](int track, int chain, float mx, float my) {
            showLfoTargetMenu(track, chain, mx, my);
        });
    m_detailPanel->setLfoTargetNameResolver(
        [this](int track, int chain) { return lfoTargetName(track, chain); });

    // Wire Auto-Sample request from MultisamplerDisplayPanel.
    // Builds the dialog Context from current project + engine state and
    // opens the modal. Refreshes the detail panel on completion so newly-
    // populated zones show up in the zone list.
    m_detailPanel->setOnAutoSampleRequested([this](instruments::Multisampler* ms) {
        if (!ms) return;
        if (m_projectPath.empty()) {
            // Captured samples live under <project>/samples/, so we need
            // a saved project as the destination root. Surface this as a
            // visible modal rather than just a log line — silently
            // doing nothing is the worst kind of UX feedback.
            LOG_WARN("AutoSample",
                "Save the project first — captured samples live under "
                "<project>/samples/.");
            ui::fw2::DialogSpec spec;
            spec.title   = "Save Project First";
            spec.message =
                "Auto-Sample captures land in <project>/samples/<name>/, "
                "which needs a saved project to write to.\n\n"
                "Use File → Save As… to give this project a folder, "
                "then click Auto-Sample again.";
            spec.buttons = { {"OK", {}, /*primary*/true, /*cancel*/true} };
            ui::fw2::Dialog::show(std::move(spec));
            return;
        }
        // Build a default capture name from the current track's name,
        // so a track called "MIDI 1" gets "midi_1_capture" by default.
        std::string trackName;
        if (m_selectedTrack >= 0 && m_selectedTrack < m_project.numTracks()) {
            trackName = m_project.track(m_selectedTrack).name;
        }

        ui::fw2::FwAutoSampleDialog::Context ctx;
        ctx.engine             = &m_audioEngine;
        ctx.midi               = &m_midiEngine;
        ctx.target             = ms;
        ctx.samplesRoot        = m_projectPath / "samples";
        ctx.defaultCaptureName = audio::defaultCaptureName(trackName);

        m_autoSampleDialog.setOnResult(
            [this](ui::fw2::FwAutoSampleDialog::Result r) {
                // Always release SDL text-input mode on close so
                // subsequent focus changes don't keep the IME armed.
                SDL_StopTextInput(m_mainWindow.getHandle());
                if (r == ui::fw2::FwAutoSampleDialog::Result::Done) {
                    // Force the detail panel to rebuild so the new zones
                    // populated by the worker show up in the zone list.
                    m_detailPanel->clear();
                    markDirty();
                    LOG_INFO("AutoSample", "Capture finished — zones added");
                } else if (r == ui::fw2::FwAutoSampleDialog::Result::Cancelled) {
                    LOG_INFO("AutoSample", "Capture cancelled");
                } else {
                    LOG_ERROR("AutoSample", "Capture failed");
                }
            });
        // Capture-name field needs SDL text-input mode active to receive
        // keystrokes. We start it on open and stop it in the result
        // callback above; the in-dialog onKey path forwards Backspace /
        // Delete / arrows etc. directly to FwTextInput::onKeyDown.
        SDL_StartTextInput(m_mainWindow.getHandle());
        m_autoSampleDialog.open(ctx);
    });

    // ── Sidechain plumbing for sidechain-aware instrument panels ──
    // Vocoder's display panel hosts an inline source dropdown — these
    // three callbacks bridge it to the project + audio engine without
    // pulling Project into DetailPanelWidget directly. (Same surface
    // is reusable for ring-mod / gate / etc. when they grow sidechain
    // UIs.)
    m_detailPanel->setTrackNamesProvider([this]() {
        std::vector<std::string> names;
        const int n = m_project.numTracks();
        names.reserve(n);
        for (int i = 0; i < n; ++i) names.push_back(m_project.track(i).name);
        return names;
    });
    m_detailPanel->setSidechainSourceProvider([this](int trackIdx) {
        if (trackIdx < 0 || trackIdx >= m_project.numTracks()) return -1;
        return m_project.track(trackIdx).sidechainSource;
    });
    m_detailPanel->setOnSetSidechainSource([this](int trackIdx, int sourceIdx) {
        if (trackIdx < 0 || trackIdx >= m_project.numTracks()) return;
        // Sanity: refuse self-routing (the picker hides self anyway,
        // but defensive — Mixer I/O could in theory push junk).
        if (sourceIdx == trackIdx) sourceIdx = -1;
        // Cycle guard: if dst → src would close a longer feedback
        // loop (A→B→C→A …), reject the assignment outright. The
        // engine has its own check as defense-in-depth, but doing it
        // here keeps the project state consistent with what the
        // engine will actually run, and lets us surface a warning to
        // the user / log instead of silently dropping the request on
        // the audio thread.
        if (m_project.wouldCreateSidechainCycle(trackIdx, sourceIdx)) {
            LOG_WARN("Sidechain",
                     "Refused track %d <- track %d: would create feedback loop",
                     trackIdx, sourceIdx);
            // Re-sync UI to whatever the project actually has so the
            // dropdown snaps back to the previous selection on next
            // displayUpdater tick.
            return;
        }
        m_project.track(trackIdx).sidechainSource = sourceIdx;
        m_audioEngine.sendCommand(audio::SetSidechainSourceMsg{trackIdx, sourceIdx});
        markDirty();
    });

    // ConvolutionReverb IR loader — opens an SDL file dialog,
    // decodes via FileIO::loadAudioFile (libsndfile), sums to mono,
    // pushes into the effect's loadIRMono. The Conv Reverb display
    // panel triggers this via its "Load IR…" button. Stashes the
    // ConvolutionReverb pointer in m_pendingConvIRReverb because
    // SDL_ShowOpenFileDialog's callback signature is C-style with
    // a void* userdata, and lambda captures don't survive that.
    // Neural Amp .nam loader — mirrors the Conv Reverb IR loader
    // exactly. Stash the effect ptr in m_pendingNamEffect because
    // SDL_ShowOpenFileDialog's C-style callback only carries a
    // void* userdata. NAM's actual file read + Reset + prewarm
    // happens inside NeuralAmp::setModelPath; we just hand it the
    // path the user picked.
    m_detailPanel->setOnLoadNamModel([this](effects::NeuralAmp* na) {
        if (!na) return;
        m_pendingNamEffect = na;
        static SDL_DialogFileFilter filter{"Neural Amp Models", "nam"};
        // Open the dialog pre-pointed at the bundled NAM folder
        // so first-time users see the starter amp captures
        // immediately (no navigation required). Falls back to
        // null (system default) if the bundled folder doesn't
        // exist — release builds always include it; dev builds
        // before the assets-copy step might not.
        const std::filesystem::path bundled =
            std::filesystem::current_path() / "assets" / "nam";
        const std::string defaultLoc = std::filesystem::exists(bundled)
            ? bundled.string() : std::string{};
        SDL_ShowOpenFileDialog(
            [](void* ud, const char* const* filelist, int) {
                auto* self = static_cast<App*>(ud);
                auto* eff  = self->m_pendingNamEffect;
                self->m_pendingNamEffect = nullptr;
                if (!eff || !filelist || !filelist[0]) return;
                eff->setModelPath(filelist[0]);
            },
            this, m_mainWindow.getHandle(),
            &filter, 1,
            defaultLoc.empty() ? nullptr : defaultLoc.c_str(),
            /*allow_many*/ false);
    });

    // Per-pad fx context menu — surfaced from a right-click on a
    // DrumRack pad. Lists every audio effect we can build (mirrors
    // the track effect chain "Add Effect" submenu) plus, when the
    // pad already has a chain, a Remove submenu listing its
    // current effects. The menu mutates the rack's pad chain
    // directly; the audio thread picks up the change on its next
    // block via the chain's lock-free m_count read.
    m_detailPanel->setOnDrumPadFxMenu(
        [this](instruments::DrumRack* dr, int note, float sx, float sy) {
            if (!dr) return;
            using namespace ui::fw2::Menu;
            using ui::fw2::MenuEntry;
            std::vector<MenuEntry> items;

            // "Add Pad FX →" submenu — every audio effect plus
            // every loaded VST3 effect.
            std::vector<MenuEntry> addItems;
            auto addFx = [this, dr, note, &addItems](const char* label,
                            std::function<std::unique_ptr<effects::AudioEffect>()> make) {
                addItems.push_back(item(label, [this, dr, note, make]() {
                    auto* chain = dr->padFxChain(note);
                    if (chain) chain->append(make());
                    LOG_INFO("User", "DrumRack pad %d → add fx", note);
                }));
            };
            // Built-in audio effects — from the single descriptor
            // table (util/Factory.h).
            for (const auto& d : audioEffectDescriptors())
                addFx(d.displayName, d.make);
            items.push_back(submenu("Add Pad FX", std::move(addItems)));

            // Remove submenu — only when the pad has a chain with
            // at least one effect on it.
            auto* existing = dr->padFxChainOrNull(note);
            if (existing && existing->count() > 0) {
                std::vector<MenuEntry> remItems;
                for (int i = 0; i < existing->count(); ++i) {
                    auto* fx = existing->effectAt(i);
                    if (!fx) continue;
                    std::string label = fx->name();
                    int slot = i;
                    remItems.push_back(item(label, [this, dr, note, slot]() {
                        auto* chain = dr->padFxChain(note);
                        if (chain) chain->removeRetired(slot);
                        LOG_INFO("User", "DrumRack pad %d → remove fx slot %d", note, slot);
                    }));
                }
                items.push_back(submenu("Remove Pad FX", std::move(remItems)));
                items.push_back(item("Clear All Pad FX", [dr, note]() {
                    dr->clearPadFx(note);
                }));
            }

            ui::fw2::ContextMenu::show(std::move(items),
                                         ui::fw::Point{sx, sy});
        });

    // Per-chain fx context menu — surfaced from a right-click on an
    // InstrumentRack chain row. Mirrors the per-pad DrumRack menu
    // above. Pad / chain submenu structure is identical: Add Chain
    // FX → list, Remove Chain FX → list, Clear All. The menu mutates
    // the rack's per-chain fx chain directly; audio thread picks up
    // changes on the next block via lock-free m_count.
    m_detailPanel->setOnInstrumentRackChainFxMenu(
        [this](instruments::InstrumentRack* ir, int chainIdx,
                  float sx, float sy) {
            if (!ir || chainIdx < 0 || chainIdx >= ir->chainCount()) return;
            using namespace ui::fw2::Menu;
            using ui::fw2::MenuEntry;
            std::vector<MenuEntry> items;

            // "Change Instrument →" submenu — swaps the chain's
            // nested instrument for any of the available types.
            // InstrumentRack is intentionally absent so users can't
            // nest a rack inside itself (would confuse the outer-
            // rack tick-rebuild fingerprint logic).
            std::vector<MenuEntry> swapItems;
            auto addSwap = [this, ir, chainIdx, &swapItems](
                    const char* label,
                    std::function<std::unique_ptr<instruments::Instrument>()> make) {
                swapItems.push_back(item(label, [this, ir, chainIdx, make]() {
                    auto inst = make();
                    if (!inst) return;
                    // setInstrument() on the engine inits the
                    // instrument with the engine's SR + block size;
                    // for chain instruments we mirror that init path
                    // manually since they don't go through the
                    // engine's instrument slot. The rack itself
                    // already shares m_sampleRate/m_maxBlockSize, so
                    // we can just re-init via the chain accessor's
                    // helper — but the simplest thing is to push it
                    // through the rack's own init() pass on the next
                    // process block. Calling init here ensures the
                    // synth is ready immediately for any UI param
                    // edits the user makes before the next note-on.
                    inst->init(m_audioEngine.sampleRate(),
                                ir->maxBlockSize());
                    // The detail panel caches raw pointers to the
                    // chain instrument's widget; clear those before
                    // destroying the old instrument so the next
                    // render doesn't dereference freed memory.
                    m_detailPanel->clear();
                    auto& ch = ir->chain(chainIdx);
                    ch.instrument = std::move(inst);
                    LOG_INFO("User", "InstrumentRack chain %d → swap "
                                      "instrument", chainIdx);
                    // Repopulate the panel immediately so the user
                    // sees the new instrument's knobs without having
                    // to click the track again.
                    updateDetailForSelectedTrack();
                }));
            };
            // Built-in instruments — from the single descriptor table
            // (util/Factory.h). Note: the rack's swap list omits
            // Instrument Rack itself (no nested racks).
            for (const auto& d : instrumentDescriptors()) {
                if (d.id == std::string("instrack")) continue;
                addSwap(d.displayName, d.make);
            }
            items.push_back(submenu("Change Instrument", std::move(swapItems)));
            items.push_back(separator());

            std::vector<MenuEntry> addItems;
            auto addFx = [this, ir, chainIdx, &addItems](const char* label,
                            std::function<std::unique_ptr<effects::AudioEffect>()> make) {
                addItems.push_back(item(label, [this, ir, chainIdx, make]() {
                    auto* chain = ir->chainFxChain(chainIdx);
                    if (chain) chain->append(make());
                    LOG_INFO("User", "InstrumentRack chain %d → add fx",
                              chainIdx);
                }));
            };
            // Built-in audio effects — from the single descriptor
            // table (util/Factory.h).
            for (const auto& d : audioEffectDescriptors())
                addFx(d.displayName, d.make);
            items.push_back(submenu("Add Chain FX", std::move(addItems)));

            auto* existing = ir->chainFxChainOrNull(chainIdx);
            if (existing && existing->count() > 0) {
                std::vector<MenuEntry> remItems;
                for (int i = 0; i < existing->count(); ++i) {
                    auto* fx = existing->effectAt(i);
                    if (!fx) continue;
                    std::string label = fx->name();
                    int slot = i;
                    remItems.push_back(item(label, [this, ir, chainIdx, slot]() {
                        auto* chain = ir->chainFxChain(chainIdx);
                        if (chain) chain->removeRetired(slot);
                        LOG_INFO("User", "InstrumentRack chain %d → "
                                          "remove fx slot %d",
                                  chainIdx, slot);
                    }));
                }
                items.push_back(submenu("Remove Chain FX", std::move(remItems)));
                items.push_back(item("Clear All Chain FX",
                                  [ir, chainIdx]() {
                    ir->clearChainFx(chainIdx);
                }));
            }

            ui::fw2::ContextMenu::show(std::move(items),
                                         ui::fw::Point{sx, sy});
        });

    m_detailPanel->setOnLoadConvIR([this](effects::ConvolutionReverb* cr) {
        if (!cr) return;
        m_pendingConvIRReverb = cr;
        static SDL_DialogFileFilter filter{"Audio files", "wav;flac;aif;aiff;ogg;mp3"};
        // Open the dialog pre-pointed at the bundled Voxengo IR
        // folder so first-time users see the bundled options
        // immediately (no navigation required). Falls back to
        // null (system default) if the bundled folder doesn't
        // exist — release builds always include it; dev builds
        // before the assets-copy step might not.
        const std::filesystem::path bundled =
            std::filesystem::current_path() / "assets" / "reverbs" / "voxengo";
        const std::string defaultLoc = std::filesystem::exists(bundled)
            ? bundled.string() : std::string{};
        SDL_ShowOpenFileDialog(
            [](void* ud, const char* const* filelist, int) {
                auto* self = static_cast<App*>(ud);
                auto* eff  = self->m_pendingConvIRReverb;
                self->m_pendingConvIRReverb = nullptr;
                if (!eff || !filelist || !filelist[0]) return;
                self->loadIRIntoConvReverb(eff, filelist[0]);
            },
            this, m_mainWindow.getHandle(),
            &filter, 1,
            defaultLoc.empty() ? nullptr : defaultLoc.c_str(),
            /*allow_many*/ false);
    });

#ifdef YAWN_HAS_VST3
    // Initialize VST3 plugin scanner
    m_vst3Scanner = std::make_unique<vst3::VST3Scanner>();
    std::string vst3CachePath = (util::AppSettings::settingsPath().parent_path()
                                  / "vst3_cache.json").string();
    m_vst3Scanner->loadCache(vst3CachePath);
    if (!m_vst3Scanner->scanComplete()) {
        // Scan out-of-process: each module is enumerated by the
        // yawn_vst3_host subprocess, so a crashy plugin only fails
        // its own cache entry instead of taking down the app at
        // startup (and looping, since the old cache only existed
        // after a *complete* in-process scan). The v2 cache is
        // written incrementally, so a killed scan resumes.
        const std::string hostExe = vst3::VST3Scanner::hostExePath();
        namespace fs = std::filesystem;
        if (!hostExe.empty() && fs::exists(hostExe)) {
            LOG_INFO("VST3", "Scanning for VST3 plugins (subprocess)...");
            m_vst3Scanner->scanOutOfProcess(vst3CachePath, hostExe);
        } else {
            LOG_WARN("VST3",
                     "yawn_vst3_host not found next to the app binary — "
                     "falling back to in-process scan (a crashy plugin "
                     "can crash this scan)");
            LOG_INFO("VST3", "Scanning for VST3 plugins...");
            m_vst3Scanner->scan();
            m_vst3Scanner->saveCache(vst3CachePath);
        }
    }
    LOG_INFO("VST3", "Found %d VST3 plugins (%d instruments, %d effects)",
             (int)m_vst3Scanner->plugins().size(),
             (int)m_vst3Scanner->instruments().size(),
             (int)m_vst3Scanner->effects().size());
#endif

    LOG_INFO("App", "Y.A.W.N initialized successfully");
    LOG_INFO("App", "  [Space] Play/Stop        [Up/Down] BPM +/-");
    LOG_INFO("App", "  [Home] Reset position    [M] Toggle mixer");
    LOG_INFO("App", "  [D] Toggle detail panel");
    LOG_INFO("App", "  Virtual keyboard: Q2W3ER5T6Y7UI9O0P  [Z/X] Octave down/up");
    LOG_INFO("App", "  Click clip slots to launch/stop");
    LOG_INFO("App", "  Drag & drop audio files to load clips");
    LOG_INFO("App", "  [Esc] Quit");

    // Surface FFmpeg-build gaps loudly — silently missing video support
    // makes Visual tracks look broken (no Set Video, empty Live Input
    // submenu) and the user has no way to know without reading source.
    // Log on stdout (so it lands in yawn.log) AND show a sticky toast.
#if !defined(YAWN_HAS_VIDEO)
    LOG_INFO("App", "[!] Video support DISABLED — this build has no FFmpeg.");
    LOG_INFO("App", "    Visual tracks can run shaders only; video files");
    LOG_INFO("App", "    and live capture (webcams) won't work.");
    LOG_INFO("App", "    Fix: download ffmpeg-master-latest-win64-gpl-shared");
    LOG_INFO("App", "    from BtbN/FFmpeg-Builds, then reconfigure CMake");
    LOG_INFO("App", "    with -DFFMPEG_ROOT=<extracted folder>.");
    m_toastManager.show("Video support disabled — set FFMPEG_ROOT and rebuild "
                        "to enable video files / webcams",
                        8.0f, ui::ToastManager::Severity::Warn);
#elif !defined(_WIN32)
    // POSIX: video is compiled in, but the libav* set is dlopen()'d at
    // runtime by FfmpegShim — the host may still lack a compatible
    // FFmpeg (wrong soname major, or none installed). Probe now so the
    // gap is surfaced once, loudly, instead of as silent menu failures.
    if (!visual::ff::probe()) {
        LOG_INFO("App", "[!] Video support UNAVAILABLE at runtime: %s",
                 visual::ff::loadError().c_str());
        LOG_INFO("App", "    Visual tracks can run shaders only; video files");
        LOG_INFO("App", "    and live capture (webcams) won't work.");
        LOG_INFO("App", "    Fix: install FFmpeg 6 or 7 (e.g. 'sudo apt install");
        LOG_INFO("App", "    ffmpeg'), or use the AppImage which bundles them.");
        m_toastManager.show("Video support unavailable — no compatible FFmpeg "
                            "(6/7) found on this system",
                            8.0f, ui::ToastManager::Severity::Warn);
    } else if (!visual::ff::hasAvdevice()) {
        LOG_INFO("App", "[!] Live capture UNAVAILABLE — libavdevice not found "
                 "on this system at runtime.");
        LOG_INFO("App", "    Video files work, but webcams / device URLs won't.");
        LOG_INFO("App", "    Fix: 'sudo apt install libavdevice61' (FFmpeg 7) or");
        LOG_INFO("App", "    'libavdevice60' (FFmpeg 6).");
        m_toastManager.show("Live capture unavailable — libavdevice missing on "
                            "this system",
                            6.0f, ui::ToastManager::Severity::Warn);
    }
#elif !defined(YAWN_HAS_AVDEVICE)
    LOG_INFO("App", "[!] Live capture DISABLED — libavdevice not linked.");
    LOG_INFO("App", "    Video files work, but webcams / dshow streams won't.");
    LOG_INFO("App", "    Fix: rebuild against an FFmpeg distro that ships");
    LOG_INFO("App", "    avdevice (BtbN's gpl-shared bundle does).");
    m_toastManager.show("Live capture disabled — libavdevice missing from "
                        "this FFmpeg build",
                        6.0f, ui::ToastManager::Severity::Warn);
#endif

    // TCP UI command channel — off unless YAWN_CMD names a port.
    // Started last so every verb (menus, dialogs, device adds) acts
    // on a fully-built UI.
    initUiCommandServer();

    updateWindowTitle();
    return true;
}

void App::run() {
    while (m_running) {
        processEvents();
        update();
        render();
        // Periodic monitoring-latency log line (self-throttled to ~2 s).
        // Surfaces cpu load, measured ADC→DAC round-trip latency, and
        // xrun count so latency drift over time is visible in the log.
        m_audioEngine.pollLatencyLog();
    }
}

void App::shutdown() {
    // Stop controller scripts
    m_controllerManager.shutdown();

    // Stop library scanner before anything else
    if (m_libraryScanner) m_libraryScanner->stop();
    m_libraryScanner.reset();
    m_libraryDb.close();

#ifdef YAWN_HAS_VST3
    m_vst3Editors.clear();
    m_vst3Scanner.reset();
#endif
    m_audioEngine.shutdown();
    // Tear down visual output window + GL resources while SDL is still up.
    m_visualEngine.shutdown();
    // Destroy cached cursors
    if (m_cursorDefault)  SDL_DestroyCursor(m_cursorDefault);
    if (m_cursorEWResize) SDL_DestroyCursor(m_cursorEWResize);
    if (m_cursorNSResize) SDL_DestroyCursor(m_cursorNSResize);
    if (m_cursorMove)     SDL_DestroyCursor(m_cursorMove);
    if (m_iconTexture) { glDeleteTextures(1, &m_iconTexture); m_iconTexture = 0; }
    m_renderer.shutdown();
    m_font.destroy();
    m_mainWindow.destroy();
    SDL_Quit();
    LOG_INFO("App", "Y.A.W.N shutdown complete");
}


} // namespace yawn
