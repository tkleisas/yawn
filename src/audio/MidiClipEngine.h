#pragma once

#include "core/Constants.h"
#include "audio/Transport.h"
#include "midi/MidiClip.h"
#include "midi/MidiTypes.h"
#include <array>
#include <bitset>
#include <cmath>

namespace yawn {
namespace audio {

// Per-track MIDI clip playback state
struct MidiClipPlayState {
    const midi::MidiClip* clip = nullptr;
    double playPositionBeats = 0.0;
    bool active = false;
    bool stopping = false;
    int sceneIndex = -1;          // which scene slot is playing
};

// Pending MIDI clip launch (for quantized launching)
struct PendingMidiLaunch {
    int trackIndex = -1;
    int sceneIndex = -1;
    const midi::MidiClip* clip = nullptr; // nullptr = stop
    bool valid = false;
};

// MidiClipEngine: plays MidiClips on the audio thread, generating MIDI
// messages into per-track MidiBuffers. Runs parallel to ClipEngine (audio).
class MidiClipEngine {
public:
    MidiClipEngine() = default;

    void setTransport(Transport* transport) { m_transport = transport; }
    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }

    // Schedule a MIDI clip to launch on a track
    void scheduleClip(int trackIndex, int sceneIndex, const midi::MidiClip* clip) {
        if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
        m_pending[trackIndex] = {trackIndex, sceneIndex, clip, true};
    }

    // Schedule a track to stop
    void scheduleStop(int trackIndex) {
        if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
        m_pending[trackIndex] = {trackIndex, -1, nullptr, true};
    }

    // Check and fire pending quantized launches. Call once per buffer.
    void checkAndFirePending() {
        if (!m_transport) return;

        for (int t = 0; t < kMaxTracks; ++t) {
            if (!m_pending[t].valid) continue;

            // For now, fire immediately (quantization handled by ClipEngine
            // which shares the same transport — we can add independent
            // quantization later if needed)
            if (m_pending[t].clip) {
                launchNow(t, m_pending[t].sceneIndex, m_pending[t].clip);
            } else {
                stopNow(t);
            }
            m_pending[t].valid = false;
        }
    }

    // Process one buffer: scan clips for notes/CCs, write into MIDI buffers.
    // Must be called BEFORE MIDI effect chains and instruments.
    void process(midi::MidiBuffer* trackMidiBuffers, int numFrames) {
        if (!m_transport) return;

        double bpm = m_transport->bpm();
        double sampleRate = m_sampleRate;
        if (bpm <= 0 || sampleRate <= 0) return;

        double samplesPerBeat = sampleRate * 60.0 / bpm;
        double beatsPerBuffer = numFrames / samplesPerBeat;

        for (int t = 0; t < kMaxTracks; ++t) {
            auto& state = m_tracks[t];
            if (!state.active || !state.clip) continue;

            const auto* clip = state.clip;
            double clipLen = clip->lengthBeats();
            if (clipLen <= 0) continue;

            double bufStartBeat = state.playPositionBeats;
            double bufEndBeat = bufStartBeat + beatsPerBuffer;

            // Handle looping: may need to scan across loop boundary
            if (clip->loop() && bufEndBeat > clipLen) {
                // First segment: bufStartBeat .. clipLen
                scanAndEmit(trackMidiBuffers[t], clip, bufStartBeat, clipLen,
                            bufStartBeat, samplesPerBeat, numFrames);
                // Second segment: 0 .. remainder
                double remainder = bufEndBeat - clipLen;
                scanAndEmit(trackMidiBuffers[t], clip, 0.0, remainder,
                            bufStartBeat - clipLen, samplesPerBeat, numFrames);

                state.playPositionBeats = std::fmod(bufEndBeat, clipLen);
            } else if (!clip->loop() && bufEndBeat > clipLen) {
                // Play to end then stop
                scanAndEmit(trackMidiBuffers[t], clip, bufStartBeat, clipLen,
                            bufStartBeat, samplesPerBeat, numFrames);
                stopNow(t);
            } else {
                scanAndEmit(trackMidiBuffers[t], clip, bufStartBeat, bufEndBeat,
                            bufStartBeat, samplesPerBeat, numFrames);
                state.playPositionBeats = bufEndBeat;
            }
        }
    }

    // Query state
    bool isTrackPlaying(int trackIndex) const {
        if (trackIndex < 0 || trackIndex >= kMaxTracks) return false;
        return m_tracks[trackIndex].active;
    }

