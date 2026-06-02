#include "transcribe/StemSeparation.h"

#include "audio/AudioBuffer.h"
#include "util/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>

#ifdef YAWN_HAS_STEM_SEPARATION
#include "demucs_api.h"   // demucs::separate (third_party/demucs)
// Process spawn/kill for the cancellable curl download.
#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

namespace yawn {
namespace transcribe {

namespace {
// GitHub release asset — see `models-v1` on the YAWN repo.
constexpr const char* kModelUrl =
    "https://github.com/tkleisas/yawn/releases/download/models-v1/htdemucs_4s.onnx";
constexpr std::uintmax_t kModelSize = 170681491;  // bytes, for an integrity check

std::filesystem::path homeDir() {
    if (const char* h = std::getenv("HOME"); h && *h) return h;        // Linux/macOS
    if (const char* u = std::getenv("USERPROFILE"); u && *u) return u;  // Windows
    return std::filesystem::temp_directory_path();
}

std::filesystem::path modelDir() { return homeDir() / ".yawn" / "models"; }

// Windowed-sinc (Lanczos) resampler with downsample anti-aliasing. The
// lowpass cutoff tracks the lower of the two Nyquists, so downsampling
// (e.g. 48k → 44.1k) doesn't alias; quality is well above plain linear.
std::vector<float> resample(const std::vector<float>& in,
                            double inRate, double outRate) {
    if (in.empty() || inRate <= 0 || outRate <= 0) return {};
    if (std::abs(inRate - outRate) < 1e-6) return in;

    constexpr double kPi = 3.14159265358979323846;     // (M_PI isn't portable on MSVC)
    constexpr int    lobes = 8;                          // Lanczos lobes → quality/cost
    const int    inN   = static_cast<int>(in.size());
    const double ratio = inRate / outRate;               // input samples per output sample
    const int    outN  = static_cast<int>(std::floor(in.size() / ratio));
    if (outN <= 0) return {};

    // Cutoff in cycles per input-sample: input Nyquist when upsampling,
    // the (lower) output Nyquist when downsampling.
    const double cutoff    = std::min(1.0, outRate / inRate);
    const double halfWidth = lobes / cutoff;             // kernel half-width, input samples

    auto sinc = [kPi](double x) {
        return x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
    };

    std::vector<float> out(outN, 0.0f);
    for (int j = 0; j < outN; ++j) {
        const double center = j * ratio;                 // position in input samples
        const int i0 = static_cast<int>(std::ceil(center - halfWidth));
        const int i1 = static_cast<int>(std::floor(center + halfWidth));
        double acc = 0.0, wsum = 0.0;
        for (int i = i0; i <= i1; ++i) {
            const double t = center - i;                 // distance, input samples
            const double w = sinc(cutoff * t) * sinc(t / halfWidth);  // lowpass × Lanczos window
            const int idx = std::clamp(i, 0, inN - 1);
            acc  += in[idx] * w;
            wsum += w;
        }
        out[j] = static_cast<float>(wsum != 0.0 ? acc / wsum : 0.0);
    }
    return out;
}
} // namespace

bool stemSeparationAvailable() {
#ifdef YAWN_HAS_STEM_SEPARATION
    return true;
#else
    return false;
#endif
}

std::string stemModelPath() {
    return (modelDir() / "htdemucs_4s.onnx").string();
}

bool stemModelPresent() {
    std::error_code ec;
    const auto p = stemModelPath();
    if (!std::filesystem::exists(p, ec)) return false;
    return std::filesystem::file_size(p, ec) == kModelSize;
}

#ifdef YAWN_HAS_STEM_SEPARATION
namespace {

// Download the model with curl (no HTTP client in yawn; curl ships on
// Linux + Windows 10+). Spawns curl as a *killable child process* so the
// download is cancellable mid-flight, polling `cancel` + reporting a
// progress % from the partial file size. Returns true on success; on
// cancel returns false with `cancel` already set (caller maps to cancelled).
bool ensureModel(const StemProgress& progress, std::atomic<bool>& cancel) {
    if (stemModelPresent()) return true;

    std::error_code ec;
    std::filesystem::create_directories(modelDir(), ec);
    const std::string dest = stemModelPath();
    const std::string tmp  = dest + ".part";
    std::filesystem::remove(tmp, ec);

    auto pollProgress = [&]() {
        std::error_code e2;
        const auto sz = std::filesystem::file_size(tmp, e2);
        const float frac = e2 ? 0.0f
                              : std::min(1.0f, static_cast<float>(sz) /
                                                   static_cast<float>(kModelSize));
        if (progress) progress("download", frac);
    };

    bool cancelled = false;
    bool success   = false;

#ifdef _WIN32
    // -L follow redirects, -f fail on HTTP error, -s silent.
    std::string cmd = "curl -L -f -s -o \"" + tmp + "\" \"" +
                      std::string(kModelUrl) + "\"";
    std::vector<char> cmdbuf(cmd.begin(), cmd.end());
    cmdbuf.push_back('\0');
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        LOG_ERROR("Stems", "Failed to launch curl. Is curl on PATH?");
        return false;
    }
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
        if (cancel.load()) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            cancelled = true;
            break;
        }
        pollProgress();
        Sleep(250);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    success = !cancelled && code == 0;
#else
    const pid_t pid = fork();
    if (pid == 0) {
        // child: exec curl silently.
        execlp("curl", "curl", "-L", "-f", "-s", "-o", tmp.c_str(),
               kModelUrl, static_cast<char*>(nullptr));
        _exit(127);  // exec failed (curl not found)
    }
    if (pid < 0) { LOG_ERROR("Stems", "fork failed for curl"); return false; }
    int status = 0;
    for (;;) {
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid || r < 0) break;
        if (cancel.load()) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            cancelled = true;
            break;
        }
        pollProgress();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    success = !cancelled && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif

