#pragma once

// VideoDecoder — thin libavcodec/libavformat/libswscale wrapper focused on
// the specific subset YAWN needs:
//
//   • Files are produced by YAWN's own import pipeline: 640×360,
//     all-intraframe H.264 in MP4, fixed 30 fps, yuv420p, no audio.
//   • Every frame is a keyframe → seek-to-frame-N is cheap.
//   • Output is an RGBA buffer sized exactly kWidth × kHeight.
//
// Lifetime: open() loads the file. seekToFrame() + decodeCurrent() return
// the requested frame into a caller-provided RGBA byte buffer. close()
// tears down the decoder.
//
// Thread model: a single VideoDecoder instance is owned by one thread at
// a time (the UI thread in the MVP). If we ever move decode off the UI
// thread, each consumer should have its own VideoDecoder.
//
// If YAWN_HAS_VIDEO is 0 this class compiles to no-ops so the rest of
// the code can reference it without #ifdefs everywhere.

#include <string>
#include <vector>
#include <cstdint>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace yawn {
namespace visual {

class VideoDecoder {
public:
    static constexpr int kWidth  = 640;
    static constexpr int kHeight = 360;

    VideoDecoder() = default;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Open an imported .mp4 (produced by our transcoder). Returns false on
    // any failure; on failure the instance is left closed.
    bool open(const std::string& path);
    void close();

    bool isOpen() const { return m_opened; }
    int  frameCount() const { return m_frameCount; }
    double fps() const { return m_fps; }

    // Decode frame `frameIndex` (0..frameCount-1) into outRGBA. Buffer must
    // be kWidth * kHeight * 4 bytes. Returns true on success.
    bool decodeFrame(int frameIndex, uint8_t* outRGBA);

private:
    // libav state — all opaque to callers, forward-declared above.
    AVFormatContext* m_fmt     = nullptr;
    AVCodecContext*  m_codec   = nullptr;
    AVFrame*         m_frame   = nullptr;
    AVFrame*         m_rgba    = nullptr;
    AVPacket*        m_packet  = nullptr;
    SwsContext*      m_sws     = nullptr;

    int    m_streamIndex = -1;
    int    m_frameCount  = 0;
    double m_fps         = 30.0;
    int    m_lastDecodedFrame = -2;
    bool   m_opened      = false;
};

} // namespace visual
} // namespace yawn
