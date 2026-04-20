#include "visual/VideoImporter.h"
#include "util/Logger.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
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

// ── Platform-specific child-process helpers ───────────────────────────────
//
// Three primitives drive the importer:
//   runFFmpeg(args)            — spawn, wait, return success flag
//   probeDuration(path)        — spawn ffprobe, read stdout, parse double
//   runFFmpegWithProgress(..)  — spawn, stream stdout line-by-line to cb
//
// POSIX uses fork+exec with pipe(); Windows uses CreateProcessW with
// CreatePipe. Identical signatures so start() below is platform-agnostic.

namespace {

#ifdef _WIN32

// UTF-8 ↔ UTF-16 conversion for the W-API boundary. CreateProcessW takes
// wchar_t*, but our args come through as UTF-8 std::string (from the
// JSON project files and user input).
std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                        static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                        static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

// Quote one argv element using the standard Windows command-line parsing
// rules (the ones CommandLineToArgvW reverses): wrap in "…", escape each
// internal " as \", and double any run of backslashes that immediately
// precedes a " or end-of-string. Essential for paths with spaces — the
// common case on Windows ("C:\Users\Foo Bar\clip.mp4").
std::string quoteArg(const std::string& in) {
    if (!in.empty()
        && in.find_first_of(" \t\"") == std::string::npos) {
        return in;
    }
    std::string out = "\"";
    for (size_t i = 0; i < in.size(); ) {
        size_t nb = 0;
        while (i < in.size() && in[i] == '\\') { ++nb; ++i; }
        if (i == in.size()) {
            // Trailing backslashes before closing quote: double them.
            out.append(nb * 2, '\\');
            break;
        }
        if (in[i] == '"') {
            out.append(nb * 2 + 1, '\\');
            out.push_back('"');
            ++i;
        } else {
            out.append(nb, '\\');
            out.push_back(in[i]);
            ++i;
        }
    }
    out.push_back('"');
    return out;
}

std::wstring buildCommandLine(const std::vector<std::string>& args) {
    std::string joined;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) joined.push_back(' ');
        joined += quoteArg(args[i]);
    }
    return toWide(joined);
}

// Resolve "ffmpeg" / "ffprobe" → absolute path when possible. Prefers
// the copy sitting next to YAWN.exe (shipped by the installer / CI
// package step) so the app is self-contained and doesn't depend on the
// user having a PATH-visible ffmpeg. Falls back to "ffmpeg.exe" which
// CreateProcessW will then resolve through PATH / App Paths.
std::string resolveBinary(const std::string& name) {
    // Already a concrete path or has an extension — take as-is.
    if (name.find('\\') != std::string::npos
     || name.find('/')  != std::string::npos
     || name.find('.')  != std::string::npos) {
        return name;
    }
    wchar_t exePath[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring p(exePath, n);
        size_t slash = p.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            std::wstring bundled = p.substr(0, slash + 1)
                                 + toWide(name) + L".exe";
            DWORD attr = GetFileAttributesW(bundled.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES
             && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                return toUtf8(bundled);
            }
        }
    }
    return name + ".exe";
}

// Thin handle bag — RAII closes everything on scope exit. The read pipe
// is non-null only when the caller asked for captured stdout.
struct ProcHandle {
    HANDLE hProcess  = nullptr;
    HANDLE hThread   = nullptr;
    HANDLE hReadPipe = nullptr;
    bool   ok() const { return hProcess != nullptr; }
    ~ProcHandle() {
        if (hReadPipe) CloseHandle(hReadPipe);
        if (hThread)   CloseHandle(hThread);
        if (hProcess)  CloseHandle(hProcess);
    }
};

