#pragma once

#include "util/RingBuffer.h"
#include "audio/ClipEngine.h"
#include "audio/ArrangementPlayback.h"
#include "automation/AutomationLane.h"
#include <cstdint>
#include <variant>
#include <vector>

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
    int sceneIndex;
    const Clip* clip;
    QuantizeMode quantize = QuantizeMode::NextBar;
    const std::vector<automation::AutomationLane>* clipAutomation = nullptr;
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

struct MetronomeSetModeMsg {
    int mode; // 0=Always, 1=RecordOnly, 2=PlayOnly, 3=Off
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
    int sceneIndex;
    const midi::MidiClip* clip;
    QuantizeMode quantize = QuantizeMode::NextBar;
    const std::vector<automation::AutomationLane>* clipAutomation = nullptr;
};

// Stop a MIDI clip on a track
struct StopMidiClipMsg {
    int trackIndex;
};

// Recording
struct TransportRecordMsg {
    bool arm;
    int sceneIndex = 0;  // target scene for armed-track recording
};

struct TransportSetCountInMsg {
    int bars;
};

// Track arm state
struct SetTrackArmedMsg {
    int trackIndex;
    bool armed;
};

struct SetTrackMonitorMsg {
    int trackIndex;
    uint8_t mode; // 0=Auto, 1=In, 2=Off (matches Track::MonitorMode)
};

// MIDI recording
struct StartMidiRecordMsg {
    int trackIndex;
    int sceneIndex;
    bool overdub; // true = overdub into existing clip, false = replace
};

struct StopMidiRecordMsg {
    int trackIndex;
    QuantizeMode quantize = QuantizeMode::NextBar;
};

struct StartAudioRecordMsg {
    int trackIndex;
    int sceneIndex;
    bool overdub = false; // true = mix into existing clip buffer
};

struct StopAudioRecordMsg {
    int trackIndex;
    QuantizeMode quantize = QuantizeMode::NextBar;
};

struct SetTrackTypeMsg {
    int trackIndex;
    uint8_t type; // 0=Audio, 1=Midi
};

struct SetTrackAudioInputChMsg {
    int trackIndex;
    int channel; // 0=none, 1=In1, 2=In2, 3=In1+2, 4=In3, 5=In3+4...
};

struct SetTrackMonoMsg {
    int trackIndex;
    bool mono;
};

struct SetTrackMidiOutputMsg {
    int trackIndex;
    int portIndex;  // -1=none
    int channel;    // -1=all, 0-15
};

struct MoveMidiEffectMsg {
    int trackIndex;
    int fromIndex;
    int toIndex;
};

struct MoveAudioEffectMsg {
    int trackIndex;
    int fromIndex;
    int toIndex;
};

// Sent after reordering MIDI effects to reset state on the audio thread
struct ResetMidiEffectChainMsg {
    int trackIndex;
};

struct SetAutoModeMsg {
    int trackIndex;
    uint8_t mode; // automation::AutoMode as uint8_t
};

struct AutoParamTouchMsg {
    int trackIndex;
    uint8_t targetType;   // automation::TargetType as uint8_t
    int chainIndex;
    int paramIndex;
    float value;
    bool touching;        // true = begin, false = release
};

// Arrangement playback control
struct SetTrackArrActiveMsg {
    int trackIndex;
    bool active;
};

// Loop range control
struct TransportSetLoopEnabledMsg {
    bool enabled;
};

struct TransportSetLoopRangeMsg {
    double startBeats;
    double endBeats;
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
    MetronomeSetModeMsg,
    SendMidiToTrackMsg,
    LaunchMidiClipMsg,
    StopMidiClipMsg,
    TransportRecordMsg,
    TransportSetCountInMsg,
    SetTrackArmedMsg,
    SetTrackMonitorMsg,
    StartMidiRecordMsg,
    StopMidiRecordMsg,
    StartAudioRecordMsg,
    StopAudioRecordMsg,
    SetTrackTypeMsg,
    SetTrackAudioInputChMsg,
    SetTrackMonoMsg,
    SetTrackMidiOutputMsg,
    MoveMidiEffectMsg,
    MoveAudioEffectMsg,
    ResetMidiEffectChainMsg,
    SetAutoModeMsg,
    AutoParamTouchMsg,
    SetTrackArrActiveMsg,
    TransportSetLoopEnabledMsg,
    TransportSetLoopRangeMsg
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
    int playingScene = -1;
    bool isMidi = false;
    double clipLengthBeats = 0.0;
    bool recording = false;
    int recordingScene = -1;
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
    bool overdub = false;
};

struct RecordBufferFullEvent {
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
    AudioRecordCompleteEvent,
    RecordBufferFullEvent
>;

// Command queue: UI → Audio (1024 slots)
using CommandQueue = util::RingBuffer<AudioCommand, 1024>;

// Event queue: Audio → UI (1024 slots)
using EventQueue = util::RingBuffer<AudioEvent, 1024>;

} // namespace audio
} // namespace yawn
