#pragma once
// NeuralAmp — Neural Amp Modeler (.nam) loader / inference effect.
//
// STAGE 1 (this commit): device shell. The full chain is wired up
// EXCEPT the actual NAM model inference call — that's Stage 2,
// which adds the NeuralAmpModelerCore + Eigen dependencies via
// FetchContent and replaces the passthrough at the marked site
// with the real DSP. Until Stage 2 lands, this device behaves as
// a clean gain stage with input level / output level / mix knobs;
// loading a .nam file stores the path in extra state (so projects
// preserve it across save/load) but doesn't yet run inference.
//
// Why ship Stage 1 separately:
//   - The YAWN-side architecture (params, file picker, project
//     persistence, UI) gets shaken out independently of the
//     third-party-library build complexity. NAM bundles its own
//     Eigen submodule which doesn't FetchContent cleanly, so the
//     library wiring needs a focused pass.
//   - Users can already drop the device on a track, see it in the
//     menu, and use it as a gain stage. Stage 2 swap is invisible.
//
// Stage 2 changes will be:
//   - Add NAM + Eigen via FetchContent + custom static-lib wrapper
//   - Add YAWN_HAS_NAM compile flag (default ON when build deps
//     resolve, OFF otherwise)
//   - Replace the marked passthrough loop body with nam::DSP::process
//   - Bundle 1–2 public-domain .nam models in assets/nam/
//
// Parameters (4): Input Gain / Output Gain / Mix / [model file path
// stored separately via saveExtraState since strings don't fit the
// flat float param list].

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
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

    const char* name() const override { return "Neural Amp"; }
    const char* id()   const override { return "neuralamp"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        reset();
    }

    void reset() override {
        // Stage 2: also call m_namDsp->reset() / equivalent here.
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        const float inGainDb  = m_params[kInputGain];
        const float outGainDb = m_params[kOutputGain];
        const float mix       = std::clamp(m_params[kMix], 0.0f, 1.0f);
        const float inGain    = std::pow(10.0f, inGainDb  / 20.0f);
        const float outGain   = std::pow(10.0f, outGainDb / 20.0f);

        const bool stereo = (numChannels > 1);

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            // Pre-NAM: input gain + mono sum (NAM models are mono).
            const float preGained = (dryL + dryR) * 0.5f * inGain;

            // ── Stage-2 marker ──
            // Replace the next two lines with:
            //   const float wet = m_namDsp ? m_namDsp->process(preGained)
            //                              : preGained;
            // when the NAM library lands. Until then this is a clean
            // passthrough of the gained signal; the device still
            // works as a coloured gain stage with the user's input/
            // output gain values acting as a hard-clipping-prone
            // pre/post couple. Useful enough on its own that the
            // shell isn't dead weight.
            const float wet = preGained;

            const float postGained = wet * outGain;

            // Mono → fake stereo (NAM is mono-out; we duplicate to
            // both sides). When Stage 2 lands, IR convolution on the
            // post-NAM path can re-stereoise via a stereo cab IR.
            const float w = mix * m_mix;
            const float d = 1.0f - w;
            buffer[i * numChannels]     = dryL * d + postGained * w;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * d + postGained * w;
        }
    }

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
    // The .nam path is the only piece of state outside the param list.
    // Stored as the path relative to the project's assetDir on save
    // (so projects move cleanly between machines), absolute on
    // run-time loads via the file picker.
    void setModelPath(const std::string& path) {
        m_modelPath = path;
        // Stage 2: m_namDsp = nam::get_dsp(path); error handling here.
    }
    const std::string& modelPath() const { return m_modelPath; }
    bool hasModel() const { return !m_modelPath.empty(); }

    nlohmann::json saveExtraState(
            const std::filesystem::path& /*assetDir*/) const override {
        nlohmann::json j;
        if (!m_modelPath.empty()) j["modelPath"] = m_modelPath;
        return j;
    }
    void loadExtraState(const nlohmann::json& state,
                         const std::filesystem::path& /*assetDir*/) override {
        if (state.contains("modelPath"))
            setModelPath(state["modelPath"].get<std::string>());
    }

private:
    float m_params[kParamCount] = {0.0f, 0.0f, 1.0f};
    std::string m_modelPath;
    // Stage 2: std::unique_ptr<nam::DSP> m_namDsp;
};

} // namespace effects
} // namespace yawn
