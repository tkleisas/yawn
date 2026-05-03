#include "audio/ClipEngine.h"
#include "util/Logger.h"
#include <cmath>
#include <algorithm>

namespace yawn {
namespace audio {

namespace {
const char* warpModeName(WarpMode m) {
    switch (m) {
        case WarpMode::Off:        return "Off";
        case WarpMode::Auto:       return "Auto";
        case WarpMode::Beats:      return "Beats";
        case WarpMode::Tones:      return "Tones (classic PV)";
        case WarpMode::Texture:    return "Texture (classic PV)";
        case WarpMode::TonesPGHI:  return "Tones (PGHI)";
        case WarpMode::Repitch:    return "Repitch";
    }
    return "?";
}
} // anon

void ClipEngine::scheduleClip(int trackIndex, int sceneIndex, const Clip* clip, QuantizeMode quantize,
                              const std::vector<automation::AutomationLane>* clipAutomation,
                              const FollowAction& followAction) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;

    if (quantize == QuantizeMode::None) {
        auto& state = m_tracks[trackIndex];
        state.clip = clip;
        state.playPosition = clip ? clip->loopStart : 0;
        state.fractionalPosition = static_cast<double>(state.playPosition);
        state.active = (clip != nullptr);
        state.stopping = false;
        state.fadeGain = 0.0f;
        state.sceneIndex = sceneIndex;
        state.clipAutomation = clipAutomation;
        state.followAction = followAction;
        state.barsPlayed = 0;
        state.barStartSample = m_transport ? m_transport->positionInSamples() : 0;

        // Initialize stretcher for pitch-preserving modes
        if (clip && clip->warpMode != WarpMode::Off && clip->warpMode != WarpMode::Repitch) {
            int bufCh = clip->buffer ? clip->buffer->numChannels() : 1;
            m_stretchers[trackIndex].init(m_sampleRate, 4096, clip->warpMode, bufCh);
        } else {
            m_stretchers[trackIndex].initialized = false;
        }
    } else {
        m_pending[trackIndex].trackIndex = trackIndex;
        m_pending[trackIndex].sceneIndex = sceneIndex;
        m_pending[trackIndex].clip = clip;
        m_pending[trackIndex].quantizeMode = quantize;
        m_pending[trackIndex].valid = true;
        m_pending[trackIndex].clipAutomation = clipAutomation;
        m_pending[trackIndex].followAction = followAction;
    }
}

void ClipEngine::scheduleStop(int trackIndex, QuantizeMode quantize) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;

    if (quantize == QuantizeMode::None) {
        m_tracks[trackIndex].stopping = true;
    } else {
        m_pending[trackIndex].trackIndex = trackIndex;
        m_pending[trackIndex].clip = nullptr;
        m_pending[trackIndex].quantizeMode = quantize;
        m_pending[trackIndex].valid = true;
    }
}

void ClipEngine::process(float* output, int numFrames, int numChannels) {
    // Check if any pending launches should trigger at a quantize boundary
    checkPendingLaunches();

    // Check follow actions (bar counting)
    checkFollowActions();

    // Process each active track
    for (int t = 0; t < kMaxTracks; ++t) {
        if (m_tracks[t].active) {
            processTrack(t, output, numFrames, numChannels);
        }
    }
}

void ClipEngine::processTrackToBuffer(int trackIndex, float* buffer, int numFrames, int numChannels) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;
    if (m_tracks[trackIndex].active) {
        processTrack(trackIndex, buffer, numFrames, numChannels);
    }
}

void ClipEngine::checkAndFirePending() {
    checkPendingLaunches();
}

bool ClipEngine::isTrackPlaying(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return false;
    return m_tracks[trackIndex].active;
}

