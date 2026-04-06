#pragma once
// ArrangementPlayback — Plays arrangement clips on the audio thread.
//
// For tracks in arrangement mode, this replaces session clip playback.
// Audio clips are rendered directly into the track buffer; MIDI clips
// are scanned and emitted into the track MIDI buffer.
//
// The UI thread sends arrangement clip lists via command messages.
// The audio thread only reads the clip data (AudioBuffer / MidiClip are
// immutable once loaded, shared via shared_ptr).

#include "core/Constants.h"
#include "audio/AudioBuffer.h"
#include "audio/Transport.h"
#include "midi/MidiClip.h"
#include "midi/MidiTypes.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace yawn {
namespace audio {

// Lightweight clip descriptor for the audio thread.
// Owns shared_ptr references to ensure data outlives the audio callback.
struct ArrClipRef {
    enum class Type : uint8_t { Audio, Midi };

    Type type = Type::Audio;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    double offsetBeats = 0.0;

    std::shared_ptr<AudioBuffer>   audioBuffer;
    std::shared_ptr<midi::MidiClip> midiClip;

    double endBeat() const { return startBeat + lengthBeats; }
};

// Per-track arrangement playback state
struct ArrTrackState {
    bool active = false;               // Arrangement mode for this track
    std::vector<ArrClipRef> clips;     // Sorted by startBeat

    // Audio playback state
    int currentClipIdx = -1;           // Index into clips, or -1 if in gap
    int64_t audioPlayPos = 0;          // Frame position within current audio clip
    float fadeGain = 1.0f;

    // MIDI playback state
    int currentMidiClipIdx = -1;
    double lastMidiBeat = -1.0;        // Last processed beat (for scanning)

    static constexpr float kFadeIncrement = 0.002f;
};

class ArrangementPlayback {
public:
    ArrangementPlayback() = default;

    void setTransport(Transport* t) { m_transport = t; }
    void setSampleRate(double sr)   { m_sampleRate = sr; }

    // Called from audio thread (via command processing)
    void setTrackActive(int track, bool active) {
        if (track < 0 || track >= kMaxTracks) return;
        m_tracks[track].active = active;
        if (!active) resetTrack(track);
    }

    bool isTrackActive(int track) const {
        if (track < 0 || track >= kMaxTracks) return false;
        return m_tracks[track].active;
    }

    // Called from UI thread — thread-safe clip submission
    void submitTrackClips(int track, std::vector<ArrClipRef> clips) {
        if (track < 0 || track >= kMaxTracks) return;
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingClips[track] = std::move(clips);
        m_pendingFlags[track].store(true, std::memory_order_release);
    }

    // Called from audio thread (once per buffer) — applies any pending clip updates
    void applyPendingClips() {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_pendingFlags[t].load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_tracks[t].clips = std::move(m_pendingClips[t]);
                m_pendingFlags[t].store(false, std::memory_order_release);
                resetTrack(t);
            }
        }
    }

    // Replace arrangement clips for a track (called from audio thread after command)
    void setTrackClips(int track, std::vector<ArrClipRef> clips) {
        if (track < 0 || track >= kMaxTracks) return;
        m_tracks[track].clips = std::move(clips);
        resetTrack(track);
    }

    // Reset playback state for a track (e.g., on seek)
    void resetTrack(int track) {
        if (track < 0 || track >= kMaxTracks) return;
        auto& s = m_tracks[track];
        s.currentClipIdx = -1;
        s.audioPlayPos = 0;
        s.fadeGain = 1.0f;
        s.currentMidiClipIdx = -1;
        s.lastMidiBeat = -1.0;
    }

    void resetAllTracks() {
        for (int t = 0; t < kMaxTracks; ++t)
            resetTrack(t);
    }

    void removeTrackSlot(int index, int last) {
        for (int i = index; i < last; ++i) {
            m_tracks[i] = std::move(m_tracks[i + 1]);
            m_pendingClips[i] = std::move(m_pendingClips[i + 1]);
            m_pendingFlags[i].store(m_pendingFlags[i + 1].load());
        }
        m_tracks[last] = {};
        m_pendingClips[last].clear();
        m_pendingFlags[last].store(false);
    }

    // Render audio for one track into buffer (called from processAudio).
    // Buffer must be pre-zeroed. Only writes if track is in arrangement mode
    // and a clip is active at the current transport position.
    void processAudioTrack(int track, float* buffer, int numFrames, int numChannels);

    // Scan MIDI arrangement clips and emit notes/CCs into the MIDI buffer.
    void processMidiTrack(int track, midi::MidiBuffer& midiBuffer, int numFrames);

private:
    // Find the clip index at the given beat position (binary search on sorted clips)
    int findClipAt(const std::vector<ArrClipRef>& clips, double beat) const {
        for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
            if (beat >= clips[i].startBeat && beat < clips[i].endBeat())
                return i;
            if (clips[i].startBeat > beat)
                break; // clips are sorted, no point continuing
        }
        return -1;
    }

    Transport* m_transport = nullptr;
    double m_sampleRate = 44100.0;
    std::array<ArrTrackState, kMaxTracks> m_tracks{};

    // Thread-safe clip submission (UI→audio)
    std::mutex m_pendingMutex;
    std::vector<ArrClipRef> m_pendingClips[kMaxTracks];
    std::atomic<bool> m_pendingFlags[kMaxTracks]{};
};

} // namespace audio
} // namespace yawn
