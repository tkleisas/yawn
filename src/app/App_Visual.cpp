// App_Visual.cpp — visual/VJ engine glue: clip launch stamping,
// arrangement playback + follow-action polling, macro mappings,
// visual-knob automation, shader/model/scene path resolution +
// localization, model clip ops, video import. Split out of App.cpp.
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

void App::stampVisualLaunch(int track, int scene) {
    if (track < 0 || track >= kMaxTracks) return;
    // Session launches use wall-clock iTime (starts at 0, advances with
    // real time). Stamping the launch beat + scene gives the per-clip
    // visual envelope poller its time origin — without it the poller is
    // gated out and the envelope stays frozen until the clip is clicked.
    m_visualEngine.setLayerWallClock(track);
    m_visualLaunchBeat[track]  = m_audioEngine.transport().positionInBeats();
    m_visualLaunchScene[track] = scene;
}

void App::launchVisualClipQuantized(int track, int scene, bool transportWillPlay) {
    if (track < 0 || track >= kMaxTracks) return;
    auto* slot = m_project.getSlot(track, scene);
    if (!slot || !slot->visualClip) return;

    const auto q = slot->launchQuantize;
    const bool playing = m_audioEngine.transport().isPlaying();

    // Launch immediately when there's no quantize, or when nothing is going
    // to advance the transport for us to sync against (a lone visual launch
    // while stopped). Otherwise defer to the boundary the audio/MIDI clips
    // will start on — including the from-stopped case, where the transport
    // spins up at the parked position and the clips fire on the next bar
    // (see ClipEngine::checkPendingLaunches).
    if (q == audio::QuantizeMode::None || (!playing && !transportWillPlay)) {
        m_visualHasPending[track] = false;
        launchVisualClipData(track, *slot->visualClip, slot->visualClip->firstShaderPath());
        stampVisualLaunch(track, scene);
        // A tempo-synced clip is driven by the transport beat clock, so it
        // needs the transport running to advance. Audio/MIDI launches
        // auto-start it (AudioEngine LaunchClipMsg); a lone visual launch
        // does not, so start it here — otherwise a synced clip would sit
        // frozen at beat 0. Free-running clips (wall-clock) don't need it.
        if (slot->visualClip->tempoSync &&
            !m_audioEngine.transport().isPlaying())
            m_audioEngine.sendCommand(audio::TransportPlayMsg{});
        return;
    }

    // Defer to the next bar/beat boundary, matching the audio engine's
    // quantize so video and audio start together.
    const double cur = m_audioEngine.transport().positionInBeats();
    const int beatsPerBar = std::max(1, m_audioEngine.transport().numerator());
    double fire = (q == audio::QuantizeMode::NextBeat)
        ? std::floor(cur) + 1.0
        : (std::floor(cur / beatsPerBar) + 1.0) * beatsPerBar;

    m_visualHasPending[track]    = true;
    m_visualPendingScene[track]  = scene;
    m_visualPendingFireBeat[track] = fire;
}

void App::pollVisualLaunchQueue() {
    const bool playing = m_audioEngine.transport().isPlaying();
    const double beat = m_audioEngine.transport().positionInBeats();
    for (int t = 0; t < kMaxTracks; ++t) {
        if (!m_visualHasPending[t]) continue;
        // Fire once the transport is playing and has reached the boundary.
        // Pending launches are only created when something will advance the
        // transport, so this won't get stuck while stopped.
        if (!playing || beat < m_visualPendingFireBeat[t]) continue;
        const int scene = m_visualPendingScene[t];
        m_visualHasPending[t] = false;
        auto* slot = m_project.getSlot(t, scene);
        if (slot && slot->visualClip) {
            launchVisualClipData(t, *slot->visualClip, slot->visualClip->firstShaderPath());
            stampVisualLaunch(t, scene);
        }
    }
}