void ClipEngine::checkFollowActions() {
    if (!m_transport || !m_transport->isPlaying()) return;

    int64_t pos = m_transport->positionInSamples();
    double samplesPerBar = m_transport->samplesPerBar();
    if (samplesPerBar <= 0.0) return;

    for (int t = 0; t < kMaxTracks; ++t) {
        auto& state = m_tracks[t];
        if (!state.active || state.stopping) continue;
        if (!state.followAction.enabled) continue;
        // Don't fire if there's already a pending launch for this track
        if (m_pending[t].valid) continue;

        // Count bars elapsed since clip started
        int64_t elapsed = pos - state.barStartSample;
        int64_t currentBar = static_cast<int64_t>(elapsed / samplesPerBar);
        if (currentBar > state.barsPlayed) {
            state.barsPlayed = currentBar;

            // Check if we've reached the target bar count
            if (state.barsPlayed >= state.followAction.barCount) {
                // Resolve A vs B by probability
                int roll = static_cast<int>(m_rng() % 100);
                FollowActionType action = (roll < state.followAction.chanceA)
                    ? state.followAction.actionA
                    : state.followAction.actionB;

                if (action == FollowActionType::None) {
                    // Reset counter and loop again
                    state.barsPlayed = 0;
                    state.barStartSample = pos;
                    continue;
                }

                if (action == FollowActionType::Stop) {
                    state.stopping = true;
                    continue;
                }

                if (action == FollowActionType::PlayAgain) {
                    // Restart this clip from the beginning
                    if (state.clip) {
                        state.playPosition = state.clip->loopStart;
                        state.fractionalPosition = static_cast<double>(state.clip->loopStart);
                    }
                    state.barsPlayed = 0;
                    state.barStartSample = pos;
                    continue;
                }

                // For navigation actions, notify the UI to resolve the target
                if (m_followActionCb) {
                    m_followActionCb(t, state.sceneIndex, action);
                }
                // Reset bar counter — the UI will schedule the next clip
                state.barsPlayed = 0;
                state.barStartSample = pos;
            }
        }
    }
}

void ClipEngine::checkPendingLaunches() {
    if (!m_transport) return;

    int64_t pos = m_transport->positionInSamples();
    double spb = m_transport->samplesPerBar();
    double spBeat = m_transport->samplesPerBeat();

    bool anyPending = false;
    for (int t = 0; t < kMaxTracks; ++t) {
        if (m_pending[t].valid) { anyPending = true; break; }
    }
    if (!anyPending) {
        m_lastQuantizeCheck = pos;
        return;
    }

    for (int t = 0; t < kMaxTracks; ++t) {
        if (!m_pending[t].valid) continue;

        QuantizeMode qm = m_pending[t].quantizeMode;
        if (qm == QuantizeMode::None) {
            // Fire immediately
        } else {
            double interval = (qm == QuantizeMode::NextBar) ? spb : spBeat;
            if (interval <= 0.0) continue;
            int64_t iInterval = static_cast<int64_t>(interval);
            if (iInterval <= 0) continue;

            int64_t currentBoundary = (pos / iInterval) * iInterval;
            bool atBoundary = (m_lastQuantizeCheck < 0) ||
                ((currentBoundary / iInterval) != (m_lastQuantizeCheck / iInterval));

            if (!atBoundary && m_transport->isPlaying()) continue;
            if (!m_transport->isPlaying()) atBoundary = true;
        }

        auto& state = m_tracks[t];
        const Clip* clip = m_pending[t].clip;

        if (clip) {
            state.clip = clip;
            state.playPosition = clip->loopStart;
            state.fractionalPosition = static_cast<double>(clip->loopStart);
            state.active = true;
            state.stopping = false;
            state.fadeGain = 0.0f;
            state.sceneIndex = m_pending[t].sceneIndex;
            state.clipAutomation = m_pending[t].clipAutomation;
            state.followAction = m_pending[t].followAction;
            state.barsPlayed = 0;
            state.barStartSample = m_transport ? m_transport->positionInSamples() : 0;

            // Initialize stretcher for pitch-preserving modes
            if (clip->warpMode != WarpMode::Off && clip->warpMode != WarpMode::Repitch) {
                int bufCh = clip->buffer ? clip->buffer->numChannels() : 1;
                m_stretchers[t].init(m_sampleRate, 4096, clip->warpMode, bufCh);
            } else {
                m_stretchers[t].initialized = false;
            }
        } else {
            state.stopping = true;
        }

        m_pending[t].valid = false;
    }

    m_lastQuantizeCheck = pos;
}

