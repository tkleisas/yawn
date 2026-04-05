#include "util/FileIO.h"
#include "util/Logger.h"
#include <sndfile.h>
#include <FLAC/stream_encoder.h>
#include <vorbis/vorbisenc.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdio>

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

// ── WAV export via libsndfile ──────────────────────────────────────
static bool saveWAV(const std::string& path,
                    const float* interleavedData, int numFrames, int numChannels,
                    int sampleRate, BitDepth bitDepth) {
    SF_INFO sfInfo{};
    sfInfo.samplerate = sampleRate;
    sfInfo.channels = numChannels;
    sfInfo.frames = numFrames;

    int subFormat;
    switch (bitDepth) {
        case BitDepth::Int16:   subFormat = SF_FORMAT_PCM_16; break;
        case BitDepth::Int24:   subFormat = SF_FORMAT_PCM_24; break;
        default:                subFormat = SF_FORMAT_FLOAT;   break;
    }
    sfInfo.format = SF_FORMAT_WAV | subFormat;

    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &sfInfo);
    if (!file) {
        LOG_ERROR("File", "Failed to create WAV '%s': %s",
            path.c_str(), sf_strerror(nullptr));
        return false;
    }
    sf_writef_float(file, interleavedData, numFrames);
    sf_close(file);
    return true;
}

// ── FLAC export via libFLAC ───────────────────────────────────────
static bool saveFLAC(const std::string& path,
                     const float* interleavedData, int numFrames, int numChannels,
                     int sampleRate, BitDepth bitDepth) {
    int bps;
    switch (bitDepth) {
        case BitDepth::Int16:   bps = 16; break;
        case BitDepth::Int24:   bps = 24; break;
        default:                bps = 24; break; // FLAC doesn't support float; use 24-bit
    }

    FLAC__StreamEncoder* encoder = FLAC__stream_encoder_new();
    if (!encoder) {
        LOG_ERROR("File", "Failed to create FLAC encoder");
        return false;
    }

    FLAC__stream_encoder_set_channels(encoder, numChannels);
    FLAC__stream_encoder_set_bits_per_sample(encoder, bps);
    FLAC__stream_encoder_set_sample_rate(encoder, sampleRate);
    FLAC__stream_encoder_set_compression_level(encoder, 5);
    FLAC__stream_encoder_set_total_samples_estimate(encoder, numFrames);

    auto status = FLAC__stream_encoder_init_file(encoder, path.c_str(), nullptr, nullptr);
    if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        LOG_ERROR("File", "Failed to init FLAC file '%s': %d", path.c_str(), (int)status);
        FLAC__stream_encoder_delete(encoder);
        return false;
    }

    // Convert float → FLAC__int32 (scaled to bit depth)
    const int kBlockSize = 4096;
    std::vector<FLAC__int32> pcm(static_cast<size_t>(kBlockSize) * numChannels);
    double scale = (1 << (bps - 1)) - 1.0;

    for (int offset = 0; offset < numFrames; offset += kBlockSize) {
        int blockFrames = (std::min)(kBlockSize, numFrames - offset);
        for (int i = 0; i < blockFrames * numChannels; ++i) {
            double s = interleavedData[static_cast<size_t>(offset) * numChannels + i];
            s = (std::max)(-1.0, (std::min)(1.0, s));
            pcm[i] = static_cast<FLAC__int32>(s * scale);
        }
        if (!FLAC__stream_encoder_process_interleaved(encoder, pcm.data(), blockFrames)) {
            LOG_ERROR("File", "FLAC encode error at frame %d", offset);
            FLAC__stream_encoder_finish(encoder);
            FLAC__stream_encoder_delete(encoder);
            return false;
        }
    }

    FLAC__stream_encoder_finish(encoder);
    FLAC__stream_encoder_delete(encoder);
    return true;
}

// ── OGG Vorbis export via libvorbisenc ────────────────────────────
static bool saveOGG(const std::string& path,
                    const float* interleavedData, int numFrames, int numChannels,
                    int sampleRate, BitDepth /*bitDepth*/) {
    // Vorbis is always lossy — bit depth parameter is ignored
    vorbis_info vi;
    vorbis_info_init(&vi);

    if (vorbis_encode_init_vbr(&vi, numChannels, sampleRate, 0.5f) != 0) {
        LOG_ERROR("File", "Failed to init Vorbis encoder");
        vorbis_info_clear(&vi);
        return false;
    }

    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "YAWN DAW");

    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);

    ogg_stream_state os;
    ogg_stream_init(&os, 42); // arbitrary serial number

    // Write Vorbis headers
    ogg_packet header, headerComm, headerCode;
    vorbis_analysis_headerout(&vd, &vc, &header, &headerComm, &headerCode);
    ogg_stream_packetin(&os, &header);
    ogg_stream_packetin(&os, &headerComm);
    ogg_stream_packetin(&os, &headerCode);

    FILE* fp = nullptr;