void App::launchVisualClipData(int track,
                                const visual::VisualClip& vc,
                                const std::string& shaderPath) {
    const int audioSource = vc.audioSource;

    // Clip without a custom shader → fall back to the bundled
    // passthrough so whatever's feeding iChannel2 (file video, live
    // input, or 3D model render) shows up full-frame.
    std::string effectiveShader = shaderPath;
    const bool hasFileVideo = !vc.videoPath.empty();
    const bool hasLiveVideo = vc.liveInput && !vc.liveUrl.empty();
    const bool hasModel     = !vc.modelPath.empty();
    const bool hasImage     = !vc.imagePath.empty();
    if (effectiveShader.empty() && hasModel) {
        effectiveShader = "assets/shaders/model_passthrough.frag";
    } else if (effectiveShader.empty() && hasImage) {
        effectiveShader = "assets/shaders/image_fit.frag";
    } else if (effectiveShader.empty() &&
                (hasFileVideo || hasLiveVideo)) {
        effectiveShader = "assets/shaders/video_passthrough.frag";
    }
    if (effectiveShader.empty()) return;

    std::string resolved = resolveShaderPath(effectiveShader);
    m_visualEngine.loadLayer(track, resolved, audioSource);
    m_visualEngine.setLayerBlendMode(track,
        static_cast<visual::VisualEngine::BlendMode>(
            m_project.track(track).visualBlendMode));

    m_visualEngine.applyLayerParamValues(track, vc.firstPassParamValues());

    // Effect chain — additional passes after the source. The chain
    // lives on the *track* (audio-FX-style) so it persists across
    // clip switches; we just re-push it here whenever the layer is
    // re-launched. Empty chain clears any prior extras. Bypassed
    // passes are still pushed (and compiled) so toggling them on is
    // instant; the engine skips them at render time.
    {
        std::vector<visual::VisualEngine::ChainPassSpec> extras;
        if (track >= 0 && track < m_project.numTracks()) {
            const auto& chain = m_project.track(track).visualEffectChain;
            extras.reserve(chain.size());
            for (const auto& p : chain) {
                visual::VisualEngine::ChainPassSpec spec;
                spec.shaderPath  = resolveShaderPath(p.shaderPath);
                spec.paramValues = p.paramValues;
                spec.bypassed    = p.bypassed;
                extras.push_back(std::move(spec));
            }
        }
        m_visualEngine.setLayerAdditionalPasses(track, extras);
    }

    // Macro values + LFO config flow from the *track*, not the clip.
    // Phase 4.1 — track.macros owns the 8 knob values and modulators;
    // launching a clip just re-pushes the current track-level state
    // into the freshly loaded layer so visuals start in sync.
    if (track >= 0 && track < m_project.numTracks()) {
        const auto& macros = m_project.track(track).macros;
        for (int i = 0; i < MacroDevice::kNumMacros; ++i) {
            m_visualEngine.setLayerKnob(track, i, macros.values[i]);
            const auto& s = macros.lfos[i];
            visual::VisualLFO lfo;
            lfo.enabled = s.enabled;
            lfo.shape   = static_cast<visual::VisualLFO::Shape>(s.shape);
            lfo.rate    = s.rate;
            lfo.depth   = s.depth;
            lfo.sync    = s.sync;
            m_visualEngine.setLayerKnobLFO(track, i, lfo);
        }
    }
    m_visualEngine.setLayerText(track, vc.text);

    // Tempo sync — applies to every clip kind (a synced shader/scene
    // gets a beat-driven iTime; a synced video stretches to lengthBeats).
    m_visualEngine.setLayerTempoSync(track, vc.tempoSync, vc.lengthBeats);

    // iChannel2 source selection — mutually exclusive per layer.
    // Priority: model > live > file. Engine enforces the teardown
    // either way; we skip the non-winning setters so we don't churn
    // decoder threads / uploads unnecessarily.
    if (!vc.imagePath.empty()) {
        m_visualEngine.setLayerImage(track, vc.imagePath);
    } else if (!vc.modelPath.empty()) {
        std::vector<std::string> extraResolved;
        for (const auto& p : vc.extraModelPaths)
            if (!p.empty()) extraResolved.push_back(resolveModelPath(p));
        m_visualEngine.setLayerModel(track,
            resolveModelPath(vc.modelPath), extraResolved);
        m_visualEngine.setLayerAnimation(track, vc.animClip, vc.animSpeed);
        m_visualEngine.setLayerSceneScript(track,
            resolveScenePath(vc.scenePath));
    } else if (vc.liveInput && !vc.liveUrl.empty()) {
        m_visualEngine.setLayerLiveInput(track, vc.liveUrl);
    } else {
        m_visualEngine.setLayerVideo(track, vc.videoPath);
        m_visualEngine.setLayerVideoTiming(track,
            vc.videoLoopBars, vc.videoRate);
        m_visualEngine.setLayerVideoTrim(track,
            vc.videoIn, vc.videoOut);
    }
}

void App::pollArrangementVisualPlayback() {
    // Initialize the per-track "last active" state on first call so
    // we don't mis-detect a transition on startup.
    if (!m_activeArrInit) {
        for (int i = 0; i < kMaxTracks; ++i) m_activeArrVisualClip[i] = -1;
        m_activeArrInit = true;
    }

    // Poll regardless of play state so that dragging the playhead
    // while paused previews whatever clip sits under it — that's
    // what makes scrubbing feel right. The underlying launch/clear
    // are idempotent on the same activeIdx.
    const double beat = m_audioEngine.transport().positionInBeats();
    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        auto& track = m_project.track(t);
        if (track.type != Track::Type::Visual) continue;
        if (!track.arrangementActive) continue;

        int activeIdx = -1;
        for (int ci = 0; ci < static_cast<int>(track.arrangementClips.size()); ++ci) {
            const auto& c = track.arrangementClips[ci];
            if (c.type != ArrangementClip::Type::Visual) continue;
            if (beat >= c.startBeat && beat < c.endBeat()) {
                activeIdx = ci;   // last-match-wins for overlapping clips
            }
        }

        if (activeIdx == m_activeArrVisualClip[t]) continue;

        // Transition detected.
        if (activeIdx < 0) {
            // Leaving a clip into a gap → clear the layer so the
            // arrangement goes quiet on this track.
            m_visualEngine.clearLayer(t);
        } else {
            const auto& nc = track.arrangementClips[activeIdx];
            if (nc.visualClip) {
                launchVisualClipData(t, *nc.visualClip,
                                      resolveShaderPath(nc.visualClip->firstShaderPath()));
                // Arrangement clips use transport-driven clock so
                // scrubbing the playhead seeks the visuals. Origin =
                // the clip's start beat on the arrangement timeline.
                m_visualEngine.setLayerTransportClock(t, nc.startBeat);
                // loop/stretch decide how a video fills the slot. Engine
                // modes: 0=loop, 1=stretch, 2=hold-last-frame (loop off).
                const int vmode = nc.stretch ? 1 : (nc.loop ? 0 : 2);
                m_visualEngine.setLayerArrangementVideo(t, vmode,
                    nc.lengthBeats, nc.offsetBeats);
            }
        }
        m_activeArrVisualClip[t] = activeIdx;
    }
}

