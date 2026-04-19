#pragma once

#include "automation/AutomationTypes.h"
#include "automation/AutomationLane.h"
#include "instruments/Instrument.h"
#include "effects/EffectChain.h"
#include "midi/MidiEffectChain.h"
#include "audio/Mixer.h"
#include "core/Constants.h"
#include <vector>
#include <cmath>

namespace yawn {
namespace automation {

// Reads automation envelopes and applies parameter values during the audio callback.
// Also handles Touch/Latch recording by inserting breakpoints into envelopes.
// Called once per buffer, before the mixer processes.
class AutomationEngine {
public:
    // Per-track automation mode (set from UI via command queue)
    AutoMode trackAutoMode(int track) const {
        if (track < 0 || track >= kMaxTracks) return AutoMode::Off;
        return m_trackMode[track];
    }
    void setTrackAutoMode(int track, AutoMode mode) {
        if (track >= 0 && track < kMaxTracks)
            m_trackMode[track] = mode;
    }

    // Supply external state needed to resolve and apply automation targets.
    // Called once per buffer from the audio callback.
    struct Context {
        double positionInBeats = 0.0;
        bool   isPlaying       = false;

        // Per-track playing clip info (for clip automation)
        struct ClipInfo {
            bool playing = false;
            double clipLocalBeat = 0.0;         // current position within clip (already looped)
            double clipLengthBeats = 0.0;        // clip length (for reference)
            const std::vector<AutomationLane>* clipLanes = nullptr;
        };
        ClipInfo clips[kMaxTracks];

        // Per-track arrangement automation lanes (mutable for recording)
        std::vector<AutomationLane>* trackLanes[kMaxTracks] = {};

        // Device accessors for applying values
        instruments::Instrument* instruments[kMaxTracks] = {};
        audio::Mixer* mixer = nullptr;
        effects::EffectChain* trackFx[kMaxTracks] = {};
        midi::MidiEffectChain* midiEffectChains[kMaxTracks] = {};
    };

    // Handle a param touch message (from UI knob interaction).
    // Called from the audio thread command processing.
    void handleParamTouch(int track, const AutomationTarget& target, float value, bool touching) {
        if (track < 0 || track >= kMaxTracks) return;
        auto& ts = m_touchState[track];

        if (touching) {
            ts.active = true;
            ts.target = target;
            ts.lastValue = value;
            ts.latchHolding = false;
        } else {
            if (ts.active && ts.target == target) {
                ts.active = false;
                if (m_trackMode[track] == AutoMode::Latch) {
                    ts.latchHolding = true;
                }
            }
        }
    }

    // Process all automation for the current buffer.
    // Should be called just before mixer.process() in the audio callback.
    void process(const Context& ctx) {
        if (!ctx.isPlaying) return;

        for (int t = 0; t < kMaxTracks; ++t) {
            AutoMode mode = m_trackMode[t];
            if (mode == AutoMode::Off) continue;

            // --- Recording (Touch / Latch) ---
            if ((mode == AutoMode::Touch || mode == AutoMode::Latch) && ctx.trackLanes[t]) {
                auto& ts = m_touchState[t];
                bool shouldRecord = ts.active || ts.latchHolding;

                if (shouldRecord) {
                    auto* lane = findOrCreateLane(*ctx.trackLanes[t], ts.target, t);
                    if (lane) {
                        lane->envelope.addPoint(ctx.positionInBeats, ts.lastValue);
                        applyValue(ctx, ts.target, ts.lastValue);
                        continue; // Skip normal read-back for this track while recording
                    }
                }
            }

            // --- Read-back: precedence rules ---
            //
            // Audio / MIDI / Mixer targets: track lanes first, clip
            // lanes second → the clip envelope wins (existing behavior,
            // "most specific wins").
            //
            // Visual-knob targets: clip lanes FIRST, track lanes
            // second → the track (arrangement) lane wins, matching
            // the VJ workflow where long-form arrangement automation
            // is the "final cut" layer on top of per-clip envelopes.
            const auto& ci = ctx.clips[t];
            const bool clipActive = ci.playing && ci.clipLanes &&
                                      !ci.clipLanes->empty();

            if (clipActive) {
                for (const auto& lane : *ci.clipLanes) {
                    if (lane.target.type != TargetType::VisualKnob &&
                        lane.target.type != TargetType::VisualParam) continue;
                    if (lane.envelope.empty()) continue;
                    float val = lane.envelope.valueAt(ci.clipLocalBeat);
                    applyValue(ctx, lane.target, val);
                }
            }

            if (ctx.trackLanes[t]) {
                for (const auto& lane : *ctx.trackLanes[t]) {
                    if (lane.envelope.empty()) continue;
                    float val = lane.envelope.valueAt(ctx.positionInBeats);
                    applyValue(ctx, lane.target, val);
                }
            }

            if (clipActive) {
                for (const auto& lane : *ci.clipLanes) {
                    if (lane.target.type == TargetType::VisualKnob ||
                        lane.target.type == TargetType::VisualParam) continue;
                    if (lane.envelope.empty()) continue;
                    float val = lane.envelope.valueAt(ci.clipLocalBeat);
                    applyValue(ctx, lane.target, val);
                }
            }
        }

        // Also process clip automation for tracks with AutoMode::Off
        // (the loop above skips those tracks entirely)
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackMode[t] != AutoMode::Off) continue; // already handled above
            const auto& ci = ctx.clips[t];
            if (ci.playing && ci.clipLanes && !ci.clipLanes->empty()) {
                for (const auto& lane : *ci.clipLanes) {
                    if (lane.envelope.empty()) continue;
                    float val = lane.envelope.valueAt(ci.clipLocalBeat);
                    applyValue(ctx, lane.target, val);
                }
            }
        }
    }

