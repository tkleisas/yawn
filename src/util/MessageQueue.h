#pragma once

#include "util/RingBuffer.h"
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

using AudioCommand = std::variant<
    TransportPlayMsg,
    TransportStopMsg,
    TransportSetBPMMsg,
    TransportSetPositionMsg,
    TestToneMsg
>;

// Messages sent from audio thread → UI thread (lock-free)
struct TransportPositionUpdate {
    int64_t positionInSamples;
    double positionInBeats;
    bool isPlaying;
};

using AudioEvent = std::variant<
    TransportPositionUpdate
>;

// Command queue: UI → Audio (1024 slots)
using CommandQueue = util::RingBuffer<AudioCommand, 1024>;

// Event queue: Audio → UI (1024 slots)
using EventQueue = util::RingBuffer<AudioEvent, 1024>;

} // namespace audio
} // namespace yawn