int App::resolveFollowActionScene(int track, int currentScene,
                                    FollowActionType action) const {
    const int numScenes = m_project.numScenes();
    if (track < 0 || track >= m_project.numTracks() || numScenes <= 0)
        return -1;

    // Build the set of occupied scenes on this track (same convention
    // as the audio follow-action handler — visual and audio/MIDI slots
    // all count as "occupied" for navigation purposes).
    std::vector<int> occupied;
    for (int s = 0; s < numScenes; ++s) {
        auto* slot = m_project.getSlot(track, s);
        if (slot && !slot->empty()) occupied.push_back(s);
    }
    if (occupied.empty()) return -1;

    switch (action) {
        case FollowActionType::Next: {
            for (int s : occupied) if (s > currentScene) return s;
            return occupied.front();  // wrap
        }
        case FollowActionType::Previous: {
            for (int i = static_cast<int>(occupied.size()) - 1; i >= 0; --i)
                if (occupied[i] < currentScene) return occupied[i];
            return occupied.back();   // wrap
        }
        case FollowActionType::First:
            return occupied.front();
        case FollowActionType::Last:
            return occupied.back();
        case FollowActionType::Random:
            return occupied[std::rand() % occupied.size()];
        case FollowActionType::Any: {
            std::vector<int> others;
            for (int s : occupied) if (s != currentScene) others.push_back(s);
            if (!others.empty()) return others[std::rand() % others.size()];
            return currentScene;
        }
        case FollowActionType::PlayAgain:
            return currentScene;
        default:
            return -1;
    }
}

void App::pollVisualFollowActions() {
    // One-shot init: mark all tracks as "not launched" on first call.
    if (!m_visualLaunchInit) {
        for (int i = 0; i < kMaxTracks; ++i) {
            m_visualLaunchBeat[i]  = kNoVisualLaunch;
            m_visualLaunchScene[i] = -1;
        }
        m_visualLaunchInit = true;
    }

    // Follow actions only fire while transport is actively advancing
    // bars — matches the audio ClipEngine behavior. Stop-then-play
    // resets each clip's bar counter (the set-launch-beat path).
    if (!m_audioEngine.transport().isPlaying()) return;

    const double beat = m_audioEngine.transport().positionInBeats();
    const int beatsPerBar = std::max(1, m_audioEngine.transport().numerator());

    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        if (m_project.track(t).type != Track::Type::Visual) continue;
        if (m_visualLaunchBeat[t] == kNoVisualLaunch) continue;

        const int scene = m_visualLaunchScene[t];
        if (scene < 0) continue;
        auto* slot = m_project.getSlot(t, scene);
        if (!slot || !slot->visualClip) continue;
        const auto& fa = slot->followAction;
        if (!fa.enabled || fa.barCount <= 0) continue;

        const double elapsedBeats = beat - m_visualLaunchBeat[t];
        if (elapsedBeats < fa.barCount * beatsPerBar) continue;

        // Roll A/B probability, pick action, resolve target scene.
        const int roll = std::rand() % 100;
        FollowActionType action = (roll < fa.chanceA) ? fa.actionA : fa.actionB;
        if (action == FollowActionType::None) {
            // Restart the bar timer so we roll again N bars later —
            // None means "loop", not "disarm".
            m_visualLaunchBeat[t] += fa.barCount * beatsPerBar;
            continue;
        }
        if (action == FollowActionType::Stop) {
            m_visualEngine.clearLayer(t);
            m_visualLaunchBeat[t]  = kNoVisualLaunch;
            m_visualLaunchScene[t] = -1;
            m_sessionPanel->updateClipState(t, false, 0, -1);
            continue;
        }

        const int target = resolveFollowActionScene(t, scene, action);
        if (target < 0) {
            // No valid target — stop to avoid infinite no-op retries.
            m_visualEngine.clearLayer(t);
            m_visualLaunchBeat[t]  = kNoVisualLaunch;
            m_visualLaunchScene[t] = -1;
            m_sessionPanel->updateClipState(t, false, 0, -1);
            continue;
        }
        auto* tslot = m_project.getSlot(t, target);
        if (!tslot || !tslot->visualClip) continue;

        launchVisualClipData(t, *tslot->visualClip,
                              resolveShaderPath(tslot->visualClip->firstShaderPath()));
        m_visualEngine.setLayerWallClock(t);
        m_project.track(t).defaultScene = target;
        m_visualLaunchBeat[t]  = beat;
        m_visualLaunchScene[t] = target;
        // Mirror the scene change into the session grid so the play
        // indicator follows the active clip instead of staying stuck
        // on the originally-launched row.
        m_sessionPanel->updateClipState(t, /*playing*/true,
                                          /*playPos*/0,
                                          /*playingScene*/target);
    }
}

