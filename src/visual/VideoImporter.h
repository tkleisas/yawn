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
#include <filesystem>
#include <string>
#include <thread>

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

} // namespace visual
} // namespace yawn
