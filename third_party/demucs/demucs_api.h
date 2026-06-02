#ifndef DEMUCS_API_H
#define DEMUCS_API_H

// demucs_api — plain-struct facade over the Demucs v4 ONNX inference.
// Keeps Eigen + ONNX Runtime confined to the `demucs` static library so
// the rest of YAWN (which never includes demucs.hpp) stays clean. Mirrors
// the basicpitch / NeuralAmp PIMPL split.

#include <functional>
#include <string>
#include <vector>

namespace demucs {

constexpr int kSampleRate = 44100;  // model input/output rate (stereo)
constexpr int kNumStems   = 4;      // drums, bass, other, vocals

// One separated stem: stereo, planar, at kSampleRate, `numFrames` long.
struct Stem {
    std::vector<float> left;
    std::vector<float> right;
};

// Result: 4 stems in fixed order [drums, bass, other, vocals].
struct Stems {
    Stem stems[kNumStems];
    int  numFrames = 0;
};

// Human-readable stem name for index 0..3.
const char* stemName(int index);

// Progress callback: (fraction 0..1, message). Return false to cancel —
// inference bails out early and separate() returns false.
using Progress = std::function<bool(float, const std::string&)>;

// Separate stereo audio (planar left/right, `numFrames` samples each, at
// kSampleRate) using the ONNX model at `modelPath`. Fills `out` and
// returns true on success; false on load failure, empty input, or cancel.
// numThreads <= 0 → use all hardware threads. Runs synchronously (call
// from a worker thread; it takes minutes on CPU).
bool separate(const float* left, const float* right, int numFrames,
              const std::string& modelPath, Stems& out,
              const Progress& progress, int numThreads = 0);

} // namespace demucs

#endif // DEMUCS_API_H
