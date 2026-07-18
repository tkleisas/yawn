#pragma once
// ArrangementPlayback — Plays arrangement clips on the audio thread.
//
// For tracks in arrangement mode, this replaces session clip playback.
// Audio clips are rendered directly into the track buffer; MIDI clips
// are scanned and emitted into the track MIDI buffer.
//
// The UI thread submits arrangement clip lists through a lock-free
// per-track mailbox (submitTrackClips); the audio thread swaps them in
// (applyPendingClips) and retires the old lists back to the UI thread
// for destruction — no locks, allocations, or frees on the audio
// thread. The audio thread only reads the clip data (AudioBuffer /
// MidiClip are immutable once loaded, shared via shared_ptr).

#include "core/Constants.h"
#include "audio/AudioBuffer.h"
#include "audio/Transport.h"
#include "audio/TimeStretcher.h"
#include "midi/MidiClip.h"
#include "midi/MidiTypes.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>
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
    // When the slot is longer than the source content: loop true repeats
    // the content from offsetBeats to the source end; false plays once
    // and goes silent past the end.
    bool   loop = true;
    // Time-stretch the content to fill the slot exactly (overrides loop).
    // MIDI scales note times; audio uses the time-stretcher (later step).
    bool   stretch = false;

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

    // Time-stretch playback (one stretcher per channel, ≤2). Lazily
    // init'd on the first stretched clip — a one-time allocation on the
    // audio thread, matching the session ClipEngine's pattern. Output is
    // produced in chunks into arrStretchOut and read one frame at a time;
    // arrStretchSrcPos is the source gather position (frames).
    TimeStretcher      arrStretch[2];
    std::vector<float> arrStretchOut[2];
    int                arrStretchAvail[2] = {0, 0};
    int                arrStretchRead[2]  = {0, 0};
    double             arrStretchSrcPos   = 0.0;
    bool               arrStretchReady    = false;   // stretchers init'd
    static constexpr int kArrStretchOutSize = 8192;

    static constexpr float kFadeIncrement = 0.002f;
};

class ArrangementPlayback {
public:
    ArrangementPlayback() = default;

    ~ArrangementPlayback() {
        for (auto& slot : m_pendingSlot)
            delete slot.exchange(nullptr, std::memory_order_acq_rel);
        collectRetired();
    }

    void setTransport(Transport* t) { m_transport = t; }
    void setSampleRate(double sr)   { m_sampleRate = sr; }

    // Pre-allocate all per-track stretch state + the gather scratch
    // (UI thread, engine init). After this, entering a stretched clip
    // never allocates on the audio thread.
    void preallocateStretchers(double sampleRate, int blockSize) {
        for (auto& ts : m_tracks) {
            for (int ch = 0; ch < 2; ++ch) {
                ts.arrStretch[ch].preallocate(sampleRate, blockSize);
                if (static_cast<int>(ts.arrStretchOut[ch].size()) <
                        ArrTrackState::kArrStretchOutSize)
                    ts.arrStretchOut[ch].assign(
                        ArrTrackState::kArrStretchOutSize, 0.0f);
            }
        }
        m_stretchGather.assign(4096, 0.0f);
    }

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

    // Called from UI thread — lock-free clip submission. Last writer
    // wins: a node the audio thread hasn't consumed yet is replaced
    // and freed here, never on the audio thread.
    void submitTrackClips(int track, std::vector<ArrClipRef> clips) {
        if (track < 0 || track >= kMaxTracks) return;
        auto* node = new PendingClipsNode{std::move(clips), nullptr};
        delete m_pendingSlot[track].exchange(node, std::memory_order_acq_rel);
        collectRetired();
    }