void App::stopAllVisualLayers() {
    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        if (m_project.track(t).type != Track::Type::Visual) continue;
        m_visualEngine.clearLayer(t);
        m_activeArrVisualClip[t]  = -1;
        m_visualLaunchBeat[t]     = kNoVisualLaunch;
        m_visualLaunchScene[t]    = -1;
        m_sessionPanel->updateClipState(t, false, 0, -1);
    }
}

void App::stopAllClips() {
    // Stop every audio/MIDI clip and wipe each track's launch memory so
    // the next transport Play starts nothing (Ableton "Stop Clips").
    // Audio/MIDI grid indicators clear via the engine's play-state
    // feedback; visual layers + their indicators are cleared below.
    for (int t = 0; t < m_project.numTracks(); ++t) {
        m_audioEngine.sendCommand(audio::StopClipMsg{t});
        m_audioEngine.sendCommand(audio::StopMidiClipMsg{t});
        m_project.track(t).defaultScene = -1;
    }
    stopAllVisualLayers();
    markDirty();
}

// Linear scan for a parameter by name on any device that exposes
// the parameterCount() + parameterInfo(idx).name pair (instruments,
// audio effects, midi effects all qualify). Returns -1 when the
// name doesn't resolve. Per-frame call site, but the param counts
// are tiny so a hash isn't worth the upkeep.
namespace {
template <typename Device>
int findParamByName(const Device& dev, const std::string& name) {
    const int n = dev.parameterCount();
    for (int i = 0; i < n; ++i) {
        const char* pn = dev.parameterInfo(i).name;
        if (pn && name == pn) return i;
    }
    return -1;
}
} // namespace

void App::applyMacroMappings() {
    // Walk every track's MacroDevice once per frame; each mapping
    // reads the live (LFO-modulated) macro value, lerps it through
    // [rangeMin, rangeMax] (a 0..1 sub-range), then unnormalises into
    // the target parameter's *natural* range and pushes it. Inverted
    // sub-ranges (rangeMin > rangeMax) drop straight out of the lerp
    // arithmetic — no special case.
    //
    // Reading the engine's *display* value (rather than the raw
    // track.macros.values entry) keeps mapped targets in lock-step
    // with what the same macro does to the shader's knob[idx]
    // uniform — so an LFO breathing the macro pulses every mapped
    // target in unison.
    const int nTracks = m_project.numTracks();
    for (int t = 0; t < nTracks; ++t) {
        auto& track = m_project.track(t);
        if (track.macros.mappings.empty()) continue;

        for (const auto& m : track.macros.mappings) {
            if (m.macroIdx < 0 ||
                m.macroIdx >= MacroDevice::kNumMacros) continue;

            // Visual macros pull modulated value from the engine
            // (which has the LFO state). For non-visual tracks we
            // fall back to the raw macro value — audio/MIDI tracks
            // don't yet have layer-style LFO eval (Phase 4.6).
            const float macroV = (track.type == Track::Type::Visual)
                ? m_visualEngine.getLayerKnobDisplayValue(t, m.macroIdx)
                : track.macros.values[m.macroIdx];
            const float normV = m.rangeMin
                + (m.rangeMax - m.rangeMin) * macroV;

            switch (m.target.kind) {
                case MacroTarget::Kind::VisualSourceParam: {
                    if (track.type != Track::Type::Visual) break;
                    float pmin = 0.0f, pmax = 1.0f;
                    if (!m_visualEngine.getLayerParamRange(
                            t, m.target.paramName, &pmin, &pmax))
                        break;
                    const float v = pmin + normV * (pmax - pmin);
                    m_visualEngine.setLayerParam(t, m.target.paramName, v);
                    break;
                }
                case MacroTarget::Kind::VisualChainParam: {
                    if (track.type != Track::Type::Visual) break;
                    float pmin = 0.0f, pmax = 1.0f;
                    if (!m_visualEngine.getLayerChainPassParamRange(
                            t, m.target.index, m.target.paramName,
                            &pmin, &pmax))
                        break;
                    const float v = pmin + normV * (pmax - pmin);
                    m_visualEngine.setLayerChainPassParam(
                        t, m.target.index, m.target.paramName, v);
                    break;
                }
                case MacroTarget::Kind::AudioInstrumentParam: {
                    auto* inst = m_audioEngine.instrument(t);
                    if (!inst) break;
                    int pi = findParamByName(*inst, m.target.paramName);
                    if (pi < 0) break;
                    const auto& info = inst->parameterInfo(pi);
                    const float v =
                        info.minValue + normV * (info.maxValue - info.minValue);
                    inst->setParameter(pi, v);
                    break;
                }
                case MacroTarget::Kind::AudioEffectParam: {
                    auto& chain = m_audioEngine.mixer().trackEffects(t);
                    auto* fx = chain.effectAt(m.target.index);
                    if (!fx) break;
                    int pi = findParamByName(*fx, m.target.paramName);
                    if (pi < 0) break;
                    const auto& info = fx->parameterInfo(pi);
                    const float v =
                        info.minValue + normV * (info.maxValue - info.minValue);
                    fx->setParameter(pi, v);
                    break;
                }
                case MacroTarget::Kind::MidiEffectParam: {
                    auto& chain = m_audioEngine.midiEffectChain(t);
                    auto* fx = chain.effect(m.target.index);
                    if (!fx) break;
                    int pi = findParamByName(*fx, m.target.paramName);
                    if (pi < 0) break;
                    const auto& info = fx->parameterInfo(pi);
                    const float v =
                        info.minValue + normV * (info.maxValue - info.minValue);
                    fx->setParameter(pi, v);
                    break;
                }
                case MacroTarget::Kind::TrackVolume: {
                    // Linear gain 0..2 (matches the mixer fader).
                    m_audioEngine.mixer().setTrackVolume(t, normV * 2.0f);
                    break;
                }
                case MacroTarget::Kind::TrackPan: {
                    // -1 left … +1 right.
                    m_audioEngine.mixer().setTrackPan(t, normV * 2.0f - 1.0f);
                    break;
                }
                case MacroTarget::Kind::None:
                    break;
            }
        }
    }
}

