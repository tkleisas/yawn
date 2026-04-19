#include "visual/VideoImporter.h"
#include "util/Logger.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <functional>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace yawn {
namespace visual {
namespace fs = std::filesystem;

// ── Hash helper ────────────────────────────────────────────────────────────
//
// Combine source path + file size via FNV-1a (64-bit). Good enough for
// cache dedup across projects — collisions are astronomically unlikely
// and never cause correctness issues, only duplicate work.

std::string VideoImporter::shortHash(const std::string& sourcePath) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            h ^= static_cast<uint8_t>(s[i]);
            h *= 1099511628211ULL;
        }
    };
    mix(sourcePath.data(), sourcePath.size());
    std::error_code ec;
    uintmax_t sz = fs::file_size(sourcePath, ec);
    if (!ec) {
        char buf[32];
        int n = std::snprintf(buf, sizeof(buf), "%ju", sz);
        mix(buf, static_cast<size_t>(n));
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016lx", static_cast<unsigned long>(h));
    return out;
}

// ── Background worker ─────────────────────────────────────────────────────

VideoImporter::~VideoImporter() {
    if (m_worker.joinable()) m_worker.join();
}

void VideoImporter::poll() {
    if (m_workerFinished.load() && m_worker.joinable()) {
        m_worker.join();
    }
}

