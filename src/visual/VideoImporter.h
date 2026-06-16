#pragma once

// VideoImporter — spawn ffmpeg in the background to transcode a source
// video into YAWN's canonical 640×360 all-intra MP4, and extract the
// audio stream (if present) to a sibling WAV. Works via fork+exec on
// POSIX — no shell interpretation, so paths with spaces are safe.
//
// Usage:
//   VideoImporter imp;
//   imp.start("/path/to/source.mp4", projectMediaDir);
//   // on each UI frame:
//   imp.poll();
//   switch (imp.state()) { ... }
//
// One importer instance drives one transcode at a time. For multiple
// concurrent imports, create separate VideoImporter instances.

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace yawn {
namespace visual {

class VideoImporter {
public:
    enum class State : int { Idle = 0, Running, Done, Failed };

    struct Result {
        std::string videoPath;     // transcoded .mp4 (absolute or project-relative)
        std::string audioPath;     // extracted .wav, empty if no audio stream
        std::string thumbnailPath; // 160x90 JPEG of the first decoded frame
        std::string sourcePath;    // the original source the user dropped
        double durationSeconds = 0.0; // source duration (ffprobe); 0 if unknown
    };

    VideoImporter() = default;
    ~VideoImporter();

    VideoImporter(const VideoImporter&) = delete;
    VideoImporter& operator=(const VideoImporter&) = delete;

    // Kick off the background transcode. Returns false if already running
    // or if inputs are invalid. mediaDir must exist (or be creatable).
    bool start(const std::string& sourcePath,
                const std::filesystem::path& mediaDir);

    // Blocking-free cleanup: when the worker finishes, joins the thread
    // and transitions the state to Done/Failed. Safe to call every frame.
    void poll();

    State state()             const { return m_state.load(); }
    const Result& result()    const { return m_result; }
    const std::string& error() const { return m_error; }
    // Fractional progress (0..1) of the current transcode pass. Returns 0
    // until the importer has figured out the source duration, then grows
    // as ffmpeg emits progress lines.
    float progress()          const { return m_progress.load(); }

    // Hash helper — deterministic short id from (path + size) so the same
    // source file consistently maps to the same transcoded asset.
    static std::string shortHash(const std::string& sourcePath);

private:
    std::atomic<State> m_state{State::Idle};
    std::thread        m_worker;
    std::atomic<bool>  m_workerFinished{false};
    std::atomic<float> m_progress{0.0f};
    Result             m_result;
    std::string        m_error;
};

// Spawn ffmpeg with the given argv (argv[0] = "ffmpeg"); blocks until it
// exits, returns true on success. Shared with the video exporter so it
// doesn't reimplement the platform process spawn.
bool runFFmpegCommand(const std::vector<std::string>& args);

// Streams raw frames straight into ffmpeg's stdin (rawvideo), so the video
// export encodes WHILE it renders — no intermediate PNG sequence and no
// separate encode pass. Cross-platform (POSIX pipe+fork / Win32 pipe).
//
//   FfmpegPipe p;
//   p.start({"ffmpeg","-f","rawvideo","-pixel_format","rgba",
//            "-video_size","640x360","-framerate","30","-i","-", ..., out});
//   for each frame: p.write(rgba, w*h*4);
//   bool ok = p.finish();   // close stdin, wait for ffmpeg
class FfmpegPipe {
public:
    FfmpegPipe() = default;
    ~FfmpegPipe();
    FfmpegPipe(const FfmpegPipe&)            = delete;
    FfmpegPipe& operator=(const FfmpegPipe&) = delete;

    // Spawn ffmpeg (args[0] == "ffmpeg") with stdin wired to a pipe. The args
    // must make ffmpeg read video from stdin (i.e. "-i", "-").
    bool start(const std::vector<std::string>& args);
    bool running() const { return m_started && !m_finished; }

    // Write one frame's worth of raw bytes to ffmpeg's stdin. Returns false
    // if the pipe broke (ffmpeg died early).
    bool write(const void* data, size_t bytes);

    // Close stdin and wait for ffmpeg to drain + exit. True on clean exit.
    bool finish();

private:
    bool     m_started  = false;
    bool     m_finished = false;
    intptr_t m_write    = -1;   // write fd (POSIX) / write HANDLE (Win32)
    intptr_t m_proc     = -1;   // pid (POSIX) / process HANDLE (Win32)
};

} // namespace visual
} // namespace yawn
