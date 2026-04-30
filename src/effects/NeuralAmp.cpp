// NeuralAmp.cpp — Neural Amp Modeler integration.
//
// This translation unit is the ONLY place in YAWN that includes
// NAM headers. It's compiled standalone with C++20 via
// set_source_files_properties in CMakeLists.txt; the rest of YAWN
// stays on C++17. NeuralAmp.h is C++17-clean (PIMPL forward decl).
//
// Build flag YAWN_HAS_NAM:
//   * ON  → real nam::DSP inference path.
//   * OFF → impl falls back to a gain-stage passthrough (the same
//           behaviour as Stage 1's shell). Lets the Neural Amp
//           device exist even on builds where NAM didn't fetch.

#include "NeuralAmp.h"
#include "util/Logger.h"

#include <cstring>
#include <vector>

#ifdef YAWN_HAS_NAM
#include <NAM/dsp.h>
#include <NAM/get_dsp.h>
#endif

namespace yawn {
namespace effects {

struct NeuralAmp::Impl {
    std::string modelPath;
    double      sampleRate     = 48000.0;
    int         maxBlockSize   = 256;

#ifdef YAWN_HAS_NAM
    // NAM's DSP base class. nam::get_dsp() returns a unique_ptr<DSP>
    // for any model type (WaveNet / LSTM / etc.); we don't need to
    // know the concrete class.
    std::unique_ptr<nam::DSP> dsp;

    // Working buffers for the NAM call. NAM expects per-channel
    // arrays of NAM_SAMPLE (= float here, since the nam target was
    // built with NAM_SAMPLE_FLOAT — see CMakeLists.txt). Pre-sized
    // at init() so process() does no heap work.
    std::vector<float>  monoIn;
    std::vector<float>  monoOut;
#endif
};

NeuralAmp::NeuralAmp() : m_impl(std::make_unique<Impl>()) {}
NeuralAmp::~NeuralAmp() = default;

void NeuralAmp::init(double sampleRate, int maxBlockSize) {
    m_sampleRate   = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_impl->sampleRate   = sampleRate;
    m_impl->maxBlockSize = maxBlockSize;
#ifdef YAWN_HAS_NAM
    m_impl->monoIn .assign(maxBlockSize, 0.0f);
    m_impl->monoOut.assign(maxBlockSize, 0.0f);
    if (m_impl->dsp) {
        // Existing model — re-prime for the new sample rate /
        // buffer size. Reset clears state without prewarming
        // (the prewarm settle pass would interrupt the audio
        // thread for many ms; we only call it on initial load).
        m_impl->dsp->Reset(sampleRate, maxBlockSize);
    }
#endif
    reset();
}

void NeuralAmp::reset() {
#ifdef YAWN_HAS_NAM
    if (m_impl->dsp)
        m_impl->dsp->Reset(m_impl->sampleRate, m_impl->maxBlockSize);
#endif
}

void NeuralAmp::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;

    const float inGainDb  = m_params[kInputGain];
    const float outGainDb = m_params[kOutputGain];
    const float mix       = std::clamp(m_params[kMix], 0.0f, 1.0f);
    const float inGain    = std::pow(10.0f, inGainDb  / 20.0f);
    const float outGain   = std::pow(10.0f, outGainDb / 20.0f);

    const bool stereo = (numChannels > 1);
    const float w = mix * m_mix;
    const float d = 1.0f - w;

#ifdef YAWN_HAS_NAM
    if (m_impl->dsp && numFrames <= m_impl->maxBlockSize) {
        // ── NAM inference path ──
        // Sum stereo to mono, apply input gain, hand to nam::DSP,
        // apply output gain, blend back into the buffer.
        for (int i = 0; i < numFrames; ++i) {
            const float l = buffer[i * numChannels];
            const float r = stereo ? buffer[i * numChannels + 1] : l;
            m_impl->monoIn[i] = (l + r) * 0.5f * inGain;
        }
        // NAM expects NAM_SAMPLE** — per-channel pointers. Mono = one
        // channel pointer. The library contract is "process num_frames
        // samples in-place across the channels"; we use separate in
        // and out buffers for safety.
        float* inCh  = m_impl->monoIn.data();
        float* outCh = m_impl->monoOut.data();
        m_impl->dsp->process(&inCh, &outCh, numFrames);

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;
            const float wet  = m_impl->monoOut[i] * outGain;
            buffer[i * numChannels] = dryL * d + wet * w;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * d + wet * w;
        }
        return;
    }
#endif

