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

    // Pre-size bus buffer arrays so the audio thread never allocates.
    m_outputBuses.assign(
        std::max<size_t>(1, m_instance->outputBusChannelCounts().size()), {});
    m_inputBuses.assign(
        std::max<size_t>(1, m_instance->inputBusChannelCounts().size()), {});

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

    // Build ProcessData covering every bus the plugin declared. Main input
    // and output use our L/R buffers; aux buses (sidechain, etc.) get
    // internally-managed silence scratch.
    float* mainIn[2]  = { m_inLeft.data(),  m_inRight.data() };
    float* mainOut[2] = { m_outLeft.data(), m_outRight.data() };
    const int numOutputBuses =
        static_cast<int>(m_instance->outputBusChannelCounts().size());
    const int numInputBuses =
        static_cast<int>(m_instance->inputBusChannelCounts().size());
    m_instance->buildInputBuses(m_inputBuses.data(), mainIn, 2, numFrames);
    m_instance->buildOutputBuses(m_outputBuses.data(), mainOut, 2, numFrames);

    // Drain queued parameter changes for the processor
    Steinberg::Vst::ParameterChanges paramChanges;
    Steinberg::Vst::ParameterChanges outParamChanges;
    m_instance->drainParameterChanges(paramChanges);

    // Minimal process context — JUCE plugins dereference processContext
    // without checking null. Provide a stable default.
    Steinberg::Vst::ProcessContext processContext{};
    processContext.sampleRate = m_sampleRate;
    processContext.tempo = 120.0;
    processContext.timeSigNumerator = 4;
    processContext.timeSigDenominator = 4;
    processContext.state = Steinberg::Vst::ProcessContext::kTempoValid
                         | Steinberg::Vst::ProcessContext::kTimeSigValid;

    // Setup ProcessData. outputParameterChanges must be non-null — DPF-based
    // plugins assert and segfault if it isn't.
    Steinberg::Vst::ProcessData processData;
    processData.processMode = Steinberg::Vst::kRealtime;
    processData.symbolicSampleSize = Steinberg::Vst::kSample32;
    processData.numSamples = numFrames;
    processData.numInputs = numInputBuses;
    processData.inputs = numInputBuses > 0 ? m_inputBuses.data() : nullptr;
    processData.numOutputs = numOutputBuses;
    processData.outputs = numOutputBuses > 0 ? m_outputBuses.data() : nullptr;
    processData.inputEvents = nullptr;
    processData.outputEvents = nullptr;
    processData.inputParameterChanges = paramChanges.getParameterCount() > 0 ? &paramChanges : nullptr;
    processData.outputParameterChanges = &outParamChanges;
    processData.processContext = &processContext;

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
