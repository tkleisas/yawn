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
    void process(const Context& ctx);

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
                                     const AutomationTarget& target, int track);
    void applyValue(const Context& ctx, const AutomationTarget& tgt, float value);
    void applyMixerValue(const Context& ctx, const AutomationTarget& tgt, float value);

    AutoMode   m_trackMode[kMaxTracks] = {};
    TouchState m_touchState[kMaxTracks] = {};
};

} // namespace automation
} // namespace yawn
