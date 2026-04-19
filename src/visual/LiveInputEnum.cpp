#include "visual/LiveInputEnum.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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

#else   // macOS, Windows, other — stubbed for now

std::vector<LiveInputDevice> enumerateLiveInputDevices() {
    return {};
}

#endif

} // namespace visual
} // namespace yawn