void ClipEngine::processTrack(int trackIndex, float* output, int numFrames, int numChannels) {
    auto& state = m_tracks[trackIndex];
    if (!state.clip || !state.clip->buffer) {
        state.active = false;
        return;
    }

    const auto& clip = *state.clip;
    const auto& buffer = *clip.buffer;
    int bufChannels = buffer.numChannels();
    int bufFrames = buffer.numFrames();
    int64_t loopStart = clip.loopStart;
    int64_t loopEnd = clip.effectiveLoopEnd();

    // Compute warp speed ratio (how fast we advance through the source)
    double projectBPM = m_transport ? m_transport->bpm() : 120.0;
    double speedRatio = clip.warpSpeedRatio(projectBPM);

    // Transpose: in Repitch/Off modes, transpose changes playback speed (and pitch).
    // In pitch-preserving modes (Beats/Tones/Texture/Auto), transpose is NOT applied
    // to speed — it would need a separate pitch-shifting step (not yet implemented).
    bool pitchPreservingMode = (clip.warpMode == WarpMode::Beats ||
                                clip.warpMode == WarpMode::Tones ||
                                clip.warpMode == WarpMode::Texture ||
                                clip.warpMode == WarpMode::TonesPGHI ||
                                clip.warpMode == WarpMode::Auto);

    if (!pitchPreservingMode && (clip.transposeSemitones != 0 || clip.detuneCents != 0)) {
        double semitones = clip.transposeSemitones + clip.detuneCents / 100.0;
        speedRatio *= std::pow(2.0, semitones / 12.0);
    }

    bool warping = (speedRatio != 1.0);
    bool hasTranspose = (clip.transposeSemitones != 0 || clip.detuneCents != 0);

    // Pitch-preserving modes: use TimeStretcher (Beats→WSOLA, Tones/Texture→PhaseVocoder)
    // Also enter this path when transpose/detune is set, even if the BPM ratio is 1.0 —
    // the stretcher provides post-stretch resampling for pitch shift.
    bool usePitchPreserving = pitchPreservingMode && (warping || hasTranspose);

    if (usePitchPreserving) {
        auto& stretcher = m_stretchers[trackIndex];
        // (Re-)initialize the stretcher whenever the mode the clip
        // wants differs from what's currently loaded. Old check
        // (`!stretcher.initialized && stretcher.activeMode != clip.warpMode`)
        // had a bug: once a stretcher was initialized at clip launch,
        // changing the warp mode mid-playback (e.g. Tones → TonesPGHI
        // via the dropdown) did NOTHING because the !initialized
        // guard short-circuited. Result: switching algorithms in the
        // UI sounded identical because the engine kept running the
        // first-loaded algo forever.
        //
        // The "too short fallback" at processTrackStretched also sets
        // initialized=false, but it doesn't change activeMode — so
        // this condition stays false and we don't re-init in that
        // case (the safeguard the old comment was trying to preserve).
        if (stretcher.activeMode != clip.warpMode) {
            LOG_INFO("Stretch", "Track %d: warp mode → %s (speedRatio=%.3f, originalBPM=%.1f, projectBPM=%.1f, transpose=%d)",
                     trackIndex, warpModeName(clip.warpMode),
                     speedRatio, clip.originalBPM, projectBPM,
                     clip.transposeSemitones);
            stretcher.init(m_sampleRate, 4096, clip.warpMode, bufChannels);
        }
        if (stretcher.initialized) {
            processTrackStretched(trackIndex, output, numFrames, numChannels);
            return;
        }
    } else if (pitchPreservingMode) {
        // Diagnostic — fires once when the stretcher would be useful
        // but isn't engaged because warping/transpose are both
        // trivial. Log once per (track, mode) so the line doesn't
        // spam every audio block.
        auto& stretcher = m_stretchers[trackIndex];
        if (stretcher.activeMode != clip.warpMode) {
            stretcher.activeMode = clip.warpMode;  // mark as "noted"
            LOG_INFO("Stretch",
                "Track %d: %s requested but stretcher BYPASSED — "
                "speedRatio=1.0 and no transpose, so the simple "
                "resampler runs instead. Set the clip's originalBPM "
                "to a value different from the project BPM, or apply "
                "transpose/detune, to engage the algorithm.",
                trackIndex, warpModeName(clip.warpMode));
        }
    }

    // Repitch mode or no warping: variable-rate playback with linear interpolation
    if (warping) {
        // Fractional position playback with linear interpolation
        double pos = state.fractionalPosition;
        if (pos < loopStart) pos = static_cast<double>(loopStart);

        for (int i = 0; i < numFrames; ++i) {
            // Handle fade-in / fade-out
            if (state.stopping) {
                state.fadeGain -= ClipPlayState::kFadeIncrement;
                if (state.fadeGain <= 0.0f) {
                    state.fadeGain = 0.0f;
                    state.active = false;
                    state.stopping = false;
                    return;
                }
            } else if (state.fadeGain < 1.0f) {
                state.fadeGain += ClipPlayState::kFadeIncrement;
                if (state.fadeGain > 1.0f) state.fadeGain = 1.0f;
            }

            float effectiveGain = clip.gain * state.fadeGain;

            // Check loop boundary
            if (pos >= loopEnd) {
                if (clip.looping) {
                    pos = static_cast<double>(loopStart) + std::fmod(pos - loopEnd, loopEnd - loopStart);
                } else {
                    state.active = false;
                    return;
                }
            }

            // Linear interpolation between adjacent samples
            int frame0 = static_cast<int>(pos);
            int frame1 = frame0 + 1;
            float frac = static_cast<float>(pos - frame0);

            if (frame0 < 0) frame0 = 0;
            if (frame1 >= bufFrames) frame1 = bufFrames - 1;

            for (int ch = 0; ch < numChannels; ++ch) {
                int srcCh = (ch < bufChannels) ? ch : (bufChannels - 1);
                float s0 = buffer.sample(srcCh, frame0);
                float s1 = buffer.sample(srcCh, frame1);
                float sample = (s0 + (s1 - s0) * frac) * effectiveGain;
                output[i * numChannels + ch] += sample;
            }

            pos += speedRatio;
        }

        state.fractionalPosition = pos;
        state.playPosition = static_cast<int64_t>(pos);
    } else {
        // Original non-warped playback (integer position, no interpolation needed)
        for (int i = 0; i < numFrames; ++i) {
            if (state.stopping) {
                state.fadeGain -= ClipPlayState::kFadeIncrement;
                if (state.fadeGain <= 0.0f) {
                    state.fadeGain = 0.0f;
                    state.active = false;
                    state.stopping = false;
                    return;
                }
            } else if (state.fadeGain < 1.0f) {
                state.fadeGain += ClipPlayState::kFadeIncrement;
                if (state.fadeGain > 1.0f) state.fadeGain = 1.0f;
            }

            float effectiveGain = clip.gain * state.fadeGain;

            if (state.playPosition >= loopEnd) {
                if (clip.looping) {
                    state.playPosition = loopStart;
                } else {
                    state.active = false;
                    return;
                }
            }

            int frame = static_cast<int>(state.playPosition);

            for (int ch = 0; ch < numChannels; ++ch) {
                int srcCh = (ch < bufChannels) ? ch : (bufChannels - 1);
                float sample = buffer.sample(srcCh, frame) * effectiveGain;
                output[i * numChannels + ch] += sample;
            }

            state.playPosition++;
        }
        state.fractionalPosition = static_cast<double>(state.playPosition);
    }
}

