#include "util/FileIO.h"
#include "util/Logger.h"
#include <sndfile.h>
#include <vector>
#include <cmath>

namespace yawn {
namespace util {

std::shared_ptr<audio::AudioBuffer> loadAudioFile(
    const std::string& path,
    AudioFileInfo* outInfo)
{
    SF_INFO sfInfo;
    sfInfo.format = 0;

    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sfInfo);
    if (!file) {
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