void App::pollVisualKnobAutomation() {
    // Evaluate visual-knob automation on the main thread. We reuse
    // the existing AutomationLane data model (TargetType::VisualKnob
    // with paramIndex = 0..7 for A..H) but bypass the audio-thread
    // AutomationEngine — visual knobs don't need audio-thread jitter
    // guarantees, and the project → audio-engine lane sync isn't
    // wired for drawn envelopes anyway.
    //
    // Precedence (per the H.3 design):
    //   1. Per-clip envelope (on the active arrangement clip)  — lower
    //   2. Per-track arrangement lane                          — higher
    // Applied in that order → the track lane's write wins when both
    // target the same knob. LFOs still compose on top via the
    // existing knobDisplayValues pipeline.
    const double transportBeat = m_audioEngine.transport().positionInBeats();
    const int nTracks = std::min(m_project.numTracks(), kMaxTracks);
    for (int t = 0; t < nTracks; ++t) {
        auto& track = m_project.track(t);
        if (track.type != Track::Type::Visual) continue;

        // Pass 1a: session-grid clip envelopes. Clip-local beat is
        // (transportBeat - launchBeat) wrapped mod lengthBeats, so
        // envelopes loop with the clip. Session launches populate
        // m_visualLaunchBeat[t] via the launch callback.
        if (m_visualLaunchBeat[t] != kNoVisualLaunch &&
            m_visualLaunchScene[t] >= 0) {
            auto* slot = m_project.getSlot(t, m_visualLaunchScene[t]);
            if (slot && slot->visualClip && !slot->clipAutomation.empty()) {
                const double launchBeat = m_visualLaunchBeat[t];
                const double lenBeats   = std::max(0.25,
                                                    slot->visualClip->lengthBeats);
                double clipBeat = std::fmod(transportBeat - launchBeat, lenBeats);
                if (clipBeat < 0.0) clipBeat += lenBeats;
                for (const auto& lane : slot->clipAutomation) {
                    if (lane.envelope.empty()) continue;
                    const float v = lane.envelope.valueAt(clipBeat);
                    if (lane.target.type == automation::TargetType::VisualKnob) {
                        const int knob = lane.target.paramIndex;
                        if (knob >= 0 && knob < 8)
                            m_visualEngine.setLayerKnob(t, knob, v);
                    } else if (lane.target.type == automation::TargetType::VisualParam) {
                        if (!lane.target.paramName.empty())
                            m_visualEngine.setLayerParam(t, lane.target.paramName, v);
                    }
                }
            }
        }

        // Pass 1b: arrangement-clip envelopes — deferred; ArrangementClip
        // doesn't store clipAutomation yet. Placeholder for that phase.

        // Pass 2: per-track arrangement lanes (absolute beat time).
        // Only applied when autoMode isn't Off. Track lanes run AFTER
        // clip envelopes so the arrangement layer always wins per the
        // H.3 precedence design.
        if (track.autoMode == automation::AutoMode::Off) continue;
        for (const auto& lane : track.automationLanes) {
            if (lane.target.trackIndex != t) continue;
            if (lane.envelope.empty()) continue;
            const float v = lane.envelope.valueAt(transportBeat);
            if (lane.target.type == automation::TargetType::VisualKnob) {
                const int knob = lane.target.paramIndex;
                if (knob >= 0 && knob < 8)
                    m_visualEngine.setLayerKnob(t, knob, v);
            } else if (lane.target.type == automation::TargetType::VisualParam) {
                if (!lane.target.paramName.empty())
                    m_visualEngine.setLayerParam(t, lane.target.paramName, v);
            }
        }
    }
}


