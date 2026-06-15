#include "audio/ArrangementPlayback.h"
#include <algorithm>
#include <cmath>

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

        // Loop region = trim-in (offsetBeats) → source end. When looping,
        // a play position past the end wraps back to loopStart; otherwise
        // it goes silent (the historical behaviour).
        int64_t loopStart = static_cast<int64_t>(clip.offsetBeats * samplesPerBeat);
        loopStart = std::clamp<int64_t>(loopStart, 0,
                                        std::max<int64_t>(0, totalFrames - 1));
        const int64_t loopLen = totalFrames - loopStart;
        auto wrapPos = [&](int64_t pos) -> int64_t {
            if (!clip.loop || loopLen <= 0 || pos < totalFrames) return pos;
            return loopStart + (pos - loopStart) % loopLen;
        };

        // If we've moved to a new clip, compute the audio position
        if (clipIdx != state.currentClipIdx) {
            state.currentClipIdx = clipIdx;
            state.fadeGain = 0.0f; // fade in

            // Compute frame position from beat offset
            double beatIntoClip = beat - clip.startBeat + clip.offsetBeats;
            double framesFromStart = beatIntoClip * samplesPerBeat;
            state.audioPlayPos = static_cast<int64_t>(framesFromStart);
            if (state.audioPlayPos < 0) state.audioPlayPos = 0;
            state.audioPlayPos = wrapPos(state.audioPlayPos);
            if (state.audioPlayPos >= totalFrames) continue;
        }

        // Bounds check
        if (state.audioPlayPos < 0) { state.audioPlayPos++; continue; }
        if (state.audioPlayPos >= totalFrames) {
            state.audioPlayPos = wrapPos(state.audioPlayPos);
            if (state.audioPlayPos >= totalFrames) { state.audioPlayPos++; continue; }
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
        state.audioPlayPos = wrapPos(state.audioPlayPos);  // loop back at the end
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
        const double contentEnd = mc.lengthBeats();          // loop region end
        const bool looping = clip.loop &&
                             (contentEnd - clip.offsetBeats) > 1e-6;

        // Emit every event whose content beat lands in [scanStart, scanEnd)
        // for one playthrough whose content sits at the timeline implied by
        // `localStart`. Two-pass (offs before ons) so back-to-back same-pitch
        // notes re-articulate cleanly on same-frame collisions.
        // `bufEndContent` is the content beat at the buffer's end for this
        // playthrough; when scanEnd falls short of it a hard boundary (loop
        // end OR clip end) cut the window, so any note still sounding across
        // that boundary is force-released there — otherwise a note straddling
        // the clip end gets a note-on with no note-off and hangs past the clip.
        // `base`/`scale` map a content beat cb → its timeline beat:
        //   timeline = base + (cb - offsetBeats) * scale
        // scale = 1 for loop/one-shot; scale = slotLen/contentLen for stretch.
        auto emitWindow = [&](double base, double scale, double scanStart,
                              double scanEnd, double bufEndContent) {
            if (scanStart >= scanEnd) return;
            const bool hardEnd = scanEnd < bufEndContent - 1e-9;
            auto frameAt = [&](double cb) {
                const double tl = base + (cb - clip.offsetBeats) * scale;
                return std::clamp(
                    static_cast<int>((tl - currentBeat) * samplesPerBeat),
                    0, numFrames - 1);
            };

            // --- Pass 1: Note-Offs ---
            for (int i = 0; i < mc.noteCount(); ++i) {
                const auto& note = mc.note(i);
                double effEnd = note.startBeat + note.duration;
                if (looping && effEnd > contentEnd) effEnd = contentEnd;
                double offBeat;
                if (effEnd >= scanStart && effEnd < scanEnd) {
                    offBeat = effEnd;                  // natural end inside the window
                } else if (hardEnd && note.startBeat < scanEnd && effEnd >= scanEnd) {
                    offBeat = scanEnd;                 // sounding across a hard boundary → cut
                } else {
                    continue;
                }
                midiBuffer.addMessage(
                    midi::MidiMessage::noteOff(note.channel, note.pitch, 0, frameAt(offBeat)));
            }

            // --- Pass 2: Note-Ons ---
            for (int i = 0; i < mc.noteCount(); ++i) {
                const auto& note = mc.note(i);
                if (note.startBeat < scanStart || note.startBeat >= scanEnd) continue;
                uint8_t vel7 = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(note.velocity >> 9)));
                if (vel7 == 0) vel7 = 1;
                midiBuffer.addMessage(
                    midi::MidiMessage::noteOn(note.channel, note.pitch, vel7,
                                              frameAt(note.startBeat)));
            }

            // --- CCs ---
            for (int i = 0; i < mc.ccCount(); ++i) {
                const auto& cc = mc.ccEvent(i);
                if (cc.beat < scanStart || cc.beat >= scanEnd) continue;
                uint8_t val7 = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(cc.value >> 25)));
                midiBuffer.addMessage(
                    midi::MidiMessage::cc(cc.channel, static_cast<uint8_t>(cc.ccNumber),
                                          val7, frameAt(cc.beat)));
            }
        };

        if (clip.stretch && (contentEnd - clip.offsetBeats) > 1e-6) {
            // Stretch: map the whole content [offsetBeats, contentEnd) onto the
            // slot. scale = slot/content (>1 slows the notes down, <1 speeds up);
            // one playthrough, no loop.
            const double contentLen = contentEnd - clip.offsetBeats;
            const double scale = clip.lengthBeats / contentLen;
            const double invScale = (scale > 1e-9) ? 1.0 / scale : 0.0;
            const double bufEndContent =
                clip.offsetBeats + (bufEndBeat - clip.startBeat) * invScale;
            const double scanStart = std::max(clip.offsetBeats,
                clip.offsetBeats + (currentBeat - clip.startBeat) * invScale);
            const double scanEnd = std::min(bufEndContent, contentEnd);
            emitWindow(clip.startBeat, scale, scanStart, scanEnd, bufEndContent);
        } else if (looping) {
            // Tile the loop region [offsetBeats, contentEnd) across the slot;
            // step through whichever iterations this buffer touches (almost
            // always one — buffers are a tiny fraction of a beat).
            const double loopLen = contentEnd - clip.offsetBeats;
            const double relStart = currentBeat - clip.startBeat;
            int k = static_cast<int>(std::floor(std::max(0.0, relStart) / loopLen));
            for (int guard = 0; guard < 256; ++guard, ++k) {
                const double vStart = clip.startBeat + static_cast<double>(k) * loopLen;
                if (vStart >= bufEndBeat || vStart >= clip.endBeat()) break;
                const double localStart = currentBeat - vStart + clip.offsetBeats;
                const double bufEndContent = bufEndBeat - vStart + clip.offsetBeats;
                const double scanStart  = std::max(localStart, clip.offsetBeats);
                const double scanEnd    = std::min({
                    bufEndContent,
                    contentEnd,
                    clip.endBeat() - vStart + clip.offsetBeats });
                emitWindow(vStart, 1.0, scanStart, scanEnd, bufEndContent);
            }
        } else {
            // One-shot: play the content once, silent past its end.
            const double clipLocalStart = currentBeat - clip.startBeat + clip.offsetBeats;
            const double clipLocalEnd   = bufEndBeat - clip.startBeat + clip.offsetBeats;
            const double scanStart = std::max(clipLocalStart, clip.offsetBeats);
            const double scanEnd   = std::min(clipLocalEnd,
                                              clip.offsetBeats + clip.lengthBeats);
            emitWindow(clip.startBeat, 1.0, scanStart, scanEnd, clipLocalEnd);
        }
    }
}

} // namespace audio
} // namespace yawn
