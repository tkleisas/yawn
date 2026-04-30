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

#include <atomic>
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
    // RCU-lite atomic pointer swap for the active DSP. Background:
    // user reported a use-after-free crash on the third .nam load.
    // The audio thread does `m_impl->dsp->process(...)` ~100x per
    // second; the UI thread does `m_impl->dsp.reset()` then
    // `m_impl->dsp = nam::get_dsp(...)` which can take 10–100 ms
    // (especially the prewarm step). Without synchronisation, an
    // audio block landing mid-load dereferences a freed DSP →
    // 0x... write-AV in the heap.
    //
    // Fix: dspPtr is atomic. Audio thread acquire-loads it, calls
    // process() on whatever it sees. UI thread builds the new DSP
    // in a local unique_ptr, then atomic-exchanges the raw pointer
    // out of dspPtr and PARKS the previous DSP in retiredDsps —
    // not destroyed yet. Destruction happens on the NEXT load (or
    // device destruction), by which time the audio thread has long
    // since moved on to the new pointer. No mutex on the audio
    // path, no torn reads, no racy delete.
    //
    // Trade-off: each surviving load leaks the previous DSP for
    // one load cycle. That's the cost of doing it lock-free. For
    // a device that loads models every few seconds at most, fine.
    std::atomic<nam::DSP*>                 dspPtr{nullptr};
    std::vector<std::unique_ptr<nam::DSP>> retiredDsps;

    // Working buffers for the NAM call. NAM expects per-channel
    // arrays of NAM_SAMPLE (= float here, since the nam target was
    // built with NAM_SAMPLE_FLOAT — see CMakeLists.txt). Pre-sized
    // at init() so process() does no heap work.
    std::vector<float>  monoIn;
    std::vector<float>  monoOut;
#endif
};

NeuralAmp::NeuralAmp() : m_impl(std::make_unique<Impl>()) {}
NeuralAmp::~NeuralAmp() {
#ifdef YAWN_HAS_NAM
    // Final cleanup of the active dspPtr — at device destruction
    // the audio thread is guaranteed not to be calling process()
    // anymore (the engine has stopped). retiredDsps cleans up
    // automatically as Impl destructs.
    if (nam::DSP* p = m_impl->dspPtr.exchange(nullptr))
        delete p;
#endif
}

void NeuralAmp::init(double sampleRate, int maxBlockSize) {
    m_sampleRate   = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_impl->sampleRate   = sampleRate;
    m_impl->maxBlockSize = maxBlockSize;
#ifdef YAWN_HAS_NAM
    m_impl->monoIn .assign(maxBlockSize, 0.0f);
    m_impl->monoOut.assign(maxBlockSize, 0.0f);
    if (nam::DSP* dsp = m_impl->dspPtr.load(std::memory_order_acquire)) {
        // Existing model — re-prime for the new sample rate /
        // buffer size. Reset clears state without prewarming
        // (the prewarm settle pass would interrupt the audio
        // thread for many ms; we only call it on initial load).
        dsp->Reset(sampleRate, maxBlockSize);
    }
#endif
    reset();
}

void NeuralAmp::reset() {
#ifdef YAWN_HAS_NAM
    if (nam::DSP* dsp = m_impl->dspPtr.load(std::memory_order_acquire))
        dsp->Reset(m_impl->sampleRate, m_impl->maxBlockSize);
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
    // Single atomic-acquire load of the DSP pointer per process()
    // call. Whatever the UI thread does to dspPtr after this load
    // doesn't affect THIS audio block — we hold a stable raw
    // pointer for the duration of the call. The pointer's
    // lifetime is guaranteed by the RCU-lite scheme: the previous
    // DSP gets parked in retiredDsps on swap and only destroyed at
    // the NEXT load, which can't happen until the current one
    // returns control to the UI thread. So `dsp` is safe to
    // dereference for the duration of this process() call.
    nam::DSP* dsp = m_impl->dspPtr.load(std::memory_order_acquire);
    if (dsp && numFrames <= m_impl->maxBlockSize) {
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
        dsp->process(&inCh, &outCh, numFrames);

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
    // Drain retired DSPs from previous loads. Safe at this point —
    // any setModelPath after the first happens with the audio
    // thread already long since switched over to the new pointer
    // (see RCU-lite comment in Impl). Doing the cleanup at the
    // START of this load (rather than mid-process) keeps the audio
    // path completely free of allocator work.
    m_impl->retiredDsps.clear();

    auto retireOldDsp = [this]() {
        // Atomic-exchange dspPtr for null and park whatever was
        // there in retiredDsps. The audio thread might still be
        // mid-call on it for one more block — that's fine; the
        // retired DSP isn't actually destroyed until the NEXT
        // setModelPath call.
        if (nam::DSP* old = m_impl->dspPtr.exchange(nullptr,
                std::memory_order_acq_rel)) {
            m_impl->retiredDsps.emplace_back(old);
        }
    };

    if (path.empty()) {
        retireOldDsp();
        return;
    }
    try {
        // nam::get_dsp returns std::unique_ptr<nam::DSP>. Throws
        // std::runtime_error on bad model files / unsupported
        // versions. We catch and clear so a bad load just leaves
        // the device in passthrough mode.
        LOG_INFO("NeuralAmp", "  → calling nam::get_dsp(path)");
        std::unique_ptr<nam::DSP> newDsp =
            nam::get_dsp(std::filesystem::path(path));
        LOG_INFO("NeuralAmp", "  ← nam::get_dsp returned (dsp=%p)",
                 static_cast<void*>(newDsp.get()));
        if (newDsp) {
            // Reset configures the model for the host's sample
            // rate + buffer size; prewarm settles initial network
            // state so the first user-played notes don't have a
            // model-warmup transient. Both are safe-but-slow here
            // on the UI / loader thread — happens BEFORE we
            // publish the new pointer to the audio thread.
            newDsp->Reset(m_impl->sampleRate, m_impl->maxBlockSize);
            newDsp->prewarm();

            // Atomic publish: swap the new fully-prepared DSP into
            // dspPtr. Audio thread will pick it up on its next
            // process() call. The previous DSP (if any) goes to
            // retiredDsps for destruction on the NEXT load.
            nam::DSP* old = m_impl->dspPtr.exchange(
                newDsp.release(), std::memory_order_acq_rel);
            if (old) m_impl->retiredDsps.emplace_back(old);
            LOG_INFO("NeuralAmp", "Loaded NAM model: %s", path.c_str());
        } else {
            LOG_WARN("NeuralAmp",
                     "nam::get_dsp returned null for %s", path.c_str());
        }
    } catch (const std::exception& e) {
        LOG_WARN("NeuralAmp", "Failed to load %s: %s", path.c_str(), e.what());
        // Don't touch dspPtr — keep whatever was there before. A
        // failed load shouldn't lose the previously-working model.
    } catch (...) {
        // NAM may throw types other than std::exception (rare but
        // possible per its source). Catch-all so a non-standard
        // exception doesn't terminate the app.
        LOG_WARN("NeuralAmp", "Failed to load %s: unknown exception",
                 path.c_str());
    }
#endif
}

const std::string& NeuralAmp::modelPath() const { return m_impl->modelPath; }

bool NeuralAmp::hasModel() const {
#ifdef YAWN_HAS_NAM
    return m_impl->dspPtr.load(std::memory_order_acquire) != nullptr;
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
