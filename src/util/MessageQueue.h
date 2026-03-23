#pragma once

#include "util/RingBuffer.h"
#include "audio/ClipEngine.h"
#include <cstdint>
#include <variant>

namespace yawn {
namespace midi { class MidiClip; }
namespace audio {

// Messages sent from UI thread → audio thread (lock-free)
struct TransportPlayMsg {};
struct TransportStopMsg {};

struct TransportSetBPMMsg {
    double bpm;
};

struct TransportSetTimeSignatureMsg {
    int numerator;
    int denominator;
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

struct MetronomeToggleMsg {
    bool enabled;
};

struct MetronomeSetVolumeMsg {
    float volume;
};

struct MetronomeSetBeatsPerBarMsg {
    int beatsPerBar;
};

// Send a MIDI message to a specific track's instrument
struct SendMidiToTrackMsg {
    int trackIndex;
    uint8_t type;       // MidiMessage::Type as uint8_t
    uint8_t channel;
    uint8_t note;
    uint16_t velocity;
    uint32_t value;     // for CC, pitch bend, etc.
    uint16_t ccNumber = 0;
};

// Launch a MIDI clip on a track
struct LaunchMidiClipMsg {
    int trackIndex;
    const midi::MidiClip* clip; // must remain valid while playing (owned by Project)
};

// Stop a MIDI clip on a track
struct StopMidiClipMsg {
    int trackIndex;
};

// Recording
struct TransportRecordMsg {
    bool arm;
};

struct TransportSetCountInMsg {
    int bars;
};

// Track arm state
struct SetTrackArmedMsg {
    int trackIndex;
    bool armed;
};

// MIDI recording
struct StartMidiRecordMsg {
    int trackIndex;
    int sceneIndex;
    bool overdub; // true = overdub into existing clip, false = replace
};

struct StopMidiRecordMsg {
    int trackIndex;
};

struct StartAudioRecordMsg {
    int trackIndex;
    int sceneIndex;
};

struct StopAudioRecordMsg {
    int trackIndex;
};

using AudioCommand = std::variant<
    TransportPlayMsg,
    TransportStopMsg,
    TransportSetBPMMsg,
    TransportSetTimeSignatureMsg,
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
    SetReturnMuteMsg,
    MetronomeToggleMsg,
    MetronomeSetVolumeMsg,
    MetronomeSetBeatsPerBarMsg,
    SendMidiToTrackMsg,
    LaunchMidiClipMsg,
    StopMidiClipMsg,
    TransportRecordMsg,
    TransportSetCountInMsg,
    SetTrackArmedMsg,
    StartMidiRecordMsg,
    StopMidiRecordMsg,
    StartAudioRecordMsg,
    StopAudioRecordMsg
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

struct TransportRecordStateUpdate {
    bool recording;
    bool countingIn;
    double countInProgress;
};

struct MidiRecordCompleteEvent {
    int trackIndex;
    int sceneIndex;
    int noteCount;
};

struct AudioRecordCompleteEvent {
    int trackIndex;
    int sceneIndex;
    int64_t frameCount;
};

using AudioEvent = std::variant<
    TransportPositionUpdate,
    ClipStateUpdate,
    MeterUpdate,
    TransportRecordStateUpdate,
    MidiRecordCompleteEvent,
    AudioRecordCompleteEvent
>;

// Command queue: UI → Audio (1024 slots)
using CommandQueue = util::RingBuffer<AudioCommand, 1024>;

// Event queue: Audio → UI (1024 slots)
using EventQueue = util::RingBuffer<AudioEvent, 1024>;

} // namespace audio
} // namespace yawn
