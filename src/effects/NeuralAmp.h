#pragma once
// NeuralAmp — Neural Amp Modeler (.nam) loader / inference effect.
//
// PIMPL split: this header stays clean C++17 so it can be included
// from anywhere in YAWN. The .cpp file is the only translation
// unit that includes <NAM/...> headers (which require C++20) and
// is compiled standalone with set_source_files_properties to bump
// just that TU to C++20. NeuralAmp::Impl is forward-declared here
// and defined in the .cpp.
//
// Behaviour:
//   * No model loaded → device is a clean gain stage with In / Out
//     gain + Mix, mono-summing the input.
//   * Model loaded → input gets fed through nam::DSP::process(), the
//     output replaces the wet signal in the standard dry/wet blend.
//     NAM models are mono in / mono out; we sum the stereo input
//     for the network and duplicate the output to both sides.
//
// Built-in amp strip (around the NAM core, all mono since the NAM
// signal is mono): a noise gate on the input, a 3-band tone stack
// (Bass/Mid/Treble) on the output, and a Normalize toggle that uses
// the model's embedded loudness metadata to level different captures
// to a common reference. These mirror what the standalone NAM plugin
// wraps around the raw model, so the device is a complete amp channel
// rather than just the network.
//
// File loading: NeuralAmp itself just stores the model path (via
// setModelPath / extra state). The actual file-read happens
// host-side (App::loadNamModel) via the AudioEffect extra-state
// hook + an SDL file dialog, mirroring how Conv Reverb loads IRs.
//
// Parameters: In / Out / Mix / Gate / Bass / Mid / Treble / Normalize.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace yawn {
namespace effects {

class NeuralAmp : public AudioEffect {
public:
    enum Param {
        // The first three are the original params — their indices are
        // pinned so projects saved before the amp strip existed still
        // deserialize correctly (older state just omits 3..7).
        kInputGain,    // 0: -30..+30 dB pre-NAM gain ("Drive")
        kOutputGain,   // 1: -30..+30 dB post-NAM gain
        kMix,          // 2: 0..1 wet/dry
        kGate,         // 3: -100..0 dB noise-gate threshold (-100 = off)
        kBass,         // 4: -12..+12 dB low shelf  (post-model tone)
        kMid,          // 5: -12..+12 dB mid peak    (post-model tone)
        kTreble,       // 6: -12..+12 dB high shelf  (post-model tone)
        kNormalize,    // 7: 0/1 normalize output to model loudness
        kParamCount
    };

    NeuralAmp();
    ~NeuralAmp() override;

    const char* name() const override { return "Neural Amp"; }
    const char* id()   const override { return "neuralamp"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"In",      -30.0f,  30.0f,    0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Out",     -30.0f,  30.0f,    0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Mix",       0.0f,   1.0f,    1.0f, "",   false, false, WidgetHint::DentedKnob},
            {"Gate",   -100.0f,   0.0f, -100.0f, "dB", false, false, WidgetHint::Knob},
            {"Bass",    -12.0f,  12.0f,    0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Mid",     -12.0f,  12.0f,    0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Treble",  -12.0f,  12.0f,    0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Norm",      0.0f,   1.0f,    0.0f, "",   true,  false, WidgetHint::Toggle},
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
        if (index == kBass || index == kMid || index == kTreble)
            updateToneStack();
    }

    // ── Model file management ───────────────────────────────────────
    // setModelPath kicks off NAM model loading (when YAWN_HAS_NAM).
    // Empty path clears the model (gain-stage passthrough). On a
    // failed load the previously-working model keeps processing and
    // modelPath() keeps describing it; lastLoadError() reports why the
    // new file was rejected (empty after a successful load or clear).
    // If a load fails with NO model active (fresh device, project
    // opened with the file missing), modelPath() keeps the requested
    // path so the project's model reference survives a resave.
    void               setModelPath(const std::string& path);
    const std::string& modelPath() const;
    const std::string& lastLoadError() const;
    bool               hasModel() const;

    // ── Sample-rate metadata ────────────────────────────────────────
    // NAM models are trained at a fixed sample rate baked into the
    // file; running inference at a different host rate subtly changes
    // tone/feel (the network sees a time-warped signal). We don't
    // resample — that's the official plugin's behaviour too — but we
    // surface the mismatch. expectedSampleRate() is the model's
    // training rate (0 if unknown / no model); sampleRateMismatch()
    // is true when it's known and differs from the host rate.
    double expectedSampleRate() const;
    bool   sampleRateMismatch() const;

