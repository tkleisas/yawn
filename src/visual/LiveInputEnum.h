#pragma once

// LiveInputEnum — best-effort enumeration of capture devices suitable
// as LiveVideoSource URLs.
//
// Intentionally shallow: we return just a short list of (url, label)
// pairs for the right-click submenu. Platform coverage:
//   • Linux   — globs /dev/video*, reads sysfs for friendly names.
//   • macOS   — not yet (returns empty; user still has the Custom URL
//                text prompt).
//   • Windows — not yet (same fallback).

#include <string>
#include <vector>

namespace yawn {
namespace visual {

struct LiveInputDevice {
    std::string url;    // ready to hand to LiveVideoSource::start()
    std::string label;  // human-readable (e.g. "HD Pro Webcam C920")
};

// Returns a fresh snapshot — cheap to call, so UI can call it on every
// menu open rather than caching.
std::vector<LiveInputDevice> enumerateLiveInputDevices();

} // namespace visual
} // namespace yawn
