#include "audio/ClipEngine.h"
#include <cmath>
#include <algorithm>

namespace yawn {
namespace audio {

void ClipEngine::scheduleClip(int trackIndex, const Clip* clip) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;

    if (m_quantizeMode == QuantizeMode::None) {
        // Launch immediately
        auto& state = m_tracks[trackIndex];
        state.clip = clip;
        state.playPosition = clip ? clip->loopStart : 0;
        state.fractionalPosition = static_cast<double>(state.playPosition);
        state.active = (clip != nullptr);
        state.stopping = false;
        state.fadeGain = 0.0f; // fade in
    } else {
        // Queue for quantized launch
        m_pending[trackIndex].trackIndex = trackIndex;
        m_pending[trackIndex].clip = clip;
        m_pending[trackIndex].valid = true;
    }
}

void ClipEngine::scheduleStop(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= kMaxTracks) return;

    if (m_quantizeMode == QuantizeMode::None) {
        m_tracks[trackIndex].stopping = true;
    } else {
        m_pending[trackIndex].trackIndex = trackIndex;
        m_pending[trackIndex].clip = nullptr;
        m_pending[trackIndex].valid = true;
    }
}

void ClipEngine::process(float* output, int numFrames, int numChannels) {
    // Check if any pending launches should trigger at a quantize boundary
    checkPendingLaunches();

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

void ClipEngine::checkPendingLaunches() {
    if (!m_transport) return;

    int64_t pos = m_transport->positionInSamples();

    // Determine quantize interval in samples
    double interval = 0.0;
    switch (m_quantizeMode) {
        case QuantizeMode::None:
            return; // shouldn't get here, but just in case
        case QuantizeMode::NextBeat:
            interval = m_transport->samplesPerBeat();
            break;
        case QuantizeMode::NextBar:
            interval = m_transport->samplesPerBar();
            break;
    }

    if (interval <= 0.0) return;

    // Check if we've crossed a quantize boundary since last check
    int64_t iInterval = static_cast<int64_t>(interval);
    if (iInterval <= 0) return;

    int64_t currentBoundary = (pos / iInterval) * iInterval;
    bool atBoundary = (m_lastQuantizeCheck < 0) ||
                      ((currentBoundary / iInterval) != (m_lastQuantizeCheck / iInterval));

    m_lastQuantizeCheck = pos;

    if (!atBoundary && m_transport->isPlaying()) return;

    // If not playing, allow immediate launch
    if (!m_transport->isPlaying()) {
        atBoundary = true;
    }

    // Fire all pending launches
    for (int t = 0; t < kMaxTracks; ++t) {
        if (!m_pending[t].valid) continue;

        auto& state = m_tracks[t];
        const Clip* clip = m_pending[t].clip;

        if (clip) {
            state.clip = clip;
            state.playPosition = clip->loopStart;
            state.fractionalPosition = static_cast<double>(clip->loopStart);
            state.active = true;
            state.stopping = false;
            state.fadeGain = 0.0f; // fade in
        } else {
            state.stopping = true;
        }

        m_pending[t].valid = false;
    }
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
    bool warping = (speedRatio != 1.0);

    // For Repitch mode, speedRatio changes both speed and pitch (simple resampling)
    // For Beats/Tones/Texture/Auto, we'd use TimeStretcher (preserves pitch).
    // Currently implementing Repitch-style variable-rate playback with interpolation.
    // TimeStretcher integration for pitch-preserving modes is a future enhancement.

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

} // namespace audio
} // namespace yawn
