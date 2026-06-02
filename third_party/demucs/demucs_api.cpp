#include "demucs_api.h"
#include "demucs.hpp"

#include <fstream>
#include <thread>

namespace demucs {

const char* stemName(int index) {
    switch (index) {
        case 0: return "drums";
        case 1: return "bass";
        case 2: return "other";
        case 3: return "vocals";
        default: return "stem";
    }
}

bool separate(const float* left, const float* right, int numFrames,
              const std::string& modelPath, Stems& out,
              const Progress& progress, int numThreads) {
    if (!left || !right || numFrames <= 0) return false;

    // Read the ONNX model file into memory.
    std::ifstream f(modelPath, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<size_t>(sz));
    if (!f.read(data.data(), sz)) return false;

    // Session options — CPU, all cores, full graph optimization.
    Ort::SessionOptions opts;
    if (numThreads <= 0)
        numThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (numThreads < 1) numThreads = 1;
    opts.SetIntraOpNumThreads(numThreads);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    demucsonnx::demucs_model model;
    if (!demucsonnx::load_model(data, model, opts)) return false;

    // Build the 2 x N stereo input matrix.
    Eigen::MatrixXf audio(2, numFrames);
    for (int i = 0; i < numFrames; ++i) {
        audio(0, i) = left[i];
        audio(1, i) = right[i];
    }

    demucsonnx::ProgressCallback cb =
        [&progress](float p, const std::string& msg) -> bool {
            return progress ? progress(p, msg) : true;
        };

    Eigen::Tensor3dXf res = demucsonnx::demucs_inference(model, audio, cb);

    // Empty result = cancelled (or failed).
    if (res.dimension(0) == 0) return false;

    const int ns = static_cast<int>(res.dimension(0));   // 4 sources
    const int N  = static_cast<int>(res.dimension(2));    // == numFrames
    out.numFrames = N;
    const int copyN = std::min(ns, kNumStems);
    for (int s = 0; s < copyN; ++s) {
        out.stems[s].left.resize(N);
        out.stems[s].right.resize(N);
        for (int k = 0; k < N; ++k) {
            out.stems[s].left[k]  = res(s, 0, k);
            out.stems[s].right[k] = res(s, 1, k);
        }
    }
    return true;
}

} // namespace demucs
