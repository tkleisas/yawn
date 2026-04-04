#pragma once

#include "core/Constants.h"
#include "audio/ClipEngine.h"
#include "audio/FollowAction.h"
#include "audio/Transport.h"
#include "automation/AutomationLane.h"
#include "midi/MidiClip.h"
#include "midi/MidiTypes.h"
#include <array>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

namespace yawn {
namespace audio {

// Per-track MIDI clip playback state
struct MidiClipPlayState {
    const midi::MidiClip* clip = nullptr;
    double playPositionBeats = 0.0;
    bool active = false;
    bool stopping = false;
    int sceneIndex = -1;          // which scene slot is playing
    const std::vector<automation::AutomationLane>* clipAutomation = nullptr;

    // Follow action tracking
    int64_t barsPlayed = 0;
    int64_t barStartSample = 0;
    FollowAction followAction;
};

// Pending MIDI clip launch (for quantized launching)
struct PendingMidiLaunch {
    int trackIndex = -1;
    int sceneIndex = -1;
    const midi::MidiClip* clip = nullptr;
    QuantizeMode quantizeMode = QuantizeMode::NextBar;
    bool valid = false;
    const std::vector<automation::AutomationLane>* clipAutomation = nullptr;
    FollowAction followAction;
};

// MidiClipEngine: plays MidiClips on the audio thread, generating MIDI
// messages into per-track MidiBuffers. Runs parallel to ClipEngine (audio).
class MidiClipEngine {
public:
    MidiClipEngine() = default;

    void setTransport(Transport* transport) { m_transport = transport; }
    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }

    // Schedule a MIDI clip to launch on a track
    void scheduleClip(int trackIndex, int sceneIndex, const midi::MidiClip* clip,
                      QuantizeMode quantize = QuantizeMode::NextBar,
                      const std::vector<automation::AutomationLane>* clipAutomation = nullptr,
                      const FollowAction& followAction = FollowAction{});

    void scheduleStop(int trackIndex,
                      QuantizeMode quantize = QuantizeMode::NextBar);

    // Check and fire pending quantized launches. Call once per buffer.
    void checkAndFirePending();

    // Process one buffer: scan clips for notes/CCs, write into MIDI buffers.
    // Must be called BEFORE MIDI effect chains and instruments.
    void process(midi::MidiBuffer* trackMidiBuffers, int numFrames);

    // Query state
    bool isTrackPlaying(int trackIndex) const {
        if (trackIndex < 0 || trackIndex >= kMaxTracks) return false;
        return m_tracks[trackIndex].active;
    }

    const MidiClipPlayState& trackState(int trackIndex) const {
        return m_tracks[trackIndex];
    }

    // Follow action callback
    using FollowActionCallback = std::function<void(int, int, FollowActionType)>;
    void setFollowActionCallback(FollowActionCallback cb) { m_followActionCb = std::move(cb); }

private:
    void launchNow(int trackIndex, int sceneIndex, const midi::MidiClip* clip,
                   const std::vector<automation::AutomationLane>* clipAutomation = nullptr,
                   const FollowAction& followAction = FollowAction{});
    void stopNow(int trackIndex);
    void checkFollowActions();

    void scanAndEmit(midi::MidiBuffer& buffer, const midi::MidiClip* clip,
                     double scanStart, double scanEnd,
                     double offsetBeat, double samplesPerBeat, int numFrames);

    Transport* m_transport = nullptr;
    double m_sampleRate = 44100.0;
    int64_t m_lastQuantizeCheck = -1;

    std::array<MidiClipPlayState, kMaxTracks> m_tracks{};
    std::array<PendingMidiLaunch, kMaxTracks> m_pending{};

    FollowActionCallback m_followActionCb;
    std::mt19937 m_rng{std::random_device{}()};

    // Reusable scratch buffers (avoid per-buffer allocations)
    std::vector<int> m_noteIndices;
    std::vector<int> m_ccIndices;
};

} // namespace audio
} // namespace yawn
