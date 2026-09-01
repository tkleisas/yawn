// MidiClipEngine.cpp — method implementations split from MidiClipEngine.h.

#include "MidiClipEngine.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace audio {

namespace {
// Bit index into MidiClipPlayState::heldNotes — the (channel, pitch)
// set of currently-sounding notes, so a clip hot-swap can release
// exactly the ones the replacement won't keep.
constexpr size_t heldBit(uint8_t ch, uint8_t pitch) {
    return static_cast<size_t>(ch) * 128 + pitch;
}
} // namespace

void MidiClipEngine::scheduleClip(int trackIndex, int sceneIndex,
                                  const midi::MidiClip* clip,
                                  QuantizeMode quantize,
                                  const std::vector<automation::AutomationLane>* clipAutomation,
                                  const FollowAction& followAction) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
    if (quantize == QuantizeMode::None) {
        if (clip) launchNow(trackIndex, sceneIndex, clip, clipAutomation, followAction);
        else stopNow(trackIndex);
    } else {
        m_pending[trackIndex] = {trackIndex, sceneIndex, clip, quantize, true, clipAutomation, followAction};
    }
}

void MidiClipEngine::scheduleStop(int trackIndex, QuantizeMode quantize) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
    if (quantize == QuantizeMode::None) {
        stopNow(trackIndex);
        // Also drop any pending launch — mirrors ClipEngine::scheduleStop.
        // Without this, a quantized launch queued before the stop fires
        // at the next boundary and re-points the track at a clip the UI
        // may already have graveyarded (structural scene edits rely on
        // this to keep deleted scenes' clips from relaunching).
        m_pending[trackIndex] = {};
    } else {
        m_pending[trackIndex] = {trackIndex, -1, nullptr, quantize, true};
    }
}

void MidiClipEngine::swapClip(int trackIndex, const midi::MidiClip* oldClip,
                              const midi::MidiClip* newClip,
                              midi::MidiBuffer& buffer) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
    if (!newClip || !oldClip) return;

    // Re-point active playback if it's the clip being replaced. We
    // compare pointers only (oldClip is not dereferenced — it may be
    // sitting in the clip graveyard awaiting purge).
    auto& state = m_tracks[trackIndex];
    if (state.clip == oldClip) {
        state.clip = newClip;
        double newLen = newClip->lengthBeats();
        if (newLen > 0.0 && state.playPositionBeats >= newLen) {
            state.playPositionBeats = newClip->loop()
                ? std::fmod(state.playPositionBeats, newLen)
                : newLen;
        }

        // Release held notes the new clip doesn't keep sounding at the
        // current position — otherwise the old clip's in-flight notes
        // (whose note-offs lived in the now-replaced clip) hang forever.
        // Notes the new clip still sustains here are left untouched so
        // an edit that preserves a note doesn't audibly cut it; the new
        // clip's scan emits their note-off at the right beat.
        double pos = state.playPositionBeats;
        m_noteIndices.clear();
        newClip->getActiveNotesAt(pos, m_noteIndices);
        auto soundsInNew = [&](uint8_t ch, uint8_t pitch) {
            for (int idx : m_noteIndices) {
                const auto& nn = newClip->note(idx);
                if (nn.channel == ch && nn.pitch == pitch) return true;
            }
            return false;
        };
        for (int ch = 0; ch < 16; ++ch) {
            for (int pitch = 0; pitch < 128; ++pitch) {
                const size_t bit = heldBit(static_cast<uint8_t>(ch),
                                           static_cast<uint8_t>(pitch));
                if (!state.heldNotes.test(bit)) continue;
                if (!soundsInNew(static_cast<uint8_t>(ch),
                                 static_cast<uint8_t>(pitch))) {
                    buffer.addMessage(midi::MidiMessage::noteOff(
                        static_cast<uint8_t>(ch),
                        static_cast<uint8_t>(pitch), 0, 0));
                    state.heldNotes.reset(bit);
                }
            }
        }
    }

    // A quantized launch may still be queued against the old pointer.
    if (m_pending[trackIndex].valid && m_pending[trackIndex].clip == oldClip)
        m_pending[trackIndex].clip = newClip;
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
            launchNow(t, m_pending[t].sceneIndex, m_pending[t].clip, m_pending[t].clipAutomation, m_pending[t].followAction);
        } else {
            stopNow(t);
        }
        m_pending[t].valid = false;
    }

    m_lastQuantizeCheck = pos;
}

void MidiClipEngine::checkFollowActions() {
    if (!m_transport || !m_transport->isPlaying()) return;

    int64_t pos = m_transport->positionInSamples();
    double samplesPerBar = m_transport->samplesPerBar();
    if (samplesPerBar <= 0.0) return;

    for (int t = 0; t < kMaxTracks; ++t) {
        auto& state = m_tracks[t];
        if (!state.active || state.stopping) continue;
        if (!state.followAction.enabled) continue;
        if (m_pending[t].valid) continue;

        int64_t elapsed = pos - state.barStartSample;
        int64_t currentBar = static_cast<int64_t>(elapsed / samplesPerBar);
        if (currentBar > state.barsPlayed) {
            state.barsPlayed = currentBar;

            if (state.barsPlayed >= state.followAction.barCount) {
                int roll = static_cast<int>(m_rng() % 100);
                FollowActionType action = (roll < state.followAction.chanceA)
                    ? state.followAction.actionA
                    : state.followAction.actionB;

                if (action == FollowActionType::None) {
                    state.barsPlayed = 0;
                    state.barStartSample = pos;
                    continue;
                }
                if (action == FollowActionType::Stop) {
                    stopNow(t);
                    continue;
                }
                if (action == FollowActionType::PlayAgain) {
                    state.playPositionBeats = 0.0;
                    state.barsPlayed = 0;
                    state.barStartSample = pos;
                    continue;
                }
                if (m_followActionCb) {
                    m_followActionCb(t, state.sceneIndex, action);
                }
                state.barsPlayed = 0;
                state.barStartSample = pos;
            }
        }
    }
}

