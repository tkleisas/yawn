#include "visual/LiveVideoSource.h"
#include "util/Logger.h"

#include <cstring>

#if defined(YAWN_HAS_VIDEO) && YAWN_HAS_VIDEO

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#if defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
#include <libavdevice/avdevice.h>
#endif
}

namespace yawn {
namespace visual {

LiveVideoSource::~LiveVideoSource() { stop(); }

void LiveVideoSource::stop() {
    m_stopFlag.store(true);
    if (m_thread.joinable()) m_thread.join();
    m_stopFlag.store(false);
    m_state.store(State::Stopped);
}

bool LiveVideoSource::start(const std::string& url) {
    stop();
    {
        std::lock_guard<std::mutex> lk(m_metaMutex);
        m_error.clear();
        m_url = url;
    }
    m_state.store(State::Connecting);
    m_thread = std::thread(&LiveVideoSource::threadMain, this, url);
    return true;
}

std::string LiveVideoSource::error() const {
    std::lock_guard<std::mutex> lk(m_metaMutex);
    return m_error;
}

std::string LiveVideoSource::url() const {
    std::lock_guard<std::mutex> lk(m_metaMutex);
    return m_url;
}

bool LiveVideoSource::latestFrame(uint8_t* outBuf) {
    const uint64_t seq = m_frameSeq.load(std::memory_order_acquire);
    if (seq == m_lastReadSeq) return false;
    std::lock_guard<std::mutex> lk(m_bufMutex);
    if (m_readyBuf.size() != size_t(kWidth) * kHeight * 4) return false;
    std::memcpy(outBuf, m_readyBuf.data(), m_readyBuf.size());
    m_lastReadSeq = seq;
    return true;
}

namespace {

// Outcome of a single open+decode attempt — steers the retry policy
// in the outer thread loop.
enum class AttemptOutcome {
    Stopped,        // m_stopFlag tripped — bail out of the thread
    OpenFailed,     // couldn't even open the input
    StreamDropped,  // had been reading frames, then the source ended
};

} // namespace

void LiveVideoSource::threadMain(std::string url) {
    LiveUrlParts parts = parseLiveUrl(url);

    const AVInputFormat* inputFmt = nullptr;
    if (!parts.inputFormat.empty()) {
#if defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
        inputFmt = av_find_input_format(parts.inputFormat.c_str());
        if (!inputFmt) {
            std::lock_guard<std::mutex> lk(m_metaMutex);
            m_error = "input format '" + parts.inputFormat +
                       "' not available (rebuild with libavdevice?)";
            m_state.store(State::Failed);
            LOG_ERROR("LiveVideo", "%s", m_error.c_str());
            return;
        }
#else
        std::lock_guard<std::mutex> lk(m_metaMutex);
        m_error = "device URL needs libavdevice (not compiled in)";
        m_state.store(State::Failed);
        LOG_ERROR("LiveVideo", "%s", m_error.c_str());
        return;
#endif
    }

    auto setError = [this](std::string msg) {
        std::lock_guard<std::mutex> lk(m_metaMutex);
        m_error = std::move(msg);
    };

    // Sleep in small chunks so stop() can interrupt the backoff without
    // the thread sitting on a long sleep. Returns true if stop was
    // requested during the wait.
    auto waitBackoff = [this](int seconds) -> bool {
        for (int i = 0; i < seconds * 10; ++i) {
            if (m_stopFlag.load()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return m_stopFlag.load();
    };

    // Single open+decode pass. Returns OpenFailed, StreamDropped, or
    // Stopped. All resources are cleaned up before return so the outer
    // retry loop starts from scratch.
    auto runOnce = [&]() -> AttemptOutcome {
        AVFormatContext* fmt = nullptr;
        // For network streams we want to fail fast rather than block
        // for minutes, and RTSP over TCP is the sane default.
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "stimeout",       "5000000", 0); // 5s (µs)
        av_dict_set(&opts, "rw_timeout",     "5000000", 0);
        av_dict_set(&opts, "rtsp_transport", "tcp",     0);

        if (avformat_open_input(&fmt, parts.filename.c_str(),
                                  inputFmt, &opts) != 0) {
            av_dict_free(&opts);
            setError("avformat_open_input failed for " + parts.filename);
            LOG_ERROR("LiveVideo", "open failed: %s", parts.filename.c_str());
            return AttemptOutcome::OpenFailed;
        }
        av_dict_free(&opts);

        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            avformat_close_input(&fmt);
            setError("find_stream_info failed for " + parts.filename);
            return AttemptOutcome::OpenFailed;
        }

        int streamIdx = -1;
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                streamIdx = static_cast<int>(i);
                break;
            }
        }
        if (streamIdx < 0) {
            avformat_close_input(&fmt);
            setError("no video stream in " + parts.filename);
            return AttemptOutcome::OpenFailed;
        }

        AVStream* st = fmt->streams[streamIdx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext* cctx = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!cctx ||
            avcodec_parameters_to_context(cctx, st->codecpar) < 0 ||
            avcodec_open2(cctx, codec, nullptr) < 0) {
            if (cctx) avcodec_free_context(&cctx);
            avformat_close_input(&fmt);
            setError("codec open failed");
            return AttemptOutcome::OpenFailed;
        }

