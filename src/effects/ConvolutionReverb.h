#pragma once
// ConvolutionReverb — IR-based reverb (and any-other-IR effect).
//
// Loads a mono or stereo .wav IR file (the loader is on the App
// side via the standard SDL file dialog) and runs it through the
// uniformly-partitioned FFT convolution engine in Convolution.h.
//
// Stereo handling for v1: the engine is mono. We instantiate two
// engines (L / R) with the SAME mono IR — "fake stereo" — and
// process channels independently. Identical IRs mean identical
// per-channel reverb, which preserves the dry signal's stereo
// image (the reverb tail is mono but it sits inside whatever
// stereo dry signal the user feeds in). True stereo IRs (4-way:
// LL / LR / RL / RR) are a future improvement.
//
// Parameters:
//   * Pre-Delay: 0–200 ms — silence inserted before the wet
//     signal hits the convolver. Useful for separating a dry
//     transient from its reverb (vocal-on-busy-mix workflow).
//   * Low Cut: 20–1000 Hz — high-pass on the wet signal,
//     removes muddy reverb lows that would smear the dry signal's
//     low end.
//   * High Cut: 2–20 kHz — low-pass on the wet signal, tames
//     bright reverbs that would clash with cymbals.
//   * IR Gain: -24..+12 dB — input gain to the convolver. The
//     loaded IR's loudness varies wildly between sources, so a
//     trim knob is essential.
//   * Mix: 0..1 — wet/dry. Per-effect; multiplied with the chain-
//     level mix.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include "effects/Convolution.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace effects {

class ConvolutionReverb : public AudioEffect {
public:
    enum Param {
        kPreDelayMs,    // 0..200 ms
        kLowCutHz,      // 20..1000 Hz
        kHighCutHz,     // 2000..20000 Hz
        kIRGainDb,      // -24..+12 dB
        kMix,           // 0..1
        kParamCount
    };

    const char* name() const override { return "Convolution Reverb"; }
    const char* id()   const override { return "convreverb"; }

    // One block of latency from the convolution engine's sub-block
    // buffering — same value on both sides (mirrored stereo IRs);
    // pulled from whichever engine is currently active. Zero when
    // no IR is loaded (the device falls through to silence in that
    // case, no buffering applied). The pre-delay parameter is part
    // of the wet path inside the algorithm and is NOT counted here
    // — it's user-controlled and always live in the wet output, not
    // a latency the host needs to compensate for.
    int latencySamples() const override {
        if (auto* eng = m_engineL.load(std::memory_order_acquire))
            return eng->latencySamples();
        return 0;
    }

