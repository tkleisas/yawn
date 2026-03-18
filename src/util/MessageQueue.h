#pragma once

#include "util/RingBuffer.h"
#include "audio/ClipEngine.h"
#include <cstdint>
#include <variant>

namespace yawn {
namespace audio {

// Messages sent from UI thread → audio thread (lock-free)
struct TransportPlayMsg {};
struct TransportStopMsg {};

struct TransportSetBPMMsg {
    double bpm;
};

struct TransportSetPositionMsg {
    int64_t positionInSamples;
};

struct TestToneMsg {
    bool enabled;
    float frequency;
};

struct LaunchClipMsg {
    int trackIndex;
    const Clip* clip; // must remain valid while playing (owned by UI-side Project)
};

struct StopClipMsg {
    int trackIndex;
};

struct SetQuantizeMsg {
    QuantizeMode mode;
};

struct SetTrackVolumeMsg {
    int trackIndex;
    float volume;
};

struct SetTrackPanMsg {
    int trackIndex;
    float pan;
};

struct SetTrackMuteMsg {
    int trackIndex;
    bool muted;
};

struct SetTrackSoloMsg {
    int trackIndex;
    bool soloed;
};

struct SetMasterVolumeMsg {
    float volume;
};

struct SetSendLevelMsg {
    int trackIndex;
    int sendIndex;
    float level;
};

struct SetSendModeMsg {
    int trackIndex;
    int sendIndex;
    int mode;  // 0 = PreFader, 1 = PostFader
};

struct SetSendEnabledMsg {
    int trackIndex;
    int sendIndex;
    bool enabled;
};

struct SetReturnVolumeMsg {
    int busIndex;
    float volume;
};

struct SetReturnPanMsg {
    int busIndex;
    float pan;
};

struct SetReturnMuteMsg {
    int busIndex;
    bool muted;
};

using AudioCommand = std::variant<
    TransportPlayMsg,
    TransportStopMsg,
    TransportSetBPMMsg,
    TransportSetPositionMsg,
    TestToneMsg,
    LaunchClipMsg,
    StopClipMsg,
    SetQuantizeMsg,
    SetTrackVolumeMsg,
    SetTrackPanMsg,
    SetTrackMuteMsg,
    SetTrackSoloMsg,
    SetMasterVolumeMsg,
    SetSendLevelMsg,
    SetSendModeMsg,
    SetSendEnabledMsg,
    SetReturnVolumeMsg,
    SetReturnPanMsg,
    SetReturnMuteMsg
>;

// Messages sent from audio thread → UI thread (lock-free)
struct TransportPositionUpdate {
    int64_t positionInSamples;
    double positionInBeats;
    bool isPlaying;
};

struct ClipStateUpdate {
    int trackIndex;
    bool playing;
    int64_t playPosition;
};

struct MeterUpdate {
    int trackIndex;     // -1 = master, -2..-5 = return buses 0..3
    float peakL;
    float peakR;
};

using AudioEvent = std::variant<
    TransportPositionUpdate,
    ClipStateUpdate,
    MeterUpdate
>;

// Command queue: UI → Audio (1024 slots)
using CommandQueue = util::RingBuffer<AudioCommand, 1024>;

// Event queue: Audio → UI (1024 slots)
using EventQueue = util::RingBuffer<AudioEvent, 1024>;

} // namespace audio
} // namespace yawn
