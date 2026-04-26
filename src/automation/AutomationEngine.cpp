#include "automation/AutomationEngine.h"

namespace yawn {
namespace automation {

AutomationLane* AutomationEngine::findOrCreateLane(std::vector<AutomationLane>& lanes,
                                                    const AutomationTarget& target, int /*track*/) {
    for (auto& lane : lanes) {
        if (lane.target == target) return &lane;
    }
    // Create a new lane for this target
    lanes.push_back({target, {}, true});
    return &lanes.back();
}

void AutomationEngine::applyValue(const Context& ctx, const AutomationTarget& tgt, float value) {
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
    case TargetType::Transport:
    case TargetType::VisualKnob:
    case TargetType::VisualParam:
        break;
    }
}

void AutomationEngine::applyMixerValue(const Context& ctx, const AutomationTarget& tgt, float value) {
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

void AutomationEngine::process(const Context& ctx) {
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

} // namespace automation
} // namespace yawn
