#pragma once

// LiveVideoSource — sibling of VideoDecoder for realtime input (webcam,
// RTSP, HTTP streams, anything libav can demux).
//
// Differences from VideoDecoder:
//   • No seek, no duration, no fixed frame count. Frames flow at the
//     device's own clock.
//   • Dedicated decode thread pushes completed RGBA frames into a
//     double-buffered "ready" slot guarded by a mutex. UI thread pulls
//     the latest frame via latestFrame(); old frames are simply
//     overwritten on overrun (drop-frames-on-overrun).
//   • Scales whatever resolution the device produces to a fixed
//     640×360 RGBA output to match the rest of the visual engine.
//
// URL handling: a few YAWN-specific prefixes resolve to libavdevice
// input formats explicitly (libav won't always auto-detect):
//   v4l2://<path>        → input fmt "v4l2",        filename <path>
//   dshow://<path>       → input fmt "dshow",       filename <path>
//   avfoundation://<path>→ input fmt "avfoundation",filename <path>
// Anything else (rtsp://, http://, a plain path, …) is passed to libav
// as-is.
//
// Thread-safety: start()/stop()/state()/latestFrame() are all safe to
// call from the UI thread. The decode thread never touches those —
// it only writes into the mutex-protected frame buffer + atomics.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yawn {
namespace visual {

// Split a YAWN live-input URL into an optional input-format hint and
// the libav filename it should be passed with. Header-inline so tests
// can exercise it without pulling libav into the link.
struct LiveUrlParts {
    std::string inputFormat;  // empty = autodetect
    std::string filename;     // what to hand to avformat_open_input
};

inline LiveUrlParts parseLiveUrl(const std::string& url) {
    LiveUrlParts out;
    struct Prefix { const char* scheme; const char* fmt; };
    static constexpr Prefix kPrefixes[] = {
        {"v4l2://",          "v4l2"},
        {"dshow://",         "dshow"},
        {"avfoundation://",  "avfoundation"},
    };
    for (const auto& p : kPrefixes) {
        size_t plen = std::strlen(p.scheme);
        if (url.size() >= plen && url.compare(0, plen, p.scheme) == 0) {
            out.inputFormat = p.fmt;
            out.filename    = url.substr(plen);
            return out;
        }
    }
    out.filename = url;
    return out;
}

class LiveVideoSource {
public:
    static constexpr int kWidth  = 640;
    static constexpr int kHeight = 360;

    enum class State { Stopped, Connecting, Connected, Failed };

    LiveVideoSource() = default;
    ~LiveVideoSource();

    LiveVideoSource(const LiveVideoSource&) = delete;
    LiveVideoSource& operator=(const LiveVideoSource&) = delete;

    // Launch the decode thread. Returns true immediately on thread
    // launch; whether the URL actually opened is reflected in state().
    // Calling start() again stops the previous session first.
    bool start(const std::string& url);
    void stop();

    State state() const { return m_state.load(); }
    std::string error() const;
    std::string url() const;

    // Copy the newest complete frame into outBuf (kWidth*kHeight*4
    // bytes). Returns true if the frame was new since the last call
    // (seq advanced), false if nothing fresh is available.
    bool latestFrame(uint8_t* outBuf);

private:
    void threadMain(std::string url);

    std::thread           m_thread;
    std::atomic<bool>     m_stopFlag{false};
    std::atomic<State>    m_state{State::Stopped};

    mutable std::mutex    m_bufMutex;
    std::vector<uint8_t>  m_readyBuf;       // kWidth*kHeight*4 once filled

    std::atomic<uint64_t> m_frameSeq{0};
    uint64_t              m_lastReadSeq = 0;

    mutable std::mutex    m_metaMutex;
    std::string           m_error;
    std::string           m_url;
};

} // namespace visual
} // namespace yawn