void MidiClipEngine::process(midi::MidiBuffer* trackMidiBuffers, int numFrames) {
    if (!m_transport) return;

    checkFollowActions();

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

        double loopStart = clip->loopStartBeat();

        // Handle looping: may need to scan across loop boundary
        if (clip->loop() && bufEndBeat > clipLen) {
            // First segment: bufStartBeat .. clipLen
            scanAndEmit(state, trackMidiBuffers[t], clip, bufStartBeat, clipLen,
                        bufStartBeat, samplesPerBeat, numFrames);
            // Second segment: wrap to loopStart (not 0)
            double loopLen = clipLen - loopStart;
            if (loopLen <= 0) loopLen = clipLen; // safety
            double overshoot = bufEndBeat - clipLen;
            scanAndEmit(state, trackMidiBuffers[t], clip, loopStart,
                        loopStart + std::min(overshoot, loopLen),
                        bufStartBeat - clipLen + loopStart, samplesPerBeat, numFrames);

            state.playPositionBeats = loopStart + std::fmod(overshoot, loopLen);
        } else if (!clip->loop() && bufEndBeat > clipLen) {
            // Play to end then stop
            scanAndEmit(state, trackMidiBuffers[t], clip, bufStartBeat, clipLen,
                        bufStartBeat, samplesPerBeat, numFrames);
            stopNow(t);
        } else {
            scanAndEmit(state, trackMidiBuffers[t], clip, bufStartBeat, bufEndBeat,
                        bufStartBeat, samplesPerBeat, numFrames);
            state.playPositionBeats = bufEndBeat;
        }
    }
}

void MidiClipEngine::launchNow(int trackIndex, int sceneIndex,
                               const midi::MidiClip* clip,
                               const std::vector<automation::AutomationLane>* clipAutomation,
                               const FollowAction& followAction) {
    auto& state = m_tracks[trackIndex];
    if (state.active) {
        stopNow(trackIndex);
    }
    state.clip = clip;
    state.playPositionBeats = 0.0;
    state.active = true;
    state.stopping = false;
    state.heldNotes.reset();
    state.sceneIndex = sceneIndex;
    state.clipAutomation = clipAutomation;
    state.followAction = followAction;
    state.barsPlayed = 0;
    state.barStartSample = m_transport ? m_transport->positionInSamples() : 0;
    LOG_DEBUG("MIDI", "MidiClipEngine launchNow track=%d scene=%d notes=%d len=%.2f",
                trackIndex, sceneIndex,
                clip ? static_cast<int>(clip->noteCount()) : 0,
                clip ? clip->lengthBeats() : 0.0);
}

void MidiClipEngine::stopNow(int trackIndex) {
    auto& state = m_tracks[trackIndex];
    // Held-note bookkeeping is cleared here; the AudioEngine flushes the
    // actual note-offs for this track (all-notes-off) on stop.
    state.active = false;
    state.stopping = false;
    // Keep state.clip so emitClipStates() continues reporting playing=false
    // to the UI (prevents stale "playing" state blocking relaunch).
    state.playPositionBeats = 0.0;
    state.heldNotes.reset();
    // Drop the cached automation-lane pointer (ClipEngine::scheduleStop
    // does the same). The automation context gates on `active`, so a
    // stale pointer is never dereferenced — but keeping it would let a
    // since-freed box's address linger in engine state, which is
    // precisely the dangling-pointer class the scene-edit quiesce
    // handshake exists to rule out.
    state.clipAutomation = nullptr;
}

void MidiClipEngine::scanAndEmit(MidiClipPlayState& state,
                                 midi::MidiBuffer& buffer,
                                 const midi::MidiClip* clip,
                                 double scanStart, double scanEnd,
                                 double offsetBeat, double samplesPerBeat,
                                 int numFrames) {
    // ── Event ordering rationale ──
    //
    // EMIT NOTE-OFFS FIRST. When a buffer boundary doesn't land
    // exactly on a beat (the common case), it can straddle the end of
    // one note and the start of the next at the same beat — i.e.,
    // Note A.endBeat == Note B.startBeat. Both events land at the
    // same frame in the same buffer. If we emit the Note-On before
    // the Note-Off, the synth allocates a new voice for B, then the
    // delayed Note-Off (which says "kill pitch P") matches the new
    // voice and kills it instead of A's lingering one. Symptom:
    // back-to-back same-pitch notes play as one held note (no
    // re-articulation).
    //
    // By emitting offs first, the synth releases A's voice cleanly
    // and then allocates a fresh voice for B. Two distinct notes,
    // proper attack/release each.
    //
    // --- Note-Offs ---
    // A note-off occurs at startBeat + duration. Scan all notes whose
    // end falls in (scanStart, scanEnd] — a note ending exactly at
    // the buffer-end fires this buffer.
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
            state.heldNotes.reset(heldBit(n.channel, n.pitch));
        }
        // Early exit: if note starts after scanEnd, no more note-offs possible
        if (n.startBeat >= scanEnd) break;
    }

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
        state.heldNotes.set(heldBit(n.channel, n.pitch));
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
