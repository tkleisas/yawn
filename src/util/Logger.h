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

#include <cstdio>
#include <cstdarg>

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

    std::fprintf(dest, "[%s][%s] ", prefix, category);
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
