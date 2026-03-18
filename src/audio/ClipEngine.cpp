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
    int64_t loopStart = clip.loopStart;
    int64_t loopEnd = clip.effectiveLoopEnd();

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

        // Read sample from buffer
        if (state.playPosition >= loopEnd) {
            if (clip.looping) {
                state.playPosition = loopStart;
            } else {
                state.active = false;
                return;
            }
        }

        int frame = static_cast<int>(state.playPosition);

        // Mix into output (interleaved)
        for (int ch = 0; ch < numChannels; ++ch) {
            int srcCh = (ch < bufChannels) ? ch : (bufChannels - 1);
            float sample = buffer.sample(srcCh, frame) * effectiveGain;
            output[i * numChannels + ch] += sample;
        }

        state.playPosition++;
    }
}

} // namespace audio
} // namespace yawn
