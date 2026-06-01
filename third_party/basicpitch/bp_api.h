#ifndef BASIC_PITCH_API_H
#define BASIC_PITCH_API_H

// bp_api — plain-struct facade over the Basic Pitch C++ port. Keeps Eigen
// + ONNX Runtime (and the C++20 bits) confined to the `basicpitch` static
// library so the rest of YAWN (C++17) can transcribe audio→notes via a
// dependency-free header. Mirrors the NeuralAmp PIMPL split.

#include <vector>

namespace basic_pitch {

// One transcribed note. Times are in seconds from the start of the input
// audio; amplitude is 0..1 (maps to velocity).
struct Note {
    float startSec  = 0.0f;
    float endSec    = 0.0f;
    int   pitch     = 60;     // MIDI note number
    float amplitude = 0.0f;   // 0..1
};

// The model's required input sample rate. Callers must resample to this
// and pass a mono buffer.
constexpr int kInputSampleRate = 22050;

// Run Basic Pitch on a mono buffer already resampled to kInputSampleRate.
// Returns notes sorted by onset (empty on null/empty input). Polyphonic;
// pitch bends are not emitted (MVP). `useMelodiaTrick` enables the
// note-continuation heuristic from the original model.
std::vector<Note> transcribe(const float* mono22050, int length,
                             bool useMelodiaTrick = true);

} // namespace basic_pitch

#endif // BASIC_PITCH_API_H
