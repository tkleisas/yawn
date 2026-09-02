#pragma once

// FfmpegShim — runtime indirection for YAWN's small libav* API surface
// (~30 functions across avcodec/avformat/avutil/swscale/avdevice).
//
// Why this exists: the Linux release tarball is built on a distro whose
// FFmpeg sonames (libavcodec.so.60, …) differ from what other distros
// ship (Debian 13 / newer Ubuntu: .so.61; older: .so.59). A hard
// DT_NEEDED link made the whole app refuse to start ("error while
// loading shared libraries: libavcodec.so.60") even though video is an
// optional feature.
//
// Behaviour by platform:
//   • Windows — FFmpeg DLLs are bundled in the release and linked at
//     build time; the pointers below are bound to the linked symbols
//     statically. probe() always succeeds there.
//   • POSIX (Linux) — the libraries are probed with dlopen() by FFmpeg
//     release family (7.x, then 6.x — one full family at a time, never
//     mixed). If no compatible set is found the app still runs; video
//     decode/live input report unavailability at runtime instead of
//     killing startup.
//
// Structs, enums and constants still come from the real FFmpeg headers
// at compile time — only function calls are indirect. The accepted
// soname families are exactly those whose public-struct layouts were
// verified field-compatible with the headers we compile against (see
// the family table in FfmpegShim.cpp); do NOT add a new major without
// checking AVFormatContext/AVStream/AVCodecContext/AVFrame/AVPacket
// field layouts in its headers first.
//
// Usage: call sites use ff::av_read_frame(...) etc. instead of the
// global symbols. Every consumer must check ff::available() first
// (ff::probe() is implicit in available()).

#include <cstdint>
#include <string>

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
namespace ff {

// Ensure the shim has attempted to load/bind the libraries. Idempotent,
// thread-safe (std::call_once internally). Returns available().
bool probe();

// True when the core set (avutil + avcodec + avformat + swscale) is
// bound — video file decode works. Implies probe().
bool available();

// True when libavdevice is additionally bound — OS device demuxers
// (v4l2/dshow/…) and avdevice_list_input_sources work.
bool hasAvdevice();

// Human-readable description of the last failed probe ("" when loaded).
const std::string& loadError();

// ── libavutil ────────────────────────────────────────────────────────
extern AVFrame* (*av_frame_alloc)(void);
extern void     (*av_frame_free)(AVFrame** frame);
extern int      (*av_strerror)(int errnum, char* errbuf, size_t errbuf_size);
extern int64_t  (*av_rescale)(int64_t a, int64_t b, int64_t c);
extern int      (*av_dict_set)(AVDictionary** pm, const char* key,
                               const char* value, int flags);
extern void     (*av_dict_free)(AVDictionary** m);

// ── libavcodec ───────────────────────────────────────────────────────
extern const AVCodec* (*avcodec_find_decoder)(enum AVCodecID id);
extern AVCodecContext* (*avcodec_alloc_context3)(const AVCodec* codec);
extern int  (*avcodec_parameters_to_context)(AVCodecContext* codec,
                                             const AVCodecParameters* par);
extern int  (*avcodec_open2)(AVCodecContext* avctx, const AVCodec* codec,
                             AVDictionary** options);
extern void (*avcodec_free_context)(AVCodecContext** avctx);
extern int  (*avcodec_send_packet)(AVCodecContext* avctx,
                                   const AVPacket* avpkt);
extern int  (*avcodec_receive_frame)(AVCodecContext* avctx, AVFrame* frame);
extern void (*avcodec_flush_buffers)(AVCodecContext* avctx);
extern AVPacket* (*av_packet_alloc)(void);
extern void (*av_packet_free)(AVPacket** pkt);
extern void (*av_packet_unref)(AVPacket* pkt);

// ── libavformat ──────────────────────────────────────────────────────
extern int  (*avformat_open_input)(AVFormatContext** ps, const char* url,
                                   const AVInputFormat* fmt,
                                   AVDictionary** options);
extern int  (*avformat_find_stream_info)(AVFormatContext* ic,
                                         AVDictionary** options);
extern void (*avformat_close_input)(AVFormatContext** s);
extern int  (*av_read_frame)(AVFormatContext* s, AVPacket* pkt);
extern int  (*av_seek_frame)(AVFormatContext* s, int stream_index,
                             int64_t timestamp, int flags);
extern const AVInputFormat* (*av_find_input_format)(const char* short_name);

// ── libswscale ───────────────────────────────────────────────────────
extern SwsContext* (*sws_getContext)(int srcW, int srcH,
                                     enum AVPixelFormat srcFormat,
                                     int dstW, int dstH,
                                     enum AVPixelFormat dstFormat,
                                     int flags, SwsFilter* srcFilter,
                                     SwsFilter* dstFilter,
                                     const double* param);
extern int  (*sws_scale)(SwsContext* c, const uint8_t* const srcSlice[],
                         const int srcStride[], int srcSliceY,
                         int srcSliceH, uint8_t* const dst[],
                         const int dstStride[]);
extern void (*sws_freeContext)(SwsContext* swsContext);

// ── libavdevice (optional — see hasAvdevice()) ───────────────────────
#if defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
// avdevice_register_all is a no-op since FFmpeg 5.1 (demuxers self-
// register) and may be ABSENT from the library entirely — nullptr when
// not exported; call through it only after a null check.
extern void (*avdevice_register_all)(void);
extern int  (*avdevice_list_input_sources)(const AVInputFormat* device,
                                           const char* device_name,
                                           AVDictionary* device_options,
                                           AVDeviceInfoList** device_list);
extern void (*avdevice_free_list_devices)(AVDeviceInfoList** device_list);
#endif

} // namespace ff
} // namespace visual
} // namespace yawn

#else  // !YAWN_HAS_VIDEO — no FFmpeg headers: minimal no-op surface.

namespace yawn {
namespace visual {
namespace ff {
inline bool probe() { return false; }
inline bool available() { return false; }
inline bool hasAvdevice() { return false; }
inline const std::string& loadError() {
    static const std::string s = "build has no FFmpeg support";
    return s;
}
} // namespace ff
} // namespace visual
} // namespace yawn

#endif  // YAWN_HAS_VIDEO