        // Scaler is lazy — we wait for the first decoded frame before
        // building it, since live sources don't always report a valid
        // pix_fmt on codecpar.
        SwsContext* sws = nullptr;
        int swsSrcW = 0, swsSrcH = 0;
        int swsSrcFmt = AV_PIX_FMT_NONE;

        AVFrame*  frame  = av_frame_alloc();
        AVPacket* packet = av_packet_alloc();
        std::vector<uint8_t> rgba(size_t(kWidth) * kHeight * 4, 0);

        m_state.store(State::Connected);
        setError("");   // clear any stale open-failure message
        LOG_INFO("LiveVideo", "Connected to %s", url.c_str());

        AttemptOutcome outcome = AttemptOutcome::StreamDropped;

        while (!m_stopFlag.load()) {
            int readRc = av_read_frame(fmt, packet);
            if (readRc < 0) {
                if (readRc == AVERROR(EAGAIN)) continue;
                LOG_WARN("LiveVideo", "av_read_frame ended (rc=%d)", readRc);
                outcome = AttemptOutcome::StreamDropped;
                break;
            }
            if (packet->stream_index != streamIdx) {
                av_packet_unref(packet);
                continue;
            }
            int send = avcodec_send_packet(cctx, packet);
            av_packet_unref(packet);
            if (send < 0) continue;

            while (!m_stopFlag.load()) {
                int recv = avcodec_receive_frame(cctx, frame);
                if (recv == AVERROR(EAGAIN) || recv == AVERROR_EOF) break;
                if (recv < 0) break;

                // (Re)build the scaler whenever the input geometry
                // changes — live sources can switch on reconnect.
                if (!sws || frame->width != swsSrcW ||
                    frame->height != swsSrcH ||
                    frame->format != swsSrcFmt) {
                    if (sws) { sws_freeContext(sws); sws = nullptr; }
                    swsSrcW   = frame->width;
                    swsSrcH   = frame->height;
                    swsSrcFmt = frame->format;
                    sws = sws_getContext(
                        swsSrcW, swsSrcH, static_cast<AVPixelFormat>(swsSrcFmt),
                        kWidth, kHeight, AV_PIX_FMT_RGBA,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!sws) {
                        LOG_ERROR("LiveVideo",
                            "sws_getContext failed for %dx%d fmt=%d",
                            swsSrcW, swsSrcH, swsSrcFmt);
                        continue;
                    }
                }

                uint8_t* dstSlice[1]  = { rgba.data() };
                int      dstStride[1] = { kWidth * 4 };
                sws_scale(sws, frame->data, frame->linesize,
                          0, swsSrcH, dstSlice, dstStride);

                {
                    std::lock_guard<std::mutex> lk(m_bufMutex);
                    m_readyBuf.assign(rgba.begin(), rgba.end());
                }
                m_frameSeq.fetch_add(1, std::memory_order_release);
            }
        }

        if (m_stopFlag.load()) outcome = AttemptOutcome::Stopped;

        if (sws)    sws_freeContext(sws);
        if (frame)  av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (cctx)   avcodec_free_context(&cctx);
        avformat_close_input(&fmt);
        return outcome;
    };

    // Outer retry loop.
    //   • Bad URL / never connected: up to 3 attempts at 1s/2s/4s
    //     backoff, then Failed. Fast feedback for typos.
    //   • Connected at least once then dropped: retry forever with
    //     backoff capped at 30s, because a running show should
    //     survive the server / camera hiccupping.
    bool everConnected = false;
    int  openFailures  = 0;
    int  backoffSec    = 1;

    while (!m_stopFlag.load()) {
        m_state.store(State::Connecting);
        AttemptOutcome rc = runOnce();

        if (rc == AttemptOutcome::Stopped) break;

        if (rc == AttemptOutcome::StreamDropped) {
            // runOnce only returns StreamDropped after it reached the
            // Connected state, so a single StreamDropped tells us the
            // source was usable at some point.
            everConnected = true;
            openFailures  = 0;
        } else {  // OpenFailed
            ++openFailures;
            if (!everConnected && openFailures >= 3) {
                m_state.store(State::Failed);
                LOG_ERROR("LiveVideo",
                    "Giving up on %s after %d open failures",
                    url.c_str(), openFailures);
                return;
            }
        }

        // Backoff: 1s → 2 → 4 → 8 → 16 → 30s cap.
        LOG_INFO("LiveVideo", "Reconnecting to %s in %ds",
                 url.c_str(), backoffSec);
        if (waitBackoff(backoffSec)) break;
        backoffSec = std::min(30, backoffSec * 2);
    }

    m_state.store(State::Stopped);
    LOG_INFO("LiveVideo", "Thread exit for %s", url.c_str());
}

} // namespace visual
} // namespace yawn

#else  // !YAWN_HAS_VIDEO — compile to no-ops so callers don't need #ifdefs

namespace yawn {
namespace visual {

LiveVideoSource::~LiveVideoSource() {}
void LiveVideoSource::stop() { m_state.store(State::Stopped); }
bool LiveVideoSource::start(const std::string&) {
    m_state.store(State::Failed);
    return false;
}
std::string LiveVideoSource::error() const { return "video disabled at build time"; }
std::string LiveVideoSource::url() const { return {}; }
bool LiveVideoSource::latestFrame(uint8_t*) { return false; }

} // namespace visual
} // namespace yawn

#endif  // YAWN_HAS_VIDEO