std::string App::resolveShaderPath(const std::string& stored) const {
    if (stored.empty()) return stored;
    std::filesystem::path p(stored);
    if (p.is_absolute()) return stored;
    // Paths from bundled YAWN resources (e.g. "assets/shaders/...") live
    // alongside the binary in the app's working directory, so pass them
    // through untouched. Only genuinely project-relative entries (the
    // ones we start writing as "shaders/<stem>.frag") get prefixed.
    if (!m_projectPath.empty() && stored.compare(0, 8, "shaders/") == 0) {
        return (m_projectPath / stored).string();
    }
    return stored;
}

std::string App::resolveModelPath(const std::string& stored) const {
    if (stored.empty()) return stored;
    std::filesystem::path p(stored);
    if (p.is_absolute()) return stored;
    if (!m_projectPath.empty() && stored.compare(0, 7, "models/") == 0) {
        return (m_projectPath / stored).string();
    }
    return stored;
}

std::string App::localizeModel(const std::string& sourcePath) {
    if (sourcePath.empty()) return sourcePath;
    if (m_projectPath.empty()) {
        LOG_WARN("Model",
            "Project not saved — model stays at %s. Save the project to "
            "copy models into <project>/models/.", sourcePath.c_str());
        return sourcePath;
    }

    namespace fs = std::filesystem;
    fs::path src(sourcePath);
    std::string ext = src.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));

    // .gltf references external .bin / texture files by relative path.
    // Copying the .gltf without those files silently breaks the load,
    // so for MVP we refuse to localize .gltf and just store the
    // absolute path the user picked.
    if (ext != ".glb") {
        LOG_INFO("Model",
            "Not localizing non-.glb model %s — use .glb for portable projects.",
            sourcePath.c_str());
        return sourcePath;
    }

    fs::path modelsDir = m_projectPath / "models";
    std::error_code ec;
    fs::create_directories(modelsDir, ec);

    std::string stem = src.stem().string();
    fs::path target = modelsDir / (stem + ".glb");

    if (!fs::exists(target)) {
        fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_ERROR("Model", "Failed to copy %s → %s: %s",
                      src.string().c_str(), target.string().c_str(),
                      ec.message().c_str());
            return sourcePath;
        }
        LOG_INFO("Model", "Localized %s → %s",
                 src.string().c_str(), target.string().c_str());
    }
    return "models/" + stem + ".glb";
}

std::string App::resolveScenePath(const std::string& stored) const {
    if (stored.empty()) return stored;
    std::filesystem::path p(stored);
    if (p.is_absolute()) return stored;
    if (!m_projectPath.empty() && stored.compare(0, 8, "scripts/") == 0) {
        return (m_projectPath / stored).string();
    }
    return stored;
}

void App::reloadVisualClipModels(int track, int scene) {
    auto* s = m_project.getSlot(track, scene);
    if (!s || !s->visualClip) return;
    if (m_project.track(track).defaultScene != scene) return;  // not the live clip
    const auto& vc = *s->visualClip;
    if (vc.modelPath.empty()) { m_visualEngine.setLayerModel(track, ""); return; }
    std::vector<std::string> extra;
    for (const auto& p : vc.extraModelPaths)
        if (!p.empty()) extra.push_back(resolveModelPath(p));
    m_visualEngine.setLayerModel(track, resolveModelPath(vc.modelPath), extra);
}

void App::addModelToClip(int track, int scene, const std::string& sourcePath) {
    auto* s = m_project.getSlot(track, scene);
    if (!s) return;
    if (!s->visualClip)
        s->visualClip = std::make_unique<visual::VisualClip>();
    auto& vc = *s->visualClip;
    std::string stored = localizeModel(sourcePath);
    if (stored.empty()) return;
    if (vc.modelPath.empty()) {
        // First model becomes the primary (index 0). Take over iChannel2.
        vc.modelPath       = stored;
        vc.modelSourcePath = sourcePath;
        vc.videoPath.clear();
        vc.thumbnailPath.clear();
        vc.liveInput = false;
        vc.liveUrl.clear();
        if (vc.name.empty())
            vc.name = std::filesystem::path(sourcePath).stem().string();
        if (vc.colorIndex == 0)
            vc.colorIndex = m_project.track(track).colorIndex;
    } else {
        vc.extraModelPaths.push_back(stored);
        vc.extraModelSourcePaths.push_back(sourcePath);
    }
    reloadVisualClipModels(track, scene);
    markDirty();
}

