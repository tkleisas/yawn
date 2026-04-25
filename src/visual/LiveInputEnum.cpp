#include "visual/LiveInputEnum.h"
#include "util/Logger.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#if defined(_WIN32) && defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE
extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}
#endif

namespace yawn {
namespace visual {
namespace fs = std::filesystem;

#if defined(__linux__)

// Pull the human-readable device name out of sysfs. Falls back to an
// empty string on any failure — the UI still has the device node path
// as a label of last resort.
static std::string readSysfsName(const std::string& devNode) {
    // /dev/videoN → /sys/class/video4linux/videoN/name
    const char* prefix = "/dev/";
    if (devNode.compare(0, std::strlen(prefix), prefix) != 0) return {};
    std::string leaf = devNode.substr(std::strlen(prefix));
    fs::path namePath = fs::path("/sys/class/video4linux") / leaf / "name";
    std::ifstream f(namePath);
    if (!f) return {};
    std::string line;
    std::getline(f, line);
    // Strip trailing whitespace/newlines (common in sysfs reads).
    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r' ||
            line.back() == ' '  || line.back() == '\t')) {
        line.pop_back();
    }
    return line;
}

std::vector<LiveInputDevice> enumerateLiveInputDevices() {
    std::vector<LiveInputDevice> out;
    std::error_code ec;
    if (!fs::exists("/dev", ec)) return out;

    // Each kernel V4L2 capture device exposes a /dev/videoN node. Some
    // cameras publish multiple nodes (one capture, one metadata) — we
    // list them all and let the user sort it out; mis-pick just yields
    // a quick "no video stream" error.
    for (auto& entry : fs::directory_iterator("/dev", ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        if (name.compare(0, 5, "video") != 0) continue;
        // Reject non-numeric suffixes like "videoControl".
        bool numeric = name.size() > 5;
        for (size_t i = 5; i < name.size() && numeric; ++i) {
            if (name[i] < '0' || name[i] > '9') numeric = false;
        }
        if (!numeric) continue;

        LiveInputDevice d;
        d.url = "v4l2://" + entry.path().string();   // e.g. v4l2:///dev/video0
        std::string friendly = readSysfsName(entry.path().string());
        d.label = friendly.empty() ? entry.path().string()
                                    : (friendly + "  (" + entry.path().string() + ")");
        out.push_back(std::move(d));
    }

    // Stable order — /dev/video0 first, then video1, … — so the submenu
    // is deterministic across menu opens.
    std::sort(out.begin(), out.end(),
              [](const LiveInputDevice& a, const LiveInputDevice& b) {
                  return a.url < b.url;
              });
    return out;
}

#elif defined(_WIN32) && defined(YAWN_HAS_AVDEVICE) && YAWN_HAS_AVDEVICE

// Windows: enumerate DirectShow capture devices through libavdevice's
// avdevice_list_input_sources API. Each entry's device_name is what
// dshow expects in avformat_open_input ("video=NAME"); we wrap it in
// our standard dshow:// scheme so LiveVideoSource::start() routes
// through the existing libav path with no special-casing.
std::vector<LiveInputDevice> enumerateLiveInputDevices() {
    // Self-register libavdevice — the static init in VideoDecoder.cpp
    // may not fire if the linker drops that TU under /OPT:REF. Idempotent.
    static std::once_flag s_initOnce;
    std::call_once(s_initOnce, []{ avdevice_register_all(); });

    std::vector<LiveInputDevice> out;
    const AVInputFormat* fmt = av_find_input_format("dshow");
    if (!fmt) {
        LOG_INFO("LiveVideo", "DirectShow input format not registered");
        return out;
    }

    AVDeviceInfoList* list = nullptr;
    int n = avdevice_list_input_sources(fmt, nullptr, nullptr, &list);
    if (n < 0) {
        char errBuf[128] = {};
        av_strerror(n, errBuf, sizeof(errBuf));
        LOG_INFO("LiveVideo",
                  "avdevice_list_input_sources(dshow) failed: %s", errBuf);
        if (list) avdevice_free_list_devices(&list);
        return out;
    }
    if (!list) {
        LOG_INFO("LiveVideo", "avdevice_list_input_sources returned null list");
        return out;
    }

    LOG_INFO("LiveVideo", "DirectShow reports %d device(s)", list->nb_devices);

    for (int i = 0; i < list->nb_devices; ++i) {
        AVDeviceInfo* d = list->devices[i];
        if (!d) continue;
        // Skip audio-only devices — avdevice tags them in media_types.
        bool hasVideo = false;
        if (d->media_types && d->nb_media_types > 0) {
            for (int m = 0; m < d->nb_media_types; ++m) {
                if (d->media_types[m] == AVMEDIA_TYPE_VIDEO) {
                    hasVideo = true;
                    break;
                }
            }
        } else {
            // No media-type info — assume video so we don't drop
            // legitimate webcams on older ffmpeg builds.
            hasVideo = true;
        }
        if (!hasVideo) continue;

        const char* name = d->device_name ? d->device_name : "";
        const char* desc = d->device_description ? d->device_description : name;
        if (!name || !*name) continue;

        LiveInputDevice dev;
        dev.url   = std::string("dshow://video=") + name;
        dev.label = (desc && *desc) ? desc : name;
        out.push_back(std::move(dev));
    }
    avdevice_free_list_devices(&list);

    std::sort(out.begin(), out.end(),
              [](const LiveInputDevice& a, const LiveInputDevice& b) {
                  return a.label < b.label;
              });
    return out;
}

#else   // macOS / Windows-without-avdevice / other — stubbed for now

std::vector<LiveInputDevice> enumerateLiveInputDevices() {
#if defined(_WIN32)
  #if defined(YAWN_HAS_AVDEVICE)
    LOG_INFO("LiveVideo",
              "enum stub: _WIN32 set, YAWN_HAS_AVDEVICE=%d "
              "(needs to be 1 for dshow enum)", YAWN_HAS_AVDEVICE);
  #else
    LOG_INFO("LiveVideo",
              "enum stub: _WIN32 set, YAWN_HAS_AVDEVICE not defined "
              "(libavdevice not linked into this build)");
  #endif
#else
    LOG_INFO("LiveVideo", "enum stub: no platform impl");
#endif
    return {};
}

#endif

} // namespace visual
} // namespace yawn