    ~ConvolutionReverb() override {
        // Engine is no longer referenced by audio thread at device
        // destruction (the chain has stopped). Free both engines
        // explicitly; m_retiredEngines self-destructs.
        delete m_engineL.exchange(nullptr);
        delete m_engineR.exchange(nullptr);
    }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate     = sampleRate;
        m_maxBlockSize   = maxBlockSize;
        m_engineBlockSize = maxBlockSize;
        // First-time init runs before audio starts, so there's no
        // race here — we can allocate engines and just publish
        // the pointers atomically. Re-init (sample-rate change /
        // restart) goes through loadIRMono below to inherit the
        // RCU-lite swap protection.
        if (!m_engineL.load() && !m_engineR.load()) {
            auto eL = std::make_unique<ConvolutionEngine>();
            auto eR = std::make_unique<ConvolutionEngine>();
            eL->init(maxBlockSize, sampleRate);
            eR->init(maxBlockSize, sampleRate);
            m_engineL.store(eL.release(), std::memory_order_release);
            m_engineR.store(eR.release(), std::memory_order_release);
        }
        // Partition size = host block size. See Convolution.h for
        // why this trade-off (zero added latency vs more FFT cost
        // per partition vs a smaller-block scheme).
        m_preDelayBuf.assign(static_cast<size_t>(0.200 * sampleRate) * 2, 0.0f);
        m_preDelayWrite = 0;
        m_wetScratchL.assign(maxBlockSize, 0.0f);
        m_wetScratchR.assign(maxBlockSize, 0.0f);
        updateFilters();
        // Re-load IR if one was set before init() (preset path).
        if (!m_pendingIR.empty()) {
            loadIRMono(m_pendingIR.data(),
                        static_cast<int>(m_pendingIR.size()),
                        m_pendingIRSampleRate);
            m_pendingIR.clear();
        }
    }

    void reset() override {
        // reset() is called from the audio thread (transport reset
        // / sample-rate change); it can't race with process() on
        // the same thread. Acquire-load the current engines and
        // mutate them in place — no swap needed. UI-thread
        // loadIRMono concurrent with reset() is rare; if it does
        // race the worst that happens is reset() does its work on
        // an engine that's about to be retired. Wasteful, not
        // unsafe.
        ConvolutionEngine* eL = m_engineL.load(std::memory_order_acquire);
        ConvolutionEngine* eR = m_engineR.load(std::memory_order_acquire);
        if (eL) eL->clear();
        if (eR) eR->clear();
        std::fill(m_preDelayBuf.begin(), m_preDelayBuf.end(), 0.0f);
        m_preDelayWrite = 0;
        m_loCutL.reset(); m_loCutR.reset();
        m_hiCutL.reset(); m_hiCutR.reset();
        // Re-prime the engines with the existing IR. setIR is
        // called directly on the active engine pointers — same
        // self-assign-safety reasoning as before (we hand the
        // engines our own m_irData buffer; they copy into their
        // internal storage).
        if (!m_irData.empty()) {
            if (eL) eL->setIR(m_irData.data(),
                              static_cast<int>(m_irData.size()));
            if (eR) eR->setIR(m_irData.data(),
                              static_cast<int>(m_irData.size()));
        }
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;
        const bool stereo  = (numChannels > 1);
        const float irGain = std::pow(10.0f, m_params[kIRGainDb] / 20.0f);
        const float mix    = std::clamp(m_params[kMix], 0.0f, 1.0f);
        const int preDelaySamp = std::clamp(
            static_cast<int>(m_params[kPreDelayMs] * 0.001f *
                              static_cast<float>(m_sampleRate)),
            0,
            static_cast<int>(m_preDelayBuf.size() / 2) - 1);

        // Acquire-load the engines once per process() call. The
        // pointers stay valid for the call's duration — see the
        // RCU-lite comment on m_engineL/R below for the lifetime
        // argument. If either pointer is null OR the engine has no
        // IR, fall through to the no-op early-out.
        ConvolutionEngine* eL = m_engineL.load(std::memory_order_acquire);
        ConvolutionEngine* eR = m_engineR.load(std::memory_order_acquire);
        if (!eL || !eR || !eL->hasIR() || numFrames <= 0) return;

        // ── 1. Pre-delay buffer (interleaved L/R) ──
        // Fill the delay line with the gained dry signal, then read
        // out delayed samples to feed the convolver.
        const int bufFrames = static_cast<int>(m_preDelayBuf.size() / 2);
        for (int i = 0; i < numFrames; ++i) {
            const int wIdx = (m_preDelayWrite + i) % bufFrames;
            const float l = buffer[i * numChannels]     * irGain;
            const float r = stereo ? buffer[i * numChannels + 1] * irGain : l;
            m_preDelayBuf[wIdx * 2 + 0] = l;
            m_preDelayBuf[wIdx * 2 + 1] = r;
        }
        // Read delayed samples into a temporary block for the engines.
        std::vector<float> delayedL(numFrames), delayedR(numFrames);
        for (int i = 0; i < numFrames; ++i) {
            const int rIdx = (m_preDelayWrite + i - preDelaySamp + bufFrames * 2) % bufFrames;
            delayedL[i] = m_preDelayBuf[rIdx * 2 + 0];
            delayedR[i] = m_preDelayBuf[rIdx * 2 + 1];
        }
        m_preDelayWrite = (m_preDelayWrite + numFrames) % bufFrames;

        // ── 2. Convolution (per channel, same mono IR for both) ──
        eL->process(delayedL.data(), m_wetScratchL.data(), numFrames);
        eR->process(delayedR.data(), m_wetScratchR.data(), numFrames);

        // ── 3. Wet path filtering (low cut + high cut on the tail) ──
        for (int i = 0; i < numFrames; ++i) {
            float wL = m_loCutL.process(m_wetScratchL[i]);
            float wR = m_loCutR.process(m_wetScratchR[i]);
            wL = m_hiCutL.process(wL);
            wR = m_hiCutR.process(wR);
            m_wetScratchL[i] = wL;
            m_wetScratchR[i] = wR;
        }

        // ── 4. Mix wet into output (additive dry/wet blend) ──
        const float w = mix * m_mix;
        const float d = 1.0f - w;
        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;
            buffer[i * numChannels]     = dryL * d + m_wetScratchL[i] * w;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * d + m_wetScratchR[i] * w;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"PreDly",  0.0f,  200.0f,    0.0f, "ms", false, false, WidgetHint::DentedKnob},
            {"LoCut",  20.0f, 1000.0f,   80.0f, "Hz", false},
            {"HiCut", 2000.0f,20000.0f, 12000.0f,"Hz", false},
            {"IR Gain",-24.0f, 12.0f,    0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Mix",     0.0f,   1.0f,    0.30f,"",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        const auto& pi = parameterInfo(index);
        m_params[index] = std::clamp(value, pi.minValue, pi.maxValue);
        if (index == kLowCutHz || index == kHighCutHz) updateFilters();
    }

    // ── IR management ───────────────────────────────────────────────
    // Mono IR loader. If the IR is at a different sample rate than
    // the host, the caller is expected to resample first (libsndfile
    // doesn't auto-resample). At init() time before sampleRate is
    // set we stash the IR in m_pendingIR and load it during init.
    void loadIRMono(const float* ir, int length, double irSampleRate) {
        if (!ir || length <= 0) return;
        m_irSampleRate = irSampleRate;
        // Self-assign guard: skip if caller hands us our own data().
        // (Used to be necessary for the buggy reset() path that's
        // since been refactored, but kept as defence in depth.)
        const bool isSelfAssign =
            (!m_irData.empty() && ir == m_irData.data()
             && static_cast<size_t>(length) == m_irData.size());
        if (!isSelfAssign) {
            m_irData.assign(ir, ir + length);
        }
        if (m_sampleRate <= 0.0) {
            // init() hasn't run yet (preset restore happens before
            // engine wiring on cold-load); stash for re-application
            // when init() finally fires.
            m_pendingIR.assign(ir, ir + length);
            m_pendingIRSampleRate = irSampleRate;
            return;
        }

        // ── RCU-lite atomic engine swap ──
        // Same pattern NeuralAmp uses: build NEW engines fully
        // configured with the IR off the audio thread, then atomic-
        // exchange them in. Old engines get parked in
        // m_retiredEngines and destroyed on the NEXT load — by which
        // time the audio thread has rotated through the new pointers
        // many times, so the old ones are provably idle. Bug it
        // fixes: use-after-free on rapid IR loads (audio thread
        // calling process() on an engine the UI thread is about to
        // destroy).
        m_retiredEngines.clear();   // destroy engines from PREVIOUS load

        auto newL = std::make_unique<ConvolutionEngine>();
        auto newR = std::make_unique<ConvolutionEngine>();
        newL->init(m_engineBlockSize, m_sampleRate);
        newR->init(m_engineBlockSize, m_sampleRate);
        newL->setIR(ir, length);
        newR->setIR(ir, length);

        // Atomic publish. Audio thread will pick up the new pointers
        // on its next process() call.
        ConvolutionEngine* oldL = m_engineL.exchange(
            newL.release(), std::memory_order_acq_rel);
        ConvolutionEngine* oldR = m_engineR.exchange(
            newR.release(), std::memory_order_acq_rel);
        if (oldL) m_retiredEngines.emplace_back(oldL);
        if (oldR) m_retiredEngines.emplace_back(oldR);
    }
    void clearIR() {
        m_irData.clear();
        m_irPath.clear();
        // Same RCU-lite swap-then-park as a load — clearing while
        // audio is mid-process must not destroy the engines under
        // the audio thread's feet.
        m_retiredEngines.clear();
        ConvolutionEngine* oldL = m_engineL.exchange(nullptr,
                std::memory_order_acq_rel);
        ConvolutionEngine* oldR = m_engineR.exchange(nullptr,
                std::memory_order_acq_rel);
        if (oldL) m_retiredEngines.emplace_back(oldL);
        if (oldR) m_retiredEngines.emplace_back(oldR);
    }
    bool hasIR() const {
        ConvolutionEngine* eL = m_engineL.load(std::memory_order_acquire);
        return eL && eL->hasIR();
    }
    int  irLengthSamples() const {
        ConvolutionEngine* eL = m_engineL.load(std::memory_order_acquire);
        return eL ? eL->irLengthSamples() : 0;
    }
    void setIRPath(const std::string& p) { m_irPath = p; }
    const std::string& irPath() const   { return m_irPath; }
    const float* irData() const         { return m_irData.data(); }
    int          irDataLength() const   { return static_cast<int>(m_irData.size()); }

    // Persist the IR file path across project save/load. Same hook
    // pattern NeuralAmp uses for its .nam path.
    nlohmann::json saveExtraState(
            const std::filesystem::path& /*assetDir*/) const override {
        nlohmann::json j;
        if (!m_irPath.empty()) j["irPath"] = m_irPath;
        return j;
    }
    void loadExtraState(const nlohmann::json& state,
                         const std::filesystem::path& /*assetDir*/) override {
        if (state.contains("irPath"))
            m_irPath = state["irPath"].get<std::string>();
        // Actual file read happens host-side; we just remember the
        // path. App is expected to walk loaded effects post-deserialize
        // and rehydrate any IRs by reading the saved paths.
    }