// Launch ffmpeg/ffprobe. When captureStdout=true, the child's stdout
// AND stderr are merged into a single pipe (simpler — our progress
// parser harmlessly ignores non-`key=value` lines, and users rarely
// want ffmpeg's stderr on a GUI app anyway). Stdin is wired to NUL so
// USESTDHANDLES has three valid handles even when YAWN.exe was launched
// from Explorer with no console attached.
ProcHandle spawnChild(const std::vector<std::string>& args,
                      bool captureStdout) {
    ProcHandle h;
    if (args.empty()) return h;
    std::vector<std::string> a = args;
    a[0] = resolveBinary(a[0]);
    std::wstring cmd = buildCommandLine(a);
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    HANDLE rd = nullptr, wr = nullptr;
    HANDLE hNulIn = INVALID_HANDLE_VALUE;
    if (captureStdout) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) return h;
        // Parent's read end must NOT be inherited — otherwise the write
        // end stays alive through the child's copies and ReadFile never
        // sees EOF after the child exits.
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        hNulIn = CreateFileW(L"NUL", GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, nullptr);
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (captureStdout) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput  = hNulIn;
        si.hStdOutput = wr;
        si.hStdError  = wr;
    }

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(),
                             nullptr, nullptr,
                             /*bInheritHandles=*/captureStdout,
                             CREATE_NO_WINDOW,
                             nullptr, nullptr,
                             &si, &pi);
    if (captureStdout) {
        // Parent doesn't need the write end; close now so the child's
        // exit is the pipe's only remaining writer.
        if (wr)     CloseHandle(wr);
        if (hNulIn != INVALID_HANDLE_VALUE) CloseHandle(hNulIn);
    }
    if (!ok) {
        if (rd) CloseHandle(rd);
        std::fprintf(stderr, "[Video] CreateProcess(%s) failed: %lu\n",
                      args[0].c_str(), GetLastError());
        return h;
    }
    h.hProcess  = pi.hProcess;
    h.hThread   = pi.hThread;
    h.hReadPipe = rd;
    return h;
}

bool waitForChild(ProcHandle& h) {
    if (!h.hProcess) return false;
    WaitForSingleObject(h.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(h.hProcess, &code);
    return code == 0;
}

bool runFFmpegWithProgress(const std::vector<std::string>& args,
                             const std::function<void(const std::string&)>& onLine) {
    ProcHandle h = spawnChild(args, /*captureStdout=*/true);
    if (!h.ok()) return false;

    std::string partial;
    char buf[512];
    for (;;) {
        DWORD n = 0;
        BOOL ok = ReadFile(h.hReadPipe, buf, sizeof(buf), &n, nullptr);
        if (!ok || n == 0) {
            if (!partial.empty()) onLine(partial);
            break;
        }
        partial.append(buf, buf + n);
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            // Strip trailing \r from Windows line endings — ffmpeg's
            // progress output on Windows uses \r\n.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            onLine(line);
            partial.erase(0, pos + 1);
        }
    }
    return waitForChild(h);
}

bool runFFmpeg(const std::vector<std::string>& args) {
    // Reuse the captured-stdout path with a no-op callback so both
    // flavors share one code path. The extra pipe is cheap; the
    // alternative (leaving stdout/stderr inherited) blows up on
    // console-less GUI launches.
    return runFFmpegWithProgress(args, [](const std::string&){});
}

double probeDuration(const std::string& sourcePath) {
    std::vector<std::string> args = {
        "ffprobe", "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        sourcePath,
    };
    ProcHandle h = spawnChild(args, /*captureStdout=*/true);
    if (!h.ok()) return 0.0;
    std::string acc;
    char buf[128];
    for (;;) {
        DWORD n = 0;
        BOOL ok = ReadFile(h.hReadPipe, buf, sizeof(buf), &n, nullptr);
        if (!ok || n == 0) break;
        acc.append(buf, buf + n);
        if (acc.size() > 4096) break;  // safety: ffprobe's number is tiny
    }
    waitForChild(h);
    try { return std::stod(acc); } catch (...) { return 0.0; }
}

#else  // POSIX

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

#endif  // _WIN32

} // anon

// ── Public API ────────────────────────────────────────────────────────────
//
// Kick off a background transcode. The worker thread owns three ffmpeg
// passes: video (with progress), audio (best-effort), thumbnail (from
// the already-transcoded MP4). Cache-hit path avoids all of that when
// the target files already exist from a previous import.

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

} // namespace visual
} // namespace yawn
