#ifdef YAWN_HAS_VST3

#include "vst3/VST3Effect.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstring>

namespace yawn {
namespace vst3 {

const effects::ParameterInfo VST3Effect::s_emptyParam = {"", 0, 1, 0, "", false};

VST3Effect::VST3Effect(const std::string& modulePath,
                         const std::string& classIDString)
    : m_modulePath(modulePath)
    , m_classIDString(classIDString)
    , m_pluginID("vst3:" + classIDString)
{
}

void VST3Effect::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    // Pre-allocate non-interleaved buffers
    m_inLeft.resize(maxBlockSize, 0.0f);
    m_inRight.resize(maxBlockSize, 0.0f);
    m_outLeft.resize(maxBlockSize, 0.0f);
    m_outRight.resize(maxBlockSize, 0.0f);

    // Load and instantiate the plugin
    VST3ModuleHandle handle;
    std::string error;
    if (!handle.load(m_modulePath, error)) {
        m_displayName = "[Failed: " + m_modulePath + "]";
        return;
    }

    m_instance = VST3PluginInstance::create(
        handle.module(), m_classIDString, sampleRate, maxBlockSize);

    if (!m_instance) {
        m_displayName = "[Failed to create VST3]";
        return;
    }

    m_displayName = m_instance->name();
    m_instance->setProcessing(true);

    buildParameterInfoCache();
}

void VST3Effect::reset() {
    if (m_instance) {
        m_instance->setProcessing(false);
        m_instance->setActive(false);
        m_instance->setupProcessing(m_sampleRate, m_maxBlockSize);
        m_instance->setActive(true);
        m_instance->setProcessing(true);
    }
}

void VST3Effect::process(float* buffer, int numFrames, int numChannels) {
    if (!m_instance || !m_instance->processor() || m_bypassed) return;

    int ch = (numChannels >= 2) ? 2 : 1;

    // Deinterleave input
    if (ch >= 2) {
        for (int i = 0; i < numFrames; ++i) {
            m_inLeft[i]  = buffer[i * numChannels + 0];
            m_inRight[i] = buffer[i * numChannels + 1];
        }
    } else {
        for (int i = 0; i < numFrames; ++i) {
            m_inLeft[i]  = buffer[i];
            m_inRight[i] = buffer[i];
        }
    }

    // Clear output buffers
    std::memset(m_outLeft.data(), 0, numFrames * sizeof(float));
    std::memset(m_outRight.data(), 0, numFrames * sizeof(float));

    // Setup VST3 AudioBusBuffers
    float* inChannels[2]  = { m_inLeft.data(),  m_inRight.data() };
    float* outChannels[2] = { m_outLeft.data(), m_outRight.data() };

    Steinberg::Vst::AudioBusBuffers inputBus;
    inputBus.numChannels = 2;
    inputBus.silenceFlags = 0;
    inputBus.channelBuffers32 = inChannels;

    Steinberg::Vst::AudioBusBuffers outputBus;
    outputBus.numChannels = 2;
    outputBus.silenceFlags = 0;
    outputBus.channelBuffers32 = outChannels;

    // Drain queued parameter changes for the processor
    Steinberg::Vst::ParameterChanges paramChanges;
    m_instance->drainParameterChanges(paramChanges);

    // Setup ProcessData
    Steinberg::Vst::ProcessData processData;
    processData.processMode = Steinberg::Vst::kRealtime;
    processData.symbolicSampleSize = Steinberg::Vst::kSample32;
    processData.numSamples = numFrames;
    processData.numInputs = 1;
    processData.inputs = &inputBus;
    processData.numOutputs = 1;
    processData.outputs = &outputBus;
    processData.inputEvents = nullptr;
    processData.outputEvents = nullptr;
    processData.inputParameterChanges = paramChanges.getParameterCount() > 0 ? &paramChanges : nullptr;
    processData.outputParameterChanges = nullptr;
    processData.processContext = nullptr;

    // Process
    m_instance->processor()->process(processData);

    // Apply wet/dry mix and reinterleave to YAWN's buffer
    float wet = m_mix;
    float dry = 1.0f - wet;

    if (ch >= 2) {
        for (int i = 0; i < numFrames; ++i) {
            buffer[i * numChannels + 0] = dry * m_inLeft[i]  + wet * m_outLeft[i];
            buffer[i * numChannels + 1] = dry * m_inRight[i] + wet * m_outRight[i];
        }
    } else {
        for (int i = 0; i < numFrames; ++i) {
            float wetMono = (m_outLeft[i] + m_outRight[i]) * 0.5f;
            buffer[i] = dry * m_inLeft[i] + wet * wetMono;
        }
    }
}

void VST3Effect::buildParameterInfoCache() {
    m_paramInfos.clear();
    m_paramNames.clear();
    m_paramUnits.clear();
    m_paramIDs.clear();

    if (!m_instance) return;

    int count = m_instance->parameterCount();
    m_paramInfos.reserve(count);
    m_paramNames.reserve(count);
    m_paramUnits.reserve(count);
    m_paramIDs.reserve(count);

    for (int i = 0; i < count; ++i) {
        auto vst3Info = m_instance->parameterInfo(i);

        if (vst3Info.flags & Steinberg::Vst::ParameterInfo::kIsHidden)
            continue;
        if (vst3Info.flags & Steinberg::Vst::ParameterInfo::kIsReadOnly)
            continue;

        // Convert char16 strings to std::string
        std::string paramName;
        for (int c = 0; vst3Info.title[c] != 0; ++c)
            paramName += static_cast<char>(vst3Info.title[c]);
        m_paramNames.push_back(paramName);

        std::string unitStr;
        for (int c = 0; vst3Info.units[c] != 0; ++c)
            unitStr += static_cast<char>(vst3Info.units[c]);
        m_paramUnits.push_back(unitStr);

        m_paramIDs.push_back(vst3Info.id);

        effects::ParameterInfo info;
        info.name = m_paramNames.back().c_str();
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = static_cast<float>(vst3Info.defaultNormalizedValue);
        info.unit = m_paramUnits.back().c_str();
        info.isBoolean = (vst3Info.stepCount == 1);

        m_paramInfos.push_back(info);
    }
}

int VST3Effect::parameterCount() const {
    return static_cast<int>(m_paramInfos.size());
}

const effects::ParameterInfo& VST3Effect::parameterInfo(int index) const {
    if (index < 0 || index >= static_cast<int>(m_paramInfos.size()))
        return s_emptyParam;
    return m_paramInfos[index];
}

float VST3Effect::getParameter(int index) const {
    if (index < 0 || index >= static_cast<int>(m_paramIDs.size()) || !m_instance)
        return 0.0f;
    return static_cast<float>(m_instance->getParameterNormalized(m_paramIDs[index]));
}

void VST3Effect::setParameter(int index, float value) {
    if (index < 0 || index >= static_cast<int>(m_paramIDs.size()) || !m_instance)
        return;
    m_instance->setParameterNormalized(m_paramIDs[index], static_cast<double>(value));
}

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