    // ── Slimmable size ("Lite" CPU-saver) ───────────────────────────
    // Some NAM models (SlimmableWavenet / SlimmableContainer — e.g. the
    // A2 captures) implement nam::SlimmableModel and can trade quality
    // for CPU at runtime. setLite(true) pins the model to its minimum
    // size (SetSlimmableSize 0.0); false restores full size (1.0). The
    // preference is remembered and re-applied on each model load. It's
    // a no-op on models that aren't slimmable (all A1 captures), and
    // isSlimmable() reports whether the loaded model supports it so the
    // UI can grey the toggle. Call from the control/UI thread (same as
    // setModelPath) — SetSlimmableSize is thread-safe but not RT-safe.
    void setLite(bool lite);
    bool lite() const { return m_lite; }
    bool isSlimmable() const;

    nlohmann::json saveExtraState(const std::filesystem::path& assetDir) const override;
    void loadExtraState(const nlohmann::json& state,
                         const std::filesystem::path& assetDir) override;

private:
    float m_params[kParamCount] =
        {0.0f, 0.0f, 1.0f, -100.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // User's Lite (CPU-saver) preference. Persisted; applied to the
    // loaded model when it's slimmable. See setLite().
    bool m_lite = false;

    // ── Built-in amp-strip DSP (host-rate, mono) ────────────────────
    // The NAM core processes a mono signal; the gate runs on its input
    // and the tone stack on its output, so a single (not stereo) filter
    // set is all that's needed. All state lives here in the C++17-clean
    // header; process() (in the .cpp) drives them per sample.

    // 3-band tone stack on the model's mono output — low shelf / mid
    // peak / high shelf, same frequencies as AmpSimulator. Recomputed
    // on init() and whenever a tone knob moves (setParameter). Guarded
    // against a zero sample rate so setParameter before init() is safe.
    Biquad m_bass, m_mid, m_treble;
    void updateToneStack() {
        const double sr = m_sampleRate > 0.0 ? m_sampleRate : 48000.0;
        m_bass  .compute(Biquad::Type::LowShelf,  sr,  200.0, m_params[kBass],   0.707);
        m_mid   .compute(Biquad::Type::Peak,      sr,  800.0, m_params[kMid],    1.0);
        m_treble.compute(Biquad::Type::HighShelf, sr, 3200.0, m_params[kTreble], 0.707);
    }

    // Noise gate on the mono input (pre-NAM). A peak-follower envelope
    // with fast attack / slow release decides the gate's open/closed
    // target; the gain itself is smoothed (2 ms open, 50 ms close) to
    // avoid clicks. Bypassed at the -100 dB threshold floor. State is
    // zeroed in reset().
    float m_gateGain = 1.0f;     // smoothed open(1)/closed(0) gain
    float m_gateEnv  = 0.0f;     // input peak envelope
    float m_gateEnvCoef   = 0.0f;
    float m_gateOpenCoef  = 0.0f;
    float m_gateCloseCoef = 0.0f;
    void updateGateCoeffs() {
        const float sr = static_cast<float>(m_sampleRate > 0.0 ? m_sampleRate : 48000.0);
        m_gateEnvCoef   = std::exp(-1.0f / (0.005f * sr));   // 5 ms env release
        m_gateOpenCoef  = std::exp(-1.0f / (0.002f * sr));   // 2 ms gate open
        m_gateCloseCoef = std::exp(-1.0f / (0.050f * sr));   // 50 ms gate close
    }
    float gateSample(float x) {
        if (m_params[kGate] <= -99.5f) return x;             // gate off
        const float threshLin = std::pow(10.0f, m_params[kGate] / 20.0f);
        const float ax = std::fabs(x);
        // Peak envelope: instant attack, exponential release.
        m_gateEnv = (ax > m_gateEnv)
                  ? ax
                  : ax + (m_gateEnv - ax) * m_gateEnvCoef;
        const float target = (m_gateEnv >= threshLin) ? 1.0f : 0.0f;
        const float coef = (target > m_gateGain) ? m_gateOpenCoef
                                                 : m_gateCloseCoef;
        m_gateGain = target + (m_gateGain - target) * coef;
        return x * m_gateGain;
    }

    // PIMPL — the impl struct holds NAM-specific state (a
    // std::unique_ptr<nam::DSP>, scratch buffers) when YAWN_HAS_NAM
    // is enabled, plus the model path and a couple of housekeeping
    // bits regardless. Defined in the .cpp.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace effects
} // namespace yawn