void ClipEngine::processTrackStretched(int trackIndex, float* output, int numFrames, int numChannels) {
    auto& state = m_tracks[trackIndex];
    auto& stretcher = m_stretchers[trackIndex];
    const auto& clip = *state.clip;
    const auto& buffer = *clip.buffer;
    int bufChannels = buffer.numChannels();
    int bufFrames = buffer.numFrames();
    int64_t loopStart = clip.loopStart;
    int64_t loopEnd = clip.effectiveLoopEnd();

    double projectBPM = m_transport ? m_transport->bpm() : 120.0;
    double globalSpeed = clip.warpSpeedRatio(projectBPM);

    // Compute pitch ratio for transpose (applied as post-stretch resampling)
    double pitchRatio = 1.0;
    if (clip.transposeSemitones != 0 || clip.detuneCents != 0) {
        double semitones = clip.transposeSemitones + clip.detuneCents / 100.0;
        pitchRatio = std::pow(2.0, semitones / 12.0);
    }

    // Adjust stretcher speed: compensate for resampling so duration stays correct.
    // Resampling by pitchRatio consumes pitchRatio stretcher frames per output frame,
    // so the stretcher must produce pitchRatio times as many frames → speed / pitchRatio.
    double adjustedSpeed = globalSpeed / pitchRatio;
    stretcher.setSpeedRatio(adjustedSpeed);

    int stretcherCh = stretcher.numChannels;
    double pos = state.fractionalPosition;
    if (pos < loopStart) pos = static_cast<double>(loopStart);
    int inputStart = static_cast<int>(pos);

    int availableFromPos = static_cast<int>(loopEnd) - inputStart;
    if (availableFromPos <= 0 && !clip.looping) {
        state.active = false;
        return;
    }

    // If clip is too short for the stretcher, fall back to Repitch
    int clipLength = static_cast<int>(loopEnd - loopStart);
    int minStretcherInput = 2048;
    if (clipLength < minStretcherInput && !clip.looping) {
        stretcher.initialized = false;
        processTrack(trackIndex, output, numFrames, numChannels);
        return;
    }

    float monoIn[4096];

    // For each channel, ensure we have enough output buffered.
    // With pitch shift, we consume pitchRatio stretcher frames per output frame.
    int neededStretcherFrames = static_cast<int>(std::ceil(numFrames * pitchRatio)) + 2;

    // Save position before channel loop — all channels must read from the same clip position
    double inputPos = pos;

    for (int ch = 0; ch < numChannels; ++ch) {
        int srcCh = (ch < bufChannels) ? ch : (bufChannels - 1);
        int strCh = (ch < stretcherCh) ? ch : (stretcherCh - 1);
        auto& str = stretcher.stretchers[strCh];

        // Each channel starts from the same position
        double chPos = inputPos;

        // Produce enough output in the intermediate buffer
        while (stretcher.outBufAvail[ch] - static_cast<int>(stretcher.resamplePos[ch]) < neededStretcherFrames) {
            // Gather input from clip at current position
            int gatherStart = static_cast<int>(chPos);
            int gathered = 0;
            int gatherPos = gatherStart;
            int maxGather = 4096;

            while (gathered < maxGather) {
                if (gatherPos >= static_cast<int>(loopEnd)) {
                    if (clip.looping)
                        gatherPos = static_cast<int>(loopStart);
                    else
                        break;
                }
                int avail = static_cast<int>(loopEnd) - gatherPos;
                int toCopy = std::min(avail, maxGather - gathered);
                for (int s = 0; s < toCopy; ++s) {
                    int frame = gatherPos + s;
                    monoIn[gathered + s] = (frame < bufFrames) ? buffer.sample(srcCh, frame) : 0.0f;
                }
                gathered += toCopy;
                gatherPos += toCopy;
            }

            if (gathered == 0) break; // no more input

            // Reset input position so stretcher reads from start of monoIn
            str.resetInputPosition();

            // Zero the output region for overlap-add (phase vocoder uses +=)
            int outSpace = TrackStretcher::kOutBufSize - stretcher.outBufAvail[ch];
            if (outSpace <= 0) break;
            float* outPtr = stretcher.outBuf[ch].data() + stretcher.outBufAvail[ch];
            std::memset(outPtr, 0, outSpace * sizeof(float));

            int consumed = 0;
            int produced = str.process(monoIn, gathered, outPtr, outSpace, consumed);
            stretcher.outBufAvail[ch] += produced;

            // Advance this channel's position
            if (consumed > 0) {
                chPos += consumed;
                while (chPos >= loopEnd && clip.looping)
                    chPos -= (loopEnd - loopStart);
            }

            if (produced == 0) break; // stretcher can't produce more
        }

        // Track the shared position from channel 0
        if (ch == 0) pos = chPos;
    }

    // Read from intermediate buffers with pitch-shift resampling
    for (int i = 0; i < numFrames; ++i) {
        if (state.stopping) {
            state.fadeGain -= ClipPlayState::kFadeIncrement;
            if (state.fadeGain <= 0.0f) {
                state.fadeGain = 0.0f;
                state.active = false;
                state.stopping = false;
                return;
            }
        } else if (state.fadeGain < 1.0f) {
            state.fadeGain += ClipPlayState::kFadeIncrement;
            if (state.fadeGain > 1.0f) state.fadeGain = 1.0f;
        }

        float gain = clip.gain * state.fadeGain;

        for (int ch = 0; ch < numChannels; ++ch) {
            float sample = 0.0f;
            double rp = stretcher.resamplePos[ch];
            int idx0 = static_cast<int>(rp);
            float frac = static_cast<float>(rp - idx0);
            if (idx0 + 1 < stretcher.outBufAvail[ch]) {
                sample = stretcher.outBuf[ch][idx0] * (1.0f - frac)
                       + stretcher.outBuf[ch][idx0 + 1] * frac;
            } else if (idx0 < stretcher.outBufAvail[ch]) {
                sample = stretcher.outBuf[ch][idx0];
            }
            stretcher.resamplePos[ch] += pitchRatio;
            output[i * numChannels + ch] += sample * gain;
        }
    }

    // Compact the intermediate buffers (shift consumed data out)
    for (int ch = 0; ch < numChannels; ++ch) {
        int consumed = static_cast<int>(stretcher.resamplePos[ch]);
        int remaining = stretcher.outBufAvail[ch] - consumed;
        if (remaining > 0 && consumed > 0) {
            std::memmove(stretcher.outBuf[ch].data(),
                         stretcher.outBuf[ch].data() + consumed,
                         remaining * sizeof(float));
        }
        stretcher.outBufAvail[ch] = std::max(0, remaining);
        // Zero the freed region to prevent stale data in overlap-add
        if (remaining > 0 && remaining < TrackStretcher::kOutBufSize) {
            std::memset(stretcher.outBuf[ch].data() + remaining, 0,
                        (TrackStretcher::kOutBufSize - remaining) * sizeof(float));
        }
        stretcher.resamplePos[ch] -= consumed;  // keep fractional part
        if (stretcher.resamplePos[ch] < 0.0) stretcher.resamplePos[ch] = 0.0;
    }

    state.fractionalPosition = pos;
    state.playPosition = static_cast<int64_t>(pos);

    // If stretcher produced no output and we've passed the end, stop
    if (!clip.looping && pos >= loopEnd) {
        state.active = false;
    }
}

