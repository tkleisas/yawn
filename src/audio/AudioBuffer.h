#pragma once

#include <vector>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <cassert>

namespace yawn {
namespace audio {

// Interleaved or non-interleaved audio buffer.
// Owns its memory. Used for storing audio data on the UI/file-loading side.
class AudioBuffer {
public:
    AudioBuffer() = default;

    AudioBuffer(int numChannels, int numFrames)
        : m_numChannels(numChannels)
        , m_numFrames(numFrames)
        , m_data(static_cast<size_t>(numChannels) * numFrames, 0.0f)
    {}

    void resize(int numChannels, int numFrames) {
        m_numChannels = numChannels;
        m_numFrames = numFrames;
        m_data.resize(static_cast<size_t>(numChannels) * numFrames, 0.0f);
    }

    void clear() {
        std::fill(m_data.begin(), m_data.end(), 0.0f);
    }

    // Access sample at [channel][frame] — non-interleaved layout
    float& sample(int channel, int frame) {
        assert(channel >= 0 && channel < m_numChannels);
        assert(frame >= 0 && frame < m_numFrames);
        return m_data[static_cast<size_t>(channel) * m_numFrames + frame];
    }

    float sample(int channel, int frame) const {
        assert(channel >= 0 && channel < m_numChannels);
        assert(frame >= 0 && frame < m_numFrames);
        return m_data[static_cast<size_t>(channel) * m_numFrames + frame];
    }

    // Pointer to channel data (contiguous frames for one channel)
    float* channelData(int channel) {
        assert(channel >= 0 && channel < m_numChannels);
        return m_data.data() + static_cast<size_t>(channel) * m_numFrames;
    }

    const float* channelData(int channel) const {
        assert(channel >= 0 && channel < m_numChannels);
        return m_data.data() + static_cast<size_t>(channel) * m_numFrames;
    }

    float* data() { return m_data.data(); }
    const float* data() const { return m_data.data(); }

    int numChannels() const { return m_numChannels; }
    int numFrames() const { return m_numFrames; }
    size_t totalSamples() const { return m_data.size(); }
    bool isEmpty() const { return m_data.empty(); }

private:
    int m_numChannels = 0;
    int m_numFrames = 0;
    std::vector<float> m_data;
};

} // namespace audio
} // namespace yawn
