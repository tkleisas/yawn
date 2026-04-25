#pragma once
// Simple logging macros for Y.A.W.N development.
//
// Usage:
//   LOG_INFO("Audio", "Engine started at %.0f Hz", sampleRate);
//   LOG_WARN("MIDI",  "No input devices found");
//   LOG_ERROR("UI",   "Failed to load font: %s", path);
//   LOG_DEBUG("Audio", "Buffer processed: %d frames", nf);
//
// Categories are free-form strings. Common ones:
//   Audio, MIDI, UI, Project, File, App

#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace yawn {
namespace log {

enum class Level { Debug, Info, Warn, Error };

// Runtime log level — messages below this are suppressed.
// Default: Info (Debug messages hidden unless enabled).
inline Level& minLevel() {
    static Level level = Level::Info;
    return level;
}

inline void setMinLevel(Level level) { minLevel() = level; }

inline void log(Level level, const char* category, const char* fmt, ...) {
    if (level < minLevel()) return;

    const char* prefix = "";
    FILE* dest = stdout;

    switch (level) {
        case Level::Debug: prefix = "DEBUG"; break;
        case Level::Info:  prefix = "INFO";  break;
        case Level::Warn:  prefix = "WARN";  dest = stderr; break;
        case Level::Error: prefix = "ERROR"; dest = stderr; break;
    }

    // HH:MM:SS.mmm timestamp — millisecond precision is enough to
    // pin down per-frame loops (16ms @ 60fps) without flooding the
    // line with seldom-changing date fields. localtime so the user
    // can match against wall-clock memory of "I clicked at 22:43".
    using namespace std::chrono;
    const auto now   = system_clock::now();
    const auto secs  = time_point_cast<seconds>(now);
    const auto ms    = duration_cast<milliseconds>(now - secs).count();
    const std::time_t tt = system_clock::to_time_t(secs);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    std::fprintf(dest, "%02d:%02d:%02d.%03lld [%s][%s] ",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  static_cast<long long>(ms),
                  prefix, category);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(dest, fmt, args);
    va_end(args);
    std::fprintf(dest, "\n");
}

} // namespace log
} // namespace yawn

#define LOG_DEBUG(cat, ...) ::yawn::log::log(::yawn::log::Level::Debug, cat, __VA_ARGS__)
#define LOG_INFO(cat, ...)  ::yawn::log::log(::yawn::log::Level::Info,  cat, __VA_ARGS__)
#define LOG_WARN(cat, ...)  ::yawn::log::log(::yawn::log::Level::Warn,  cat, __VA_ARGS__)
#define LOG_ERROR(cat, ...) ::yawn::log::log(::yawn::log::Level::Error, cat, __VA_ARGS__)