double ClipEngine::computeLocalSpeedRatio(const Clip& clip, double samplePos, double projectBPM) const {
    // If no warp markers, use global BPM ratio
    if (clip.warpMarkers.size() < 2)
        return clip.warpSpeedRatio(projectBPM);

    // Find the two warp markers surrounding the current position
    const auto& markers = clip.warpMarkers;
    size_t idx = 0;
    for (size_t i = 0; i + 1 < markers.size(); ++i) {
        if (samplePos >= markers[i].samplePosition &&
            samplePos < markers[i + 1].samplePosition) {
            idx = i;
            break;
        }
        if (i + 2 == markers.size()) idx = i; // clamp to last segment
    }

    // Compute local speed ratio between these two markers
    double srcSamples = static_cast<double>(markers[idx + 1].samplePosition - markers[idx].samplePosition);
    double beatSpan = markers[idx + 1].beatPosition - markers[idx].beatPosition;
    if (beatSpan <= 0.0 || srcSamples <= 0.0) return 1.0;

    // How many output samples this beat span occupies at project BPM
    double secondsPerBeat = 60.0 / projectBPM;
    double dstSamples = beatSpan * secondsPerBeat * m_sampleRate;

    return srcSamples / dstSamples;
}

} // namespace audio
} // namespace yawn
