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
// File loading: NeuralAmp itself just stores the model path (via
// setModelPath / extra state). The actual file-read happens
// host-side (App::loadNamModel) via the AudioEffect extra-state
// hook + an SDL file dialog, mirroring how Conv Reverb loads IRs.
//
// Parameters (3): Input Gain / Output Gain / Mix.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace yawn {
namespace effects {

class NeuralAmp : public AudioEffect {
public:
    enum Param {
        kInputGain,    // -30..+30 dB pre-NAM gain
        kOutputGain,   // -30..+30 dB post-NAM gain
        kMix,          // 0..1 wet/dry
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
            {"In",     -30.0f, 30.0f,   0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Out",    -30.0f, 30.0f,   0.0f, "dB", false, false, WidgetHint::DentedKnob},
            {"Mix",      0.0f,  1.0f,   1.0f, "",   false, false, WidgetHint::DentedKnob},
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
    float m_params[kParamCount] = {0.0f, 0.0f, 1.0f};

    // User's Lite (CPU-saver) preference. Persisted; applied to the
    // loaded model when it's slimmable. See setLite().
    bool m_lite = false;

    // PIMPL — the impl struct holds NAM-specific state (a
    // std::unique_ptr<nam::DSP>, scratch buffers) when YAWN_HAS_NAM
    // is enabled, plus the model path and a couple of housekeeping
    // bits regardless. Defined in the .cpp.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace effects
} // namespace yawn
