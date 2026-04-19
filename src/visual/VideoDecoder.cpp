#include "visual/VideoDecoder.h"
#include "util/Logger.h"

#include <cstring>

#if defined(YAWN_HAS_VIDEO) && YAWN_HAS_VIDEO

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace yawn {
namespace visual {

VideoDecoder::~VideoDecoder() { close(); }

void VideoDecoder::close() {
    if (m_sws)    { sws_freeContext(m_sws); m_sws = nullptr; }
    if (m_frame)  { av_frame_free(&m_frame); m_frame = nullptr; }
    if (m_rgba)   { av_frame_free(&m_rgba);  m_rgba = nullptr; }
    if (m_packet) { av_packet_free(&m_packet); m_packet = nullptr; }
    if (m_codec)  { avcodec_free_context(&m_codec); m_codec = nullptr; }
    if (m_fmt)    { avformat_close_input(&m_fmt); m_fmt = nullptr; }
    m_streamIndex = -1;
    m_frameCount  = 0;
    m_fps         = 30.0;
    m_lastDecodedFrame = -2;
    m_opened      = false;
}

bool VideoDecoder::open(const std::string& path) {
    close();

    if (avformat_open_input(&m_fmt, path.c_str(), nullptr, nullptr) != 0) {
        LOG_ERROR("Video", "avformat_open_input failed: %s", path.c_str());
        return false;
    }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
        LOG_ERROR("Video", "find_stream_info failed: %s", path.c_str());
        close();
        return false;
    }

    // Pick the first video stream.
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
        if (m_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_streamIndex = static_cast<int>(i);
            break;
        }
    }
    if (m_streamIndex < 0) {
        LOG_ERROR("Video", "no video stream: %s", path.c_str());
        close();
        return false;
    }

    AVStream* st = m_fmt->streams[m_streamIndex];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        LOG_ERROR("Video", "no decoder for codec %d", st->codecpar->codec_id);
        close();
        return false;
    }
    m_codec = avcodec_alloc_context3(codec);
    if (!m_codec ||
        avcodec_parameters_to_context(m_codec, st->codecpar) < 0 ||
        avcodec_open2(m_codec, codec, nullptr) < 0) {
        LOG_ERROR("Video", "avcodec open failed");
        close();
        return false;
    }

    // Our transcoded assets are always 640×360 yuv420p, but we compute
    // frame counts / fps from the stream so we work on any file.
    if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0) {
        m_fps = static_cast<double>(st->avg_frame_rate.num) /
                 static_cast<double>(st->avg_frame_rate.den);
    } else {
        m_fps = 30.0;
    }
    if (st->nb_frames > 0) {
        m_frameCount = static_cast<int>(st->nb_frames);
    } else if (m_fmt->duration > 0) {
        const double durSec = static_cast<double>(m_fmt->duration) / AV_TIME_BASE;
        m_frameCount = static_cast<int>(durSec * m_fps + 0.5);
    } else {
        m_frameCount = 0;
    }

    m_frame  = av_frame_alloc();
    m_rgba   = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_frame || !m_rgba || !m_packet) { close(); return false; }

    // Scaler: decoder's native pixel format → RGBA8.
    m_sws = sws_getContext(
        kWidth, kHeight, m_codec->pix_fmt,
        kWidth, kHeight, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) {
        LOG_ERROR("Video", "sws_getContext failed");
        close();
        return false;
    }

    m_opened = true;
    LOG_INFO("Video", "Opened %s (%d frames @ %.2f fps)",
             path.c_str(), m_frameCount, m_fps);
    return true;
}

bool VideoDecoder::decodeFrame(int frameIndex, uint8_t* outRGBA) {
    if (!m_opened || frameIndex < 0) return false;
    if (m_frameCount > 0 && frameIndex >= m_frameCount)
        frameIndex = frameIndex % m_frameCount;

    // Fast path: if the requested frame is the next one in stream order,
    // skip the seek. Helps during normal forward playback where we tick
    // roughly in step with the video's fps.
    AVStream* st = m_fmt->streams[m_streamIndex];
    const bool sequential = (frameIndex == m_lastDecodedFrame + 1);

    if (!sequential) {
        // Seek to the frame by timestamp (every frame is a keyframe in our
        // transcoded assets, so ANY_KEYFRAME gives us exactly that frame).
        int64_t ts = av_rescale(frameIndex * st->avg_frame_rate.den,
                                 st->time_base.den,
                                 st->avg_frame_rate.num * st->time_base.num);
        av_seek_frame(m_fmt, m_streamIndex, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_codec);
    }

    // Decode until we have the desired frame.
    while (av_read_frame(m_fmt, m_packet) >= 0) {
        if (m_packet->stream_index != m_streamIndex) {
            av_packet_unref(m_packet);
            continue;
        }
        int send = avcodec_send_packet(m_codec, m_packet);
        av_packet_unref(m_packet);
        if (send < 0) return false;

        while (true) {
            int recv = avcodec_receive_frame(m_codec, m_frame);
            if (recv == AVERROR(EAGAIN) || recv == AVERROR_EOF) break;
            if (recv < 0) return false;

            // Compute the frame index of what we just decoded. pts can be
            // AV_NOPTS_VALUE for some odd streams — fall back to a running
            // counter via m_lastDecodedFrame in that case.
            int got;
            if (m_frame->pts != AV_NOPTS_VALUE) {
                got = static_cast<int>(
                    av_rescale(m_frame->pts,
                               st->avg_frame_rate.num * st->time_base.num,
                               st->avg_frame_rate.den * st->time_base.den));
            } else {
                got = m_lastDecodedFrame + 1;
            }

            if (got >= frameIndex) {
                // Convert to RGBA and copy into the caller's buffer.
                uint8_t* dstSlice[1]  = { outRGBA };
                int      dstStride[1] = { kWidth * 4 };
                sws_scale(m_sws, m_frame->data, m_frame->linesize,
                          0, kHeight, dstSlice, dstStride);
                m_lastDecodedFrame = got;
                return true;
            }
            m_lastDecodedFrame = got;
        }
    }
    return false;
}

} // namespace visual
} // namespace yawn

#else  // !YAWN_HAS_VIDEO

// No-op stubs so the rest of the codebase can reference VideoDecoder.
namespace yawn {
namespace visual {
VideoDecoder::~VideoDecoder() {}
void VideoDecoder::close() { m_opened = false; }
bool VideoDecoder::open(const std::string&) { return false; }
bool VideoDecoder::decodeFrame(int, uint8_t*) { return false; }
} // namespace visual
} // namespace yawn

#endif  // YAWN_HAS_VIDEO
