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

using AudioCommand = std::variant<
    TransportPlayMsg,
    TransportStopMsg,
    TransportSetBPMMsg,
    TransportSetPositionMsg,
    TestToneMsg,
    LaunchClipMsg,
    StopClipMsg,
    SetQuantizeMsg
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

using AudioEvent = std::variant<
    TransportPositionUpdate,
    ClipStateUpdate
>;

// Command queue: UI → Audio (1024 slots)
using CommandQueue = util::RingBuffer<AudioCommand, 1024>;

// Event queue: Audio → UI (1024 slots)
using EventQueue = util::RingBuffer<AudioEvent, 1024>;

} // namespace audio
} // namespace yawn
