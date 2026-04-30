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
#include <cmath>
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

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // Partition size = host block size. See Convolution.h for
        // why this trade-off (zero added latency vs more FFT cost
        // per partition vs a smaller-block scheme).
        m_engineL.init(maxBlockSize, sampleRate);
        m_engineR.init(maxBlockSize, sampleRate);
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
        m_engineL.clear();
        m_engineR.clear();
        std::fill(m_preDelayBuf.begin(), m_preDelayBuf.end(), 0.0f);
        m_preDelayWrite = 0;
        m_loCutL.reset(); m_loCutR.reset();
        m_hiCutL.reset(); m_hiCutR.reset();
        // Re-prime the engines with the existing IR. We call setIR
        // DIRECTLY rather than going through loadIRMono — going via
        // loadIRMono would self-assign m_irData (m_irData.assign(
        // m_irData.data(), …)), which is UB; the vector's assign
        // is allowed to clear() before copying, destroying the
        // source range, which left m_irData empty / dangling and
        // produced a 0x0 read access violation in setIR's per-
        // sample copy a moment later. Now we hand the engines our
        // own buffer directly — they make their own copies, no
        // self-assign anywhere.
        if (!m_irData.empty()) {
            m_engineL.setIR(m_irData.data(),
                            static_cast<int>(m_irData.size()));
            m_engineR.setIR(m_irData.data(),
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

        // No IR loaded → device is a no-op (mix-aware: still applies
        // m_mix * 0 wet, which is a passthrough). Skip the work.
        if (!m_engineL.hasIR() || numFrames <= 0) return;

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
        m_engineL.process(delayedL.data(), m_wetScratchL.data(), numFrames);
        m_engineR.process(delayedR.data(), m_wetScratchR.data(), numFrames);

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
        // Self-assign guard: if the caller is handing us our own
        // buffer's data() (which happens with a poorly-written
        // reset() — fixed in this commit, but defended here too
        // for any future caller who makes the same mistake),
        // skip the assign. The engines' setIR() copies into their
        // own internal storage, so we don't need m_irData updated
        // when the data is the same.
        const bool isSelfAssign =
            (!m_irData.empty() && ir == m_irData.data()
             && static_cast<size_t>(length) == m_irData.size());
        if (!isSelfAssign) {
            m_irData.assign(ir, ir + length);
        }
        if (m_sampleRate > 0.0) {
            m_engineL.setIR(ir, length);
            m_engineR.setIR(ir, length);
        } else {
            // init() hasn't run yet (preset restore happens before
            // engine wiring on cold-load); stash for re-application
            // when init() finally fires.
            m_pendingIR.assign(ir, ir + length);
            m_pendingIRSampleRate = irSampleRate;
        }
    }
    void clearIR() {
        m_irData.clear();
        m_irPath.clear();
        m_engineL.clear();
        m_engineR.clear();
    }
    bool hasIR() const { return m_engineL.hasIR(); }
    int  irLengthSamples() const { return m_engineL.irLengthSamples(); }
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

    ConvolutionEngine m_engineL, m_engineR;

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