#ifndef _WIN32
namespace {

// fork+exec ffmpeg with the given argv. Returns true if child exited
// with status 0. Child stderr is inherited from the parent so ffmpeg's
// warnings / errors show up in YAWN's terminal — crucial for
// debugging "ffmpeg failed" reports.
bool runFFmpeg(const std::vector<std::string>& args) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Child.
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        // execvp only returns on error — the most common being "command
        // not in $PATH", which deserves an explicit message.
        std::fprintf(stderr, "[Video] execvp(%s) failed: %s\n",
                      argv[0], std::strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Run ffprobe and read the source duration in seconds. Returns 0 on any
// failure (and the caller falls back to indeterminate progress).
double probeDuration(const std::string& sourcePath) {
    int pfd[2];
    if (pipe(pfd) < 0) return 0.0;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return 0.0; }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        const char* args[] = {
            "ffprobe", "-v", "error",
            "-show_entries", "format=duration",
            "-of", "default=noprint_wrappers=1:nokey=1",
            sourcePath.c_str(), nullptr
        };
        execvp("ffprobe", const_cast<char* const*>(args));
        _exit(127);
    }
    close(pfd[1]);
    char buf[128];
    ssize_t n = read(pfd[0], buf, sizeof(buf) - 1);
    close(pfd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (n <= 0) return 0.0;
    buf[n] = '\0';
    try { return std::stod(buf); } catch (...) { return 0.0; }
}

// Like runFFmpeg but intercepts stdout so we can parse -progress pipe:1
// output (one key=value per line). onLine fires for each full text line.
bool runFFmpegWithProgress(const std::vector<std::string>& args,
                             const std::function<void(const std::string&)>& onLine) {
    int pfd[2];
    if (pipe(pfd) < 0) return false;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return false; }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        std::fprintf(stderr, "[Video] execvp(%s) failed: %s\n",
                      argv[0], std::strerror(errno));
        _exit(127);
    }
    close(pfd[1]);

    std::string partial;
    char buf[512];
    while (true) {
        ssize_t n = read(pfd[0], buf, sizeof(buf));
        if (n > 0) {
            partial.append(buf, buf + n);
            size_t pos;
            while ((pos = partial.find('\n')) != std::string::npos) {
                onLine(partial.substr(0, pos));
                partial.erase(0, pos + 1);
            }
        } else if (n == 0) {
            if (!partial.empty()) onLine(partial);
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    close(pfd[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // anon

bool VideoImporter::start(const std::string& sourcePath,
                            const fs::path& mediaDir) {
    if (m_state.load() == State::Running) return false;
    if (!fs::exists(sourcePath)) {
        m_error = "source file does not exist";
        m_state = State::Failed;
        return false;
    }
    std::error_code ec;
    fs::create_directories(mediaDir, ec);

    const std::string id       = shortHash(sourcePath);
    const fs::path    videoOut = mediaDir / (id + ".mp4");
    const fs::path    audioOut = mediaDir / (id + ".wav");
    const fs::path    thumbOut = mediaDir / (id + "_thumb.jpg");

    m_result.sourcePath    = sourcePath;
    m_result.videoPath     = videoOut.string();
    m_result.audioPath.clear();
    m_result.thumbnailPath.clear();
    m_error.clear();

    // Cache hit — skip the transcode entirely.
    if (fs::exists(videoOut)) {
        if (fs::exists(audioOut)) m_result.audioPath = audioOut.string();
        if (fs::exists(thumbOut)) m_result.thumbnailPath = thumbOut.string();
        LOG_INFO("Video", "Import cache hit: %s", videoOut.string().c_str());
        m_state = State::Done;
        m_workerFinished = true;
        return true;
    }

    m_state = State::Running;
    m_workerFinished = false;
    m_progress.store(0.0f);

    // Launch background thread. Detach-and-poll pattern: worker sets
    // m_workerFinished; poll() joins.
    m_worker = std::thread([this, sourcePath, videoOut, audioOut, thumbOut]() {
        // Probe source duration so we can compute progress %. A zero
        // return leaves progress as indeterminate.
        const double dur = probeDuration(sourcePath);

        // Video pass: 640×360 @ 30 fps, all-I H.264, yuv420p, no audio.
        // Aspect-preserving fit: scale to fit inside 640×360, then pad
        // the remaining region with black. So a 4:3 source pillarboxes,
        // a vertical phone video gets thick side bars, 16:9 fills exactly.
        std::vector<std::string> vArgs = {
            "ffmpeg", "-y", "-hide_banner", "-loglevel", "warning",
            "-progress", "pipe:1",
            "-i", sourcePath,
            "-vf",
            "scale=640:360:force_original_aspect_ratio=decrease,"
            "pad=640:360:(ow-iw)/2:(oh-ih)/2,"
            "fps=30",
            "-c:v", "libx264",
            "-preset", "medium",
            "-crf", "20",
            "-g", "1", "-keyint_min", "1", "-sc_threshold", "0", "-bf", "0",
            "-pix_fmt", "yuv420p",
            "-an",
            videoOut.string()
        };
        bool vOK = runFFmpegWithProgress(vArgs,
            [this, dur](const std::string& line) {
                // Parse `key=value` progress lines. The relevant one is
                // `out_time_us=<microseconds>` — fractional progress is
                // that divided by the source duration.
                auto eq = line.find('=');
                if (eq == std::string::npos) return;
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                if (key == "out_time_us" && dur > 0.0) {
                    try {
                        double us = std::stod(val);
                        float p = static_cast<float>(us / (dur * 1e6));
                        if (p > 0.0f && p <= 1.0f) m_progress.store(p);
                    } catch (...) {}
                } else if (key == "progress" && val == "end") {
                    m_progress.store(1.0f);
                }
            });

        // Audio pass (optional — skip silently if source has no audio).
        // We run it as a separate invocation so a missing audio stream
        // just produces no .wav rather than failing the whole import.
        bool hasAudio = false;
        if (vOK) {
            std::vector<std::string> aArgs = {
                "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
                "-i", sourcePath,
                "-vn",
                "-c:a", "pcm_s16le",
                "-ar", "44100",
                "-ac", "2",
                audioOut.string()
            };
            hasAudio = runFFmpeg(aArgs) && fs::exists(audioOut);
        }

        // Thumbnail pass — grab the first frame of the already-transcoded
        // MP4. Cheap and deterministic since every frame is a keyframe.
        bool hasThumb = false;
        if (vOK) {
            std::vector<std::string> tArgs = {
                "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
                "-i", videoOut.string(),
                "-vframes", "1",
                "-vf", "scale=160:90",
                "-q:v", "5",
                thumbOut.string()
            };
            hasThumb = runFFmpeg(tArgs) && fs::exists(thumbOut);
        }

        if (vOK) {
            m_result.audioPath     = hasAudio ? audioOut.string() : std::string();
            m_result.thumbnailPath = hasThumb ? thumbOut.string() : std::string();
            m_state = State::Done;
            LOG_INFO("Video", "Import done: %s (audio=%s, thumb=%s)",
                     videoOut.string().c_str(),
                     hasAudio ? "yes" : "no",
                     hasThumb ? "yes" : "no");
        } else {
            m_error = "ffmpeg failed";
            m_state = State::Failed;
            LOG_ERROR("Video", "Import failed: %s", sourcePath.c_str());
        }
        m_workerFinished.store(true);
    });
    return true;
}

#else  // _WIN32 — stub that reports the feature isn't available here yet

bool VideoImporter::start(const std::string& sourcePath,
                            const fs::path& /*mediaDir*/) {
    m_result.sourcePath = sourcePath;
    m_error  = "video import not implemented on Windows yet";
    m_state  = State::Failed;
    m_workerFinished = true;
    return false;
}

#endif  // _WIN32

} // namespace visual
} // namespace yawn
