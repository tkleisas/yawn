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
    // Empty path or load-failure clears the model and the device
    // falls back to gain-stage passthrough behaviour.
    void               setModelPath(const std::string& path);
    const std::string& modelPath() const;
    bool               hasModel() const;

    nlohmann::json saveExtraState(const std::filesystem::path& assetDir) const override;
    void loadExtraState(const nlohmann::json& state,
                         const std::filesystem::path& assetDir) override;

private:
    float m_params[kParamCount] = {0.0f, 0.0f, 1.0f};

    // PIMPL — the impl struct holds NAM-specific state (a
    // std::unique_ptr<nam::DSP>, scratch buffers) when YAWN_HAS_NAM
    // is enabled, plus the model path and a couple of housekeeping
    // bits regardless. Defined in the .cpp.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace effects
} // namespace yawn