    // Called from audio thread (once per buffer) — applies any pending
    // clip updates. No locks and no heap traffic: the new list is
    // swapped in and the node (now carrying the OLD list, whose
    // destruction may drop the last shared_ptr to clip data) is pushed
    // onto the retired stack for the UI thread to free.
    void applyPendingClips() {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (!m_pendingSlot[t].load(std::memory_order_acquire))
                continue;
            auto* node = m_pendingSlot[t].exchange(nullptr, std::memory_order_acq_rel);
            if (!node) continue;
            std::swap(m_tracks[t].clips, node->clips);
            retire(node);
            resetTrack(t);
        }
    }

    // Called from UI thread — frees clip lists the audio thread has
    // swapped out. Invoked on every submit and once per frame from
    // App::update() so retired lists don't pin clip data.
    void collectRetired() {
        auto* node = m_retiredHead.exchange(nullptr, std::memory_order_acquire);
        while (node) {
            auto* next = node->next;
            delete node;
            node = next;
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
        s.arrStretchAvail[0] = s.arrStretchAvail[1] = 0;
        s.arrStretchRead[0]  = s.arrStretchRead[1]  = 0;
        // currentClipIdx = -1 forces a re-prime of the stretcher on the
        // next stretched frame, so no need to reset the stretchers here.
    }

    void resetAllTracks() {
        for (int t = 0; t < kMaxTracks; ++t)
            resetTrack(t);
    }

    // Called from UI thread with transport stopped (see
    // AudioEngine::removeTrackSlot). Pending nodes shift down with
    // their track; a node displaced from a slot is freed here.
    void removeTrackSlot(int index, int last) {
        for (int i = index; i < last; ++i) {
            m_tracks[i] = std::move(m_tracks[i + 1]);
            auto* moved = m_pendingSlot[i + 1].exchange(nullptr, std::memory_order_acq_rel);
            delete m_pendingSlot[i].exchange(moved, std::memory_order_acq_rel);
        }
        m_tracks[last] = {};
        delete m_pendingSlot[last].exchange(nullptr, std::memory_order_acq_rel);
    }

    // Render audio for one track into buffer (called from processAudio).
    // Buffer must be pre-zeroed. Only writes if track is in arrangement mode
    // and a clip is active at the current transport position.
    void processAudioTrack(int track, float* buffer, int numFrames, int numChannels);

    // Scan MIDI arrangement clips and emit notes/CCs into the MIDI buffer.
    void processMidiTrack(int track, midi::MidiBuffer& midiBuffer, int numFrames);

private:
    // Refill a stretched audio clip's per-channel output buffers so each
    // has at least one unread frame. Returns false when the source is
    // exhausted and the buffers are drained (clip finished).
    bool fillStretch(ArrTrackState& st, const ArrClipRef& clip, int numChannels);
    // Heap node carrying a clip list across threads. Allocated and
    // freed only on the UI thread; the audio thread just exchanges
    // pointers and swaps vector contents.
    struct PendingClipsNode {
        std::vector<ArrClipRef> clips;
        PendingClipsNode* next = nullptr;
    };

    // Audio thread — push a consumed node (holding the old clip list)
    // onto the retired stack. Single producer; the only contention is
    // the UI thread taking the whole stack in collectRetired().
    void retire(PendingClipsNode* node) {
        auto* head = m_retiredHead.load(std::memory_order_relaxed);
        do {
            node->next = head;
        } while (!m_retiredHead.compare_exchange_weak(
            head, node, std::memory_order_release, std::memory_order_relaxed));
    }

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
    double m_sampleRate = kDefaultSampleRate;
    std::array<ArrTrackState, kMaxTracks> m_tracks{};
    // Scratch for fillStretch's source gather — sized once in
    // preallocateStretchers (the previous thread_local copy allocated
    // on first use on the audio thread). Audio thread only, fully
    // rewritten per call.
    std::vector<float> m_stretchGather;

    // Lock-free clip submission (UI→audio): per-track mailbox of the
    // latest submitted list, plus a stack of consumed nodes awaiting
    // UI-thread destruction.
    std::atomic<PendingClipsNode*> m_pendingSlot[kMaxTracks]{};
    std::atomic<PendingClipsNode*> m_retiredHead{nullptr};
};

} // namespace audio
} // namespace yawn