private:
    void updateFilters() {
        const float lc = std::clamp(m_params[kLowCutHz],   20.0f, 1000.0f);
        const float hc = std::clamp(m_params[kHighCutHz], 2000.0f, 20000.0f);
        m_loCutL.compute(Biquad::Type::HighPass, m_sampleRate, lc, 0.0, 0.707);
        m_loCutR.compute(Biquad::Type::HighPass, m_sampleRate, lc, 0.0, 0.707);
        m_hiCutL.compute(Biquad::Type::LowPass,  m_sampleRate, hc, 0.0, 0.707);
        m_hiCutR.compute(Biquad::Type::LowPass,  m_sampleRate, hc, 0.0, 0.707);
    }

    float m_params[kParamCount] = {
        0.0f, 80.0f, 12000.0f, 0.0f, 0.30f
    };

    // RCU-lite atomic engine pointers + retired list. Audio thread
    // reads via acquire-load, gets a stable raw pointer for the
    // duration of process(). UI thread (loadIRMono) builds NEW
    // engines fully off the audio thread, atomic-exchanges them
    // in, parks the previous engines in m_retiredEngines. Old
    // engines are destroyed at the START of the NEXT loadIRMono
    // (or device destruction) — by which time the audio thread
    // has long since switched to the new pointers.
    //
    // Cost: each surviving load leaks the previous engines for one
    // load cycle. For 10 s IRs the partition + FDL state can be
    // 10–20 MB per engine; 2 engines × one cycle = 20–40 MB
    // potentially leaked between loads. Acceptable given how rare
    // IR loads are.
    std::atomic<ConvolutionEngine*> m_engineL{nullptr};
    std::atomic<ConvolutionEngine*> m_engineR{nullptr};
    std::vector<std::unique_ptr<ConvolutionEngine>> m_retiredEngines;
    int m_engineBlockSize = 0;

    // IR storage (lives on the device so a reset() can re-prime the
    // engines without needing the host to re-load the file).
    std::vector<float> m_irData;
    double             m_irSampleRate = 48000.0;
    std::string        m_irPath;

    // Init-deferred IR (set before init() — happens during preset
    // restore where the file's already been read but the engines
    // aren't allocated yet).
    std::vector<float> m_pendingIR;
    double             m_pendingIRSampleRate = 48000.0;

    // Pre-delay (interleaved stereo).
    std::vector<float> m_preDelayBuf;
    int                m_preDelayWrite = 0;

    // Wet-path filters.
    Biquad m_loCutL, m_loCutR;
    Biquad m_hiCutL, m_hiCutR;

    // Per-block scratch for the wet signal between convolution and
    // dry-mix-add. Avoids per-sample heap allocs.
    std::vector<float> m_wetScratchL, m_wetScratchR;
};

} // namespace effects
} // namespace yawn