    // Update value while touching (called when UI knob moves during recording)
    void updateTouchValue(int track, float value) {
        if (track >= 0 && track < kMaxTracks && m_touchState[track].active)
            m_touchState[track].lastValue = value;
    }

    // Check if a specific track+target is currently being recorded
    bool isRecording(int track) const {
        if (track < 0 || track >= kMaxTracks) return false;
        const auto& ts = m_touchState[track];
        return ts.active || ts.latchHolding;
    }

    // Stop latch hold (called when transport stops or mode changes)
    void stopLatchHold(int track) {
        if (track >= 0 && track < kMaxTracks)
            m_touchState[track].latchHolding = false;
    }

    // Per-track touch state (visible for testing)
    struct TouchState {
        bool active = false;
        bool latchHolding = false;
        AutomationTarget target;
        float lastValue = 0.0f;
    };
    const TouchState& touchState(int track) const {
        static TouchState empty;
        if (track < 0 || track >= kMaxTracks) return empty;
        return m_touchState[track];
    }

    void removeTrackSlot(int index, int last) {
        for (int i = index; i < last; ++i) {
            m_trackMode[i] = m_trackMode[i + 1];
            m_touchState[i] = m_touchState[i + 1];
        }
        m_trackMode[last] = AutoMode::Off;
        m_touchState[last] = {};
    }

private:
    AutomationLane* findOrCreateLane(std::vector<AutomationLane>& lanes,
                                      const AutomationTarget& target, int track) {
        for (auto& lane : lanes) {
            if (lane.target == target) return &lane;
        }
        // Create a new lane for this target
        lanes.push_back({target, {}, true});
        return &lanes.back();
    }

    void applyValue(const Context& ctx, const AutomationTarget& tgt, float value) {
        int t = tgt.trackIndex;
        switch (tgt.type) {
        case TargetType::Instrument:
            if (ctx.instruments[t])
                ctx.instruments[t]->setParameter(tgt.paramIndex, value);
            break;

        case TargetType::AudioEffect:
            if (ctx.trackFx[t]) {
                auto* fx = ctx.trackFx[t]->effectAt(tgt.chainIndex);
                if (fx) fx->setParameter(tgt.paramIndex, value);
            }
            break;

        case TargetType::MidiEffect:
            if (ctx.midiEffectChains[t]) {
                auto* mfx = ctx.midiEffectChains[t]->effect(tgt.chainIndex);
                if (mfx) mfx->setParameter(tgt.paramIndex, value);
            }
            break;

        case TargetType::Mixer:
            if (ctx.mixer) applyMixerValue(ctx, tgt, value);
            break;
        }
    }

    void applyMixerValue(const Context& ctx, const AutomationTarget& tgt, float value) {
        int t = tgt.trackIndex;
        auto mp = static_cast<MixerParam>(tgt.paramIndex);
        switch (mp) {
        case MixerParam::Volume: ctx.mixer->setTrackVolume(t, value); break;
        case MixerParam::Pan:    ctx.mixer->setTrackPan(t, value);    break;
        case MixerParam::Mute:   ctx.mixer->setTrackMute(t, value >= 0.5f); break;
        case MixerParam::SendLevel0: ctx.mixer->setSendLevel(t, 0, value); break;
        case MixerParam::SendLevel1: ctx.mixer->setSendLevel(t, 1, value); break;
        case MixerParam::SendLevel2: ctx.mixer->setSendLevel(t, 2, value); break;
        case MixerParam::SendLevel3: ctx.mixer->setSendLevel(t, 3, value); break;
        case MixerParam::SendLevel4: ctx.mixer->setSendLevel(t, 4, value); break;
        case MixerParam::SendLevel5: ctx.mixer->setSendLevel(t, 5, value); break;
        case MixerParam::SendLevel6: ctx.mixer->setSendLevel(t, 6, value); break;
        case MixerParam::SendLevel7: ctx.mixer->setSendLevel(t, 7, value); break;
        }
    }

    AutoMode   m_trackMode[kMaxTracks] = {};
    TouchState m_touchState[kMaxTracks] = {};
};

} // namespace automation
} // namespace yawn
