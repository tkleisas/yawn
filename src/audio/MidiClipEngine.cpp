// MidiClipEngine.cpp — method implementations split from MidiClipEngine.h.

#include "MidiClipEngine.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace audio {

void MidiClipEngine::scheduleClip(int trackIndex, int sceneIndex,
                                  const midi::MidiClip* clip,
                                  QuantizeMode quantize) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
    if (quantize == QuantizeMode::None) {
        if (clip) launchNow(trackIndex, sceneIndex, clip);
        else stopNow(trackIndex);
    } else {
        m_pending[trackIndex] = {trackIndex, sceneIndex, clip, quantize, true};
    }
}

void MidiClipEngine::scheduleStop(int trackIndex, QuantizeMode quantize) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
    if (quantize == QuantizeMode::None) {
        stopNow(trackIndex);
    } else {
        m_pending[trackIndex] = {trackIndex, -1, nullptr, quantize, true};
    }
}

void MidiClipEngine::checkAndFirePending() {
    if (!m_transport) return;

    int64_t pos = m_transport->positionInSamples();
    double spb = m_transport->samplesPerBar();
    double spBeat = m_transport->samplesPerBeat();

    for (int t = 0; t < kMaxTracks; ++t) {
        if (!m_pending[t].valid) continue;

        QuantizeMode qm = m_pending[t].quantizeMode;
        if (qm != QuantizeMode::None) {
            double interval = (qm == QuantizeMode::NextBar) ? spb : spBeat;
            if (interval <= 0.0) continue;
            int64_t iInterval = static_cast<int64_t>(interval);
            if (iInterval <= 0) continue;

            int64_t currentBoundary = (pos / iInterval) * iInterval;
            bool atBoundary = (m_lastQuantizeCheck < 0) ||
                ((currentBoundary / iInterval) != (m_lastQuantizeCheck / iInterval));

            if (!atBoundary && m_transport->isPlaying()) continue;
        }

        if (m_pending[t].clip) {
            launchNow(t, m_pending[t].sceneIndex, m_pending[t].clip);
        } else {
            stopNow(t);
        }
        m_pending[t].valid = false;
    }

    m_lastQuantizeCheck = pos;
}

void MidiClipEngine::process(midi::MidiBuffer* trackMidiBuffers, int numFrames) {
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

void MidiClipEngine::launchNow(int trackIndex, int sceneIndex,
                               const midi::MidiClip* clip) {
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
    LOG_DEBUG("MIDI", "MidiClipEngine launchNow track=%d scene=%d notes=%d len=%.2f",
                trackIndex, sceneIndex,
                clip ? static_cast<int>(clip->noteCount()) : 0,
                clip ? clip->lengthBeats() : 0.0);
}

void MidiClipEngine::stopNow(int trackIndex) {
    auto& state = m_tracks[trackIndex];
    // Note-off cleanup is tracked via m_activeNotes
    // The AudioEngine should call allNotesOff() for this track
    state.active = false;
    state.stopping = false;
    state.clip = nullptr;
    state.playPositionBeats = 0.0;
}

void MidiClipEngine::scanAndEmit(midi::MidiBuffer& buffer,
                                 const midi::MidiClip* clip,
                                 double scanStart, double scanEnd,
                                 double offsetBeat, double samplesPerBeat,
                                 int numFrames) {
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

} // namespace audio
} // namespace yawn
