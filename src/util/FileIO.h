#pragma once

#include "audio/AudioBuffer.h"
#include <string>
#include <memory>

namespace yawn {
namespace util {

struct AudioFileInfo {
    int sampleRate = 0;
    int channels = 0;
    int64_t frames = 0;
    std::string format;
};

// Load an audio file (WAV, FLAC, OGG, AIFF) into an AudioBuffer.
// Returns nullptr on failure. Audio is stored non-interleaved.
std::shared_ptr<audio::AudioBuffer> loadAudioFile(
    const std::string& path,
    AudioFileInfo* outInfo = nullptr
);

// Resample a buffer from srcRate to dstRate (simple linear interpolation).
// Used when loaded file sample rate doesn't match engine sample rate.
std::shared_ptr<audio::AudioBuffer> resampleBuffer(
    const audio::AudioBuffer& src,
    double srcRate,
    double dstRate
);

} // namespace util
} // namespace yawn
