#include "util/FileIO.h"
#include "util/Logger.h"
#include <sndfile.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cctype>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

namespace yawn {
namespace util {

// Helper: case-insensitive extension check
static bool hasExtension(const std::string& path, const std::string& ext) {
    if (path.size() < ext.size()) return false;
    std::string tail = path.substr(path.size() - ext.size());
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return tail == ext;
}

// Load MP3 via minimp3
static std::shared_ptr<audio::AudioBuffer> loadMP3(
    const std::string& path, AudioFileInfo* outInfo)
{
    mp3dec_t mp3d;
    mp3dec_file_info_t info{};
    if (mp3dec_load(&mp3d, path.c_str(), &info, nullptr, nullptr)) {
        LOG_ERROR("File", "Failed to decode MP3 '%s'", path.c_str());
        return nullptr;
    }
    if (info.samples == 0 || info.channels == 0) {
        if (info.buffer) free(info.buffer);
        LOG_ERROR("File", "Empty MP3 '%s'", path.c_str());
        return nullptr;
    }

    int frames = static_cast<int>(info.samples / info.channels);
    auto buffer = std::make_shared<audio::AudioBuffer>(info.channels, frames);

    // minimp3 produces interleaved int16-range floats (actually mp3dec_f32)
    // mp3dec_load with mp3dec_file_info_t gives interleaved int16 samples
    // Convert interleaved int16 → non-interleaved float
    for (int ch = 0; ch < info.channels; ++ch) {
        float* dst = buffer->channelData(ch);
        for (int i = 0; i < frames; ++i) {
            dst[i] = info.buffer[i * info.channels + ch] / 32768.0f;
        }
    }

    if (outInfo) {
        outInfo->sampleRate = info.hz;
        outInfo->channels = info.channels;
        outInfo->frames = frames;
        outInfo->format = "MP3";
    }

    LOG_INFO("File", "Loaded MP3 '%s': %d frames, %d ch, %d Hz",
             path.c_str(), frames, info.channels, info.hz);

    free(info.buffer);
    return buffer;
}

std::shared_ptr<audio::AudioBuffer> loadAudioFile(
    const std::string& path,
    AudioFileInfo* outInfo)
{
    // Try MP3 path first for .mp3 files (libsndfile doesn't support MP3)
    if (hasExtension(path, ".mp3")) {
        return loadMP3(path, outInfo);
    }

    SF_INFO sfInfo;
    sfInfo.format = 0;

    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sfInfo);
    if (!file) {
        // Fallback: try MP3 decoder for unknown formats
        auto mp3Result = loadMP3(path, outInfo);
        if (mp3Result) return mp3Result;

        LOG_ERROR("File", "Failed to open audio file '%s': %s",
            path.c_str(), sf_strerror(nullptr));
        return nullptr;
    }

    if (sfInfo.frames <= 0 || sfInfo.channels <= 0) {
        LOG_ERROR("File", "Invalid audio file '%s': %lld frames, %d channels",
            path.c_str(), static_cast<long long>(sfInfo.frames), sfInfo.channels);
        sf_close(file);
        return nullptr;
    }

    // Read interleaved float data
    std::vector<float> interleaved(static_cast<size_t>(sfInfo.frames) * sfInfo.channels);
    sf_count_t framesRead = sf_readf_float(file, interleaved.data(), sfInfo.frames);
    sf_close(file);

    if (framesRead <= 0) {
        LOG_ERROR("File", "Failed to read audio data from '%s'", path.c_str());
        return nullptr;
    }

    // Convert interleaved → non-interleaved AudioBuffer
    auto buffer = std::make_shared<audio::AudioBuffer>(sfInfo.channels, static_cast<int>(framesRead));

    for (int ch = 0; ch < sfInfo.channels; ++ch) {
        float* dst = buffer->channelData(ch);
        for (int64_t i = 0; i < framesRead; ++i) {
            dst[i] = interleaved[static_cast<size_t>(i) * sfInfo.channels + ch];
        }
    }

    if (outInfo) {
        outInfo->sampleRate = sfInfo.samplerate;
        outInfo->channels = sfInfo.channels;
        outInfo->frames = framesRead;

        int majorFormat = sfInfo.format & SF_FORMAT_TYPEMASK;
        switch (majorFormat) {
            case SF_FORMAT_WAV:  outInfo->format = "WAV"; break;
            case SF_FORMAT_FLAC: outInfo->format = "FLAC"; break;
            case SF_FORMAT_OGG:  outInfo->format = "OGG"; break;
            case SF_FORMAT_AIFF: outInfo->format = "AIFF"; break;
            default:             outInfo->format = "Unknown"; break;
        }
    }

    LOG_INFO("File", "Loaded '%s': %lld frames, %d ch, %d Hz",
        path.c_str(), static_cast<long long>(framesRead),
        sfInfo.channels, sfInfo.samplerate);

    return buffer;
}

std::shared_ptr<audio::AudioBuffer> resampleBuffer(
    const audio::AudioBuffer& src,
    double srcRate,
    double dstRate)
{
    if (srcRate <= 0.0 || dstRate <= 0.0 || src.isEmpty()) return nullptr;

    double ratio = dstRate / srcRate;
    int newFrames = static_cast<int>(std::ceil(src.numFrames() * ratio));

    auto dst = std::make_shared<audio::AudioBuffer>(src.numChannels(), newFrames);

    for (int ch = 0; ch < src.numChannels(); ++ch) {
        const float* srcData = src.channelData(ch);
        float* dstData = dst->channelData(ch);

        for (int i = 0; i < newFrames; ++i) {
            double srcPos = i / ratio;
            int idx = static_cast<int>(srcPos);
            double frac = srcPos - idx;

            if (idx + 1 < src.numFrames()) {
                dstData[i] = static_cast<float>(
                    srcData[idx] * (1.0 - frac) + srcData[idx + 1] * frac
                );
            } else if (idx < src.numFrames()) {
                dstData[i] = srcData[idx];
            } else {
                dstData[i] = 0.0f;
            }
        }
    }

    return dst;
}

bool saveAudioFile(const std::string& path,
                   const float* interleavedData, int numFrames, int numChannels,
                   int sampleRate) {
    SF_INFO sfInfo{};
    sfInfo.samplerate = sampleRate;
    sfInfo.channels = numChannels;
    sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    sfInfo.frames = numFrames;

    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &sfInfo);
    if (!file) {
        LOG_ERROR("File", "Failed to create audio file '%s': %s",
            path.c_str(), sf_strerror(nullptr));
        return false;
    }

    sf_writef_float(file, interleavedData, numFrames);
    sf_close(file);
    return true;
}

bool saveAudioBuffer(const std::string& path,
                     const audio::AudioBuffer& buffer,
                     int sampleRate) {
    if (buffer.isEmpty()) return false;

    int nc = buffer.numChannels();
    int nf = buffer.numFrames();

    // Convert non-interleaved → interleaved for libsndfile
    std::vector<float> interleaved(static_cast<size_t>(nf) * nc);
    for (int ch = 0; ch < nc; ++ch) {
        const float* src = buffer.channelData(ch);
        for (int i = 0; i < nf; ++i) {
            interleaved[static_cast<size_t>(i) * nc + ch] = src[i];
        }
    }

    return saveAudioFile(path, interleaved.data(), nf, nc, sampleRate);
}

} // namespace util
} // namespace yawn