    if (cancelled) { std::filesystem::remove(tmp, ec); return false; }
    if (!success) {
        std::filesystem::remove(tmp, ec);
        LOG_ERROR("Stems", "Model download failed (curl). Is curl installed + reachable?");
        return false;
    }
    // Integrity: size must match the known asset.
    if (std::filesystem::file_size(tmp, ec) != kModelSize) {
        std::filesystem::remove(tmp, ec);
        LOG_ERROR("Stems", "Downloaded model has unexpected size — discarding.");
        return false;
    }
    std::filesystem::rename(tmp, dest, ec);
    if (ec) { std::filesystem::remove(tmp, ec); return false; }
    LOG_INFO("Stems", "Model downloaded to %s", dest.c_str());
    return true;
}

} // namespace
#endif // YAWN_HAS_STEM_SEPARATION

StemOutput separateStems(const audio::AudioBuffer& buffer, double sampleRate,
                         const StemProgress& progress, std::atomic<bool>& cancel) {
    StemOutput result;
#ifdef YAWN_HAS_STEM_SEPARATION
    const int nCh = buffer.numChannels();
    const int nFr = buffer.numFrames();
    if (nCh <= 0 || nFr <= 0 || sampleRate <= 0) {
        result.error = "empty audio";
        return result;
    }

    if (!ensureModel(progress, cancel)) {
        if (cancel.load()) { result.cancelled = true; }
        else result.error = "model download failed";
        return result;
    }
    if (cancel.load()) { result.cancelled = true; return result; }

    // Build stereo channels at the source rate, then resample to 44.1 kHz.
    std::vector<float> srcL(nFr), srcR(nFr);
    for (int i = 0; i < nFr; ++i) {
        srcL[i] = buffer.sample(0, i);
        srcR[i] = buffer.sample(nCh > 1 ? 1 : 0, i);
    }
    const double modelRate = static_cast<double>(demucs::kSampleRate);
    std::vector<float> inL = resample(srcL, sampleRate, modelRate);
    std::vector<float> inR = resample(srcR, sampleRate, modelRate);
    const int inN = static_cast<int>(std::min(inL.size(), inR.size()));
    if (inN <= 0) { result.error = "resample failed"; return result; }

    demucs::Stems stems;
    auto cb = [&progress, &cancel](float frac, const std::string&) -> bool {
        if (progress) progress("separate", frac);
        return !cancel.load();   // false → demucs cancels
    };
    const bool ok = demucs::separate(inL.data(), inR.data(), inN,
                                     stemModelPath(), stems, cb);
    if (!ok) {
        if (cancel.load()) result.cancelled = true;
        else result.error = "inference failed";
        return result;
    }

    // Resample each stem back to the source rate and pack into AudioBuffers.
    for (int s = 0; s < demucs::kNumStems; ++s) {
        std::vector<float> outL =
            resample(stems.stems[s].left, modelRate, sampleRate);
        std::vector<float> outR =
            resample(stems.stems[s].right, modelRate, sampleRate);
        const int outN = static_cast<int>(std::min(outL.size(), outR.size()));
        auto buf = std::make_shared<audio::AudioBuffer>(2, std::max(1, outN));
        float* dl = buf->channelData(0);
        float* dr = buf->channelData(1);
        for (int i = 0; i < outN; ++i) { dl[i] = outL[i]; dr[i] = outR[i]; }
        result.stems.push_back(std::move(buf));
        result.names.emplace_back(demucs::stemName(s));
    }
    result.ok = true;
    return result;
#else
    (void)buffer; (void)sampleRate; (void)progress; (void)cancel;
    result.error = "stem separation not built";
    return result;
#endif
}

} // namespace transcribe
} // namespace yawn
