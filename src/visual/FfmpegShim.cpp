#include "visual/FfmpegShim.h"

#if defined(YAWN_HAS_VIDEO) && YAWN_HAS_VIDEO

#include <mutex>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace yawn {
namespace visual {
namespace ff {

#if defined(_WIN32)

// ── Windows: link-time binding ───────────────────────────────────────
// FFmpeg ships as bundled DLLs next to the exe; the pointers are bound
// to the linked symbols directly. probe() is a trivial success.

AVFrame* (*av_frame_alloc)(void) = &::av_frame_alloc;
void     (*av_frame_free)(AVFrame**) = &::av_frame_free;
int      (*av_strerror)(int, char*, size_t) = &::av_strerror;
int64_t  (*av_rescale)(int64_t, int64_t, int64_t) = &::av_rescale;
int      (*av_dict_set)(AVDictionary**, const char*, const char*, int) = &::av_dict_set;
void     (*av_dict_free)(AVDictionary**) = &::av_dict_free;

const AVCodec* (*avcodec_find_decoder)(enum AVCodecID) = &::avcodec_find_decoder;
AVCodecContext* (*avcodec_alloc_context3)(const AVCodec*) = &::avcodec_alloc_context3;
int  (*avcodec_parameters_to_context)(AVCodecContext*, const AVCodecParameters*) = &::avcodec_parameters_to_context;
int  (*avcodec_open2)(AVCodecContext*, const AVCodec*, AVDictionary**) = &::avcodec_open2;
void (*avcodec_free_context)(AVCodecContext**) = &::avcodec_free_context;
int  (*avcodec_send_packet)(AVCodecContext*, const AVPacket*) = &::avcodec_send_packet;
int  (*avcodec_receive_frame)(AVCodecContext*, AVFrame*) = &::avcodec_receive_frame;
void (*avcodec_flush_buffers)(AVCodecContext*) = &::avcodec_flush_buffers;
AVPacket* (*av_packet_alloc)(void) = &::av_packet_alloc;
void (*av_packet_free)(AVPacket**) = &::av_packet_free;
void (*av_packet_unref)(AVPacket*) = &::av_packet_unref;

int  (*avformat_open_input)(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**) = &::avformat_open_input;
int  (*avformat_find_stream_info)(AVFormatContext*, AVDictionary**) = &::avformat_find_stream_info;
void (*avformat_close_input)(AVFormatContext**) = &::avformat_close_input;
int  (*av_read_frame)(AVFormatContext*, AVPacket*) = &::av_read_frame;
int  (*av_seek_frame)(AVFormatContext*, int, int64_t, int) = &::av_seek_frame;
const AVInputFormat* (*av_find_input_format)(const char*) = &::av_find_input_format;

SwsContext* (*sws_getContext)(int, int, enum AVPixelFormat, int, int,
                              enum AVPixelFormat, int, SwsFilter*,
                              SwsFilter*, const double*) = &::sws_getContext;
int  (*sws_scale)(SwsContext*, const uint8_t* const[], const int[], int,
                  int, uint8_t* const[], const int[]) = &::sws_scale;
void (*sws_freeContext)(SwsContext*) = &::sws_freeContext;

#if defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
void (*avdevice_register_all)(void) = &::avdevice_register_all;
int  (*avdevice_list_input_sources)(const AVInputFormat*, const char*,
                                    AVDictionary*, AVDeviceInfoList**) = &::avdevice_list_input_sources;
void (*avdevice_free_list_devices)(AVDeviceInfoList**) = &::avdevice_free_list_devices;
#endif

bool probe() { return true; }
bool available() { return true; }
#if defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
bool hasAvdevice() { return true; }
#else
bool hasAvdevice() { return false; }
#endif
const std::string& loadError() {
    static const std::string s;
    return s;
}

#else  // POSIX: runtime dlopen probing

// ── POSIX: runtime loading by FFmpeg release family ──────────────────
//
// Each family is a matched set of sonames — the libs are versioned
// together upstream and depend on each other, so we never mix majors.
// Newest first. A family is accepted only if every core symbol below
// resolves; avdevice is optional (live input degrades without it).
//
// ⚠ ABI GATE: only families whose public-struct layouts were verified
// field-compatible with the headers we compile against are listed.
// Verified (FFmpeg n5.1.6 / n6.1.2 / n7.1.1 headers, field-by-field):
//   • 6 ↔ 7 is safe for everything EXCEPT AVFormatContext::duration and
//     AVCodecContext::{pix_fmt,time_base} — the code avoids those three
//     fields entirely (duration comes from AVStream, scaler geometry
//     from decoded AVFrame fields).
//   • 5.x is EXCLUDED — AVStream's layout changed in 6.0 (av_class
//     prepended, codecpar relocated).
//   • 8.x is NOT probed — AVStream side_data removal (major ≥ 62)
//     shifts later fields; verify layouts before adding.
namespace {
struct Family {
    const char* name;      // for the error message
    const char* avutil;
    const char* avcodec;
    const char* avformat;
    const char* swscale;
    const char* avdevice;
};
const Family kFamilies[] = {
    {"FFmpeg 7.x", "libavutil.so.59", "libavcodec.so.61",
                   "libavformat.so.61", "libswscale.so.8",
                   "libavdevice.so.61"},
    {"FFmpeg 6.x", "libavutil.so.58", "libavcodec.so.60",
                   "libavformat.so.60", "libswscale.so.7",
                   "libavdevice.so.60"},
};
} // namespace

AVFrame* (*av_frame_alloc)(void) = nullptr;
void     (*av_frame_free)(AVFrame**) = nullptr;
int      (*av_strerror)(int, char*, size_t) = nullptr;
int64_t  (*av_rescale)(int64_t, int64_t, int64_t) = nullptr;
int      (*av_dict_set)(AVDictionary**, const char*, const char*, int) = nullptr;
void     (*av_dict_free)(AVDictionary**) = nullptr;

const AVCodec* (*avcodec_find_decoder)(enum AVCodecID) = nullptr;
AVCodecContext* (*avcodec_alloc_context3)(const AVCodec*) = nullptr;
int  (*avcodec_parameters_to_context)(AVCodecContext*, const AVCodecParameters*) = nullptr;
int  (*avcodec_open2)(AVCodecContext*, const AVCodec*, AVDictionary**) = nullptr;
void (*avcodec_free_context)(AVCodecContext**) = nullptr;
int  (*avcodec_send_packet)(AVCodecContext*, const AVPacket*) = nullptr;
int  (*avcodec_receive_frame)(AVCodecContext*, AVFrame*) = nullptr;
void (*avcodec_flush_buffers)(AVCodecContext*) = nullptr;
AVPacket* (*av_packet_alloc)(void) = nullptr;
void (*av_packet_free)(AVPacket**) = nullptr;
void (*av_packet_unref)(AVPacket*) = nullptr;

int  (*avformat_open_input)(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**) = nullptr;
int  (*avformat_find_stream_info)(AVFormatContext*, AVDictionary**) = nullptr;
void (*avformat_close_input)(AVFormatContext**) = nullptr;
int  (*av_read_frame)(AVFormatContext*, AVPacket*) = nullptr;
int  (*av_seek_frame)(AVFormatContext*, int, int64_t, int) = nullptr;
const AVInputFormat* (*av_find_input_format)(const char*) = nullptr;

SwsContext* (*sws_getContext)(int, int, enum AVPixelFormat, int, int,
                              enum AVPixelFormat, int, SwsFilter*,
                              SwsFilter*, const double*) = nullptr;
int  (*sws_scale)(SwsContext*, const uint8_t* const[], const int[], int,
                  int, uint8_t* const[], const int[]) = nullptr;
void (*sws_freeContext)(SwsContext*) = nullptr;

#if defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
void (*avdevice_register_all)(void) = nullptr;
int  (*avdevice_list_input_sources)(const AVInputFormat*, const char*,
                                    AVDictionary*, AVDeviceInfoList**) = nullptr;
void (*avdevice_free_list_devices)(AVDeviceInfoList**) = nullptr;
#endif

namespace {

bool g_available = false;
bool g_hasAvdevice = false;
std::string g_loadError;
std::once_flag g_probeOnce;

// dlsym into a function pointer (POSIX allows void*→fnptr casts via the
// reinterpret trick; compilers accept it with a warning at most).
template <typename Fn>
bool resolve(void* lib, const char* name, Fn& out) {
    void* p = dlsym(lib, name);
    if (!p) return false;
    *reinterpret_cast<void**>(&out) = p;
    return true;
}

// Resolve one symbol, recording the miss. Returns false if missing.
#define FF_REQ(lib, sym)                                                \
    do {                                                                \
        if (!resolve(lib, #sym, sym)) {                                 \
            if (!missing.empty()) missing += ", ";                      \
            missing += #sym;                                            \
            ok = false;                                                 \
        }                                                               \
    } while (0)

void probeImpl() {
    for (const Family& fam : kFamilies) {
        void* hUtil   = dlopen(fam.avutil,   RTLD_NOW | RTLD_LOCAL);
        void* hCodec  = hUtil   ? dlopen(fam.avcodec,  RTLD_NOW | RTLD_LOCAL) : nullptr;
        void* hFormat = hCodec  ? dlopen(fam.avformat, RTLD_NOW | RTLD_LOCAL) : nullptr;
        void* hScale  = hFormat ? dlopen(fam.swscale,  RTLD_NOW | RTLD_LOCAL) : nullptr;
        if (!hScale) {
            // Missing/incomplete family — close what we opened and try
            // the next one down.
            if (hFormat) dlclose(hFormat);
            if (hCodec)  dlclose(hCodec);
            if (hUtil)   dlclose(hUtil);
            if (!g_loadError.empty()) g_loadError += "; ";
            g_loadError += std::string(fam.name) + ": core libs not found";
            continue;
        }

        std::string missing;
        bool ok = true;

        FF_REQ(hUtil,   av_frame_alloc);
        FF_REQ(hUtil,   av_frame_free);
        FF_REQ(hUtil,   av_strerror);
        FF_REQ(hUtil,   av_rescale);
        FF_REQ(hUtil,   av_dict_set);
        FF_REQ(hUtil,   av_dict_free);

        FF_REQ(hCodec,  avcodec_find_decoder);
        FF_REQ(hCodec,  avcodec_alloc_context3);
        FF_REQ(hCodec,  avcodec_parameters_to_context);
        FF_REQ(hCodec,  avcodec_open2);
        FF_REQ(hCodec,  avcodec_free_context);
        FF_REQ(hCodec,  avcodec_send_packet);
        FF_REQ(hCodec,  avcodec_receive_frame);
        FF_REQ(hCodec,  avcodec_flush_buffers);
        FF_REQ(hCodec,  av_packet_alloc);
        FF_REQ(hCodec,  av_packet_free);
        FF_REQ(hCodec,  av_packet_unref);

        FF_REQ(hFormat, avformat_open_input);
        FF_REQ(hFormat, avformat_find_stream_info);
        FF_REQ(hFormat, avformat_close_input);
        FF_REQ(hFormat, av_read_frame);
        FF_REQ(hFormat, av_seek_frame);
        FF_REQ(hFormat, av_find_input_format);

        FF_REQ(hScale,  sws_getContext);
        FF_REQ(hScale,  sws_scale);
        FF_REQ(hScale,  sws_freeContext);

        if (!ok) {
            dlclose(hScale); dlclose(hFormat); dlclose(hCodec); dlclose(hUtil);
            if (!g_loadError.empty()) g_loadError += "; ";
            g_loadError += std::string(fam.name) + ": missing " + missing;
            continue;
        }

        // Optional: libavdevice (device demuxers). register_all is a
        // no-op since FFmpeg 5.1 and may not be exported at all — never
        // a hard requirement.
#if defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
        void* hDevice = dlopen(fam.avdevice, RTLD_NOW | RTLD_LOCAL);
        if (hDevice) {
            bool devOk = resolve(hDevice, "avdevice_list_input_sources",
                                 avdevice_list_input_sources) &&
                         resolve(hDevice, "avdevice_free_list_devices",
                                 avdevice_free_list_devices);
            // Best-effort, legitimately absent on newer FFmpeg.
            resolve(hDevice, "avdevice_register_all", avdevice_register_all);
            if (devOk) {
                g_hasAvdevice = true;
                if (avdevice_register_all) avdevice_register_all();
            } else {
                dlclose(hDevice);
            }
        }
#endif

        g_available = true;
        g_loadError.clear();
        // Intentionally never dlclose — the handles live for the
        // process lifetime (matches what the dynamic linker would do).
        return;
    }
}

} // namespace

bool probe() {
    std::call_once(g_probeOnce, probeImpl);
    return g_available;
}

bool available() { return probe(); }
bool hasAvdevice() { return probe() && g_hasAvdevice; }

const std::string& loadError() {
    probe();
    return g_loadError;
}

#endif  // !_WIN32

} // namespace ff
} // namespace visual
} // namespace yawn

#endif  // YAWN_HAS_VIDEO