    // ── Fallback path: gain stage with mono sum ──
    // Hits when NAM isn't compiled in OR no model is loaded OR the
    // host block size exceeds our pre-sized buffers.
    for (int i = 0; i < numFrames; ++i) {
        const float dryL = buffer[i * numChannels];
        const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;
        const float pre  = (dryL + dryR) * 0.5f * inGain;
        const float wet  = pre * outGain;
        buffer[i * numChannels] = dryL * d + wet * w;
        if (stereo)
            buffer[i * numChannels + 1] = dryR * d + wet * w;
    }
}

void NeuralAmp::setModelPath(const std::string& path) {
    m_impl->modelPath = path;
    // Diagnostic — fires regardless of YAWN_HAS_NAM so the user
    // sees a log entry every time setModelPath is called. Useful
    // for narrowing "the panel shows the filename but the engine
    // is idle" reports — distinguishes "wasn't called", "was
    // called with bad path", "was called but YAWN_HAS_NAM is off",
    // and "was called and load threw".
    // Compile-time #ifdef flag captured into a runtime string so
    // we can pass it through LOG_INFO's variadic args (preprocessor
    // directives inside macro arguments are UB).
#ifdef YAWN_HAS_NAM
    const char* hasNamStr = "1";
#else
    const char* hasNamStr = "0";
#endif
    LOG_INFO("NeuralAmp", "setModelPath('%s') YAWN_HAS_NAM=%s",
             path.c_str(), hasNamStr);
#ifdef YAWN_HAS_NAM
    if (path.empty()) {
        m_impl->dsp.reset();
        return;
    }
    try {
        // nam::get_dsp returns std::unique_ptr<nam::DSP>. Throws
        // std::runtime_error on bad model files / unsupported
        // versions. We catch and clear so a bad load just leaves
        // the device in passthrough mode.
        LOG_INFO("NeuralAmp", "  → calling nam::get_dsp(path)");
        m_impl->dsp = nam::get_dsp(std::filesystem::path(path));
        LOG_INFO("NeuralAmp", "  ← nam::get_dsp returned (dsp=%p)",
                 static_cast<void*>(m_impl->dsp.get()));
        if (m_impl->dsp) {
            // Reset configures the model for the host's sample
            // rate + buffer size; prewarm settles initial network
            // state so the first user-played notes don't have a
            // model-warmup transient. Both are safe-but-slow
            // here on the UI / loader thread.
            m_impl->dsp->Reset(m_impl->sampleRate, m_impl->maxBlockSize);
            m_impl->dsp->prewarm();
            LOG_INFO("NeuralAmp", "Loaded NAM model: %s", path.c_str());
        } else {
            LOG_WARN("NeuralAmp",
                     "nam::get_dsp returned null for %s", path.c_str());
        }
    } catch (const std::exception& e) {
        LOG_WARN("NeuralAmp", "Failed to load %s: %s", path.c_str(), e.what());
        m_impl->dsp.reset();
    } catch (...) {
        // NAM may throw types other than std::exception (rare but
        // possible per its source). Catch-all so a non-standard
        // exception doesn't terminate the app.
        LOG_WARN("NeuralAmp", "Failed to load %s: unknown exception",
                 path.c_str());
        m_impl->dsp.reset();
    }
#endif
}

const std::string& NeuralAmp::modelPath() const { return m_impl->modelPath; }

bool NeuralAmp::hasModel() const {
#ifdef YAWN_HAS_NAM
    return m_impl->dsp != nullptr;
#else
    return false;
#endif
}

nlohmann::json NeuralAmp::saveExtraState(
        const std::filesystem::path& /*assetDir*/) const {
    nlohmann::json j;
    if (!m_impl->modelPath.empty()) j["modelPath"] = m_impl->modelPath;
    return j;
}

void NeuralAmp::loadExtraState(const nlohmann::json& state,
                                const std::filesystem::path& /*assetDir*/) {
    if (state.contains("modelPath"))
        setModelPath(state["modelPath"].get<std::string>());
}

} // namespace effects
} // namespace yawn