void App::removeModelFromClip(int track, int scene, int listIndex) {
    auto* s = m_project.getSlot(track, scene);
    if (!s || !s->visualClip) return;
    auto& vc = *s->visualClip;

    // Flatten to the same order the scene script indexes: primary first,
    // then non-empty extras (with their parallel source paths).
    std::vector<std::string> paths, srcs;
    if (!vc.modelPath.empty()) {
        paths.push_back(vc.modelPath);
        srcs.push_back(vc.modelSourcePath);
    }
    for (size_t i = 0; i < vc.extraModelPaths.size(); ++i) {
        if (vc.extraModelPaths[i].empty()) continue;
        paths.push_back(vc.extraModelPaths[i]);
        srcs.push_back(i < vc.extraModelSourcePaths.size()
                       ? vc.extraModelSourcePaths[i] : std::string());
    }
    if (listIndex < 0 || listIndex >= static_cast<int>(paths.size())) return;
    paths.erase(paths.begin() + listIndex);
    srcs.erase(srcs.begin() + listIndex);

    // Re-seat: list[0] is the new primary; the rest are extras.
    vc.modelPath       = paths.empty() ? std::string() : paths[0];
    vc.modelSourcePath = srcs.empty()  ? std::string() : srcs[0];
    vc.extraModelPaths.assign(paths.begin() + (paths.empty() ? 0 : 1), paths.end());
    vc.extraModelSourcePaths.assign(srcs.begin() + (srcs.empty() ? 0 : 1), srcs.end());

    if (vc.modelPath.empty()) {
        // No models left — a scene script has nothing to drive.
        vc.scenePath.clear();
        if (m_project.track(track).defaultScene == scene)
            m_visualEngine.setLayerSceneScript(track, "");
    }
    reloadVisualClipModels(track, scene);
    markDirty();
}

void App::assignModelFromLibrary(const std::string& sourcePath) {
    const int t = m_selectedTrack, s = m_selectedScene;
    if (t < 0 || t >= m_project.numTracks() || s < 0) {
        m_toastManager.show("Select a visual clip slot first", 2.5f,
                            ui::ToastManager::Severity::Info);
        return;
    }
    if (m_project.track(t).type != Track::Type::Visual) {
        m_toastManager.show("Select a Visual track to assign a model", 2.5f,
                            ui::ToastManager::Severity::Info);
        return;
    }
    auto* slot = m_project.getSlot(t, s);
    if (!slot) return;
    if (!slot->visualClip)
        slot->visualClip = std::make_unique<visual::VisualClip>();
    auto& vc = *slot->visualClip;
    std::string stored = localizeModel(sourcePath);
    if (stored.empty()) return;
    // Set as the primary model (replace), clearing the other iChannel2
    // sources — mirrors Set Model….
    vc.modelPath       = stored;
    vc.modelSourcePath = sourcePath;
    vc.videoPath.clear();
    vc.thumbnailPath.clear();
    vc.liveInput = false;
    vc.liveUrl.clear();
    if (vc.name.empty())
        vc.name = std::filesystem::path(sourcePath).stem().string();
    if (vc.colorIndex == 0)
        vc.colorIndex = m_project.track(t).colorIndex;
    reloadVisualClipModels(t, s);
    m_toastManager.show("Assigned model: " + vc.name, 2.0f,
                        ui::ToastManager::Severity::Info);
    markDirty();
}

std::string App::localizeScene(const std::string& sourcePath) {
    if (sourcePath.empty()) return sourcePath;
    if (m_projectPath.empty()) {
        LOG_WARN("Scene",
            "Project not saved — scene script stays at %s. Save the "
            "project to copy scripts into <project>/scripts/.",
            sourcePath.c_str());
        return sourcePath;
    }
    namespace fs = std::filesystem;
    fs::path src(sourcePath);
    std::string stem = src.stem().string();
    std::string ext  = src.extension().string();
    if (ext.empty()) ext = ".lua";

    fs::path scriptsDir = m_projectPath / "scripts";
    std::error_code ec;
    fs::create_directories(scriptsDir, ec);
    fs::path target = scriptsDir / (stem + ext);

    if (!fs::exists(target)) {
        fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_ERROR("Scene", "Failed to copy %s → %s: %s",
                      src.string().c_str(), target.string().c_str(),
                      ec.message().c_str());
            return sourcePath;
        }
        LOG_INFO("Scene", "Localized %s → %s",
                 src.string().c_str(), target.string().c_str());
    }
    return "scripts/" + stem + ext;
}

std::string App::localizeShader(const std::string& sourcePath) {
    if (sourcePath.empty()) return sourcePath;
    if (m_projectPath.empty()) {
        LOG_WARN("Shader",
            "Project not saved — shader stays at %s. Save the project to "
            "copy shaders into <project>/shaders/.", sourcePath.c_str());
        return sourcePath;
    }

    namespace fs = std::filesystem;
    fs::path shadersDir = m_projectPath / "shaders";
    std::error_code ec;
    fs::create_directories(shadersDir, ec);

    fs::path src(sourcePath);
    std::string stem = src.stem().string();
    std::string ext  = src.extension().string();
    if (ext.empty()) ext = ".frag";

    fs::path target = shadersDir / (stem + ext);

    // If a shader with the same stem already exists in the project:
    //   - If `target` IS `src` (source IS project-local), nothing to do.
    //   - Otherwise share it (user can Fork for a separate copy).
    if (!fs::exists(target)) {
        fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_ERROR("Shader", "Failed to copy %s → %s: %s",
                      src.string().c_str(), target.string().c_str(),
                      ec.message().c_str());
            return sourcePath;
        }
        LOG_INFO("Shader", "Localized %s → %s",
                 src.string().c_str(), target.string().c_str());
    }
    return "shaders/" + stem + ext;
}

