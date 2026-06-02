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

#ifdef YAWN_HAS_STEM_SEPARATION
#include "demucs_api.h"   // demucs::separate (third_party/demucs)
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

// Linear resample a single channel from inRate to outRate.
std::vector<float> resampleLinear(const std::vector<float>& in,
                                  double inRate, double outRate) {
    if (in.empty() || inRate <= 0 || outRate <= 0) return {};
    if (std::abs(inRate - outRate) < 1e-6) return in;
    const double ratio = inRate / outRate;             // in samples per out sample
    const int inN = static_cast<int>(in.size());
    const int outN = static_cast<int>(std::floor(in.size() / ratio));
    std::vector<float> out(std::max(0, outN));
    for (int i = 0; i < outN; ++i) {
        const double pos = i * ratio;
        const int i0 = static_cast<int>(pos);
        const int i1 = std::min(i0 + 1, inN - 1);
        const float frac = static_cast<float>(pos - i0);
        out[i] = in[i0] + (in[i1] - in[i0]) * frac;
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

// Download the model with curl (no HTTP client in yawn). curl ships on
// Linux + Windows 10+. Runs curl in a side thread while we poll the
// partial file size for a progress %; not cancellable mid-download (it's
// a one-time fetch). Returns true on success.
bool ensureModel(const StemProgress& progress) {
    if (stemModelPresent()) return true;

    std::error_code ec;
    std::filesystem::create_directories(modelDir(), ec);
    const std::string dest = stemModelPath();
    const std::string tmp  = dest + ".part";
    std::filesystem::remove(tmp, ec);

    // -L follow redirects, -f fail on HTTP error, -s silent (we show our
    // own progress), -o output.
    const std::string cmd =
        "curl -L -f -s -o \"" + tmp + "\" \"" + std::string(kModelUrl) + "\"";

    std::atomic<bool> done{false};
    std::atomic<int>  rc{-1};
    std::thread dl([&]() { rc.store(std::system(cmd.c_str())); done.store(true); });

    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        std::error_code e2;
        const auto sz = std::filesystem::file_size(tmp, e2);
        const float frac = e2 ? 0.0f
                              : std::min(1.0f, static_cast<float>(sz) /
                                                   static_cast<float>(kModelSize));
        if (progress) progress("download", frac);
    }
    dl.join();

    if (rc.load() != 0) {
        std::filesystem::remove(tmp, ec);
        LOG_ERROR("Stems", "Model download failed (curl rc=%d). Is curl installed?",
                  rc.load());
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

    if (!ensureModel(progress)) {
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
    std::vector<float> inL = resampleLinear(srcL, sampleRate, modelRate);
    std::vector<float> inR = resampleLinear(srcR, sampleRate, modelRate);
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
            resampleLinear(stems.stems[s].left, modelRate, sampleRate);
        std::vector<float> outR =
            resampleLinear(stems.stems[s].right, modelRate, sampleRate);
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
