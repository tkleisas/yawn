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

enum class ExportFormat { WAV, FLAC, OGG };
enum class BitDepth { Int16, Int24, Float32 };

// Load an audio file (WAV, FLAC, OGG, AIFF, MP3) into an AudioBuffer.
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

// Save interleaved audio data to a file with specified format and bit depth.
bool saveAudioFile(const std::string& path,
                   const float* interleavedData, int numFrames, int numChannels,
                   int sampleRate,
                   ExportFormat format = ExportFormat::WAV,
                   BitDepth bitDepth = BitDepth::Float32);

// Save a non-interleaved AudioBuffer to a file with specified format and bit depth.
bool saveAudioBuffer(const std::string& path,
                     const audio::AudioBuffer& buffer,
                     int sampleRate,
                     ExportFormat format = ExportFormat::WAV,
                     BitDepth bitDepth = BitDepth::Float32);

// Get file extension for a given format.
const char* formatExtension(ExportFormat format);

} // namespace util
} // namespace yawn
