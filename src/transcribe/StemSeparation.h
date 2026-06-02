#pragma once

// StemSeparation — Demucs v4 four-stem separation (drums/bass/other/vocals)
// via the `demucs` ONNX lib. The ~170 MB model is downloaded on demand
// (curl) into ~/.yawn/models/. Heavy (minutes on CPU) — separate() is meant
// to run on a worker thread; it reports progress and polls a cancel flag.
// When YAWN_HAS_STEM_SEPARATION is off, available() is false and separate()
// returns a failed result.

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace audio { class AudioBuffer; }
namespace transcribe {

// True when stem separation was compiled in.
bool stemSeparationAvailable();

// Absolute path to the cached model (~/.yawn/models/htdemucs_4s.onnx).
std::string stemModelPath();
// True if the model file is already present (and the expected size).
bool stemModelPresent();

struct StemOutput {
    // 4 stems as stereo buffers at the requested output sample rate,
    // order [drums, bass, other, vocals]; `names` parallels them.
    std::vector<std::shared_ptr<audio::AudioBuffer>> stems;
    std::vector<std::string> names;
    bool ok = false;
    bool cancelled = false;
    std::string error;
};

// Progress reporting: (phase, fraction 0..1). phase is "download" or
// "separate". Called from the worker thread — the callback must be
// thread-safe (e.g. store into atomics).
using StemProgress = std::function<void(const std::string& phase, float fraction)>;

// Ensures the model is downloaded, resamples `buffer` (any channel count,
// at `sampleRate`) to 44.1 kHz stereo, runs Demucs, and resamples the four
// stems back to `sampleRate`. Polls `cancel` between segments. Returns a
// StemOutput (ok=false on failure; cancelled=true if the user cancelled).
StemOutput separateStems(const audio::AudioBuffer& buffer, double sampleRate,
                         const StemProgress& progress, std::atomic<bool>& cancel);

} // namespace transcribe
} // namespace yawn