void App::addImageToSlot(int track, int scene, const std::string& sourcePath) {
    auto* slot = m_project.getSlot(track, scene);
    if (!slot) return;
    if (!slot->visualClip) {
        auto vc = std::make_unique<visual::VisualClip>();
        std::filesystem::path p(sourcePath);
        vc->name       = p.stem().string();
        vc->colorIndex = m_project.track(track).colorIndex;
        m_project.setVisualClip(track, scene, std::move(vc));
        slot = m_project.getSlot(track, scene);
        if (!slot || !slot->visualClip) return;
    }
    // The image is the sole iChannel2 source — clear the others. No transcode:
    // we reference the file in place (absolute path).
    slot->visualClip->imagePath       = sourcePath;
    slot->visualClip->thumbnailPath   = sourcePath;   // the image is its own thumb
    slot->visualClip->videoPath.clear();
    slot->visualClip->videoSourcePath.clear();
    slot->visualClip->liveInput = false;
    slot->visualClip->liveUrl.clear();
    slot->visualClip->modelPath.clear();
    markDirty();
    m_toastManager.show("Image loaded: " +
        std::filesystem::path(sourcePath).filename().string(),
        2.0f, ui::ToastManager::Severity::Info);
}

void App::startVideoImport(int track, int scene, const std::string& sourcePath) {
    if (m_projectPath.empty()) return;

    // Ensure the slot has a VisualClip — create one named after the file
    // if necessary. The videoPath gets filled when the import finishes.
    auto* slot = m_project.getSlot(track, scene);
    if (!slot) return;
    if (!slot->visualClip) {
        auto vc = std::make_unique<visual::VisualClip>();
        std::filesystem::path p(sourcePath);
        vc->name       = p.stem().string();
        vc->colorIndex = m_project.track(track).colorIndex;
        m_project.setVisualClip(track, scene, std::move(vc));
    }

    auto importer = std::make_unique<visual::VideoImporter>();
    std::filesystem::path mediaDir = m_projectPath / "media";
    if (!importer->start(sourcePath, mediaDir)) {
        LOG_ERROR("Video", "Import failed to start: %s", sourcePath.c_str());
        m_toastManager.show(
            std::string("Video import failed: ") + importer->error(),
            3.0f, ui::ToastManager::Severity::Error);
        return;
    }

    PendingVideoImport pi;
    pi.track      = track;
    pi.scene      = scene;
    pi.sourcePath = sourcePath;
    pi.importer   = std::move(importer);
    m_pendingImports.push_back(std::move(pi));

    m_sessionPanel->setSlotImporting(track, scene, true);
    LOG_INFO("Video", "Import started: %s", sourcePath.c_str());
}

void App::onVideoImportDone(PendingVideoImport& pi) {
    const auto& r = pi.importer->result();
    auto* slot = m_project.getSlot(pi.track, pi.scene);
    if (slot && slot->visualClip) {
        slot->visualClip->videoPath       = r.videoPath;
        slot->visualClip->thumbnailPath   = r.thumbnailPath;
        slot->visualClip->videoSourcePath = pi.sourcePath;

        // Auto-detect the clip's musical length from the source duration
        // and turn on tempo sync, so the imported video follows the
        // project tempo like audio/MIDI clips do. At the current BPM it
        // plays ~native speed; changing the BPM rescales it. Rounded to a
        // whole beat for a tidy "N bars" readout — the user can fine-tune
        // via Clip Length, or disable sync to free-run at native rate.
        if (r.durationSeconds > 0.0) {
            const double bpm = std::max(1.0, m_audioEngine.transport().bpm());
            double beats = std::round(r.durationSeconds * bpm / 60.0);
            slot->visualClip->lengthBeats = std::max(1.0, beats);
            slot->visualClip->tempoSync   = true;
        }
        markDirty();
    }

    // If audio was extracted, append a new audio track named
    // "<stem> audio" and load the WAV as an audio clip at the same scene
    // so the user can scene-launch both together.
    if (!r.audioPath.empty()) {
        std::filesystem::path srcP(pi.sourcePath);
        std::string newTrackName = srcP.stem().string() + " audio";
        m_project.addTrack(newTrackName, Track::Type::Audio);
        int newTrack = m_project.numTracks() - 1;
        m_audioEngine.sendCommand(audio::SetTrackTypeMsg{newTrack, 0});
        if (loadClipToSlot(r.audioPath, newTrack, pi.scene)) {
            LOG_INFO("Video", "Audio track created for %s at scene %d",
                      srcP.stem().string().c_str(), pi.scene);
        }
        syncTracksToEngine();
        markDirty();
    }

    m_sessionPanel->setSlotImporting(pi.track, pi.scene, false);
    LOG_INFO("Video", "Import done: %s", pi.sourcePath.c_str());
    m_toastManager.show("Video imported",
                        1.5f, ui::ToastManager::Severity::Info);
}


} // namespace yawn