#ifdef _WIN32
    fopen_s(&fp, path.c_str(), "wb");
#else
    fp = fopen(path.c_str(), "wb");
#endif
    if (!fp) {
        LOG_ERROR("File", "Failed to open '%s' for writing", path.c_str());
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        ogg_stream_clear(&os);
        return false;
    }

    // Flush headers
    ogg_page og;
    while (ogg_stream_flush(&os, &og)) {
        fwrite(og.header, 1, og.header_len, fp);
        fwrite(og.body, 1, og.body_len, fp);
    }

    // Encode audio in blocks
    const int kBlockSize = 4096;
    bool eos = false;
    for (int offset = 0; !eos; offset += kBlockSize) {
        int blockFrames = (std::min)(kBlockSize, numFrames - offset);
        if (blockFrames > 0) {
            float** buffer = vorbis_analysis_buffer(&vd, blockFrames);
            for (int i = 0; i < blockFrames; ++i) {
                for (int ch = 0; ch < numChannels; ++ch) {
                    buffer[ch][i] = interleavedData[static_cast<size_t>(offset + i) * numChannels + ch];
                }
            }
            vorbis_analysis_wrote(&vd, blockFrames);
        } else {
            vorbis_analysis_wrote(&vd, 0); // signal end of data
        }

        ogg_packet op;
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (!eos) {
                    if (ogg_stream_pageout(&os, &og) == 0) break;
                    fwrite(og.header, 1, og.header_len, fp);
                    fwrite(og.body, 1, og.body_len, fp);
                    if (ogg_page_eos(&og)) eos = true;
                }
            }
        }
        if (offset + kBlockSize >= numFrames && !eos) {
            // Force flush remaining packets
            vorbis_analysis_wrote(&vd, 0);
            while (vorbis_analysis_blockout(&vd, &vb) == 1) {
                vorbis_analysis(&vb, nullptr);
                vorbis_bitrate_addblock(&vb);
                while (vorbis_bitrate_flushpacket(&vd, &op)) {
                    ogg_stream_packetin(&os, &op);
                    while (ogg_stream_pageout(&os, &og)) {
                        fwrite(og.header, 1, og.header_len, fp);
                        fwrite(og.body, 1, og.body_len, fp);
                        if (ogg_page_eos(&og)) eos = true;
                    }
                }
            }
            eos = true;
        }
    }

    fclose(fp);
    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    return true;
}

bool saveAudioFile(const std::string& path,
                   const float* interleavedData, int numFrames, int numChannels,
                   int sampleRate, ExportFormat format, BitDepth bitDepth) {
    switch (format) {
        case ExportFormat::WAV:  return saveWAV(path, interleavedData, numFrames, numChannels, sampleRate, bitDepth);
        case ExportFormat::FLAC: return saveFLAC(path, interleavedData, numFrames, numChannels, sampleRate, bitDepth);
        case ExportFormat::OGG:  return saveOGG(path, interleavedData, numFrames, numChannels, sampleRate, bitDepth);
    }
    return false;
}

bool saveAudioBuffer(const std::string& path,
                     const audio::AudioBuffer& buffer,
                     int sampleRate, ExportFormat format, BitDepth bitDepth) {
    if (buffer.isEmpty()) return false;

    int nc = buffer.numChannels();
    int nf = buffer.numFrames();

    // Convert non-interleaved → interleaved
    std::vector<float> interleaved(static_cast<size_t>(nf) * nc);
    for (int ch = 0; ch < nc; ++ch) {
        const float* src = buffer.channelData(ch);
        for (int i = 0; i < nf; ++i) {
            interleaved[static_cast<size_t>(i) * nc + ch] = src[i];
        }
    }

    return saveAudioFile(path, interleaved.data(), nf, nc, sampleRate, format, bitDepth);
}

const char* formatExtension(ExportFormat format) {
    switch (format) {
        case ExportFormat::WAV:  return ".wav";
        case ExportFormat::FLAC: return ".flac";
        case ExportFormat::OGG:  return ".ogg";
    }
    return ".wav";
}

} // namespace util
} // namespace yawn
