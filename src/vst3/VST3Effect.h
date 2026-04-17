#pragma once

// VST3Effect — Wraps a VST3 audio effect plugin as a YAWN AudioEffect.
// Handles buffer format conversion (interleaved ↔ non-interleaved)
// and parameter mapping.

#ifdef YAWN_HAS_VST3

#include "effects/AudioEffect.h"
#include "vst3/VST3Host.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace vst3 {

class VST3Effect : public effects::AudioEffect {
public:
    VST3Effect(const std::string& modulePath, const std::string& classIDString);
    ~VST3Effect() override = default;

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;

    void process(float* buffer, int numFrames, int numChannels) override;

    const char* name() const override { return m_displayName.c_str(); }
    const char* id()   const override { return m_pluginID.c_str(); }

    int parameterCount() const override;
    const effects::ParameterInfo& parameterInfo(int index) const override;
    float getParameter(int index) const override;
    void  setParameter(int index, float value) override;

    // Access to the underlying VST3 instance
    VST3PluginInstance* instance() const { return m_instance.get(); }

    const std::string& modulePath() const { return m_modulePath; }
    const std::string& classIDString() const { return m_classIDString; }

private:
    void buildParameterInfoCache();

    std::string m_modulePath;
    std::string m_classIDString;
    std::string m_displayName;
    std::string m_pluginID;    // "vst3:<classID>"

    std::unique_ptr<VST3PluginInstance> m_instance;

    // Parameter cache
    std::vector<effects::ParameterInfo> m_paramInfos;
    std::vector<std::string> m_paramNames;
    std::vector<std::string> m_paramUnits;
    std::vector<Steinberg::Vst::ParamID> m_paramIDs;

    // Pre-allocated processing buffers (non-interleaved)
    std::vector<float> m_inLeft, m_inRight;
    std::vector<float> m_outLeft, m_outRight;

    // Pre-sized AudioBusBuffers arrays — resized in init() to match plugin layout.
    std::vector<Steinberg::Vst::AudioBusBuffers> m_outputBuses;
    std::vector<Steinberg::Vst::AudioBusBuffers> m_inputBuses;

    static const effects::ParameterInfo s_emptyParam;
};

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
