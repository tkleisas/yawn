#pragma once

#include <cstdint>
#include <functional>

namespace yawn {
namespace automation {

// Identifies what kind of device a parameter belongs to
enum class TargetType : uint8_t {
    Instrument,     // Track instrument parameter
    AudioEffect,    // Audio effect chain parameter
    MidiEffect,     // MIDI effect chain parameter
    Mixer,          // Mixer channel control (volume, pan, send, etc.)
    Transport,      // Transport controls (BPM, play, stop, record)
    VisualKnob      // Generic A..H knob on a visual layer (paramIndex = 0..7)
};

// Sub-types for mixer automation (volume, pan, sends, etc.)
enum class MixerParam : uint8_t {
    Volume,
    Pan,
    Mute,
    SendLevel0, SendLevel1, SendLevel2, SendLevel3,
    SendLevel4, SendLevel5, SendLevel6, SendLevel7
};

// Transport parameter sub-types
enum class TransportParam : uint8_t {
    BPM,
    Play,
    Stop,
    Record
};

// Uniquely identifies any automatable parameter in the project
struct AutomationTarget {
    TargetType type = TargetType::Instrument;
    int trackIndex  = 0;
    int chainIndex  = 0;    // effect slot index (for AudioEffect/MidiEffect)
    int paramIndex  = 0;    // parameter index within device, or MixerParam cast

    bool operator==(const AutomationTarget& o) const {
        return type == o.type && trackIndex == o.trackIndex &&
               chainIndex == o.chainIndex && paramIndex == o.paramIndex;
    }
    bool operator!=(const AutomationTarget& o) const { return !(*this == o); }

    // For use as map key
    bool operator<(const AutomationTarget& o) const {
        if (type != o.type) return type < o.type;
        if (trackIndex != o.trackIndex) return trackIndex < o.trackIndex;
        if (chainIndex != o.chainIndex) return chainIndex < o.chainIndex;
        return paramIndex < o.paramIndex;
    }

    // Convenience factories
    static AutomationTarget instrument(int track, int param) {
        return {TargetType::Instrument, track, 0, param};
    }
    static AutomationTarget audioEffect(int track, int slot, int param) {
        return {TargetType::AudioEffect, track, slot, param};
    }
    static AutomationTarget midiEffect(int track, int slot, int param) {
        return {TargetType::MidiEffect, track, slot, param};
    }
    static AutomationTarget mixer(int track, MixerParam mp) {
        return {TargetType::Mixer, track, 0, static_cast<int>(mp)};
    }
    static AutomationTarget transport(TransportParam tp) {
        return {TargetType::Transport, 0, 0, static_cast<int>(tp)};
    }
    static AutomationTarget visualKnob(int track, int knobIdx) {
        return {TargetType::VisualKnob, track, 0, knobIdx};
    }
};

// Automation recording mode per track
enum class AutoMode : uint8_t {
    Off,    // No automation playback or recording
    Read,   // Play back automation envelopes
    Touch,  // Record while knob is held, revert on release
    Latch   // Record while knob is held, keep last value after release
};

} // namespace automation
} // namespace yawn

// std::hash specialization for use in unordered containers
namespace std {
template<> struct hash<yawn::automation::AutomationTarget> {
    size_t operator()(const yawn::automation::AutomationTarget& t) const noexcept {
        size_t h = static_cast<size_t>(t.type);
        h = h * 31 + static_cast<size_t>(t.trackIndex);
        h = h * 31 + static_cast<size_t>(t.chainIndex);
        h = h * 31 + static_cast<size_t>(t.paramIndex);
        return h;
    }
};
} // namespace std
