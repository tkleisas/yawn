#include "audio/ArrangementPlayback.h"
#include <algorithm>

namespace yawn {
namespace audio {

void ArrangementPlayback::processAudioTrack(int track, float* buffer,
                                             int numFrames, int numChannels) {
    if (track < 0 || track >= kMaxTracks) return;
    auto& state = m_tracks[track];
    if (!state.active || state.clips.empty() || !m_transport) return;
    if (!m_transport->isPlaying()) return;

    double bpm = m_transport->bpm();
    if (bpm <= 0.0 || m_sampleRate <= 0.0) return;

    double samplesPerBeat = m_sampleRate * 60.0 / bpm;
    double currentBeat = m_transport->positionInBeats();

    for (int frame = 0; frame < numFrames; ++frame) {
        double beat = currentBeat + static_cast<double>(frame) / samplesPerBeat;

        // Find which clip we're in
        int clipIdx = findClipAt(state.clips, beat);

        if (clipIdx < 0) {
            // In a gap — fade out if needed
            if (state.fadeGain > 0.0f) {
                state.fadeGain -= ArrTrackState::kFadeIncrement;
                if (state.fadeGain <= 0.0f) {
                    state.fadeGain = 0.0f;
                    state.currentClipIdx = -1;
                }
            }
            continue;
        }

        auto& clip = state.clips[clipIdx];
        if (clip.type != ArrClipRef::Type::Audio || !clip.audioBuffer) continue;

        auto& buf = *clip.audioBuffer;
        int64_t totalFrames = buf.numFrames();
        int nc = std::min(buf.numChannels(), numChannels);

        // If we've moved to a new clip, compute the audio position
        if (clipIdx != state.currentClipIdx) {
            state.currentClipIdx = clipIdx;
            state.fadeGain = 0.0f; // fade in

            // Compute frame position from beat offset
            double beatIntoClip = beat - clip.startBeat + clip.offsetBeats;
            double framesFromStart = beatIntoClip * samplesPerBeat;
            state.audioPlayPos = static_cast<int64_t>(framesFromStart);
            if (state.audioPlayPos < 0) state.audioPlayPos = 0;
            if (state.audioPlayPos >= totalFrames) continue;
        }

        // Bounds check
        if (state.audioPlayPos < 0 || state.audioPlayPos >= totalFrames) {
            state.audioPlayPos++;
            continue;
        }

        // Fade in/out
        if (state.fadeGain < 1.0f) {
            state.fadeGain = std::min(1.0f, state.fadeGain + ArrTrackState::kFadeIncrement);
        }

        float gain = state.fadeGain;

        // Write samples
        for (int ch = 0; ch < nc; ++ch) {
            buffer[frame * numChannels + ch] += buf.sample(ch, state.audioPlayPos) * gain;
        }

        state.audioPlayPos++;
    }
}

void ArrangementPlayback::processMidiTrack(int track, midi::MidiBuffer& midiBuffer,
                                            int numFrames) {
    if (track < 0 || track >= kMaxTracks) return;
    auto& state = m_tracks[track];
    if (!state.active || state.clips.empty() || !m_transport) return;
    if (!m_transport->isPlaying()) return;

    double bpm = m_transport->bpm();
    if (bpm <= 0.0 || m_sampleRate <= 0.0) return;

    double samplesPerBeat = m_sampleRate * 60.0 / bpm;
    double currentBeat = m_transport->positionInBeats();
    double bufEndBeat = currentBeat + static_cast<double>(numFrames) / samplesPerBeat;

    // Scan all clips that overlap this buffer's time range
    for (auto& clip : state.clips) {
        if (clip.type != ArrClipRef::Type::Midi || !clip.midiClip) continue;
        if (clip.endBeat() <= currentBeat || clip.startBeat >= bufEndBeat) continue;

        auto& mc = *clip.midiClip;
        double clipLocalStart = currentBeat - clip.startBeat + clip.offsetBeats;
        double clipLocalEnd   = bufEndBeat - clip.startBeat + clip.offsetBeats;

        // Clamp to clip boundaries
        double scanStart = std::max(clipLocalStart, clip.offsetBeats);
        double scanEnd   = std::min(clipLocalEnd, clip.offsetBeats + clip.lengthBeats);
        if (scanStart >= scanEnd) continue;

        // Two-pass scan to keep Note-Offs ahead of Note-Ons in the
        // emitted buffer. When a buffer boundary doesn't align with
        // a beat (the common case), Note-A.endBeat == Note-B.startBeat
        // makes both events land at the same frame. If the on lands
        // first, the synth allocates a new voice for B, then the
        // delayed Note-Off (matching pitch P) kills the new voice
        // instead of A's lingering one — back-to-back same-pitch
        // notes end up sounding as one held note (no re-articulation).
        // Off-then-on guarantees clean release-then-attack at the
        // boundary regardless of frame collisions.

        // --- Pass 1: Note-Offs ---
        for (int i = 0; i < mc.noteCount(); ++i) {
            const auto& note = mc.note(i);
            double noteEnd = note.startBeat + note.duration;
            if (noteEnd >= scanStart && noteEnd < scanEnd) {
                double beatInBuffer = (noteEnd - clipLocalStart);
                int frameOff = static_cast<int>(beatInBuffer * samplesPerBeat);
                frameOff = std::clamp(frameOff, 0, numFrames - 1);

                midiBuffer.addMessage(
                    midi::MidiMessage::noteOff(note.channel, note.pitch, 0, frameOff));
            }
        }

        // --- Pass 2: Note-Ons ---
        for (int i = 0; i < mc.noteCount(); ++i) {
            const auto& note = mc.note(i);
            if (note.startBeat >= scanStart && note.startBeat < scanEnd) {
                double beatInBuffer = (note.startBeat - clipLocalStart);
                int frameOff = static_cast<int>(beatInBuffer * samplesPerBeat);
                frameOff = std::clamp(frameOff, 0, numFrames - 1);

                // Convert 16-bit velocity to 7-bit
                uint8_t vel7 = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(note.velocity >> 9)));
                if (vel7 == 0) vel7 = 1;

                midiBuffer.addMessage(
                    midi::MidiMessage::noteOn(note.channel, note.pitch, vel7, frameOff));
            }
        }

        // Scan CCs
        for (int i = 0; i < mc.ccCount(); ++i) {
            const auto& cc = mc.ccEvent(i);
            if (cc.beat >= scanStart && cc.beat < scanEnd) {
                double beatInBuffer = (cc.beat - clipLocalStart);
                int frameOff = static_cast<int>(beatInBuffer * samplesPerBeat);
                frameOff = std::clamp(frameOff, 0, numFrames - 1);

                // Convert 32-bit value to 7-bit CC
                uint8_t val7 = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(cc.value >> 25)));
                midiBuffer.addMessage(
                    midi::MidiMessage::cc(cc.channel, static_cast<uint8_t>(cc.ccNumber),
                                          val7, frameOff));
            }
        }
    }
}

} // namespace audio
} // namespace yawn