    const MidiClipPlayState& trackState(int trackIndex) const {
        return m_tracks[trackIndex];
    }

private:
    void launchNow(int trackIndex, int sceneIndex, const midi::MidiClip* clip) {
        auto& state = m_tracks[trackIndex];
        // Send note-offs for any currently active notes before switching
        // (handled by stopNow if previously active)
        if (state.active) {
            stopNow(trackIndex);
        }
        state.clip = clip;
        state.playPositionBeats = 0.0;
        state.active = true;
        state.stopping = false;
        state.sceneIndex = sceneIndex;
    }

    void stopNow(int trackIndex) {
        auto& state = m_tracks[trackIndex];
        // Note-off cleanup is tracked via m_activeNotes
        // The AudioEngine should call allNotesOff() for this track
        state.active = false;
        state.stopping = false;
        state.clip = nullptr;
        state.playPositionBeats = 0.0;
    }

    // Scan a beat range within the clip for note-ons, note-offs, and CCs,
    // then emit them into the MidiBuffer with correct frame offsets.
    // offsetBeat: the beat position of frame 0 in the buffer
    void scanAndEmit(midi::MidiBuffer& buffer, const midi::MidiClip* clip,
                     double scanStart, double scanEnd,
                     double offsetBeat, double samplesPerBeat, int numFrames) {
        // --- Note-Ons ---
        m_noteIndices.clear();
        clip->getNotesInRange(scanStart, scanEnd, m_noteIndices);
        for (int idx : m_noteIndices) {
            const auto& n = clip->note(idx);
            double beatInBuf = n.startBeat - offsetBeat;
            int frame = static_cast<int>(beatInBuf * samplesPerBeat);
            frame = std::max(0, std::min(frame, numFrames - 1));

            // Convert 16-bit velocity to 7-bit for standard MIDI
            uint8_t vel7 = static_cast<uint8_t>(
                std::min(127, static_cast<int>(n.velocity >> 9)));
            if (vel7 == 0) vel7 = 1; // NoteOn with vel=0 is NoteOff

            buffer.addMessage(
                midi::MidiMessage::noteOn(n.channel, n.pitch, vel7, frame));
        }

        // --- Note-Offs: check if any note ends within this range ---
        // We need to scan all notes that could have note-offs in this range.
        // A note-off occurs at startBeat + duration.
        // Scan notes that started before scanEnd and whose end falls in range.
        for (int i = 0; i < clip->noteCount(); ++i) {
            const auto& n = clip->note(i);
            double noteEnd = n.startBeat + n.duration;
            if (noteEnd > scanStart && noteEnd <= scanEnd) {
                double beatInBuf = noteEnd - offsetBeat;
                int frame = static_cast<int>(beatInBuf * samplesPerBeat);
                frame = std::max(0, std::min(frame, numFrames - 1));

                uint8_t relVel7 = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(n.releaseVelocity >> 9)));

                buffer.addMessage(
                    midi::MidiMessage::noteOff(n.channel, n.pitch, relVel7, frame));
            }
            // Early exit: if note starts after scanEnd, no more note-offs possible
            if (n.startBeat >= scanEnd) break;
        }

        // --- CC Events ---
        m_ccIndices.clear();
        clip->getCCInRange(scanStart, scanEnd, m_ccIndices);
        for (int idx : m_ccIndices) {
            const auto& cc = clip->ccEvent(idx);
            double beatInBuf = cc.beat - offsetBeat;
            int frame = static_cast<int>(beatInBuf * samplesPerBeat);
            frame = std::max(0, std::min(frame, numFrames - 1));

            // Convert 32-bit value to 7-bit for standard CC
            uint8_t val7 = static_cast<uint8_t>(
                std::min(127, static_cast<int>(cc.value >> 25)));

            buffer.addMessage(
                midi::MidiMessage::cc(cc.channel,
                                      static_cast<uint8_t>(cc.ccNumber),
                                      val7, frame));
        }
    }

    Transport* m_transport = nullptr;
    double m_sampleRate = 44100.0;

    std::array<MidiClipPlayState, kMaxTracks> m_tracks{};
    std::array<PendingMidiLaunch, kMaxTracks> m_pending{};

    // Reusable scratch buffers (avoid per-buffer allocations)
    std::vector<int> m_noteIndices;
    std::vector<int> m_ccIndices;
};

} // namespace audio
} // namespace yawn
